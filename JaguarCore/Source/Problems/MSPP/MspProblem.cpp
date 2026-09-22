#include "JaguarCore/Problems/MSpp.h"
#include "JaguarCore/Assertions.h"
#include "JaguarCore/NumericPolicy.h"

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
    return std::abs(first - second) <=
           NumericPolicy::DefaultEqualityTolerance();
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

bool sameStripContainer(const Container& container,
                        const MultiStrip& strip) noexcept
{
    constexpr double tolerance = NumericPolicy::DefaultEqualityTolerance();
    const auto& shape = container.GetOriginalShape().shape;
    if (shape.VertexCount() != 4)
        return false;

    const std::array<Point2d, 4> expected{{
        {0.0, 0.0},
        {strip.width, 0.0},
        {strip.width, strip.fixedHeight},
        {0.0, strip.fixedHeight}}};
    for (std::size_t i = 0; i < expected.size(); ++i)
    {
        if (std::abs(shape.Vertices()[i].x - expected[i].x) > tolerance ||
            std::abs(shape.Vertices()[i].y - expected[i].y) > tolerance)
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
    if (layoutIndex >= m_Layouts.size() || !m_Layouts[layoutIndex].layout.has_value())
        return nullptr;
    return &m_Layouts[layoutIndex];
}

const MSPProblem::LayoutSlot* MSPProblem::GetLayout(
    std::size_t layoutIndex) const noexcept
{
    if (layoutIndex >= m_Layouts.size() || !m_Layouts[layoutIndex].layout.has_value())
        return nullptr;
    return &m_Layouts[layoutIndex];
}

JAGUARCORE_API Result<std::size_t> MSPProblem::AddLayout(MultiStrip strip)
{
    const auto container = strip.CreateContainer();
    if (!container.HasValue())
        return Result<std::size_t>::FromError(container.GetError());
    m_Layouts.push_back(LayoutSlot{Layout{container.Value()}, strip});
    return Result<std::size_t>::FromValue(m_Layouts.size() - 1);
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
    slot->layout.reset();
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
    if (m_RemainingDemand[placement.itemIndex] == 0)
    {
        return Result<MSPPlacedHandle>::FromError(
            {ErrorCode::Infeasible, "MSPP item demand is exhausted"});
    }
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
    const double fittedWidth =
        *rightmost * 1.00001 + slot->strip.shapeModifyConfig.offset.value_or(0.0);
    if (!std::isfinite(fittedWidth) || fittedWidth <= 0.0)
    {
        return Result<void>::FromError(
            {ErrorCode::Infeasible, "computed MSPP strip width is invalid"});
    }
    return ChangeStripWidth(layoutIndex, fittedWidth);
}

JAGUARCORE_API MSPSolution MSPProblem::Save() const
{
    MSPSolution solution;
    for (std::size_t i = 0; i < m_Layouts.size(); ++i)
    {
        if (m_Layouts[i].layout.has_value())
        {
            solution.layouts.push_back(
                {i, m_Layouts[i].strip, m_Layouts[i].layout->Save()});
        }
    }
    return solution;
}

JAGUARCORE_API Result<void> MSPProblem::Restore(const MSPSolution& solution)
{
    std::size_t layoutCount = 0;
    for (const MSPLayoutSnapshot& snapshot : solution.layouts)
        layoutCount = std::max(layoutCount, snapshot.layoutIndex + 1);
    std::vector<LayoutSlot> newLayouts(layoutCount);
    std::vector<bool> seen(layoutCount, false);
    std::vector<std::size_t> remaining(m_Instance.ItemCount(), 0);
    for (std::size_t i = 0; i < remaining.size(); ++i)
        remaining[i] = m_Instance.ItemDemand(i);

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
            !near(snapshot.strip.maxWidth, m_Instance.BaseStrip().maxWidth) ||
            !near(snapshot.strip.fixedHeight,
                  m_Instance.BaseStrip().fixedHeight) ||
            !sameCdeConfig(snapshot.strip.cdeConfig,
                           m_Instance.BaseStrip().cdeConfig) ||
            !sameShapeModifyConfig(snapshot.strip.shapeModifyConfig,
                                   m_Instance.BaseStrip().shapeModifyConfig) ||
            !sameStripContainer(snapshot.layoutSnapshot.container,
                                snapshot.strip))
        {
            return Result<void>::FromError(
                {ErrorCode::SnapshotMismatch, "MSPP solution strip is invalid"});
        }
        Layout restored(snapshot.layoutSnapshot.container);
        const auto restoredResult = restored.Restore(snapshot.layoutSnapshot);
        if (!restoredResult.HasValue())
            return restoredResult;
        for (const LayoutPlacementSnapshot& placement :
             snapshot.layoutSnapshot.placements)
        {
            const ItemIndex itemIndex = placement.placedItem.GetItemIndex();
            if (itemIndex >= remaining.size() || remaining[itemIndex] == 0 ||
                !sameItemDefinition(m_Instance.GetItem(itemIndex), placement.placedItem))
            {
                return Result<void>::FromError(
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
    DebugAssertMSPSolution(*this);
    return Result<void>::FromValue();
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

JAGUARCORE_API double MSPProblem::TotalStripWidth() const noexcept
{
    double result = 0.0;
    for (const LayoutSlot& slot : m_Layouts)
    {
        if (slot.layout.has_value())
            result += slot.strip.width;
    }
    return result;
}

JAGUARCORE_API double MSPProblem::Density() const
{
    double containerArea = 0.0;
    double itemArea = 0.0;
    for (const LayoutSlot& slot : m_Layouts)
    {
        if (!slot.layout.has_value())
            continue;
        containerArea += slot.strip.fixedHeight * slot.strip.width;
        itemArea += slot.layout->Density() * slot.layout->GetContainer().Area();
    }
    return containerArea > 0.0 ? itemArea / containerArea : 0.0;
}

JAGUARCORE_API const MSPInstance& MSPProblem::Instance() const noexcept
{
    return m_Instance;
}
}
