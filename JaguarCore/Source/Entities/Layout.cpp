#include "JaguarCore/Entities/Layout.h"
#include "JaguarCore/Assertions.h"
#include "JaguarCore/NumericPolicy.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace jaguar
{
namespace
{
bool samePolygon(const SimplePolygon& first, const SimplePolygon& second) noexcept
{
    const double tolerance = NumericPolicy::DefaultEqualityTolerance();
    if (first.VertexCount() != second.VertexCount())
        return false;
    for (std::size_t i = 0; i < first.VertexCount(); ++i)
    {
        if (std::abs(first.Vertices()[i].x - second.Vertices()[i].x) > tolerance ||
            std::abs(first.Vertices()[i].y - second.Vertices()[i].y) > tolerance)
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
      m_Cde(m_Container.CreateCollisionEngine())
{
}

Layout::Slot* Layout::Get(PlacedItemHandle handle) noexcept
{
    if (handle.index >= m_Slots.size())
        return nullptr;
    Slot& slot = m_Slots[handle.index];
    if (slot.generation != handle.generation || !slot.placedItem.has_value())
        return nullptr;
    return &slot;
}

const Layout::Slot* Layout::Get(PlacedItemHandle handle) const noexcept
{
    if (handle.index >= m_Slots.size())
        return nullptr;
    const Slot& slot = m_Slots[handle.index];
    if (slot.generation != handle.generation || !slot.placedItem.has_value())
        return nullptr;
    return &slot;
}

PlacedItemHandle Layout::AllocateHandle()
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

Hazard Layout::MakeHazard(const PlacedItem& placedItem)
{
    return Hazard{
        {HazardKind::PlacedItem, placedItem.GetItemIndex(), 0, 0},
        std::make_shared<const SimplePolygon>(placedItem.CollisionShape()),
        true};
}

JAGUARCORE_API Result<PlacedItemHandle> Layout::PlaceItem(const Item& item,
                                                          Transform2d transform)
{
    if (!m_Cde)
    {
        return Result<PlacedItemHandle>::FromError(
            {ErrorCode::CollisionEngineFailure, "layout collision engine is unavailable"});
    }

    const auto created = PlacedItem::Create(item, transform);
    if (!created.HasValue())
        return Result<PlacedItemHandle>::FromError(created.GetError());

    const PlacedItemHandle handle = AllocateHandle();
    Slot& slot = m_Slots[handle.index];
    slot.placedItem = created.Value();

    const auto registered = m_Cde->RegisterHazard(MakeHazard(*slot.placedItem));
    if (!registered.HasValue())
    {
        slot.placedItem.reset();
        return Result<PlacedItemHandle>::FromError(registered.GetError());
    }
    slot.cdeHandle = registered.Value();
    DebugAssertLayout(*this);
    return Result<PlacedItemHandle>::FromValue(handle);
}

JAGUARCORE_API Result<PlacedItem> Layout::RemoveItem(PlacedItemHandle handle)
{
    Slot* slot = Get(handle);
    if (!slot)
    {
        return Result<PlacedItem>::FromError(
            {ErrorCode::InvalidInput, "invalid placed item handle"});
    }

    const auto deregistered = m_Cde->DeregisterHazard(slot->cdeHandle);
    if (!deregistered.HasValue())
        return Result<PlacedItem>::FromError(deregistered.GetError());

    PlacedItem removed = std::move(*slot->placedItem);
    slot->placedItem.reset();
    ++slot->generation;
    DebugAssertLayout(*this);
    return Result<PlacedItem>::FromValue(std::move(removed));
}

JAGUARCORE_API Result<void> Layout::SwapContainer(Container container)
{
    auto newCde = container.CreateCollisionEngine();
    if (!newCde)
    {
        return Result<void>::FromError(
            {ErrorCode::CollisionEngineFailure,
             "new container collision engine is unavailable"});
    }

    std::vector<HazardHandle> newHandles(m_Slots.size());
    for (std::size_t i = 0; i < m_Slots.size(); ++i)
    {
        if (!m_Slots[i].placedItem.has_value())
            continue;
        const auto registered = newCde->RegisterHazard(
            MakeHazard(*m_Slots[i].placedItem));
        if (!registered.HasValue())
        {
            return Result<void>::FromError(
                {ErrorCode::CollisionEngineFailure,
                 "could not re-register a placed item in the new container"});
        }
        newHandles[i] = registered.Value();
    }

    m_Container = std::move(container);
    m_Cde = std::move(newCde);
    for (std::size_t i = 0; i < m_Slots.size(); ++i)
    {
        if (m_Slots[i].placedItem.has_value())
            m_Slots[i].cdeHandle = newHandles[i];
    }
    DebugAssertLayout(*this);
    return Result<void>::FromValue();
}

JAGUARCORE_API bool Layout::IsFeasible() const
{
    if (!m_Cde)
        return false;

    for (std::size_t i = 0; i < m_Slots.size(); ++i)
    {
        if (!m_Slots[i].placedItem.has_value())
            continue;

        const HazardHandle ownHazard = m_Slots[i].cdeHandle;
        const std::optional<std::size_t> minQuality =
            m_Slots[i].placedItem->GetItem().MinQuality();
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
        if (m_Cde->DetectPolygonCollision(
                m_Slots[i].placedItem->CollisionShape(), filter))
            return false;
    }
    return true;
}

JAGUARCORE_API double Layout::Density() const
{
    const double containerArea = m_Container.Area();
    if (containerArea <= 0.0)
        return 0.0;

    double placedArea = 0.0;
    for (const Slot& slot : m_Slots)
    {
        if (slot.placedItem.has_value())
            placedArea += slot.placedItem->Area();
    }
    return placedArea / containerArea;
}

JAGUARCORE_API LayoutSnapshot Layout::Save() const
{
    LayoutSnapshot snapshot{m_Container, {}, m_Cde->Save()};
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

JAGUARCORE_API Result<void> Layout::Restore(const LayoutSnapshot& snapshot)
{
    if (!m_Container.SameBase(snapshot.container) ||
        snapshot.cdeSnapshot.dynamicHazards.size() != snapshot.placements.size())
    {
        return Result<void>::FromError(
            {ErrorCode::SnapshotMismatch,
             "layout and snapshot do not share the same static state"});
    }

    std::vector<Hazard> expectedHazards;
    expectedHazards.reserve(snapshot.placements.size());
    for (const LayoutPlacementSnapshot& placement : snapshot.placements)
        expectedHazards.push_back(MakeHazard(placement.placedItem));

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
            return Result<void>::FromError(
                {ErrorCode::SnapshotMismatch,
                 "snapshot dynamic hazards do not match placements"});
        }
    }

    auto newCde = snapshot.container.CreateCollisionEngine();
    if (!newCde)
    {
        return Result<void>::FromError(
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
            return Result<void>::FromError(
                {ErrorCode::SnapshotMismatch,
                 "snapshot contains an invalid or duplicate placement handle"});
        }

        Slot& slot = newSlots[placement.handle.index];
        slot.generation = placement.handle.generation;
        slot.placedItem = placement.placedItem;
        const auto registered = newCde->RegisterHazard(MakeHazard(*slot.placedItem));
        if (!registered.HasValue())
        {
            return Result<void>::FromError(
                {ErrorCode::SnapshotMismatch,
                 "snapshot placement could not be registered"});
        }
        slot.cdeHandle = registered.Value();
        seen[placement.handle.index] = true;
    }

    m_Container = snapshot.container;
    m_Cde = std::move(newCde);
    m_Slots = std::move(newSlots);
    return Result<void>::FromValue();
}

JAGUARCORE_API const Container& Layout::GetContainer() const noexcept
{
    return m_Container;
}

JAGUARCORE_API std::size_t Layout::PlacedItemCount() const noexcept
{
    return static_cast<std::size_t>(std::count_if(
        m_Slots.begin(), m_Slots.end(), [](const Slot& slot) {
            return slot.placedItem.has_value();
        }));
}

JAGUARCORE_API std::optional<double> Layout::RightmostCollisionX() const noexcept
{
    std::optional<double> rightmost;
    for (const Slot& slot : m_Slots)
    {
        if (!slot.placedItem.has_value())
            continue;
        const double xMax = slot.placedItem->CollisionShape().BoundingBox().xMax;
        if (!rightmost.has_value() || xMax > *rightmost)
            rightmost = xMax;
    }
    return rightmost;
}
}
