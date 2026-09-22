#include "JaguarCore/Problems/Bpp.h"
#include "JaguarCore/Assertions.h"
#include "JaguarCore/NumericPolicy.h"

#include <algorithm>
#include <utility>

namespace jaguar
{
namespace
{
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
      m_RemainingDemand(std::move(remainingDemand)),
      m_RemainingBinStock(std::move(remainingBinStock))
{
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
    if (layoutIndex >= m_Layouts.size() || !m_Layouts[layoutIndex].layout.has_value())
        return nullptr;
    return &m_Layouts[layoutIndex];
}

const BPProblem::LayoutSlot* BPProblem::GetLayout(
    std::size_t layoutIndex) const noexcept
{
    if (layoutIndex >= m_Layouts.size() || !m_Layouts[layoutIndex].layout.has_value())
        return nullptr;
    return &m_Layouts[layoutIndex];
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
    m_Layouts.push_back(LayoutSlot{std::move(layout), binIndex});
    --m_RemainingBinStock[binIndex];
    return Result<std::size_t>::FromValue(m_Layouts.size() - 1);
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
            m_Layouts[layoutIndex].layout.reset();
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
            slot->layout.reset();
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
        slot->layout.reset();
        result.targetKind = LayoutTargetKind::Closed;
        result.layoutIndex = 0;
    }
    DebugAssertBPSolution(*this);
    return Result<BPPlacement>::FromValue(result);
}

JAGUARCORE_API BPSolution BPProblem::Save() const
{
    BPSolution solution;
    for (std::size_t i = 0; i < m_Layouts.size(); ++i)
    {
        if (m_Layouts[i].layout.has_value())
        {
            solution.layouts.push_back(
                {i, m_Layouts[i].binIndex, m_Layouts[i].layout->Save()});
        }
    }
    return solution;
}

JAGUARCORE_API Result<void> BPProblem::Restore(const BPSolution& solution)
{
    std::size_t slotCount = 0;
    for (const BPLayoutSnapshot& snapshot : solution.layouts)
        slotCount = std::max(slotCount, snapshot.layoutIndex + 1);
    std::vector<LayoutSlot> newLayouts(slotCount);
    std::vector<bool> seenLayouts(slotCount, false);
    std::vector<std::size_t> remainingDemand(m_Instance.ItemCount(), 0);
    for (std::size_t i = 0; i < remainingDemand.size(); ++i)
        remainingDemand[i] = m_Instance.ItemDemand(i);
    std::vector<std::size_t> remainingStock(m_Instance.BinCount(), 0);
    for (std::size_t i = 0; i < remainingStock.size(); ++i)
        remainingStock[i] = m_Instance.GetBin(i).stock;

    for (const BPLayoutSnapshot& snapshot : solution.layouts)
    {
        if (snapshot.layoutIndex >= newLayouts.size() ||
            seenLayouts[snapshot.layoutIndex] ||
            snapshot.binIndex >= m_Instance.BinCount() ||
            remainingStock[snapshot.binIndex] == 0 ||
            !snapshot.layoutSnapshot.container.SameBase(
                m_Instance.GetBin(snapshot.binIndex).container))
        {
            return Result<void>::FromError(
                {ErrorCode::SnapshotMismatch, "BPP solution layout or bin is invalid"});
        }

        Layout restored(snapshot.layoutSnapshot.container);
        const auto restoredResult = restored.Restore(snapshot.layoutSnapshot);
        if (!restoredResult.HasValue())
            return restoredResult;

        for (const LayoutPlacementSnapshot& placement :
             snapshot.layoutSnapshot.placements)
        {
            const ItemIndex itemIndex = placement.placedItem.GetItemIndex();
            if (itemIndex >= remainingDemand.size() || remainingDemand[itemIndex] == 0 ||
                !sameItemDefinition(m_Instance.GetItem(itemIndex), placement.placedItem))
            {
                return Result<void>::FromError(
                    {ErrorCode::SnapshotMismatch,
                     "BPP solution contains an item beyond demand"});
            }
            --remainingDemand[itemIndex];
        }

        newLayouts[snapshot.layoutIndex] =
            LayoutSlot{std::move(restored), snapshot.binIndex};
        seenLayouts[snapshot.layoutIndex] = true;
        --remainingStock[snapshot.binIndex];
    }

    m_Layouts = std::move(newLayouts);
    m_RemainingDemand = std::move(remainingDemand);
    m_RemainingBinStock = std::move(remainingStock);
    DebugAssertBPSolution(*this);
    return Result<void>::FromValue();
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
    double totalContainerArea = 0.0;
    double totalItemArea = 0.0;
    for (const LayoutSlot& slot : m_Layouts)
    {
        if (!slot.layout.has_value())
            continue;
        totalContainerArea += slot.layout->GetContainer().Area();
        totalItemArea += slot.layout->Density() * slot.layout->GetContainer().Area();
    }
    return totalContainerArea > 0.0 ? totalItemArea / totalContainerArea : 0.0;
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
