#include "JaguarCore/Problems/Spp.h"
#include "JaguarCore/Assertions.h"
#include "JaguarCore/NumericPolicy.h"

#include <cmath>
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

bool sameItemDefinition(const Item& expected, const PlacedItem& actual) noexcept
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
    // jagua-rs restore() accepts any strip configuration/width and only falls
    // back to a full rebuild when the layout snapshot does not match.
    std::vector<std::size_t> remaining(m_Instance.ItemCount(), 0);
    for (std::size_t i = 0; i < remaining.size(); ++i)
        remaining[i] = m_Instance.ItemDemand(i);

    for (const LayoutPlacementSnapshot& placement :
         solution.layoutSnapshot.placements)
    {
        const ItemIndex itemIndex = placement.placedItem.GetItemIndex();
        if (itemIndex >= remaining.size() || remaining[itemIndex] == 0 ||
            !sameItemDefinition(m_Instance.GetItem(itemIndex), placement.placedItem))
        {
            return Result<void>::FromError(
                {ErrorCode::SnapshotMismatch,
                 "SPP solution contains an item beyond demand"});
        }
        --remaining[itemIndex];
    }

    Layout restored(solution.layoutSnapshot.container);
    const auto restoredResult = restored.Restore(solution.layoutSnapshot);
    if (!restoredResult.HasValue())
        return restoredResult;

    const auto restoredInPlace = m_Layout.Restore(solution.layoutSnapshot);
    if (!restoredInPlace.HasValue())
        m_Layout = std::move(restored);

    m_Strip = solution.strip;
    m_RemainingDemand = std::move(remaining);
    DebugAssertSPSolution(*this);
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
