#include "JaguarCore/assertions.h"

#include "JaguarCore/entities/layout.h"
#include "JaguarCore/problems/bpp.h"
#include "JaguarCore/problems/mspp.h"
#include "JaguarCore/problems/spp.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <vector>

namespace jaguar
{
namespace
{
constexpr double kTolerance = 1.0e-9;

bool near(double first, double second) noexcept
{
    return std::abs(first - second) <= kTolerance;
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
    if (first.vertexCount() != second.vertexCount())
        return false;
    for (std::size_t i = 0; i < first.vertexCount(); ++i)
    {
        if (!near(first.vertices()[i].x, second.vertices()[i].x) ||
            !near(first.vertices()[i].y, second.vertices()[i].y))
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
    return first.itemIndex() == second.itemIndex() &&
           sameTransform(first.transform(), second.transform()) &&
           samePolygon(first.collisionShape(), second.collisionShape());
}

bool sameItemDefinition(const Item& expected,
                        const PlacedItem& actual) noexcept
{
    if (expected.index() != actual.itemIndex() ||
        expected.externalId() != actual.item().externalId() ||
        expected.collisionShape().vertexCount() !=
            actual.item().collisionShape().vertexCount())
    {
        return false;
    }
    for (std::size_t i = 0; i < expected.collisionShape().vertexCount(); ++i)
    {
        if (!near(expected.collisionShape().vertices()[i].x,
                  actual.item().collisionShape().vertices()[i].x) ||
            !near(expected.collisionShape().vertices()[i].y,
                  actual.item().collisionShape().vertices()[i].y))
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
        const ItemIndex itemIndex = placement.placedItem.itemIndex();
        if (itemIndex >= instance.itemCount() ||
            !sameItemDefinition(instance.item(itemIndex), placement.placedItem))
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
    if (!first.container.sameBase(second.container) ||
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
    return Result<void>::fromError({ErrorCode::SnapshotMismatch, message});
}

Result<void> validateStandaloneSnapshot(const LayoutSnapshot& snapshot)
{
    Layout rebuilt(snapshot.container);
    const auto restored = rebuilt.restore(snapshot);
    if (!restored.hasValue())
        return restored;
    if (!sameLayoutSnapshot(snapshot, rebuilt.save()))
        return mismatch("rebuilt layout differs from snapshot");
    return Result<void>::fromValue();
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
        const ItemIndex itemIndex = placement.placedItem.itemIndex();
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

JAGUARCORE_API Result<void> validateLayoutSnapshot(
    const Layout& layout, const LayoutSnapshot& snapshot)
{
    const LayoutSnapshot current = layout.save();
    if (!sameLayoutSnapshot(current, snapshot))
        return mismatch("layout and snapshot differ");
    return validateStandaloneSnapshot(snapshot);
}

JAGUARCORE_API Result<void> validateLayout(const Layout& layout)
{
    const LayoutSnapshot snapshot = layout.save();
    return validateLayoutSnapshot(layout, snapshot);
}

JAGUARCORE_API Result<void> validateSPSolution(
    const SPProblem& problem, const SPSolution& solution)
{
    const SPSolution current = problem.save();
    if (!near(current.strip.fixedHeight, solution.strip.fixedHeight) ||
        !near(current.strip.width, solution.strip.width) ||
        !sameCdeConfig(current.strip.cdeConfig, solution.strip.cdeConfig) ||
        !sameShapeModifyConfig(current.strip.shapeModifyConfig,
                               solution.strip.shapeModifyConfig) ||
        !sameLayoutSnapshot(current.layoutSnapshot, solution.layoutSnapshot) ||
        !placementsMatchInstance(problem.instance(),
                                 solution.layoutSnapshot.placements) ||
        !demandMatches(
            [&problem] {
                std::vector<std::size_t> result(problem.instance().itemCount());
                for (std::size_t i = 0; i < result.size(); ++i)
                    result[i] = problem.instance().itemDemand(i);
                return result;
            }(),
            [&problem] {
                std::vector<std::size_t> result(problem.instance().itemCount());
                for (std::size_t i = 0; i < result.size(); ++i)
                    result[i] = problem.remainingDemand(i);
                return result;
            }(),
            solution.layoutSnapshot.placements))
        return mismatch("SP solution differs from current problem state");
    return validateStandaloneSnapshot(solution.layoutSnapshot);
}

JAGUARCORE_API Result<void> validateBPSolution(
    const BPProblem& problem, const BPSolution& solution)
{
    const BPSolution current = problem.save();
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
        if (!valid.hasValue())
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
        if (!placementsMatchInstance(problem.instance(), placements))
            return mismatch("BP solution item identity differs from instance");
        std::vector<std::size_t> required(problem.instance().itemCount());
        std::vector<std::size_t> remaining(problem.instance().itemCount());
        for (std::size_t i = 0; i < required.size(); ++i)
        {
            required[i] = problem.instance().itemDemand(i);
            remaining[i] = problem.remainingDemand(i);
        }
        if (!demandMatches(required, remaining, placements))
            return mismatch("BP solution demand differs from current state");

        std::vector<std::size_t> usedBins(problem.instance().binCount(), 0);
        for (const BPLayoutSnapshot& snapshot : solution.layouts)
        {
            if (snapshot.binIndex >= usedBins.size())
                return mismatch("BP solution references an invalid bin");
            ++usedBins[snapshot.binIndex];
        }
        for (std::size_t i = 0; i < usedBins.size(); ++i)
        {
            if (usedBins[i] > problem.instance().bin(i).stock ||
                problem.instance().bin(i).stock - usedBins[i] !=
                    problem.remainingBinStock(i))
                return mismatch("BP solution bin stock differs from current state");
        }
    }
    return Result<void>::fromValue();
}

JAGUARCORE_API Result<void> validateMSPSolution(
    const MSPProblem& problem, const MSPSolution& solution)
{
    const MSPSolution current = problem.save();
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
        if (!valid.hasValue())
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
        if (!placementsMatchInstance(problem.instance(), placements))
            return mismatch("MSP solution item identity differs from instance");
        std::vector<std::size_t> required(problem.instance().itemCount());
        std::vector<std::size_t> remaining(problem.instance().itemCount());
        for (std::size_t i = 0; i < required.size(); ++i)
        {
            required[i] = problem.instance().itemDemand(i);
            remaining[i] = problem.remainingDemand(i);
        }
        if (!demandMatches(required, remaining, placements))
            return mismatch("MSP solution demand differs from current state");
    }
    return Result<void>::fromValue();
}

JAGUARCORE_API void debugAssertLayout(const Layout& layout)
{
#ifndef NDEBUG
    assert(validateLayout(layout).hasValue());
#else
    (void)layout;
#endif
}

JAGUARCORE_API void debugAssertSPSolution(const SPProblem& problem)
{
#ifndef NDEBUG
    assert(validateSPSolution(problem, problem.save()).hasValue());
#else
    (void)problem;
#endif
}

JAGUARCORE_API void debugAssertBPSolution(const BPProblem& problem)
{
#ifndef NDEBUG
    assert(validateBPSolution(problem, problem.save()).hasValue());
#else
    (void)problem;
#endif
}

JAGUARCORE_API void debugAssertMSPSolution(const MSPProblem& problem)
{
#ifndef NDEBUG
    assert(validateMSPSolution(problem, problem.save()).hasValue());
#else
    (void)problem;
#endif
}
}
