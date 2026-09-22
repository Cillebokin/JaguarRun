#include "JaguarCore/Assertions.h"

#include "JaguarCore/Entities/Layout.h"
#include "JaguarCore/Problems/Bpp.h"
#include "JaguarCore/Problems/MSpp.h"
#include "JaguarCore/Problems/Spp.h"
#include "JaguarCore/NumericPolicy.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <vector>

namespace jaguar
{
namespace
{
constexpr double TOLERANCE = NumericPolicy::DefaultEqualityTolerance();

bool near(double first, double second) noexcept
{
    return std::abs(first - second) <= TOLERANCE;
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

bool samePolygon(const SimplePolygon& first, const SimplePolygon& second) noexcept
{
    if (first.VertexCount() != second.VertexCount())
        return false;
    for (std::size_t i = 0; i < first.VertexCount(); ++i)
    {
        if (!near(first.Vertices()[i].x, second.Vertices()[i].x) ||
            !near(first.Vertices()[i].y, second.Vertices()[i].y))
            return false;
    }
    return true;
}

bool sameTransform(const Transform2d& first, const Transform2d& second) noexcept
{
    return near(first.rotationRad, second.rotationRad) &&
           near(first.translation.x, second.translation.x) &&
           near(first.translation.y, second.translation.y);
}

bool samePlacedItem(const PlacedItem& first, const PlacedItem& second) noexcept
{
    return first.GetItemIndex() == second.GetItemIndex() &&
           sameTransform(first.Transform(), second.Transform()) &&
           samePolygon(first.CollisionShape(), second.CollisionShape());
}

bool sameItemDefinition(const Item& expected,
                        const PlacedItem& actual) noexcept
{
    if (expected.Index() != actual.GetItemIndex() ||
        expected.ExternalId() != actual.GetItem().ExternalId() ||
        expected.CollisionShape().VertexCount() !=
            actual.GetItem().CollisionShape().VertexCount())
    {
        return false;
    }
    for (std::size_t i = 0; i < expected.CollisionShape().VertexCount(); ++i)
    {
        if (!near(expected.CollisionShape().Vertices()[i].x,
                  actual.GetItem().CollisionShape().Vertices()[i].x) ||
            !near(expected.CollisionShape().Vertices()[i].y,
                  actual.GetItem().CollisionShape().Vertices()[i].y))
        {
            return false;
        }
    }
    return true;
}

template <typename Instance>
bool placementsMatchInstance(
    const Instance& instance,
    const std::vector<LayoutPlacementSnapshot>& placements) noexcept
{
    for (const LayoutPlacementSnapshot& placement : placements)
    {
        const ItemIndex itemIndex = placement.placedItem.GetItemIndex();
        if (itemIndex >= instance.ItemCount() ||
            !sameItemDefinition(instance.GetItem(itemIndex), placement.placedItem))
            return false;
    }
    return true;
}

bool sameHazard(const Hazard& first, const Hazard& second) noexcept
{
    return first.dynamic == second.dynamic && first.entity == second.entity &&
           first.shape && second.shape && samePolygon(*first.shape, *second.shape);
}

bool sameLayoutSnapshot(const LayoutSnapshot& first,
                        const LayoutSnapshot& second) noexcept
{
    if (!first.container.SameBase(second.container) ||
        first.placements.size() != second.placements.size() ||
        first.cdeSnapshot.dynamicHazards.size() !=
            second.cdeSnapshot.dynamicHazards.size())
        return false;

    for (const LayoutPlacementSnapshot& placement : first.placements)
    {
        const auto match = std::find_if(
            second.placements.begin(), second.placements.end(),
            [&placement](const LayoutPlacementSnapshot& candidate) {
                return candidate.handle == placement.handle &&
                       samePlacedItem(candidate.placedItem, placement.placedItem);
            });
        if (match == second.placements.end())
            return false;
    }

    std::vector<bool> matched(second.cdeSnapshot.dynamicHazards.size(), false);
    for (const Hazard& hazard : first.cdeSnapshot.dynamicHazards)
    {
        bool found = false;
        for (std::size_t i = 0; i < second.cdeSnapshot.dynamicHazards.size(); ++i)
        {
            if (!matched[i] && sameHazard(hazard, second.cdeSnapshot.dynamicHazards[i]))
            {
                matched[i] = true;
                found = true;
                break;
            }
        }
        if (!found)
            return false;
    }
    return true;
}

Result<void> mismatch(const char* message)
{
    return Result<void>::FromError({ErrorCode::SnapshotMismatch, message});
}

Result<void> validateStandaloneSnapshot(const LayoutSnapshot& snapshot)
{
    Layout rebuilt(snapshot.container);
    const auto restored = rebuilt.Restore(snapshot);
    if (!restored.HasValue())
        return restored;
    if (!sameLayoutSnapshot(snapshot, rebuilt.Save()))
        return mismatch("rebuilt layout differs from snapshot");
    return Result<void>::FromValue();
}

bool demandMatches(const std::vector<std::size_t>& required,
                   const std::vector<std::size_t>& remaining,
                   const std::vector<LayoutPlacementSnapshot>& placements) noexcept
{
    if (required.size() != remaining.size())
        return false;
    std::vector<std::size_t> used(required.size(), 0);
    for (const LayoutPlacementSnapshot& placement : placements)
    {
        const ItemIndex itemIndex = placement.placedItem.GetItemIndex();
        if (itemIndex >= used.size())
            return false;
        ++used[itemIndex];
    }
    for (std::size_t i = 0; i < required.size(); ++i)
    {
        if (used[i] > required[i] || required[i] - used[i] != remaining[i])
            return false;
    }
    return true;
}
}

JAGUARCORE_API Result<void> ValidateLayoutSnapshot(
    const Layout& layout, const LayoutSnapshot& snapshot)
{
    const LayoutSnapshot current = layout.Save();
    if (!sameLayoutSnapshot(current, snapshot))
        return mismatch("layout and snapshot differ");
    return validateStandaloneSnapshot(snapshot);
}

JAGUARCORE_API Result<void> ValidateLayout(const Layout& layout)
{
    const LayoutSnapshot snapshot = layout.Save();
    return ValidateLayoutSnapshot(layout, snapshot);
}

JAGUARCORE_API Result<void> ValidateSPSolution(
    const SPProblem& problem, const SPSolution& solution)
{
    const SPSolution current = problem.Save();
    if (!near(current.strip.fixedHeight, solution.strip.fixedHeight) ||
        !near(current.strip.width, solution.strip.width) ||
        !sameCdeConfig(current.strip.cdeConfig, solution.strip.cdeConfig) ||
        !sameShapeModifyConfig(current.strip.shapeModifyConfig,
                               solution.strip.shapeModifyConfig) ||
        !sameLayoutSnapshot(current.layoutSnapshot, solution.layoutSnapshot) ||
        !placementsMatchInstance(problem.Instance(),
                                 solution.layoutSnapshot.placements) ||
        !demandMatches(
            [&problem] {
                std::vector<std::size_t> result(problem.Instance().ItemCount());
                for (std::size_t i = 0; i < result.size(); ++i)
                    result[i] = problem.Instance().ItemDemand(i);
                return result;
            }(),
            [&problem] {
                std::vector<std::size_t> result(problem.Instance().ItemCount());
                for (std::size_t i = 0; i < result.size(); ++i)
                    result[i] = problem.RemainingDemand(i);
                return result;
            }(),
            solution.layoutSnapshot.placements))
        return mismatch("SP solution differs from current problem state");
    return validateStandaloneSnapshot(solution.layoutSnapshot);
}

JAGUARCORE_API Result<void> ValidateBPSolution(
    const BPProblem& problem, const BPSolution& solution)
{
    const BPSolution current = problem.Save();
    if (current.layouts.size() != solution.layouts.size())
        return mismatch("BP solution layout count differs from current state");
    for (const BPLayoutSnapshot& expected : current.layouts)
    {
        const auto actual = std::find_if(
            solution.layouts.begin(), solution.layouts.end(),
            [&expected](const BPLayoutSnapshot& candidate) {
                return candidate.layoutIndex == expected.layoutIndex &&
                       candidate.binIndex == expected.binIndex &&
                       sameLayoutSnapshot(candidate.layoutSnapshot,
                                          expected.layoutSnapshot);
            });
        if (actual == solution.layouts.end())
            return mismatch("BP solution layout differs from current state");
        const auto valid = validateStandaloneSnapshot(actual->layoutSnapshot);
        if (!valid.HasValue())
            return valid;
    }
    {
        std::vector<LayoutPlacementSnapshot> placements;
        for (const BPLayoutSnapshot& snapshot : solution.layouts)
        {
            placements.insert(placements.end(),
                              snapshot.layoutSnapshot.placements.begin(),
                              snapshot.layoutSnapshot.placements.end());
        }
        if (!placementsMatchInstance(problem.Instance(), placements))
            return mismatch("BP solution item identity differs from instance");
        std::vector<std::size_t> required(problem.Instance().ItemCount());
        std::vector<std::size_t> remaining(problem.Instance().ItemCount());
        for (std::size_t i = 0; i < required.size(); ++i)
        {
            required[i] = problem.Instance().ItemDemand(i);
            remaining[i] = problem.RemainingDemand(i);
        }
        if (!demandMatches(required, remaining, placements))
            return mismatch("BP solution demand differs from current state");

        std::vector<std::size_t> usedBins(problem.Instance().BinCount(), 0);
        for (const BPLayoutSnapshot& snapshot : solution.layouts)
        {
            if (snapshot.binIndex >= usedBins.size())
                return mismatch("BP solution references an invalid bin");
            ++usedBins[snapshot.binIndex];
        }
        for (std::size_t i = 0; i < usedBins.size(); ++i)
        {
            if (usedBins[i] > problem.Instance().GetBin(i).stock ||
                problem.Instance().GetBin(i).stock - usedBins[i] !=
                    problem.RemainingBinStock(i))
                return mismatch("BP solution bin stock differs from current state");
        }
    }
    return Result<void>::FromValue();
}

JAGUARCORE_API Result<void> ValidateMSPSolution(
    const MSPProblem& problem, const MSPSolution& solution)
{
    const MSPSolution current = problem.Save();
    if (current.layouts.size() != solution.layouts.size())
        return mismatch("MSP solution layout count differs from current state");
    for (const MSPLayoutSnapshot& expected : current.layouts)
    {
        const auto actual = std::find_if(
            solution.layouts.begin(), solution.layouts.end(),
            [&expected](const MSPLayoutSnapshot& candidate) {
                return candidate.layoutIndex == expected.layoutIndex &&
                       near(candidate.strip.maxWidth, expected.strip.maxWidth) &&
                       near(candidate.strip.fixedHeight, expected.strip.fixedHeight) &&
                       near(candidate.strip.width, expected.strip.width) &&
                       sameCdeConfig(candidate.strip.cdeConfig,
                                     expected.strip.cdeConfig) &&
                       sameShapeModifyConfig(candidate.strip.shapeModifyConfig,
                                              expected.strip.shapeModifyConfig) &&
                       sameLayoutSnapshot(candidate.layoutSnapshot,
                                          expected.layoutSnapshot);
            });
        if (actual == solution.layouts.end())
            return mismatch("MSP solution layout differs from current state");
        const auto valid = validateStandaloneSnapshot(actual->layoutSnapshot);
        if (!valid.HasValue())
            return valid;
    }
    {
        std::vector<LayoutPlacementSnapshot> placements;
        for (const MSPLayoutSnapshot& snapshot : solution.layouts)
        {
            placements.insert(placements.end(),
                              snapshot.layoutSnapshot.placements.begin(),
                              snapshot.layoutSnapshot.placements.end());
        }
        if (!placementsMatchInstance(problem.Instance(), placements))
            return mismatch("MSP solution item identity differs from instance");
        std::vector<std::size_t> required(problem.Instance().ItemCount());
        std::vector<std::size_t> remaining(problem.Instance().ItemCount());
        for (std::size_t i = 0; i < required.size(); ++i)
        {
            required[i] = problem.Instance().ItemDemand(i);
            remaining[i] = problem.RemainingDemand(i);
        }
        if (!demandMatches(required, remaining, placements))
            return mismatch("MSP solution demand differs from current state");
    }
    return Result<void>::FromValue();
}

JAGUARCORE_API void DebugAssertLayout(const Layout& layout)
{
#ifndef NDEBUG
    assert(ValidateLayout(layout).HasValue());
#else
    (void)layout;
#endif
}

JAGUARCORE_API void DebugAssertSPSolution(const SPProblem& problem)
{
#ifndef NDEBUG
    assert(ValidateSPSolution(problem, problem.Save()).HasValue());
#else
    (void)problem;
#endif
}

JAGUARCORE_API void DebugAssertBPSolution(const BPProblem& problem)
{
#ifndef NDEBUG
    assert(ValidateBPSolution(problem, problem.Save()).HasValue());
#else
    (void)problem;
#endif
}

JAGUARCORE_API void DebugAssertMSPSolution(const MSPProblem& problem)
{
#ifndef NDEBUG
    assert(ValidateMSPSolution(problem, problem.Save()).HasValue());
#else
    (void)problem;
#endif
}
}
