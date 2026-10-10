#include "JaguarCore/Entities/Layout.h"
#include "JaguarCore/Assertions.h"
#include "JaguarCore/NumericPolicy.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
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

JAGUARCORE_API float LayoutSnapshot::PlacedItemArea() const
{
    std::vector<const LayoutPlacementSnapshot*> orderedPlacements;
    orderedPlacements.reserve(placements.size());
    for (const LayoutPlacementSnapshot& placement : placements)
        orderedPlacements.push_back(&placement);
    std::sort(orderedPlacements.begin(), orderedPlacements.end(),
              [](const LayoutPlacementSnapshot* left,
                 const LayoutPlacementSnapshot* right) {
                  if (left->handle.index != right->handle.index)
                      return left->handle.index < right->handle.index;
                  return left->handle.generation < right->handle.generation;
              });

    float result = 0.0F;
    for (const LayoutPlacementSnapshot* placement : orderedPlacements)
        result += static_cast<float>(placement->placedItem.Area());
    return result;
}

Layout::Layout(Container container)
    : m_Container(std::move(container)),
      m_Cde(m_Container.CreateCollisionEngine()),
      m_Slots(1)
{
    m_Slots.front().generation = 0;
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
    if (!m_FreeSlotIndices.empty())
    {
        const std::size_t slotIndex = m_FreeSlotIndices.back();
        m_FreeSlotIndices.pop_back();
        Slot& slot = m_Slots[slotIndex];
        slot.generation |= 1U;
        return PlacedItemHandle{slotIndex, slot.generation};
    }

    m_Slots.push_back(Slot{});
    return PlacedItemHandle{m_Slots.size() - 1, m_Slots.back().generation};
}

Hazard Layout::MakeHazard(const PlacedItem& placedItem,
                          PlacedItemHandle handle)
{
    return Hazard{
        {HazardKind::PlacedItem, placedItem.GetItemIndex(), 0, 0,
         handle, placedItem.Transform()},
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

    const auto registered =
        m_Cde->RegisterHazard(MakeHazard(*slot.placedItem, handle));
    if (!registered.HasValue())
    {
        slot.placedItem.reset();
        ++slot.generation;
        m_FreeSlotIndices.push_back(handle.index);
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
    m_FreeSlotIndices.push_back(handle.index);
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
    const CollisionSnapshot oldCdeSnapshot = m_Cde->Save();
    for (const Hazard& hazard : oldCdeSnapshot.dynamicHazards)
    {
        const PlacedItemHandle handle = hazard.entity.placedItemHandle;
        if (hazard.entity.kind != HazardKind::PlacedItem || !Get(handle))
        {
            return Result<void>::FromError(
                {ErrorCode::SnapshotMismatch,
                 "dynamic collision hazard does not match a placed item"});
        }

        const auto registered = newCde->RegisterHazard(hazard);
        if (!registered.HasValue())
        {
            return Result<void>::FromError(
                {ErrorCode::CollisionEngineFailure,
                 "could not re-register a placed item in the new container"});
        }
        newHandles[handle.index] = registered.Value();
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
        HazardFilter filter;
        filter.irrelevantHandle = [ownHazard](HazardHandle handle,
                                              const HazardEntity&) {
            return handle == ownHazard;
        };
        if (m_Cde->DetectPolygonCollision(
                m_Slots[i].placedItem->CollisionShape(), filter))
            return false;
    }
    return true;
}

JAGUARCORE_API float Layout::PlacedItemArea() const noexcept
{
    float placedArea = 0.0f;
    for (const Slot& slot : m_Slots)
    {
        if (slot.placedItem.has_value())
            placedArea += static_cast<float>(slot.placedItem->Area());
    }
    return placedArea;
}

JAGUARCORE_API double Layout::Density() const
{
    const float containerArea = static_cast<float>(m_Container.Area());
    return static_cast<double>(PlacedItemArea() / containerArea);
}

JAGUARCORE_API LayoutSnapshot Layout::Save() const
{
    LayoutSnapshot snapshot{m_Container, {}, m_Cde->Save(), {},
                            m_FreeSlotIndices};
    snapshot.slotGenerations.reserve(m_Slots.size());
    for (const Slot& slot : m_Slots)
        snapshot.slotGenerations.push_back(slot.generation);
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
    const bool dynamicHazardsIncomplete =
        !snapshot.placements.empty() &&
        snapshot.cdeSnapshot.dynamicHazards.size() < snapshot.placements.size();
    if (!m_Container.SameBase(snapshot.container) ||
        snapshot.cdeSnapshot.dynamicHazards.size() > snapshot.placements.size())
    {
        return Result<void>::FromError(
            {ErrorCode::SnapshotMismatch,
             "layout and snapshot do not share the same static state"});
    }

    std::vector<Hazard> expectedHazards;
    expectedHazards.reserve(snapshot.placements.size());
    for (const LayoutPlacementSnapshot& placement : snapshot.placements)
        expectedHazards.push_back(
            MakeHazard(placement.placedItem, placement.handle));

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

    const bool hasSlotMetadata = !snapshot.slotGenerations.empty();
    if (!hasSlotMetadata && !snapshot.freeSlotIndices.empty())
    {
        return Result<void>::FromError(
            {ErrorCode::SnapshotMismatch,
             "snapshot free slots have no generation metadata"});
    }

    std::size_t slotCount = snapshot.slotGenerations.size();
    if (!hasSlotMetadata)
    {
        slotCount = 1;
        for (const LayoutPlacementSnapshot& placement : snapshot.placements)
            slotCount = std::max(slotCount, placement.handle.index + 1);
    }
    std::vector<Slot> newSlots(slotCount);
    std::vector<bool> seen(slotCount, false);
    if (hasSlotMetadata)
    {
        if (snapshot.slotGenerations[0] != 0)
        {
            return Result<void>::FromError(
                {ErrorCode::SnapshotMismatch,
                 "snapshot sentinel slot metadata is invalid"});
        }
        for (std::size_t i = 0; i < slotCount; ++i)
            newSlots[i].generation = snapshot.slotGenerations[i];
    }
    else
    {
        newSlots[0].generation = 0;
    }

    for (const LayoutPlacementSnapshot& placement : snapshot.placements)
    {
        if (placement.handle.index == 0 ||
            placement.handle.generation == 0 ||
            (placement.handle.generation & 1U) == 0 ||
            placement.handle.index >= newSlots.size() ||
            seen[placement.handle.index] ||
            (hasSlotMetadata &&
             snapshot.slotGenerations[placement.handle.index] !=
                 placement.handle.generation))
        {
            return Result<void>::FromError(
                {ErrorCode::SnapshotMismatch,
                 "snapshot contains an invalid or duplicate placement handle"});
        }

        Slot& slot = newSlots[placement.handle.index];
        slot.generation = placement.handle.generation;
        slot.placedItem = placement.placedItem;
        seen[placement.handle.index] = true;
    }

    std::vector<std::size_t> newFreeSlotIndices;
    if (hasSlotMetadata)
    {
        std::vector<bool> freeSeen(slotCount, false);
        for (const std::size_t slotIndex : snapshot.freeSlotIndices)
        {
            if (slotIndex >= slotCount || seen[slotIndex] ||
                freeSeen[slotIndex] ||
                (newSlots[slotIndex].generation & 1U) != 0)
            {
                return Result<void>::FromError(
                    {ErrorCode::SnapshotMismatch,
                     "snapshot free slot metadata is invalid"});
            }
            freeSeen[slotIndex] = true;
        }
        for (std::size_t slotIndex = 0; slotIndex < slotCount; ++slotIndex)
        {
            if (slotIndex == 0)
            {
                if (seen[slotIndex] || freeSeen[slotIndex])
                    return Result<void>::FromError(
                        {ErrorCode::SnapshotMismatch,
                         "snapshot sentinel slot metadata is invalid"});
                continue;
            }
            if (seen[slotIndex] == freeSeen[slotIndex])
            {
                return Result<void>::FromError(
                    {ErrorCode::SnapshotMismatch,
                     "snapshot slot metadata is incomplete"});
            }
        }
        newFreeSlotIndices = snapshot.freeSlotIndices;
    }
    else
    {
        for (std::size_t slotIndex = 1; slotIndex < slotCount; ++slotIndex)
        {
            if (seen[slotIndex])
                continue;
            newSlots[slotIndex].generation = 0;
            newFreeSlotIndices.push_back(slotIndex);
        }
    }

    const auto restoredCde = m_Cde->Restore(snapshot.cdeSnapshot);
    if (!restoredCde.HasValue())
        return restoredCde;

    for (std::size_t slotIndex = 1; slotIndex < newSlots.size(); ++slotIndex)
    {
        Slot& slot = newSlots[slotIndex];
        if (!slot.placedItem.has_value())
            continue;
        const Hazard hazard = MakeHazard(
            *slot.placedItem, {slotIndex, slot.generation});
        const auto cdeHandle = m_Cde->FindHazardHandle(hazard.entity);
        if (!cdeHandle.has_value())
        {
            if (dynamicHazardsIncomplete)
                continue;
            return Result<void>::FromError(
                {ErrorCode::SnapshotMismatch,
                 "restored collision hazard is missing"});
        }
        slot.cdeHandle = *cdeHandle;
    }

    m_Container = snapshot.container;
    m_Slots = std::move(newSlots);
    m_FreeSlotIndices = std::move(newFreeSlotIndices);
#ifndef NDEBUG
    if (dynamicHazardsIncomplete)
        throw std::logic_error("layout quadtree does not match placed items");
#endif
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

JAGUARCORE_API std::vector<std::reference_wrapper<const Item>> Layout::Items() const
{
    std::vector<std::reference_wrapper<const Item>> items;
    std::vector<ItemIndex> seenIndices;
    for (const Slot& slot : m_Slots)
    {
        if (!slot.placedItem.has_value())
            continue;

        const Item& item = slot.placedItem->GetItem();
        if (std::find(seenIndices.begin(), seenIndices.end(), item.Index()) !=
            seenIndices.end())
        {
            continue;
        }

        seenIndices.push_back(item.Index());
        items.emplace_back(item);
    }
    return items;
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
