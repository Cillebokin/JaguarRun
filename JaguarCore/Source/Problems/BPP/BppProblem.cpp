#include "JaguarCore/Problems/Bpp.h"
#include "JaguarCore/Assertions.h"
#include "JaguarCore/NumericPolicy.h"

#include <algorithm>
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

BPProblem::BPProblem(BPInstance instance,
                     std::vector<std::size_t> remainingDemand,
                     std::vector<std::size_t> remainingBinStock)
    : m_Instance(std::move(instance)),
      m_Layouts(1),
      m_RemainingDemand(std::move(remainingDemand)),
      m_RemainingBinStock(std::move(remainingBinStock))
{
    m_Layouts.front().generation = 0;
}

JAGUARCORE_API Result<BPProblem> BPProblem::Create(BPInstance instance)
{
    std::vector<std::size_t> demand(instance.ItemCount());
    for (std::size_t i = 0; i < demand.size(); ++i)
        demand[i] = instance.ItemDemand(i);
    std::vector<std::size_t> stock(instance.BinCount());
    for (std::size_t i = 0; i < stock.size(); ++i)
        stock[i] = instance.GetBin(i).stock;
    return Result<BPProblem>::FromValue(
        BPProblem{std::move(instance), std::move(demand), std::move(stock)});
}

BPProblem::LayoutSlot* BPProblem::GetLayout(std::size_t layoutIndex) noexcept
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

const BPProblem::LayoutSlot* BPProblem::GetLayout(
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

std::size_t BPProblem::AllocateLayoutSlot(LayoutSlot slot)
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

void BPProblem::ReleaseLayoutSlot(std::size_t layoutIndex)
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

Result<std::size_t> BPProblem::OpenLayout(std::size_t binIndex)
{
    if (binIndex >= m_Instance.BinCount())
    {
        return Result<std::size_t>::FromError(
            {ErrorCode::InvalidInput, "BPP bin index is invalid"});
    }
    if (m_RemainingBinStock[binIndex] == 0)
    {
        return Result<std::size_t>::FromError(
            {ErrorCode::Infeasible, "BPP bin stock is exhausted"});
    }

    Layout layout(m_Instance.GetBin(binIndex).container);
    const std::size_t layoutIndex = AllocateLayoutSlot(
        LayoutSlot{std::move(layout), binIndex});
    --m_RemainingBinStock[binIndex];
    return Result<std::size_t>::FromValue(layoutIndex);
}

JAGUARCORE_API Result<BPPlacedHandle> BPProblem::PlaceItem(
    const BPPlacement& placement)
{
    if (placement.itemIndex >= m_Instance.ItemCount())
    {
        return Result<BPPlacedHandle>::FromError(
            {ErrorCode::InvalidInput, "BPP placement item index is invalid"});
    }
    if (m_RemainingDemand[placement.itemIndex] == 0)
    {
        return Result<BPPlacedHandle>::FromError(
            {ErrorCode::Infeasible, "BPP item demand is exhausted"});
    }

    std::size_t layoutIndex = placement.layoutIndex;
    bool opened = false;
    if (placement.targetKind == LayoutTargetKind::Closed)
    {
        const auto openedLayout = OpenLayout(placement.binIndex);
        if (!openedLayout.HasValue())
            return Result<BPPlacedHandle>::FromError(openedLayout.GetError());
        layoutIndex = openedLayout.Value();
        opened = true;
    }

    LayoutSlot* slot = GetLayout(layoutIndex);
    if (!slot)
    {
        if (opened)
        {
            ++m_RemainingBinStock[placement.binIndex];
            ReleaseLayoutSlot(layoutIndex);
        }
        return Result<BPPlacedHandle>::FromError(
            {ErrorCode::InvalidInput, "BPP open layout index is invalid"});
    }

    const auto placed = slot->layout->PlaceItem(
        m_Instance.GetItem(placement.itemIndex), placement.transform);
    if (!placed.HasValue())
    {
        if (opened)
        {
            ++m_RemainingBinStock[slot->binIndex];
            ReleaseLayoutSlot(layoutIndex);
        }
        return Result<BPPlacedHandle>::FromError(placed.GetError());
    }

    --m_RemainingDemand[placement.itemIndex];
    DebugAssertBPSolution(*this);
    return Result<BPPlacedHandle>::FromValue(
        BPPlacedHandle{layoutIndex, placed.Value()});
}

JAGUARCORE_API Result<BPPlacement> BPProblem::RemoveItem(
    const BPPlacedHandle& handle)
{
    LayoutSlot* slot = GetLayout(handle.layoutIndex);
    if (!slot)
    {
        return Result<BPPlacement>::FromError(
            {ErrorCode::InvalidInput, "BPP layout index is invalid"});
    }

    const auto removed = slot->layout->RemoveItem(handle.placedItemHandle);
    if (!removed.HasValue())
        return Result<BPPlacement>::FromError(removed.GetError());

    const ItemIndex itemIndex = removed.Value().GetItemIndex();
    if (itemIndex >= m_RemainingDemand.size())
    {
        return Result<BPPlacement>::FromError(
            {ErrorCode::SnapshotMismatch, "BPP placed item is not in the instance"});
    }
    ++m_RemainingDemand[itemIndex];

    BPPlacement result{
        LayoutTargetKind::Open, handle.layoutIndex, slot->binIndex,
        itemIndex, removed.Value().Transform()};
    if (slot->layout->PlacedItemCount() == 0)
    {
        const std::size_t binIndex = slot->binIndex;
        ++m_RemainingBinStock[binIndex];
        ReleaseLayoutSlot(handle.layoutIndex);
        result.targetKind = LayoutTargetKind::Closed;
        result.layoutIndex = 0;
    }
    DebugAssertBPSolution(*this);
    return Result<BPPlacement>::FromValue(result);
}

JAGUARCORE_API Result<void> BPProblem::RemoveLayout(std::size_t layoutIndex)
{
    LayoutSlot* slot = GetLayout(layoutIndex);
    if (!slot)
    {
        return Result<void>::FromError(
            {ErrorCode::InvalidInput, "BPP layout index is invalid"});
    }
    if (slot->binIndex >= m_RemainingBinStock.size())
    {
        return Result<void>::FromError(
            {ErrorCode::SnapshotMismatch, "BPP layout bin is not in instance"});
    }

    const LayoutSnapshot snapshot = slot->layout->Save();
    for (const LayoutPlacementSnapshot& placement : snapshot.placements)
    {
        if (placement.placedItem.GetItemIndex() >= m_RemainingDemand.size())
        {
            return Result<void>::FromError(
                {ErrorCode::SnapshotMismatch,
                 "BPP layout item is not in instance"});
        }
    }
    for (const LayoutPlacementSnapshot& placement : snapshot.placements)
        ++m_RemainingDemand[placement.placedItem.GetItemIndex()];

    ++m_RemainingBinStock[slot->binIndex];
    ReleaseLayoutSlot(layoutIndex);
    DebugAssertBPSolution(*this);
    return Result<void>::FromValue();
}

JAGUARCORE_API BPSolution BPProblem::Save() const
{
    BPSolution solution;
    for (std::size_t slotIndex = 0; slotIndex < m_Layouts.size(); ++slotIndex)
    {
        const LayoutSlot& slot = m_Layouts[slotIndex];
        if (slot.layout.has_value())
            solution.layouts.push_back(
                {makeLayoutKey(slotIndex, slot.generation), slot.binIndex,
                 slot.layout->Save()});
    }
    return solution;
}

JAGUARCORE_API Result<bool> BPProblem::Restore(const BPSolution& solution)
{
    struct PreparedLayout
    {
        std::size_t requestedKey = 0;
        std::size_t binIndex = 0;
        Layout layout;
        std::size_t slotIndex = 0;
        bool preservesKey = false;
    };

    std::vector<std::size_t> remainingDemand(m_Instance.ItemCount(), 0);
    for (std::size_t i = 0; i < remainingDemand.size(); ++i)
        remainingDemand[i] = m_Instance.ItemDemand(i);
    std::vector<std::size_t> remainingStock(m_Instance.BinCount(), 0);
    for (std::size_t i = 0; i < remainingStock.size(); ++i)
        remainingStock[i] = m_Instance.GetBin(i).stock;

    std::unordered_set<std::size_t> seenKeys;
    std::unordered_set<std::size_t> seenSlots;
    std::vector<PreparedLayout> prepared;
    prepared.reserve(solution.layouts.size());
    for (const BPLayoutSnapshot& snapshot : solution.layouts)
    {
        std::size_t requestedSlot = 0;
        std::uint32_t requestedGeneration = 0;
        splitLayoutKey(snapshot.layoutIndex, requestedSlot, requestedGeneration);
        if (requestedSlot == 0 || (requestedGeneration & 1U) == 0 ||
            !seenKeys.insert(snapshot.layoutIndex).second ||
            !seenSlots.insert(requestedSlot).second ||
            snapshot.binIndex >= m_Instance.BinCount() ||
            remainingStock[snapshot.binIndex] == 0 ||
            !snapshot.layoutSnapshot.container.SameBase(
                m_Instance.GetBin(snapshot.binIndex).container))
        {
            return Result<bool>::FromError(
                {ErrorCode::SnapshotMismatch, "BPP solution layout or bin is invalid"});
        }

        Layout restored(snapshot.layoutSnapshot.container);
        const auto restoredResult = restored.Restore(snapshot.layoutSnapshot);
        if (!restoredResult.HasValue())
            return Result<bool>::FromError(restoredResult.GetError());

        for (const LayoutPlacementSnapshot& placement :
             snapshot.layoutSnapshot.placements)
        {
            const ItemIndex itemIndex = placement.placedItem.GetItemIndex();
            if (itemIndex >= remainingDemand.size() || remainingDemand[itemIndex] == 0 ||
                !sameItemDefinition(m_Instance.GetItem(itemIndex), placement.placedItem))
            {
                return Result<bool>::FromError(
                    {ErrorCode::SnapshotMismatch,
                     "BPP solution contains an item beyond demand"});
            }
            --remainingDemand[itemIndex];
        }

        prepared.push_back(PreparedLayout{
            snapshot.layoutIndex, snapshot.binIndex, std::move(restored)});
        --remainingStock[snapshot.binIndex];
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
                    preserved.binIndex = layout.binIndex;
                    preserved.generation = generation;
                    newLayouts[slotIndex] = std::move(preserved);
                    continue;
                }
            }
        }
        newLayouts[slotIndex] = LayoutSlot{
            std::move(layout.layout), layout.binIndex, generation};
    }

    m_Layouts = std::move(newLayouts);
    m_FreeLayoutIndices = std::move(newFreeLayoutIndices);
    m_RemainingDemand = std::move(remainingDemand);
    m_RemainingBinStock = std::move(remainingStock);
    DebugAssertBPSolution(*this);
    return Result<bool>::FromValue(layoutKeysChanged);
}

JAGUARCORE_API std::size_t BPProblem::RemainingDemand(ItemIndex index) const noexcept
{
    return index < m_RemainingDemand.size() ? m_RemainingDemand[index] : 0;
}

JAGUARCORE_API std::size_t BPProblem::RemainingBinStock(
    std::size_t index) const noexcept
{
    return index < m_RemainingBinStock.size() ? m_RemainingBinStock[index] : 0;
}

JAGUARCORE_API std::size_t BPProblem::PlacedItemCount() const noexcept
{
    std::size_t count = 0;
    for (const LayoutSlot& slot : m_Layouts)
    {
        if (slot.layout.has_value())
            count += slot.layout->PlacedItemCount();
    }
    return count;
}

JAGUARCORE_API std::size_t BPProblem::OpenLayoutCount() const noexcept
{
    return static_cast<std::size_t>(std::count_if(
        m_Layouts.begin(), m_Layouts.end(), [](const LayoutSlot& slot) {
            return slot.layout.has_value();
        }));
}

JAGUARCORE_API double BPProblem::Density() const
{
    float totalContainerArea = 0.0f;
    float totalItemArea = 0.0f;
    for (const LayoutSlot& slot : m_Layouts)
    {
        if (!slot.layout.has_value())
            continue;
        totalContainerArea +=
            static_cast<float>(slot.layout->GetContainer().Area());
        totalItemArea += slot.layout->PlacedItemArea();
    }
    return static_cast<double>(totalItemArea / totalContainerArea);
}

JAGUARCORE_API std::uint64_t BPProblem::BinCost() const noexcept
{
    std::uint64_t cost = 0;
    for (const LayoutSlot& slot : m_Layouts)
    {
        if (slot.layout.has_value())
            cost += m_Instance.GetBin(slot.binIndex).cost;
    }
    return cost;
}

JAGUARCORE_API const BPInstance& BPProblem::Instance() const noexcept
{
    return m_Instance;
}
}
