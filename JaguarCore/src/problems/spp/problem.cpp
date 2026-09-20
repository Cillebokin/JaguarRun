#include "JaguarCore/problems/spp.h"
#include "JaguarCore/assertions.h"

#include <array>
#include <cmath>
#include <utility>

namespace jaguar
{
namespace
{
bool near(double first, double second) noexcept
{
    return std::abs(first - second) <= 1.0e-9;
}

bool sameCdeConfig(const CDEConfig& first,
                   const CDEConfig& second) noexcept
{
    return first.quadtreeDepth == second.quadtreeDepth &&
           first.cdThreshold == second.cdThreshold &&
           first.itemSurrogateConfig == second.itemSurrogateConfig;
}

bool sameOptionalDouble(const std::optional<double>& first,
                        const std::optional<double>& second) noexcept
{
    if (first.has_value() != second.has_value())
        return false;
    return !first.has_value() || near(*first, *second);
}

bool sameShapeModifyConfig(const ShapeModifyConfig& first,
                           const ShapeModifyConfig& second) noexcept
{
    if (!sameOptionalDouble(first.simplifyTolerance,
                            second.simplifyTolerance) ||
        !sameOptionalDouble(first.offset, second.offset) ||
        first.narrowConcavityCutoff.has_value() !=
            second.narrowConcavityCutoff.has_value())
        return false;
    if (!first.narrowConcavityCutoff.has_value())
        return true;
    return near(first.narrowConcavityCutoff->first,
                second.narrowConcavityCutoff->first) &&
           near(first.narrowConcavityCutoff->second,
                second.narrowConcavityCutoff->second);
}

bool sameStripContainer(const Container& container, const Strip& strip) noexcept
{
    const auto& shape = container.originalShape().shape;
    if (shape.vertexCount() != 4)
        return false;
    const std::array<Point2d, 4> expected{{
        {0.0, 0.0}, {strip.width, 0.0},
        {strip.width, strip.fixedHeight}, {0.0, strip.fixedHeight}}};
    for (std::size_t i = 0; i < expected.size(); ++i)
    {
        if (!near(shape.vertices()[i].x, expected[i].x) ||
            !near(shape.vertices()[i].y, expected[i].y))
            return false;
    }
    return true;
}

bool sameItemDefinition(const Item& expected, const PlacedItem& actual) noexcept
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

SPProblem::SPProblem(SPInstance instance, Strip strip, Layout layout)
    : m_Instance(std::move(instance)),
      m_Strip(strip),
      m_Layout(std::move(layout)),
      m_RemainingDemand(m_Instance.itemCount(), 0)
{
    for (std::size_t i = 0; i < m_RemainingDemand.size(); ++i)
        m_RemainingDemand[i] = m_Instance.itemDemand(i);
}

JAGUARCORE_API Result<SPProblem> SPProblem::create(SPInstance instance)
{
    const Strip strip = instance.baseStrip();
    const auto container = strip.createContainer();
    if (!container.hasValue())
        return Result<SPProblem>::fromError(container.error());

    return Result<SPProblem>::fromValue(
        SPProblem{std::move(instance), strip, Layout{container.value()}});
}

JAGUARCORE_API Result<PlacedItemHandle> SPProblem::placeItem(
    const SPPlacement& placement)
{
    if (placement.itemIndex >= m_Instance.itemCount())
    {
        return Result<PlacedItemHandle>::fromError(
            {ErrorCode::InvalidInput, "SPP placement item index is invalid"});
    }
    if (m_RemainingDemand[placement.itemIndex] == 0)
    {
        return Result<PlacedItemHandle>::fromError(
            {ErrorCode::Infeasible, "SPP item demand is exhausted"});
    }

    const auto placed = m_Layout.placeItem(
        m_Instance.item(placement.itemIndex), placement.transform);
    if (!placed.hasValue())
        return placed;
    --m_RemainingDemand[placement.itemIndex];
    debugAssertSPSolution(*this);
    return placed;
}

JAGUARCORE_API Result<SPPlacement> SPProblem::removeItem(PlacedItemHandle handle)
{
    const auto removed = m_Layout.removeItem(handle);
    if (!removed.hasValue())
        return Result<SPPlacement>::fromError(removed.error());

    const ItemIndex itemIndex = removed.value().itemIndex();
    if (itemIndex >= m_RemainingDemand.size())
    {
        return Result<SPPlacement>::fromError(
            {ErrorCode::SnapshotMismatch, "placed item is not in the SPP instance"});
    }
    ++m_RemainingDemand[itemIndex];
    debugAssertSPSolution(*this);
    return Result<SPPlacement>::fromValue(
        SPPlacement{itemIndex, removed.value().transform()});
}

JAGUARCORE_API Result<void> SPProblem::changeStripWidth(double newWidth)
{
    Strip nextStrip = m_Strip;
    const auto changed = nextStrip.setWidth(newWidth);
    if (!changed.hasValue())
        return changed;

    const auto container = nextStrip.createContainer();
    if (!container.hasValue())
        return Result<void>::fromError(container.error());
    const auto swapped = m_Layout.swapContainer(container.value());
    if (!swapped.hasValue())
        return swapped;

    m_Strip = nextStrip;
    debugAssertSPSolution(*this);
    return Result<void>::fromValue();
}

JAGUARCORE_API Result<void> SPProblem::fitStrip()
{
    const auto rightmost = m_Layout.rightmostCollisionX();
    if (!rightmost.has_value())
    {
        return Result<void>::fromError(
            {ErrorCode::Infeasible, "cannot fit an empty strip"});
    }

    const double offset = m_Strip.shapeModifyConfig.offset.value_or(0.0);
    const double fittedWidth = *rightmost * 1.00001 + offset;
    if (!std::isfinite(fittedWidth) || fittedWidth <= 0.0)
    {
        return Result<void>::fromError(
            {ErrorCode::Infeasible, "computed strip width is invalid"});
    }
    return changeStripWidth(fittedWidth);
}

JAGUARCORE_API SPSolution SPProblem::save() const
{
    return SPSolution{m_Strip, m_Layout.save()};
}

JAGUARCORE_API Result<void> SPProblem::restore(const SPSolution& solution)
{
    if (!std::isfinite(solution.strip.fixedHeight) ||
        solution.strip.fixedHeight != m_Instance.baseStrip().fixedHeight ||
        !std::isfinite(solution.strip.width) || solution.strip.width <= 0.0)
    {
        return Result<void>::fromError(
             {ErrorCode::SnapshotMismatch, "SPP solution strip is incompatible"});
    }
    if (!sameCdeConfig(solution.strip.cdeConfig,
                       m_Instance.baseStrip().cdeConfig) ||
        !sameShapeModifyConfig(solution.strip.shapeModifyConfig,
                               m_Instance.baseStrip().shapeModifyConfig))
    {
        return Result<void>::fromError(
            {ErrorCode::SnapshotMismatch,
             "SPP solution strip configuration is incompatible"});
    }
    if (!sameStripContainer(solution.layoutSnapshot.container, solution.strip))
    {
        return Result<void>::fromError(
            {ErrorCode::SnapshotMismatch,
             "SPP solution strip does not match its layout container"});
    }

    std::vector<std::size_t> remaining(m_Instance.itemCount(), 0);
    for (std::size_t i = 0; i < remaining.size(); ++i)
        remaining[i] = m_Instance.itemDemand(i);

    for (const LayoutPlacementSnapshot& placement :
         solution.layoutSnapshot.placements)
    {
        const ItemIndex itemIndex = placement.placedItem.itemIndex();
        if (itemIndex >= remaining.size() || remaining[itemIndex] == 0 ||
            !sameItemDefinition(m_Instance.item(itemIndex), placement.placedItem))
        {
            return Result<void>::fromError(
                {ErrorCode::SnapshotMismatch,
                 "SPP solution contains an item beyond demand"});
        }
        --remaining[itemIndex];
    }

    Layout restored(solution.layoutSnapshot.container);
    const auto restoredResult = restored.restore(solution.layoutSnapshot);
    if (!restoredResult.hasValue())
        return restoredResult;

    m_Strip = solution.strip;
    m_Layout = std::move(restored);
    m_RemainingDemand = std::move(remaining);
    debugAssertSPSolution(*this);
    return Result<void>::fromValue();
}

JAGUARCORE_API std::size_t SPProblem::remainingDemand(ItemIndex index) const noexcept
{
    return index < m_RemainingDemand.size() ? m_RemainingDemand[index] : 0;
}

JAGUARCORE_API std::size_t SPProblem::placedItemCount() const noexcept
{
    return m_Layout.placedItemCount();
}

JAGUARCORE_API double SPProblem::density() const
{
    return m_Layout.density();
}

JAGUARCORE_API double SPProblem::stripWidth() const noexcept
{
    return m_Strip.width;
}

JAGUARCORE_API const SPInstance& SPProblem::instance() const noexcept
{
    return m_Instance;
}
}
