#include "JaguarCore/entities/layout.h"
#include "JaguarCore/assertions.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace jaguar
{
namespace
{
bool samePolygon(const SimplePolygon& first, const SimplePolygon& second) noexcept
{
    constexpr double tolerance = 1.0e-9;
    if (first.vertexCount() != second.vertexCount())
        return false;
    for (std::size_t i = 0; i < first.vertexCount(); ++i)
    {
        if (std::abs(first.vertices()[i].x - second.vertices()[i].x) > tolerance ||
            std::abs(first.vertices()[i].y - second.vertices()[i].y) > tolerance)
            return false;
    }
    return true;
}

bool sameHazard(const Hazard& first, const Hazard& second) noexcept
{
    return first.dynamic == second.dynamic && first.entity == second.entity &&
           first.shape && second.shape && samePolygon(*first.shape, *second.shape);
}
}

Layout::Layout(Container container)
    : m_Container(std::move(container)),
      m_Cde(m_Container.createCollisionEngine())
{
}

Layout::Slot* Layout::get(PlacedItemHandle handle) noexcept
{
    if (handle.index >= m_Slots.size())
        return nullptr;
    Slot& slot = m_Slots[handle.index];
    if (slot.generation != handle.generation || !slot.placedItem.has_value())
        return nullptr;
    return &slot;
}

const Layout::Slot* Layout::get(PlacedItemHandle handle) const noexcept
{
    if (handle.index >= m_Slots.size())
        return nullptr;
    const Slot& slot = m_Slots[handle.index];
    if (slot.generation != handle.generation || !slot.placedItem.has_value())
        return nullptr;
    return &slot;
}

PlacedItemHandle Layout::allocateHandle()
{
    for (std::size_t i = 0; i < m_Slots.size(); ++i)
    {
        if (!m_Slots[i].placedItem.has_value())
        {
            ++m_Slots[i].generation;
            return PlacedItemHandle{i, m_Slots[i].generation};
        }
    }

    m_Slots.push_back(Slot{});
    return PlacedItemHandle{m_Slots.size() - 1, m_Slots.back().generation};
}

Hazard Layout::makeHazard(const PlacedItem& placedItem)
{
    return Hazard{
        {HazardKind::PlacedItem, placedItem.itemIndex(), 0, 0},
        std::make_shared<const SimplePolygon>(placedItem.collisionShape()),
        true};
}

JAGUARCORE_API Result<PlacedItemHandle> Layout::placeItem(const Item& item,
                                                          Transform2d transform)
{
    if (!m_Cde)
    {
        return Result<PlacedItemHandle>::fromError(
            {ErrorCode::CollisionEngineFailure, "layout collision engine is unavailable"});
    }

    const auto created = PlacedItem::create(item, transform);
    if (!created.hasValue())
        return Result<PlacedItemHandle>::fromError(created.error());

    const PlacedItemHandle handle = allocateHandle();
    Slot& slot = m_Slots[handle.index];
    slot.placedItem = created.value();

    const auto registered = m_Cde->registerHazard(makeHazard(*slot.placedItem));
    if (!registered.hasValue())
    {
        slot.placedItem.reset();
        return Result<PlacedItemHandle>::fromError(registered.error());
    }
    slot.cdeHandle = registered.value();
    debugAssertLayout(*this);
    return Result<PlacedItemHandle>::fromValue(handle);
}

JAGUARCORE_API Result<PlacedItem> Layout::removeItem(PlacedItemHandle handle)
{
    Slot* slot = get(handle);
    if (!slot)
    {
        return Result<PlacedItem>::fromError(
            {ErrorCode::InvalidInput, "invalid placed item handle"});
    }

    const auto deregistered = m_Cde->deregisterHazard(slot->cdeHandle);
    if (!deregistered.hasValue())
        return Result<PlacedItem>::fromError(deregistered.error());

    PlacedItem removed = std::move(*slot->placedItem);
    slot->placedItem.reset();
    ++slot->generation;
    debugAssertLayout(*this);
    return Result<PlacedItem>::fromValue(std::move(removed));
}

JAGUARCORE_API Result<void> Layout::swapContainer(Container container)
{
    auto newCde = container.createCollisionEngine();
    if (!newCde)
    {
        return Result<void>::fromError(
            {ErrorCode::CollisionEngineFailure,
             "new container collision engine is unavailable"});
    }

    std::vector<HazardHandle> newHandles(m_Slots.size());
    for (std::size_t i = 0; i < m_Slots.size(); ++i)
    {
        if (!m_Slots[i].placedItem.has_value())
            continue;
        const auto registered = newCde->registerHazard(
            makeHazard(*m_Slots[i].placedItem));
        if (!registered.hasValue())
        {
            return Result<void>::fromError(
                {ErrorCode::CollisionEngineFailure,
                 "could not re-register a placed item in the new container"});
        }
        newHandles[i] = registered.value();
    }

    m_Container = std::move(container);
    m_Cde = std::move(newCde);
    for (std::size_t i = 0; i < m_Slots.size(); ++i)
    {
        if (m_Slots[i].placedItem.has_value())
            m_Slots[i].cdeHandle = newHandles[i];
    }
    debugAssertLayout(*this);
    return Result<void>::fromValue();
}

JAGUARCORE_API bool Layout::isFeasible() const
{
    if (!m_Cde)
        return false;

    for (std::size_t i = 0; i < m_Slots.size(); ++i)
    {
        if (!m_Slots[i].placedItem.has_value())
            continue;

        const HazardHandle ownHazard = m_Slots[i].cdeHandle;
        const std::optional<std::size_t> minQuality =
            m_Slots[i].placedItem->item().minQuality();
        HazardFilter filter;
        filter.irrelevantHandle = [ownHazard, minQuality](
                                      HazardHandle handle,
                                      const HazardEntity& entity) {
            if (handle == ownHazard)
                return true;
            return minQuality.has_value() &&
                   entity.kind == HazardKind::InferiorQualityZone &&
                   entity.quality < *minQuality;
        };
        if (m_Cde->detectPolygonCollision(
                m_Slots[i].placedItem->collisionShape(), filter))
            return false;
    }
    return true;
}

JAGUARCORE_API double Layout::density() const
{
    const double containerArea = m_Container.area();
    if (containerArea <= 0.0)
        return 0.0;

    double placedArea = 0.0;
    for (const Slot& slot : m_Slots)
    {
        if (slot.placedItem.has_value())
            placedArea += slot.placedItem->area();
    }
    return placedArea / containerArea;
}

JAGUARCORE_API LayoutSnapshot Layout::save() const
{
    LayoutSnapshot snapshot{m_Container, {}, m_Cde->save()};
    for (std::size_t i = 0; i < m_Slots.size(); ++i)
    {
        if (m_Slots[i].placedItem.has_value())
        {
            snapshot.placements.push_back(
                {PlacedItemHandle{i, m_Slots[i].generation},
                 *m_Slots[i].placedItem});
        }
    }
    return snapshot;
}

JAGUARCORE_API Result<void> Layout::restore(const LayoutSnapshot& snapshot)
{
    if (!m_Container.sameBase(snapshot.container) ||
        snapshot.cdeSnapshot.dynamicHazards.size() != snapshot.placements.size())
    {
        return Result<void>::fromError(
            {ErrorCode::SnapshotMismatch,
             "layout and snapshot do not share the same static state"});
    }

    std::vector<Hazard> expectedHazards;
    expectedHazards.reserve(snapshot.placements.size());
    for (const LayoutPlacementSnapshot& placement : snapshot.placements)
        expectedHazards.push_back(makeHazard(placement.placedItem));

    std::vector<bool> matched(expectedHazards.size(), false);
    for (const Hazard& actual : snapshot.cdeSnapshot.dynamicHazards)
    {
        bool found = false;
        for (std::size_t i = 0; i < expectedHazards.size(); ++i)
        {
            if (!matched[i] && sameHazard(actual, expectedHazards[i]))
            {
                matched[i] = true;
                found = true;
                break;
            }
        }
        if (!found)
        {
            return Result<void>::fromError(
                {ErrorCode::SnapshotMismatch,
                 "snapshot dynamic hazards do not match placements"});
        }
    }

    auto newCde = snapshot.container.createCollisionEngine();
    if (!newCde)
    {
        return Result<void>::fromError(
            {ErrorCode::CollisionEngineFailure,
             "snapshot collision engine is unavailable"});
    }

    std::size_t slotCount = 0;
    for (const LayoutPlacementSnapshot& placement : snapshot.placements)
        slotCount = std::max(slotCount, placement.handle.index + 1);
    std::vector<Slot> newSlots(slotCount);
    std::vector<bool> seen(slotCount, false);

    for (const LayoutPlacementSnapshot& placement : snapshot.placements)
    {
        if (placement.handle.generation == 0 ||
            placement.handle.index >= newSlots.size() ||
            seen[placement.handle.index])
        {
            return Result<void>::fromError(
                {ErrorCode::SnapshotMismatch,
                 "snapshot contains an invalid or duplicate placement handle"});
        }

        Slot& slot = newSlots[placement.handle.index];
        slot.generation = placement.handle.generation;
        slot.placedItem = placement.placedItem;
        const auto registered = newCde->registerHazard(makeHazard(*slot.placedItem));
        if (!registered.hasValue())
        {
            return Result<void>::fromError(
                {ErrorCode::SnapshotMismatch,
                 "snapshot placement could not be registered"});
        }
        slot.cdeHandle = registered.value();
        seen[placement.handle.index] = true;
    }

    m_Container = snapshot.container;
    m_Cde = std::move(newCde);
    m_Slots = std::move(newSlots);
    return Result<void>::fromValue();
}

JAGUARCORE_API const Container& Layout::container() const noexcept
{
    return m_Container;
}

JAGUARCORE_API std::size_t Layout::placedItemCount() const noexcept
{
    return static_cast<std::size_t>(std::count_if(
        m_Slots.begin(), m_Slots.end(), [](const Slot& slot) {
            return slot.placedItem.has_value();
        }));
}

JAGUARCORE_API std::optional<double> Layout::rightmostCollisionX() const noexcept
{
    std::optional<double> rightmost;
    for (const Slot& slot : m_Slots)
    {
        if (!slot.placedItem.has_value())
            continue;
        const double xMax = slot.placedItem->collisionShape().boundingBox().xMax;
        if (!rightmost.has_value() || xMax > *rightmost)
            rightmost = xMax;
    }
    return rightmost;
}
}
