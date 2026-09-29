#include "JaguarCore/Problems/MSpp.h"
#include "JaguarCore/Assertions.h"
#include "JaguarCore/NumericPolicy.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>
#include <utility>

namespace jaguar
{
namespace
{
constexpr std::size_t kLayoutKeyPartBits = sizeof(std::size_t) * 4U;
constexpr std::size_t kLayoutKeyPartMask =
    std::numeric_limits<std::size_t>::max() >> kLayoutKeyPartBits;

std::size_t makeLayoutKey(std::size_t slotIndex,
                          std::uint32_t generation) noexcept
{
    return (static_cast<std::size_t>(generation & kLayoutKeyPartMask)
            << kLayoutKeyPartBits) |
           (slotIndex & kLayoutKeyPartMask);
}

void splitLayoutKey(std::size_t key,
                    std::size_t& slotIndex,
                    std::uint32_t& generation) noexcept
{
    slotIndex = key & kLayoutKeyPartMask;
    generation = static_cast<std::uint32_t>(key >> kLayoutKeyPartBits);
}

std::uint32_t nextLayoutGeneration(std::uint32_t generation) noexcept
{
    return static_cast<std::uint32_t>(
        (static_cast<std::size_t>(generation) + 1U) & kLayoutKeyPartMask);
}

bool near(double first, double second) noexcept
{
    return std::abs(first - second) <=
           NumericPolicy::DefaultEqualityTolerance();
}

bool sameItemDefinition(const Item& expected,
                        const PlacedItem& actual) noexcept
{
    if (expected.Index() != actual.GetItemIndex() ||
        expected.ExternalId() != actual.GetItem().ExternalId() ||
        expected.CollisionShape().VertexCount() !=
            actual.GetItem().CollisionShape().VertexCount())
        return false;
    for (std::size_t i = 0; i < expected.CollisionShape().VertexCount(); ++i)
    {
        if (!near(expected.CollisionShape().Vertices()[i].x,
                  actual.GetItem().CollisionShape().Vertices()[i].x) ||
            !near(expected.CollisionShape().Vertices()[i].y,
                  actual.GetItem().CollisionShape().Vertices()[i].y))
            return false;
    }
    return true;
}
}

MSPProblem::MSPProblem(MSPInstance instance,
                       std::vector<std::size_t> remainingDemand)
    : m_Instance(std::move(instance)),
      m_Layouts(1),
      m_RemainingDemand(std::move(remainingDemand))
{
    m_Layouts.front().generation = 0;
}

JAGUARCORE_API Result<MSPProblem> MSPProblem::Create(MSPInstance instance)
{
    std::vector<std::size_t> remaining(instance.ItemCount());
    for (std::size_t i = 0; i < remaining.size(); ++i)
        remaining[i] = instance.ItemDemand(i);
    return Result<MSPProblem>::FromValue(
        MSPProblem{std::move(instance), std::move(remaining)});
}

MSPProblem::LayoutSlot* MSPProblem::GetLayout(std::size_t layoutIndex) noexcept
{
    std::size_t slotIndex = 0;
    std::uint32_t generation = 0;
    splitLayoutKey(layoutIndex, slotIndex, generation);
    if (slotIndex >= m_Layouts.size() ||
        m_Layouts[slotIndex].generation != generation ||
        !m_Layouts[slotIndex].layout.has_value())
        return nullptr;
    return &m_Layouts[slotIndex];
}

const MSPProblem::LayoutSlot* MSPProblem::GetLayout(
    std::size_t layoutIndex) const noexcept
{
    std::size_t slotIndex = 0;
    std::uint32_t generation = 0;
    splitLayoutKey(layoutIndex, slotIndex, generation);
    if (slotIndex >= m_Layouts.size() ||
        m_Layouts[slotIndex].generation != generation ||
        !m_Layouts[slotIndex].layout.has_value())
        return nullptr;
    return &m_Layouts[slotIndex];
}

std::size_t MSPProblem::AllocateLayoutSlot(LayoutSlot slot)
{
    std::size_t slotIndex = 0;
    if (!m_FreeLayoutIndices.empty())
    {
        slotIndex = m_FreeLayoutIndices.back();
        m_FreeLayoutIndices.pop_back();
        slot.generation = m_Layouts[slotIndex].generation | 1U;
        m_Layouts[slotIndex] = std::move(slot);
    }
    else
    {
        slotIndex = m_Layouts.size();
        m_Layouts.push_back(std::move(slot));
    }
    return makeLayoutKey(slotIndex, m_Layouts[slotIndex].generation);
}

void MSPProblem::ReleaseLayoutSlot(std::size_t layoutIndex)
{
    std::size_t slotIndex = 0;
    std::uint32_t generation = 0;
    splitLayoutKey(layoutIndex, slotIndex, generation);
    if (slotIndex >= m_Layouts.size() ||
        m_Layouts[slotIndex].generation != generation ||
        !m_Layouts[slotIndex].layout.has_value())
        return;
    m_Layouts[slotIndex].layout.reset();
    m_Layouts[slotIndex].generation = nextLayoutGeneration(generation);
    m_FreeLayoutIndices.push_back(slotIndex);
}

JAGUARCORE_API Result<std::size_t> MSPProblem::AddLayout(MultiStrip strip)
{
    const auto normalized = MultiStrip::Create(
        strip.maxWidth, strip.fixedHeight, strip.cdeConfig,
        strip.shapeModifyConfig, strip.width);
    if (!normalized.HasValue())
        return Result<std::size_t>::FromError(normalized.GetError());
    strip = normalized.Value();

    const auto container = strip.CreateContainer();
    if (!container.HasValue())
        return Result<std::size_t>::FromError(container.GetError());
    const std::size_t layoutIndex = AllocateLayoutSlot(
        LayoutSlot{Layout{container.Value()}, strip});
    return Result<std::size_t>::FromValue(layoutIndex);
}

JAGUARCORE_API Result<void> MSPProblem::RemoveLayout(std::size_t layoutIndex)
{
    LayoutSlot* slot = GetLayout(layoutIndex);
    if (!slot)
    {
        return Result<void>::FromError(
            {ErrorCode::InvalidInput, "MSPP layout index is invalid"});
    }
    const auto snapshot = slot->layout->Save();
    for (const LayoutPlacementSnapshot& placement : snapshot.placements)
    {
        const ItemIndex itemIndex = placement.placedItem.GetItemIndex();
        if (itemIndex >= m_RemainingDemand.size())
        {
            return Result<void>::FromError(
                {ErrorCode::SnapshotMismatch, "MSPP layout item is not in instance"});
        }
        ++m_RemainingDemand[itemIndex];
    }
    ReleaseLayoutSlot(layoutIndex);
    DebugAssertMSPSolution(*this);
    return Result<void>::FromValue();
}

JAGUARCORE_API Result<MSPPlacedHandle> MSPProblem::PlaceItem(
    const MSPPlacement& placement)
{
    if (placement.itemIndex >= m_Instance.ItemCount())
    {
        return Result<MSPPlacedHandle>::FromError(
            {ErrorCode::InvalidInput, "MSPP placement item index is invalid"});
    }
#ifndef NDEBUG
    if (m_RemainingDemand[placement.itemIndex] == 0)
    {
        return Result<MSPPlacedHandle>::FromError(
            {ErrorCode::Infeasible, "MSPP item demand is exhausted"});
    }
#endif
    LayoutSlot* slot = GetLayout(placement.layoutIndex);
    if (!slot)
    {
        return Result<MSPPlacedHandle>::FromError(
            {ErrorCode::InvalidInput, "MSPP layout index is invalid"});
    }
    const auto placed = slot->layout->PlaceItem(
        m_Instance.GetItem(placement.itemIndex), placement.transform);
    if (!placed.HasValue())
        return Result<MSPPlacedHandle>::FromError(placed.GetError());
    --m_RemainingDemand[placement.itemIndex];
    DebugAssertMSPSolution(*this);
    return Result<MSPPlacedHandle>::FromValue(
        MSPPlacedHandle{placement.layoutIndex, placed.Value()});
}

JAGUARCORE_API Result<MSPPlacement> MSPProblem::RemoveItem(
    const MSPPlacedHandle& handle)
{
    LayoutSlot* slot = GetLayout(handle.layoutIndex);
    if (!slot)
    {
        return Result<MSPPlacement>::FromError(
            {ErrorCode::InvalidInput, "MSPP layout index is invalid"});
    }
    const auto removed = slot->layout->RemoveItem(handle.placedItemHandle);
    if (!removed.HasValue())
        return Result<MSPPlacement>::FromError(removed.GetError());
    const ItemIndex itemIndex = removed.Value().GetItemIndex();
    if (itemIndex >= m_RemainingDemand.size())
    {
        return Result<MSPPlacement>::FromError(
            {ErrorCode::SnapshotMismatch, "MSPP item is not in instance"});
    }
    ++m_RemainingDemand[itemIndex];
    DebugAssertMSPSolution(*this);
    return Result<MSPPlacement>::FromValue(
        MSPPlacement{handle.layoutIndex, itemIndex, removed.Value().Transform()});
}

JAGUARCORE_API Result<void> MSPProblem::ChangeStripWidth(
    std::size_t layoutIndex, double width)
{
    LayoutSlot* slot = GetLayout(layoutIndex);
    if (!slot)
    {
        return Result<void>::FromError(
            {ErrorCode::InvalidInput, "MSPP layout index is invalid"});
    }
    MultiStrip next = slot->strip;
    const auto changed = next.SetWidth(width);
    if (!changed.HasValue())
        return changed;
    const auto container = next.CreateContainer();
    if (!container.HasValue())
        return Result<void>::FromError(container.GetError());
    const auto swapped = slot->layout->SwapContainer(container.Value());
    if (!swapped.HasValue())
        return swapped;
    slot->strip = next;
    DebugAssertMSPSolution(*this);
    return Result<void>::FromValue();
}

JAGUARCORE_API Result<void> MSPProblem::FitStrip(std::size_t layoutIndex)
{
    const LayoutSlot* slot = GetLayout(layoutIndex);
    if (!slot)
    {
        return Result<void>::FromError(
            {ErrorCode::InvalidInput, "MSPP layout index is invalid"});
    }
    const auto rightmost = slot->layout->RightmostCollisionX();
    if (!rightmost.has_value())
    {
        return Result<void>::FromError(
            {ErrorCode::Infeasible, "cannot fit an empty MSPP strip"});
    }
    const float itemXMax = static_cast<float>(*rightmost);
    const float offset = static_cast<float>(
        slot->strip.shapeModifyConfig.offset.value_or(0.0));
    const float fittedWidth = itemXMax * 1.00001F + offset;
    if (!std::isfinite(fittedWidth) || fittedWidth <= 0.0F)
    {
        return Result<void>::FromError(
            {ErrorCode::Infeasible, "computed MSPP strip width is invalid"});
    }
    return ChangeStripWidth(layoutIndex, static_cast<double>(fittedWidth));
}

JAGUARCORE_API MSPSolution MSPProblem::Save() const
{
    MSPSolution solution;
    for (std::size_t slotIndex = 0; slotIndex < m_Layouts.size(); ++slotIndex)
    {
        const LayoutSlot& slot = m_Layouts[slotIndex];
        if (slot.layout.has_value())
            solution.layouts.push_back(
                {makeLayoutKey(slotIndex, slot.generation), slot.strip,
                 slot.layout->Save()});
    }
    return solution;
}

JAGUARCORE_API Result<bool> MSPProblem::Restore(const MSPSolution& solution)
{
    struct PreparedLayout
    {
        std::size_t requestedKey = 0;
        MultiStrip strip{};
        Layout layout;
        std::size_t slotIndex = 0;
        bool preservesKey = false;
    };

    std::vector<std::size_t> remaining(m_Instance.ItemCount(), 0);
    for (std::size_t i = 0; i < remaining.size(); ++i)
        remaining[i] = m_Instance.ItemDemand(i);

    std::unordered_set<std::size_t> seenKeys;
    std::unordered_set<std::size_t> seenSlots;
    std::vector<PreparedLayout> prepared;
    prepared.reserve(solution.layouts.size());
    for (const MSPLayoutSnapshot& snapshot : solution.layouts)
    {
        std::size_t requestedSlot = 0;
        std::uint32_t requestedGeneration = 0;
        splitLayoutKey(snapshot.layoutIndex, requestedSlot, requestedGeneration);
        if (requestedSlot == 0 || (requestedGeneration & 1U) == 0 ||
            !seenKeys.insert(snapshot.layoutIndex).second ||
            !seenSlots.insert(requestedSlot).second)
        {
            return Result<bool>::FromError(
                {ErrorCode::SnapshotMismatch, "MSPP solution strip is invalid"});
        }
        Layout restored(snapshot.layoutSnapshot.container);
        const auto restoredResult = restored.Restore(snapshot.layoutSnapshot);
        if (!restoredResult.HasValue())
            return Result<bool>::FromError(restoredResult.GetError());
        for (const LayoutPlacementSnapshot& placement :
             snapshot.layoutSnapshot.placements)
        {
            const ItemIndex itemIndex = placement.placedItem.GetItemIndex();
            if (itemIndex >= remaining.size() || remaining[itemIndex] == 0 ||
                !sameItemDefinition(m_Instance.GetItem(itemIndex), placement.placedItem))
            {
                return Result<bool>::FromError(
                    {ErrorCode::SnapshotMismatch,
                     "MSPP solution contains an item beyond demand"});
            }
            --remaining[itemIndex];
        }
        prepared.push_back(PreparedLayout{
            snapshot.layoutIndex, snapshot.strip, std::move(restored)});
    }

    // Rust iterates its SecondaryMap by slot index. Keep stale-key reinsertions
    // deterministic even when a C++ solution vector is ordered differently.
    std::sort(prepared.begin(), prepared.end(),
              [](const PreparedLayout& left, const PreparedLayout& right) {
                  return (left.requestedKey & kLayoutKeyPartMask) <
                         (right.requestedKey & kLayoutKeyPartMask);
              });

    std::vector<bool> preservedCurrent(m_Layouts.size(), false);
    for (PreparedLayout& layout : prepared)
    {
        std::size_t slotIndex = 0;
        std::uint32_t generation = 0;
        splitLayoutKey(layout.requestedKey, slotIndex, generation);
        if (GetLayout(layout.requestedKey))
        {
            layout.slotIndex = slotIndex;
            layout.preservesKey = true;
            preservedCurrent[slotIndex] = true;
        }
    }

    bool layoutKeysChanged = false;
    std::vector<LayoutSlot> newLayouts(m_Layouts.size());
    std::vector<std::size_t> newFreeLayoutIndices = m_FreeLayoutIndices;
    for (std::size_t slotIndex = 0; slotIndex < m_Layouts.size(); ++slotIndex)
    {
        const LayoutSlot& current = m_Layouts[slotIndex];
        newLayouts[slotIndex].generation = current.generation;
        if (current.layout.has_value() && !preservedCurrent[slotIndex])
        {
            newLayouts[slotIndex].generation =
                nextLayoutGeneration(current.generation);
            newFreeLayoutIndices.push_back(slotIndex);
            layoutKeysChanged = true;
        }
    }

    for (PreparedLayout& layout : prepared)
    {
        std::size_t slotIndex = layout.slotIndex;
        if (!layout.preservesKey)
        {
            layoutKeysChanged = true;
            if (newFreeLayoutIndices.empty())
            {
                slotIndex = newLayouts.size();
                newLayouts.emplace_back();
            }
            else
            {
                slotIndex = newFreeLayoutIndices.back();
                newFreeLayoutIndices.pop_back();
            }
        }

        const std::uint32_t generation = newLayouts[slotIndex].generation | 1U;
        if (layout.preservesKey)
        {
            LayoutSlot* current = GetLayout(layout.requestedKey);
            if (current)
            {
                const auto restored =
                    current->layout->Restore(layout.layout.Save());
                if (restored.HasValue())
                {
                    LayoutSlot preserved = std::move(m_Layouts[slotIndex]);
                    preserved.strip = layout.strip;
                    preserved.generation = generation;
                    newLayouts[slotIndex] = std::move(preserved);
                    continue;
                }
            }
        }
        newLayouts[slotIndex] = LayoutSlot{
            std::move(layout.layout), layout.strip, generation};
    }

    m_Layouts = std::move(newLayouts);
    m_FreeLayoutIndices = std::move(newFreeLayoutIndices);
    m_RemainingDemand = std::move(remaining);
    DebugAssertMSPSolution(*this);
    return Result<bool>::FromValue(layoutKeysChanged);
}

JAGUARCORE_API std::size_t MSPProblem::RemainingDemand(
    ItemIndex index) const noexcept
{
    return index < m_RemainingDemand.size() ? m_RemainingDemand[index] : 0;
}

JAGUARCORE_API std::size_t MSPProblem::PlacedItemCount() const noexcept
{
    std::size_t count = 0;
    for (const LayoutSlot& slot : m_Layouts)
    {
        if (slot.layout.has_value())
            count += slot.layout->PlacedItemCount();
    }
    return count;
}

JAGUARCORE_API std::size_t MSPProblem::LayoutCount() const noexcept
{
    return static_cast<std::size_t>(std::count_if(
        m_Layouts.begin(), m_Layouts.end(), [](const LayoutSlot& slot) {
            return slot.layout.has_value();
        }));
}

JAGUARCORE_API double MSPProblem::StripWidth(std::size_t layoutIndex) const noexcept
{
    const LayoutSlot* slot = GetLayout(layoutIndex);
    return slot ? slot->strip.width : 0.0;
}

JAGUARCORE_API std::vector<std::size_t> MSPProblem::ExtendableStrips() const
{
    std::vector<std::size_t> result;
    for (std::size_t slotIndex = 0; slotIndex < m_Layouts.size(); ++slotIndex)
    {
        const LayoutSlot& slot = m_Layouts[slotIndex];
        if (slot.layout.has_value() && slot.strip.width < slot.strip.maxWidth)
            result.push_back(makeLayoutKey(slotIndex, slot.generation));
    }
    return result;
}

JAGUARCORE_API double MSPProblem::TotalStripWidth() const noexcept
{
    float result = 0.0F;
    for (const LayoutSlot& slot : m_Layouts)
    {
        if (slot.layout.has_value())
            result += static_cast<float>(slot.strip.width);
    }
    return static_cast<double>(result);
}

JAGUARCORE_API double MSPProblem::Density() const
{
    float containerArea = 0.0f;
    float itemArea = 0.0f;
    for (const LayoutSlot& slot : m_Layouts)
    {
        if (!slot.layout.has_value())
            continue;
        containerArea +=
            static_cast<float>(slot.layout->GetContainer().Area());
        itemArea += slot.layout->PlacedItemArea();
    }
    return static_cast<double>(itemArea / containerArea);
}

JAGUARCORE_API const MSPInstance& MSPProblem::Instance() const noexcept
{
    return m_Instance;
}
}
