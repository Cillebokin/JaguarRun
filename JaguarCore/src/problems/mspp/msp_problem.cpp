#include "JaguarCore/problems/mspp.h"
#include "JaguarCore/assertions.h"

#include <algorithm>
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

bool sameSurrogateConfig(const SurrogateConfig& first,
                         const SurrogateConfig& second) noexcept
{
    return first == second;
}

bool sameCdeConfig(const CDEConfig& first,
                   const CDEConfig& second) noexcept
{
    return first.quadtreeDepth == second.quadtreeDepth &&
           first.cdThreshold == second.cdThreshold &&
           sameSurrogateConfig(first.itemSurrogateConfig,
                               second.itemSurrogateConfig);
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
        !sameOptionalDouble(first.offset, second.offset))
        return false;
    if (first.narrowConcavityCutoff.has_value() !=
        second.narrowConcavityCutoff.has_value())
        return false;
    if (!first.narrowConcavityCutoff.has_value())
        return true;
    return near(first.narrowConcavityCutoff->first,
                second.narrowConcavityCutoff->first) &&
           near(first.narrowConcavityCutoff->second,
                second.narrowConcavityCutoff->second);
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

bool sameStripContainer(const Container& container,
                        const MultiStrip& strip) noexcept
{
    constexpr double tolerance = 1.0e-9;
    const auto& shape = container.originalShape().shape;
    if (shape.vertexCount() != 4)
        return false;

    const std::array<Point2d, 4> expected{{
        {0.0, 0.0},
        {strip.width, 0.0},
        {strip.width, strip.fixedHeight},
        {0.0, strip.fixedHeight}}};
    for (std::size_t i = 0; i < expected.size(); ++i)
    {
        if (std::abs(shape.vertices()[i].x - expected[i].x) > tolerance ||
            std::abs(shape.vertices()[i].y - expected[i].y) > tolerance)
            return false;
    }
    return true;
}
}

MSPProblem::MSPProblem(MSPInstance instance,
                       std::vector<std::size_t> remainingDemand)
    : m_Instance(std::move(instance)),
      m_RemainingDemand(std::move(remainingDemand))
{
}

JAGUARCORE_API Result<MSPProblem> MSPProblem::create(MSPInstance instance)
{
    std::vector<std::size_t> remaining(instance.itemCount());
    for (std::size_t i = 0; i < remaining.size(); ++i)
        remaining[i] = instance.itemDemand(i);
    return Result<MSPProblem>::fromValue(
        MSPProblem{std::move(instance), std::move(remaining)});
}

MSPProblem::LayoutSlot* MSPProblem::getLayout(std::size_t layoutIndex) noexcept
{
    if (layoutIndex >= m_Layouts.size() || !m_Layouts[layoutIndex].layout.has_value())
        return nullptr;
    return &m_Layouts[layoutIndex];
}

const MSPProblem::LayoutSlot* MSPProblem::getLayout(
    std::size_t layoutIndex) const noexcept
{
    if (layoutIndex >= m_Layouts.size() || !m_Layouts[layoutIndex].layout.has_value())
        return nullptr;
    return &m_Layouts[layoutIndex];
}

JAGUARCORE_API Result<std::size_t> MSPProblem::addLayout(MultiStrip strip)
{
    const auto container = strip.createContainer();
    if (!container.hasValue())
        return Result<std::size_t>::fromError(container.error());
    m_Layouts.push_back(LayoutSlot{Layout{container.value()}, strip});
    return Result<std::size_t>::fromValue(m_Layouts.size() - 1);
}

JAGUARCORE_API Result<void> MSPProblem::removeLayout(std::size_t layoutIndex)
{
    LayoutSlot* slot = getLayout(layoutIndex);
    if (!slot)
    {
        return Result<void>::fromError(
            {ErrorCode::InvalidInput, "MSPP layout index is invalid"});
    }
    const auto snapshot = slot->layout->save();
    for (const LayoutPlacementSnapshot& placement : snapshot.placements)
    {
        const ItemIndex itemIndex = placement.placedItem.itemIndex();
        if (itemIndex >= m_RemainingDemand.size())
        {
            return Result<void>::fromError(
                {ErrorCode::SnapshotMismatch, "MSPP layout item is not in instance"});
        }
        ++m_RemainingDemand[itemIndex];
    }
    slot->layout.reset();
    debugAssertMSPSolution(*this);
    return Result<void>::fromValue();
}

JAGUARCORE_API Result<MSPPlacedHandle> MSPProblem::placeItem(
    const MSPPlacement& placement)
{
    if (placement.itemIndex >= m_Instance.itemCount())
    {
        return Result<MSPPlacedHandle>::fromError(
            {ErrorCode::InvalidInput, "MSPP placement item index is invalid"});
    }
    if (m_RemainingDemand[placement.itemIndex] == 0)
    {
        return Result<MSPPlacedHandle>::fromError(
            {ErrorCode::Infeasible, "MSPP item demand is exhausted"});
    }
    LayoutSlot* slot = getLayout(placement.layoutIndex);
    if (!slot)
    {
        return Result<MSPPlacedHandle>::fromError(
            {ErrorCode::InvalidInput, "MSPP layout index is invalid"});
    }
    const auto placed = slot->layout->placeItem(
        m_Instance.item(placement.itemIndex), placement.transform);
    if (!placed.hasValue())
        return Result<MSPPlacedHandle>::fromError(placed.error());
    --m_RemainingDemand[placement.itemIndex];
    debugAssertMSPSolution(*this);
    return Result<MSPPlacedHandle>::fromValue(
        MSPPlacedHandle{placement.layoutIndex, placed.value()});
}

JAGUARCORE_API Result<MSPPlacement> MSPProblem::removeItem(
    const MSPPlacedHandle& handle)
{
    LayoutSlot* slot = getLayout(handle.layoutIndex);
    if (!slot)
    {
        return Result<MSPPlacement>::fromError(
            {ErrorCode::InvalidInput, "MSPP layout index is invalid"});
    }
    const auto removed = slot->layout->removeItem(handle.placedItemHandle);
    if (!removed.hasValue())
        return Result<MSPPlacement>::fromError(removed.error());
    const ItemIndex itemIndex = removed.value().itemIndex();
    if (itemIndex >= m_RemainingDemand.size())
    {
        return Result<MSPPlacement>::fromError(
            {ErrorCode::SnapshotMismatch, "MSPP item is not in instance"});
    }
    ++m_RemainingDemand[itemIndex];
    debugAssertMSPSolution(*this);
    return Result<MSPPlacement>::fromValue(
        MSPPlacement{handle.layoutIndex, itemIndex, removed.value().transform()});
}

JAGUARCORE_API Result<void> MSPProblem::changeStripWidth(
    std::size_t layoutIndex, double width)
{
    LayoutSlot* slot = getLayout(layoutIndex);
    if (!slot)
    {
        return Result<void>::fromError(
            {ErrorCode::InvalidInput, "MSPP layout index is invalid"});
    }
    MultiStrip next = slot->strip;
    const auto changed = next.setWidth(width);
    if (!changed.hasValue())
        return changed;
    const auto container = next.createContainer();
    if (!container.hasValue())
        return Result<void>::fromError(container.error());
    const auto swapped = slot->layout->swapContainer(container.value());
    if (!swapped.hasValue())
        return swapped;
    slot->strip = next;
    debugAssertMSPSolution(*this);
    return Result<void>::fromValue();
}

JAGUARCORE_API Result<void> MSPProblem::fitStrip(std::size_t layoutIndex)
{
    const LayoutSlot* slot = getLayout(layoutIndex);
    if (!slot)
    {
        return Result<void>::fromError(
            {ErrorCode::InvalidInput, "MSPP layout index is invalid"});
    }
    const auto rightmost = slot->layout->rightmostCollisionX();
    if (!rightmost.has_value())
    {
        return Result<void>::fromError(
            {ErrorCode::Infeasible, "cannot fit an empty MSPP strip"});
    }
    const double fittedWidth =
        *rightmost * 1.00001 + slot->strip.shapeModifyConfig.offset.value_or(0.0);
    if (!std::isfinite(fittedWidth) || fittedWidth <= 0.0)
    {
        return Result<void>::fromError(
            {ErrorCode::Infeasible, "computed MSPP strip width is invalid"});
    }
    return changeStripWidth(layoutIndex, fittedWidth);
}

JAGUARCORE_API MSPSolution MSPProblem::save() const
{
    MSPSolution solution;
    for (std::size_t i = 0; i < m_Layouts.size(); ++i)
    {
        if (m_Layouts[i].layout.has_value())
        {
            solution.layouts.push_back(
                {i, m_Layouts[i].strip, m_Layouts[i].layout->save()});
        }
    }
    return solution;
}

JAGUARCORE_API Result<void> MSPProblem::restore(const MSPSolution& solution)
{
    std::size_t layoutCount = 0;
    for (const MSPLayoutSnapshot& snapshot : solution.layouts)
        layoutCount = std::max(layoutCount, snapshot.layoutIndex + 1);
    std::vector<LayoutSlot> newLayouts(layoutCount);
    std::vector<bool> seen(layoutCount, false);
    std::vector<std::size_t> remaining(m_Instance.itemCount(), 0);
    for (std::size_t i = 0; i < remaining.size(); ++i)
        remaining[i] = m_Instance.itemDemand(i);

    for (const MSPLayoutSnapshot& snapshot : solution.layouts)
    {
        if (snapshot.layoutIndex >= newLayouts.size() ||
            seen[snapshot.layoutIndex] ||
            !std::isfinite(snapshot.strip.maxWidth) ||
            !std::isfinite(snapshot.strip.fixedHeight) ||
            !std::isfinite(snapshot.strip.width) ||
            snapshot.strip.maxWidth <= 0.0 ||
            snapshot.strip.fixedHeight <= 0.0 ||
            snapshot.strip.width <= 0.0 ||
            snapshot.strip.width > snapshot.strip.maxWidth ||
            !near(snapshot.strip.maxWidth, m_Instance.baseStrip().maxWidth) ||
            !near(snapshot.strip.fixedHeight,
                  m_Instance.baseStrip().fixedHeight) ||
            !sameCdeConfig(snapshot.strip.cdeConfig,
                           m_Instance.baseStrip().cdeConfig) ||
            !sameShapeModifyConfig(snapshot.strip.shapeModifyConfig,
                                   m_Instance.baseStrip().shapeModifyConfig) ||
            !sameStripContainer(snapshot.layoutSnapshot.container,
                                snapshot.strip))
        {
            return Result<void>::fromError(
                {ErrorCode::SnapshotMismatch, "MSPP solution strip is invalid"});
        }
        Layout restored(snapshot.layoutSnapshot.container);
        const auto restoredResult = restored.restore(snapshot.layoutSnapshot);
        if (!restoredResult.hasValue())
            return restoredResult;
        for (const LayoutPlacementSnapshot& placement :
             snapshot.layoutSnapshot.placements)
        {
            const ItemIndex itemIndex = placement.placedItem.itemIndex();
            if (itemIndex >= remaining.size() || remaining[itemIndex] == 0 ||
                !sameItemDefinition(m_Instance.item(itemIndex), placement.placedItem))
            {
                return Result<void>::fromError(
                    {ErrorCode::SnapshotMismatch,
                     "MSPP solution contains an item beyond demand"});
            }
            --remaining[itemIndex];
        }
        newLayouts[snapshot.layoutIndex] =
            LayoutSlot{std::move(restored), snapshot.strip};
        seen[snapshot.layoutIndex] = true;
    }

    m_Layouts = std::move(newLayouts);
    m_RemainingDemand = std::move(remaining);
    debugAssertMSPSolution(*this);
    return Result<void>::fromValue();
}

JAGUARCORE_API std::size_t MSPProblem::remainingDemand(
    ItemIndex index) const noexcept
{
    return index < m_RemainingDemand.size() ? m_RemainingDemand[index] : 0;
}

JAGUARCORE_API std::size_t MSPProblem::placedItemCount() const noexcept
{
    std::size_t count = 0;
    for (const LayoutSlot& slot : m_Layouts)
    {
        if (slot.layout.has_value())
            count += slot.layout->placedItemCount();
    }
    return count;
}

JAGUARCORE_API std::size_t MSPProblem::layoutCount() const noexcept
{
    return static_cast<std::size_t>(std::count_if(
        m_Layouts.begin(), m_Layouts.end(), [](const LayoutSlot& slot) {
            return slot.layout.has_value();
        }));
}

JAGUARCORE_API double MSPProblem::stripWidth(std::size_t layoutIndex) const noexcept
{
    const LayoutSlot* slot = getLayout(layoutIndex);
    return slot ? slot->strip.width : 0.0;
}

JAGUARCORE_API double MSPProblem::totalStripWidth() const noexcept
{
    double result = 0.0;
    for (const LayoutSlot& slot : m_Layouts)
    {
        if (slot.layout.has_value())
            result += slot.strip.width;
    }
    return result;
}

JAGUARCORE_API double MSPProblem::density() const
{
    double containerArea = 0.0;
    double itemArea = 0.0;
    for (const LayoutSlot& slot : m_Layouts)
    {
        if (!slot.layout.has_value())
            continue;
        containerArea += slot.strip.fixedHeight * slot.strip.width;
        itemArea += slot.layout->density() * slot.layout->container().area();
    }
    return containerArea > 0.0 ? itemArea / containerArea : 0.0;
}

JAGUARCORE_API const MSPInstance& MSPProblem::instance() const noexcept
{
    return m_Instance;
}
}
