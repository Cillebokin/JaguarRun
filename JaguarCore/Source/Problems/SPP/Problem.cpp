#include "JaguarCore/Problems/Spp.h"
#include "JaguarCore/Assertions.h"

#include <stdexcept>
#include <utility>

namespace jaguar
{
SPProblem::SPProblem(SPInstance instance, Strip strip, Layout layout)
    : m_Instance(std::move(instance)),
      m_Strip(strip),
      m_Layout(std::move(layout)),
      m_RemainingDemand(m_Instance.ItemCount(), 0)
{
    for (std::size_t i = 0; i < m_RemainingDemand.size(); ++i)
        m_RemainingDemand[i] = m_Instance.ItemDemand(i);
}

JAGUARCORE_API Result<SPProblem> SPProblem::Create(SPInstance instance)
{
    const Strip strip = instance.BaseStrip();
    const auto container = strip.CreateContainer();
    if (!container.HasValue())
        return Result<SPProblem>::FromError(container.GetError());

    return Result<SPProblem>::FromValue(
        SPProblem{std::move(instance), strip, Layout{container.Value()}});
}

JAGUARCORE_API Result<PlacedItemHandle> SPProblem::PlaceItem(
    const SPPlacement& placement)
{
    if (placement.itemIndex >= m_Instance.ItemCount())
    {
        return Result<PlacedItemHandle>::FromError(
            {ErrorCode::InvalidInput, "SPP placement item index is invalid"});
    }
#ifndef NDEBUG
    if (m_RemainingDemand[placement.itemIndex] == 0)
    {
        return Result<PlacedItemHandle>::FromError(
            {ErrorCode::Infeasible, "SPP item demand is exhausted"});
    }
#endif

    const auto placed = m_Layout.PlaceItem(
        m_Instance.GetItem(placement.itemIndex), placement.transform);
    if (!placed.HasValue())
        return placed;
    --m_RemainingDemand[placement.itemIndex];
    DebugAssertSPSolution(*this);
    return placed;
}

JAGUARCORE_API Result<SPPlacement> SPProblem::RemoveItem(PlacedItemHandle handle)
{
    const auto removed = m_Layout.RemoveItem(handle);
    if (!removed.HasValue())
        return Result<SPPlacement>::FromError(removed.GetError());

    const ItemIndex itemIndex = removed.Value().GetItemIndex();
    if (itemIndex >= m_RemainingDemand.size())
    {
        return Result<SPPlacement>::FromError(
            {ErrorCode::SnapshotMismatch, "placed item is not in the SPP instance"});
    }
    ++m_RemainingDemand[itemIndex];
    DebugAssertSPSolution(*this);
    return Result<SPPlacement>::FromValue(
        SPPlacement{itemIndex, removed.Value().Transform()});
}

JAGUARCORE_API Result<void> SPProblem::ChangeStripWidth(double newWidth)
{
    Strip nextStrip = m_Strip;
    const auto changed = nextStrip.SetWidth(newWidth);
    if (!changed.HasValue())
        return changed;

    const auto container = nextStrip.CreateContainer();
    if (!container.HasValue())
        return Result<void>::FromError(container.GetError());
    const auto swapped = m_Layout.SwapContainer(container.Value());
    if (!swapped.HasValue())
        return swapped;

    m_Strip = nextStrip;
    DebugAssertSPSolution(*this);
    return Result<void>::FromValue();
}

JAGUARCORE_API Result<void> SPProblem::FitStrip()
{
    const auto rightmost = m_Layout.RightmostCollisionX();
    if (!rightmost.has_value())
    {
        return Result<void>::FromError(
            {ErrorCode::Infeasible, "cannot fit an empty strip"});
    }

    const float itemXMax = static_cast<float>(*rightmost);
    const float offset = static_cast<float>(
        m_Strip.shapeModifyConfig.offset.value_or(0.0));
    const float fittedWidth = itemXMax * 1.00001F + offset;
    if (!std::isfinite(fittedWidth) || fittedWidth <= 0.0F)
    {
        return Result<void>::FromError(
            {ErrorCode::Infeasible, "computed strip width is invalid"});
    }
    return ChangeStripWidth(static_cast<double>(fittedWidth));
}

JAGUARCORE_API SPSolution SPProblem::Save() const
{
    return SPSolution{m_Strip, m_Layout.Save()};
}

JAGUARCORE_API Result<void> SPProblem::Restore(const SPSolution& solution)
{
    // Rust first attempts to restore the existing layout and rebuilds it only
    // when the snapshot belongs to another static container base.
    if (m_Layout.GetContainer().SameBase(solution.layoutSnapshot.container))
    {
        const auto restored = m_Layout.Restore(solution.layoutSnapshot);
        if (!restored.HasValue())
            return restored;
    }
    else
    {
        Layout restored(solution.layoutSnapshot.container);
        const auto restoredResult = restored.Restore(solution.layoutSnapshot);
        if (!restoredResult.HasValue())
            return restoredResult;
        m_Layout = std::move(restored);
    }

    m_Strip = solution.strip;
    for (std::size_t i = 0; i < m_RemainingDemand.size(); ++i)
        m_RemainingDemand[i] = m_Instance.ItemDemand(i);

    for (const LayoutPlacementSnapshot& placement :
         solution.layoutSnapshot.placements)
    {
        const ItemIndex itemIndex = placement.placedItem.GetItemIndex();
        if (itemIndex >= m_RemainingDemand.size())
            throw std::out_of_range("SPP restored item index is out of range");
#ifndef NDEBUG
        if (m_RemainingDemand[itemIndex] == 0U)
            throw std::underflow_error("attempt to subtract with overflow");
#endif
        --m_RemainingDemand[itemIndex];
    }

    // Rust's restore assertion checks snapshot/layout consistency but does not
    // require the snapshot's item definition to equal the current instance.
    DebugAssertLayout(m_Layout);
    return Result<void>::FromValue();
}

JAGUARCORE_API std::size_t SPProblem::RemainingDemand(ItemIndex index) const noexcept
{
    return index < m_RemainingDemand.size() ? m_RemainingDemand[index] : 0;
}

JAGUARCORE_API std::size_t SPProblem::PlacedItemCount() const noexcept
{
    return m_Layout.PlacedItemCount();
}

JAGUARCORE_API double SPProblem::Density() const
{
    return m_Layout.Density();
}

JAGUARCORE_API double SPProblem::StripWidth() const noexcept
{
    return m_Strip.width;
}

JAGUARCORE_API const SPInstance& SPProblem::Instance() const noexcept
{
    return m_Instance;
}
}
