#include "JaguarCore/problems/bpp.h"
#include "JaguarCore/assertions.h"

#include <algorithm>
#include <utility>

namespace jaguar
{
namespace
{
bool near(double first, double second) noexcept
{
    return std::abs(first - second) <= 1.0e-9;
}

bool sameItemDefinition(const Item& expected,
                        const PlacedItem& actual) noexcept
{
    if (expected.index() != actual.itemIndex() ||
        expected.externalId() != actual.item().externalId() ||
        expected.collisionShape().vertexCount() !=
            actual.item().collisionShape().vertexCount())
        return false;
    for (std::size_t i = 0; i < expected.collisionShape().vertexCount(); ++i)
    {
        if (!near(expected.collisionShape().vertices()[i].x,
                  actual.item().collisionShape().vertices()[i].x) ||
            !near(expected.collisionShape().vertices()[i].y,
                  actual.item().collisionShape().vertices()[i].y))
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

JAGUARCORE_API Result<BPProblem> BPProblem::create(BPInstance instance)
{
    std::vector<std::size_t> demand(instance.itemCount());
    for (std::size_t i = 0; i < demand.size(); ++i)
        demand[i] = instance.itemDemand(i);
    std::vector<std::size_t> stock(instance.binCount());
    for (std::size_t i = 0; i < stock.size(); ++i)
        stock[i] = instance.bin(i).stock;
    return Result<BPProblem>::fromValue(
        BPProblem{std::move(instance), std::move(demand), std::move(stock)});
}

BPProblem::LayoutSlot* BPProblem::getLayout(std::size_t layoutIndex) noexcept
{
    if (layoutIndex >= m_Layouts.size() || !m_Layouts[layoutIndex].layout.has_value())
        return nullptr;
    return &m_Layouts[layoutIndex];
}

const BPProblem::LayoutSlot* BPProblem::getLayout(
    std::size_t layoutIndex) const noexcept
{
    if (layoutIndex >= m_Layouts.size() || !m_Layouts[layoutIndex].layout.has_value())
        return nullptr;
    return &m_Layouts[layoutIndex];
}

Result<std::size_t> BPProblem::openLayout(std::size_t binIndex)
{
    if (binIndex >= m_Instance.binCount())
    {
        return Result<std::size_t>::fromError(
            {ErrorCode::InvalidInput, "BPP bin index is invalid"});
    }
    if (m_RemainingBinStock[binIndex] == 0)
    {
        return Result<std::size_t>::fromError(
            {ErrorCode::Infeasible, "BPP bin stock is exhausted"});
    }

    Layout layout(m_Instance.bin(binIndex).container);
    m_Layouts.push_back(LayoutSlot{std::move(layout), binIndex});
    --m_RemainingBinStock[binIndex];
    return Result<std::size_t>::fromValue(m_Layouts.size() - 1);
}

JAGUARCORE_API Result<BPPlacedHandle> BPProblem::placeItem(
    const BPPlacement& placement)
{
    if (placement.itemIndex >= m_Instance.itemCount())
    {
        return Result<BPPlacedHandle>::fromError(
            {ErrorCode::InvalidInput, "BPP placement item index is invalid"});
    }
    if (m_RemainingDemand[placement.itemIndex] == 0)
    {
        return Result<BPPlacedHandle>::fromError(
            {ErrorCode::Infeasible, "BPP item demand is exhausted"});
    }

    std::size_t layoutIndex = placement.layoutIndex;
    bool opened = false;
    if (placement.targetKind == LayoutTargetKind::Closed)
    {
        const auto openedLayout = openLayout(placement.binIndex);
        if (!openedLayout.hasValue())
            return Result<BPPlacedHandle>::fromError(openedLayout.error());
        layoutIndex = openedLayout.value();
        opened = true;
    }

    LayoutSlot* slot = getLayout(layoutIndex);
    if (!slot)
    {
        if (opened)
        {
            ++m_RemainingBinStock[placement.binIndex];
            m_Layouts[layoutIndex].layout.reset();
        }
        return Result<BPPlacedHandle>::fromError(
            {ErrorCode::InvalidInput, "BPP open layout index is invalid"});
    }

    const auto placed = slot->layout->placeItem(
        m_Instance.item(placement.itemIndex), placement.transform);
    if (!placed.hasValue())
    {
        if (opened)
        {
            ++m_RemainingBinStock[slot->binIndex];
            slot->layout.reset();
        }
        return Result<BPPlacedHandle>::fromError(placed.error());
    }

    --m_RemainingDemand[placement.itemIndex];
    debugAssertBPSolution(*this);
    return Result<BPPlacedHandle>::fromValue(
        BPPlacedHandle{layoutIndex, placed.value()});
}

JAGUARCORE_API Result<BPPlacement> BPProblem::removeItem(
    const BPPlacedHandle& handle)
{
    LayoutSlot* slot = getLayout(handle.layoutIndex);
    if (!slot)
    {
        return Result<BPPlacement>::fromError(
            {ErrorCode::InvalidInput, "BPP layout index is invalid"});
    }

    const auto removed = slot->layout->removeItem(handle.placedItemHandle);
    if (!removed.hasValue())
        return Result<BPPlacement>::fromError(removed.error());

    const ItemIndex itemIndex = removed.value().itemIndex();
    if (itemIndex >= m_RemainingDemand.size())
    {
        return Result<BPPlacement>::fromError(
            {ErrorCode::SnapshotMismatch, "BPP placed item is not in the instance"});
    }
    ++m_RemainingDemand[itemIndex];

    BPPlacement result{
        LayoutTargetKind::Open, handle.layoutIndex, slot->binIndex,
        itemIndex, removed.value().transform()};
    if (slot->layout->placedItemCount() == 0)
    {
        const std::size_t binIndex = slot->binIndex;
        ++m_RemainingBinStock[binIndex];
        slot->layout.reset();
        result.targetKind = LayoutTargetKind::Closed;
        result.layoutIndex = 0;
    }
    debugAssertBPSolution(*this);
    return Result<BPPlacement>::fromValue(result);
}

JAGUARCORE_API BPSolution BPProblem::save() const
{
    BPSolution solution;
    for (std::size_t i = 0; i < m_Layouts.size(); ++i)
    {
        if (m_Layouts[i].layout.has_value())
        {
            solution.layouts.push_back(
                {i, m_Layouts[i].binIndex, m_Layouts[i].layout->save()});
        }
    }
    return solution;
}

JAGUARCORE_API Result<void> BPProblem::restore(const BPSolution& solution)
{
    std::size_t slotCount = 0;
    for (const BPLayoutSnapshot& snapshot : solution.layouts)
        slotCount = std::max(slotCount, snapshot.layoutIndex + 1);
    std::vector<LayoutSlot> newLayouts(slotCount);
    std::vector<bool> seenLayouts(slotCount, false);
    std::vector<std::size_t> remainingDemand(m_Instance.itemCount(), 0);
    for (std::size_t i = 0; i < remainingDemand.size(); ++i)
        remainingDemand[i] = m_Instance.itemDemand(i);
    std::vector<std::size_t> remainingStock(m_Instance.binCount(), 0);
    for (std::size_t i = 0; i < remainingStock.size(); ++i)
        remainingStock[i] = m_Instance.bin(i).stock;

    for (const BPLayoutSnapshot& snapshot : solution.layouts)
    {
        if (snapshot.layoutIndex >= newLayouts.size() ||
            seenLayouts[snapshot.layoutIndex] ||
            snapshot.binIndex >= m_Instance.binCount() ||
            remainingStock[snapshot.binIndex] == 0 ||
            !snapshot.layoutSnapshot.container.sameBase(
                m_Instance.bin(snapshot.binIndex).container))
        {
            return Result<void>::fromError(
                {ErrorCode::SnapshotMismatch, "BPP solution layout or bin is invalid"});
        }

        Layout restored(snapshot.layoutSnapshot.container);
        const auto restoredResult = restored.restore(snapshot.layoutSnapshot);
        if (!restoredResult.hasValue())
            return restoredResult;

        for (const LayoutPlacementSnapshot& placement :
             snapshot.layoutSnapshot.placements)
        {
            const ItemIndex itemIndex = placement.placedItem.itemIndex();
            if (itemIndex >= remainingDemand.size() || remainingDemand[itemIndex] == 0 ||
                !sameItemDefinition(m_Instance.item(itemIndex), placement.placedItem))
            {
                return Result<void>::fromError(
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
    debugAssertBPSolution(*this);
    return Result<void>::fromValue();
}

JAGUARCORE_API std::size_t BPProblem::remainingDemand(ItemIndex index) const noexcept
{
    return index < m_RemainingDemand.size() ? m_RemainingDemand[index] : 0;
}

JAGUARCORE_API std::size_t BPProblem::remainingBinStock(
    std::size_t index) const noexcept
{
    return index < m_RemainingBinStock.size() ? m_RemainingBinStock[index] : 0;
}

JAGUARCORE_API std::size_t BPProblem::placedItemCount() const noexcept
{
    std::size_t count = 0;
    for (const LayoutSlot& slot : m_Layouts)
    {
        if (slot.layout.has_value())
            count += slot.layout->placedItemCount();
    }
    return count;
}

JAGUARCORE_API std::size_t BPProblem::openLayoutCount() const noexcept
{
    return static_cast<std::size_t>(std::count_if(
        m_Layouts.begin(), m_Layouts.end(), [](const LayoutSlot& slot) {
            return slot.layout.has_value();
        }));
}

JAGUARCORE_API double BPProblem::density() const
{
    double totalContainerArea = 0.0;
    double totalItemArea = 0.0;
    for (const LayoutSlot& slot : m_Layouts)
    {
        if (!slot.layout.has_value())
            continue;
        totalContainerArea += slot.layout->container().area();
        totalItemArea += slot.layout->density() * slot.layout->container().area();
    }
    return totalContainerArea > 0.0 ? totalItemArea / totalContainerArea : 0.0;
}

JAGUARCORE_API std::uint64_t BPProblem::binCost() const noexcept
{
    std::uint64_t cost = 0;
    for (const LayoutSlot& slot : m_Layouts)
    {
        if (slot.layout.has_value())
            cost += m_Instance.bin(slot.binIndex).cost;
    }
    return cost;
}

JAGUARCORE_API const BPInstance& BPProblem::instance() const noexcept
{
    return m_Instance;
}
}
