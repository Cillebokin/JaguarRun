#include "SparrowCore/Optimizer/Exploration.h"

#include "SparrowCore/Optimizer/Separator.h"

#include "JaguarCore/Assertions.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

namespace
{
sparrow::Result<sparrow::LbfMetrics> CalculateMetrics(
    const jaguar::SPInstance& instance,
    const jaguar::SPSolution& solution,
    std::uint64_t seed)
{
    auto problemResult = jaguar::SPProblem::Create(instance);
    if (!problemResult.HasValue())
    {
        return sparrow::Result<sparrow::LbfMetrics>::FromError(
            problemResult.GetError());
    }
    jaguar::SPProblem problem = std::move(problemResult.Value());
    const auto restored = problem.Restore(solution);
    if (!restored.HasValue())
    {
        return sparrow::Result<sparrow::LbfMetrics>::FromError(
            restored.GetError());
    }
    jaguar::Layout layout(solution.layoutSnapshot.container);
    const auto layoutRestored = layout.Restore(solution.layoutSnapshot);
    if (!layoutRestored.HasValue())
    {
        return sparrow::Result<sparrow::LbfMetrics>::FromError(
            layoutRestored.GetError());
    }
    sparrow::LbfMetrics metrics;
    metrics.seed = seed;
    metrics.placedItemCount = problem.PlacedItemCount();
    for (std::size_t index = 0U; index < instance.ItemCount(); ++index)
    {
        metrics.remainingDemand += problem.RemainingDemand(index);
    }
    metrics.stripWidth = solution.StripWidth();
    metrics.density = solution.Density();
    metrics.feasible = metrics.remainingDemand == 0U
        && jaguar::ValidateSPSolution(problem, solution).HasValue()
        && layout.IsFeasible();
    return sparrow::Result<sparrow::LbfMetrics>::FromValue(metrics);
}

double CalculateMinimumStripWidth(
    const jaguar::SPInstance& instance,
    const jaguar::SPSolution& solution) noexcept
{
    constexpr double relativeTolerance = 1.0e-6;
    const jaguar::Rect2d containerBounds =
        solution.layoutSnapshot.container.CollisionShape().BoundingBox();
    if (!std::isfinite(containerBounds.Width())
        || !std::isfinite(containerBounds.Height())
        || containerBounds.Width() <= 0.0
        || containerBounds.Height() <= 0.0)
    {
        return 0.0;
    }

    double totalArea = 0.0;
    for (std::size_t index = 0U; index < instance.ItemCount(); ++index)
    {
        totalArea += instance.GetItem(index).CollisionShape().Area()
            * static_cast<double>(instance.ItemDemand(index));
    }
    double minimumWidth = totalArea / containerBounds.Height()
        * (1.0 - relativeTolerance);

    for (std::size_t index = 0U; index < instance.ItemCount(); ++index)
    {
        const jaguar::Item& item = instance.GetItem(index);
        const auto& rotation = item.AllowedRotation();
        if (rotation.mode == jaguar::RotationMode::Continuous)
        {
            continue;
        }

        std::vector<double> rotations;
        if (rotation.mode == jaguar::RotationMode::None)
        {
            rotations.push_back(0.0);
        }
        else
        {
            rotations = rotation.radians;
        }
        double itemWidth = std::numeric_limits<double>::infinity();
        const double tolerance = item.CollisionShape().Diameter()
            * relativeTolerance;
        for (const double angle : rotations)
        {
            if (!std::isfinite(angle))
            {
                continue;
            }
            const auto transformed = item.CollisionShape().TransformedRustF32(
                jaguar::Transform2d::FromRotationTranslation(angle, {0.0, 0.0}));
            if (!transformed.HasValue())
            {
                continue;
            }
            const jaguar::Rect2d bounds = transformed.Value().BoundingBox();
            if (bounds.Height() <= containerBounds.Height() + tolerance)
            {
                itemWidth = std::min(
                    itemWidth,
                    std::max(0.0, bounds.Width() - tolerance));
            }
        }
        if (std::isfinite(itemWidth))
        {
            minimumWidth = std::max(minimumWidth, itemWidth);
        }
    }

    const double widthInset = solution.StripWidth() - containerBounds.Width();
    const double result = minimumWidth + widthInset;
    return std::isfinite(result) && result > 0.0 ? result : 0.0;
}

double NormalizeRotation(double value) noexcept
{
    constexpr double twoPi = 6.28318530717958647692;
    const double normalized = std::fmod(value, twoPi);
    return normalized < 0.0 ? normalized + twoPi : normalized;
}

double RotationDistance(double first, double second) noexcept
{
    constexpr double pi = 3.14159265358979323846;
    const double distance = std::abs(
        NormalizeRotation(first) - NormalizeRotation(second));
    return std::min(distance, 2.0 * pi - distance);
}

jaguar::Transform2d LegalizeTransform(
    jaguar::Transform2d transform,
    const jaguar::Item& item)
{
    switch (item.AllowedRotation().mode)
    {
    case jaguar::RotationMode::None:
        transform.rotationRad = 0.0;
        break;
    case jaguar::RotationMode::Discrete:
        if (!item.AllowedRotation().radians.empty())
        {
            double nearest = std::numeric_limits<double>::infinity();
            double selected = item.AllowedRotation().radians.front();
            for (const double allowed : item.AllowedRotation().radians)
            {
                if (!std::isfinite(allowed))
                {
                    continue;
                }
                const double distance = RotationDistance(
                    transform.rotationRad, allowed);
                if (distance < nearest)
                {
                    nearest = distance;
                    selected = allowed;
                }
            }
            transform.rotationRad = selected;
        }
        else
        {
            transform.rotationRad = 0.0;
        }
        break;
    case jaguar::RotationMode::Continuous:
        break;
    }
    return transform;
}

std::uint64_t BoundedIndex(sparrow::SparrowRng& rng,
                           std::uint64_t bound) noexcept
{
    if (bound <= 1U)
    {
        return 0U;
    }
    const std::uint64_t threshold =
        (std::numeric_limits<std::uint64_t>::max() % bound + 1U) % bound;
    std::uint64_t value = 0U;
    do
    {
        value = rng.NextU64();
    } while (value < threshold);
    return value % bound;
}

double DoubleFromBits(std::uint64_t bits) noexcept
{
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

// rand_distr 0.6 samples StandardNormal with ZIGNOR in f64, then casts to f32.
// Keep this local to Exploration so other SparrowRng consumers retain their sampler.
struct NormalZigguratTables
{
    std::array<double, 257U> x{};
    std::array<double, 257U> f{};
};

const NormalZigguratTables& GetNormalZigguratTables() noexcept
{
    static const NormalZigguratTables tables = [] {
        constexpr double tail = 3.654152885361008796;
        constexpr double area = 0.00492867323399;
        NormalZigguratTables result;
        result.x[1U] = tail;
        for (std::size_t index = 2U; index < result.x.size(); ++index)
        {
            const double previous = result.x[index - 1U];
            result.x[index] = std::sqrt(
                -2.0 * std::log(
                    area / previous
                    + std::exp(-previous * previous / 2.0)));
        }
        result.x[0U] = area / std::exp(-tail * tail / 2.0);
        for (std::size_t index = 0U; index < result.x.size(); ++index)
        {
            result.f[index] =
                std::exp(-result.x[index] * result.x[index] / 2.0);
        }
        return result;
    }();
    return tables;
}

double RustUniform01F64(sparrow::SparrowRng& rng) noexcept
{
    constexpr double scale = 1.0 / 9007199254740992.0;
    return static_cast<double>(rng.NextU64() >> 11U) * scale;
}

double RustOpen01F64(sparrow::SparrowRng& rng) noexcept
{
    constexpr std::uint64_t exponentOne = 0x3ff0000000000000ULL;
    const double oneToTwo = DoubleFromBits(
        exponentOne | (rng.NextU64() >> 12U));
    return oneToTwo
        - (1.0 - std::numeric_limits<double>::epsilon() / 2.0);
}

double RustStandardNormalF64(sparrow::SparrowRng& rng)
{
    constexpr std::uint64_t exponentTwo = 0x4000000000000000ULL;
    constexpr double tail = 3.654152885361008796;
    const NormalZigguratTables& tables = GetNormalZigguratTables();
    for (;;)
    {
        const std::uint64_t bits = rng.NextU64();
        const std::size_t index = static_cast<std::size_t>(bits & 0xffU);
        const double u = DoubleFromBits(exponentTwo | (bits >> 12U)) - 3.0;
        const double x = u * tables.x[index];
        if (std::abs(x) < tables.x[index + 1U])
        {
            return x;
        }
        if (index == 0U)
        {
            double tailX = 1.0;
            double tailY = 0.0;
            while (-2.0 * tailY < tailX * tailX)
            {
                tailX = std::log(RustOpen01F64(rng)) / tail;
                tailY = std::log(RustOpen01F64(rng));
            }
            return u < 0.0 ? tailX - tail : tail - tailX;
        }

        const double vertical = tables.f[index + 1U]
            + (tables.f[index] - tables.f[index + 1U])
                * RustUniform01F64(rng);
        if (vertical < std::exp(-x * x / 2.0))
        {
            return x;
        }
    }
}

float RustNormalF32(sparrow::SparrowRng& rng, float standardDeviation)
{
    return standardDeviation
        * static_cast<float>(RustStandardNormalF64(rng));
}

const jaguar::LayoutPlacementSnapshot* FindPlacement(
    const jaguar::LayoutSnapshot& snapshot,
    jaguar::PlacedItemHandle handle) noexcept
{
    for (const auto& placement : snapshot.placements)
    {
        if (placement.handle == handle)
        {
            return &placement;
        }
    }
    return nullptr;
}

struct InfeasibleCandidate
{
    InfeasibleCandidate(jaguar::SPSolution value, float candidateLoss)
        : solution(std::move(value)),
          loss(candidateLoss)
    {
    }

    jaguar::SPSolution solution;
    float loss = 0.0F;
};

sparrow::Result<std::vector<jaguar::PlacedItemHandle>>
CollectContainedHandles(
    const jaguar::SPSolution& solution,
    jaguar::PlacedItemHandle containerHandle)
{
    jaguar::Layout layout(solution.layoutSnapshot.container);
    const auto restored = layout.Restore(solution.layoutSnapshot);
    if (!restored.HasValue())
    {
        return sparrow::Result<std::vector<jaguar::PlacedItemHandle>>::FromError(
            restored.GetError());
    }
    const auto* containerPlacement = FindPlacement(
        solution.layoutSnapshot, containerHandle);
    if (containerPlacement == nullptr)
    {
        return sparrow::Result<std::vector<jaguar::PlacedItemHandle>>::FromError(
            {jaguar::ErrorCode::InvalidInput,
             "contained-item query handle is not in the layout"});
    }

    auto engine = solution.layoutSnapshot.container.CreateCollisionEngine();
    if (!engine || !engine->Restore(solution.layoutSnapshot.cdeSnapshot).HasValue())
    {
        return sparrow::Result<std::vector<jaguar::PlacedItemHandle>>::FromError(
            {jaguar::ErrorCode::CollisionEngineFailure,
             "contained-item collision engine is unavailable"});
    }
    jaguar::HazardFilter filter;
    // Collision-engine handles include static hazards; the entity stores the layout handle.
    filter.irrelevant = [containerHandle](
                            const jaguar::HazardEntity& entity) {
        return entity.kind == jaguar::HazardKind::PlacedItem
            && entity.placedItemHandle == containerHandle;
    };
    jaguar::HazardCollector collector;
    engine->CollectPolygonCollisionsUntil(
        containerPlacement->placedItem.CollisionShape(),
        collector,
        filter,
        [](const jaguar::HazardEntity&) { return false; });

    std::vector<jaguar::PlacedItemHandle> result;
    for (const auto& entry : collector.Entries())
    {
        if (entry.second.kind != jaguar::HazardKind::PlacedItem)
        {
            continue;
        }
        const auto* other = FindPlacement(
            solution.layoutSnapshot, entry.second.placedItemHandle);
        if (other != nullptr
            && containerPlacement->placedItem.CollisionShape().ContainsRustF32(
                   other->placedItem.CollisionShape()
                       .PointOfInaccessibility().center))
        {
            result.push_back(entry.second.placedItemHandle);
        }
    }
    return sparrow::Result<std::vector<jaguar::PlacedItemHandle>>::FromValue(
        std::move(result));
}

sparrow::Result<void> DisruptSolution(
    sparrow::Separator& separator,
    const sparrow::ExplorationConfig& config)
{
    if (config.disruptionCount == 0U
        && config.containedRelocationCount == 0U)
    {
        return sparrow::Result<void>::FromValue();
    }

    const jaguar::SPSolution solution = separator.SaveSolution();
    if (solution.layoutSnapshot.placements.size() < 2U)
    {
        return sparrow::Result<void>::FromValue();
    }

    double totalConvexHullArea = 0.0;
    std::vector<double> itemHullAreas;
    itemHullAreas.reserve(separator.Problem().Instance().ItemCount());
    for (std::size_t index = 0U;
         index < separator.Problem().Instance().ItemCount();
         ++index)
    {
        const jaguar::Surrogate* surrogate = separator.Problem().Instance()
            .GetItem(index).CollisionShape().GetSurrogate();
        const double hullArea = surrogate == nullptr
            ? 0.0
            : surrogate->ConvexHullArea();
        itemHullAreas.push_back(hullArea);
        totalConvexHullArea += hullArea * static_cast<double>(
            separator.Problem().Instance().ItemDemand(index));
    }
    const double cutoff = totalConvexHullArea
        * config.largeItemChAreaCutoffPercentile;
    std::vector<std::pair<double, std::size_t>> ordered;
    for (std::size_t index = 0U; index < itemHullAreas.size(); ++index)
    {
        ordered.emplace_back(itemHullAreas[index], index);
    }
    std::sort(ordered.begin(), ordered.end(),
              [](const auto& left, const auto& right) {
                  if (left.first != right.first)
                  {
                      return left.first > right.first;
                  }
                  return left.second < right.second;
              });
    double cumulative = 0.0;
    double cutoffArea = 0.0;
    for (const auto& entry : ordered)
    {
        cumulative += entry.first * static_cast<double>(
            separator.Problem().Instance().ItemDemand(entry.second));
        if (cumulative > cutoff)
        {
            cutoffArea = entry.first;
            break;
        }
    }

    std::vector<std::size_t> large;
    std::vector<std::size_t> all;
    for (std::size_t index = 0U;
         index < solution.layoutSnapshot.placements.size();
         ++index)
    {
        all.push_back(index);
        const auto& placed = solution.layoutSnapshot.placements[index].placedItem;
        const jaguar::Surrogate* surrogate =
            placed.CollisionShape().GetSurrogate();
        if (surrogate != nullptr && surrogate->ConvexHullArea() >= cutoffArea)
        {
            large.push_back(index);
        }
    }
    if (large.empty())
    {
        large = all;
    }

    const std::size_t firstOffset = static_cast<std::size_t>(BoundedIndex(
        separator.Random(), static_cast<std::uint64_t>(large.size())));
    const std::size_t firstIndex = large[firstOffset];
    std::vector<std::size_t> distinct;
    for (const std::size_t index : large)
    {
        if (index == firstIndex)
        {
            continue;
        }
        const auto& firstItem = solution.layoutSnapshot.placements[firstIndex]
            .placedItem;
        const auto& candidate = solution.layoutSnapshot.placements[index]
            .placedItem;
        const double areaTolerance = firstItem.Area() * 0.01;
        const double diameterTolerance = firstItem.CollisionShape().Diameter()
            * 0.01;
        if (std::abs(candidate.Area() - firstItem.Area()) > areaTolerance
            && std::abs(candidate.CollisionShape().Diameter()
                        - firstItem.CollisionShape().Diameter())
                   > diameterTolerance)
        {
            distinct.push_back(index);
        }
    }
    const std::vector<std::size_t>& secondPool = distinct.empty() ? all : distinct;
    std::vector<std::size_t> filteredSecondPool;
    for (const std::size_t index : secondPool)
    {
        if (index != firstIndex)
        {
            filteredSecondPool.push_back(index);
        }
    }
    if (filteredSecondPool.empty())
    {
        return sparrow::Result<void>::FromValue();
    }
    const std::size_t secondIndex = filteredSecondPool[static_cast<std::size_t>(
        BoundedIndex(separator.Random(), static_cast<std::uint64_t>(
            filteredSecondPool.size())))];

    const auto& firstPlacement = solution.layoutSnapshot.placements[firstIndex];
    const auto& secondPlacement = solution.layoutSnapshot.placements[secondIndex];
    const jaguar::Transform2d firstOld = firstPlacement.placedItem.Transform();
    const jaguar::Transform2d secondOld = secondPlacement.placedItem.Transform();
    const jaguar::Transform2d firstNew = LegalizeTransform(
        secondOld, firstPlacement.placedItem.GetItem());
    const jaguar::Transform2d secondNew = LegalizeTransform(
        firstOld, secondPlacement.placedItem.GetItem());

    const auto firstMoved = separator.MoveItem(
        firstPlacement.handle, firstNew);
    if (!firstMoved.HasValue())
    {
        return sparrow::Result<void>::FromError(firstMoved.GetError());
    }
    const auto secondMoved = separator.MoveItem(
        secondPlacement.handle, secondNew);
    if (!secondMoved.HasValue())
    {
        return sparrow::Result<void>::FromError(secondMoved.GetError());
    }

    const jaguar::Transform2d firstConversion = firstNew.Inverse()
        .FollowedBy(firstOld);
    const auto firstContained = CollectContainedHandles(
        separator.SaveSolution(), firstMoved.Value());
    if (!firstContained.HasValue())
    {
        return sparrow::Result<void>::FromError(firstContained.GetError());
    }
    for (const jaguar::PlacedItemHandle handle : firstContained.Value())
    {
        if (handle == secondMoved.Value())
        {
            continue;
        }
        const jaguar::SPSolution current = separator.SaveSolution();
        const auto* placement = FindPlacement(current.layoutSnapshot, handle);
        if (placement == nullptr)
        {
            continue;
        }
        const auto moved = separator.MoveItem(
            handle,
            LegalizeTransform(
                placement->placedItem.Transform().FollowedBy(firstConversion),
                placement->placedItem.GetItem()));
        if (!moved.HasValue())
        {
            return sparrow::Result<void>::FromError(moved.GetError());
        }
    }

    const jaguar::Transform2d secondConversion = secondNew.Inverse()
        .FollowedBy(secondOld);
    const auto secondContained = CollectContainedHandles(
        separator.SaveSolution(), secondMoved.Value());
    if (!secondContained.HasValue())
    {
        return sparrow::Result<void>::FromError(secondContained.GetError());
    }
    for (const jaguar::PlacedItemHandle handle : secondContained.Value())
    {
        if (handle == firstMoved.Value())
        {
            continue;
        }
        const jaguar::SPSolution current = separator.SaveSolution();
        const auto* placement = FindPlacement(current.layoutSnapshot, handle);
        if (placement == nullptr)
        {
            continue;
        }
        const auto moved = separator.MoveItem(
            handle,
            LegalizeTransform(
                placement->placedItem.Transform().FollowedBy(secondConversion),
                placement->placedItem.GetItem()));
        if (!moved.HasValue())
        {
            return sparrow::Result<void>::FromError(moved.GetError());
        }
    }
    return sparrow::Result<void>::FromValue();
}

sparrow::OptimizationProgress MakeProgress(
    sparrow::OptimizationPhase phase,
    std::size_t iteration,
    const sparrow::LbfMetrics& metrics,
    float collisionLoss)
{
    return {phase,
            iteration,
            metrics.stripWidth,
            metrics.density,
            collisionLoss,
            metrics.feasible};
}
}

namespace sparrow
{
Result<void> ExplorationConfig::Validate() const
{
    if (!std::isfinite(shrinkStep) || shrinkStep <= 0.0
        || shrinkStep >= 1.0
        || !std::isfinite(solutionPoolDistributionStddev)
        || solutionPoolDistributionStddev <= 0.0
        || !std::isfinite(largeItemChAreaCutoffPercentile)
        || largeItemChAreaCutoffPercentile < 0.0
        || largeItemChAreaCutoffPercentile > 1.0
        || timeLimit.count() < 0)
    {
        return Result<void>::FromError(
            {jaguar::ErrorCode::InvalidInput,
             "exploration limits, shrinkStep, or disruption settings are invalid"});
    }
    return separator.Validate();
}

Result<ExplorationResult> Explore(
    const jaguar::SPInstance& instance,
    const jaguar::SPSolution& initialSolution,
    const ExplorationConfig& config,
    std::uint64_t seed,
    OptimizationTerminator& terminator,
    OptimizationListener* listener)
{
    const Result<void> configResult = config.Validate();
    if (!configResult.HasValue())
    {
        return Result<ExplorationResult>::FromError(configResult.GetError());
    }
    const auto initialMetrics = CalculateMetrics(instance, initialSolution, seed);
    if (!initialMetrics.HasValue())
    {
        return Result<ExplorationResult>::FromError(initialMetrics.GetError());
    }

    auto problemResult = jaguar::SPProblem::Create(instance);
    if (!problemResult.HasValue())
    {
        return Result<ExplorationResult>::FromError(problemResult.GetError());
    }
    jaguar::SPProblem problem = std::move(problemResult.Value());
    const auto restored = problem.Restore(initialSolution);
    if (!restored.HasValue())
    {
        return Result<ExplorationResult>::FromError(restored.GetError());
    }
    auto separatorResult = Separator::Create(std::move(problem), seed);
    if (!separatorResult.HasValue())
    {
        return Result<ExplorationResult>::FromError(
            separatorResult.GetError());
    }
    Separator separator = std::move(separatorResult.Value());

    jaguar::SPSolution bestSolution = initialSolution;
    LbfMetrics bestMetrics = initialMetrics.Value();
    const double minimumWidth = CalculateMinimumStripWidth(
        instance, initialSolution);
    double currentWidth = separator.Problem().StripWidth();
    double bestWidth = currentWidth;
    std::size_t totalSurrogateRejectCount = 0U;
    std::size_t totalExactCollisionCount = 0U;
    std::size_t failures = 0U;
    std::size_t iteration = 0U;
    std::vector<InfeasibleCandidate> infeasiblePool;
    OptimizationHistory history;
    PublishSolutionReport(
        history,
        listener,
        MakeSolutionReport(
            bestMetrics.feasible
                ? OptimizationReportType::OPTIMIZATION_REPORT_EXPLORATION_FEASIBLE
                : OptimizationReportType::OPTIMIZATION_REPORT_EXPLORATION_INFEASIBLE,
            OptimizationPhase::OPTIMIZATION_PHASE_EXPLORATION,
            0U,
            bestSolution,
            0.0F,
            bestMetrics.feasible));
    if (listener != nullptr)
    {
        listener->OnProgress(MakeProgress(
            OptimizationPhase::OPTIMIZATION_PHASE_EXPLORATION,
            0U,
            bestMetrics,
            0.0F));
    }

    while (!terminator.ShouldTerminate(iteration)
           && (config.maxIterations == 0U
               || iteration < config.maxIterations))
    {
        const auto separated = separator.Run(
            config.separator,
            terminator,
            listener,
            &history,
            OptimizationPhase::OPTIMIZATION_PHASE_EXPLORATION);
        if (!separated.HasValue())
        {
            return Result<ExplorationResult>::FromError(
                separated.GetError());
        }
        const float totalLoss = separator.Tracker().TotalLoss();
        const auto metricsResult = CalculateMetrics(
            instance, separated.Value().solution, seed);
        if (!metricsResult.HasValue())
        {
            return Result<ExplorationResult>::FromError(
                metricsResult.GetError());
        }
        LbfMetrics metrics = metricsResult.Value();
        metrics.surrogateRejectCount = separated.Value().surrogateRejectCount;
        metrics.exactCollisionCount = separated.Value().exactCollisionCount;
        totalSurrogateRejectCount += metrics.surrogateRejectCount;
        totalExactCollisionCount += metrics.exactCollisionCount;
        if (totalLoss != 0.0F)
        {
            PublishSolutionReport(
                history,
                listener,
                MakeSolutionReport(
                    OptimizationReportType::OPTIMIZATION_REPORT_EXPLORATION_INFEASIBLE,
                    OptimizationPhase::OPTIMIZATION_PHASE_EXPLORATION,
                    iteration,
                    separated.Value().solution,
                    totalLoss,
                    false));
        }
        if (listener != nullptr)
        {
            listener->OnProgress(MakeProgress(
                OptimizationPhase::OPTIMIZATION_PHASE_EXPLORATION,
                iteration,
                metrics,
                totalLoss));
        }

        if (totalLoss == 0.0F)
        {
            if (currentWidth < bestWidth)
            {
                bestWidth = currentWidth;
                bestSolution = separated.Value().solution;
                bestMetrics = metrics;
                if (listener != nullptr)
                {
                    listener->OnImprovement(MakeProgress(
                        OptimizationPhase::OPTIMIZATION_PHASE_EXPLORATION,
                        iteration,
                        metrics,
                        totalLoss));
                }
                PublishSolutionReport(
                    history,
                    listener,
                    MakeSolutionReport(
                        OptimizationReportType::OPTIMIZATION_REPORT_EXPLORATION_FEASIBLE,
                        OptimizationPhase::OPTIMIZATION_PHASE_EXPLORATION,
                        iteration,
                        bestSolution,
                        totalLoss,
                        true));
            }
            const double nextWidth = currentWidth * (1.0 - config.shrinkStep);
            if (!std::isfinite(nextWidth) || nextWidth <= 0.0
                || nextWidth >= currentWidth
                || (minimumWidth > 0.0 && nextWidth < minimumWidth))
            {
                break;
            }
            const auto changed = separator.ChangeStripWidth(nextWidth);
            if (!changed.HasValue())
            {
                return Result<ExplorationResult>::FromError(
                    changed.GetError());
            }
            currentWidth = nextWidth;
            infeasiblePool.clear();
            failures = 0U;
        }
        else
        {
            InfeasibleCandidate candidate(
                separated.Value().solution, totalLoss);
            const auto insertion = std::lower_bound(
                infeasiblePool.begin(),
                infeasiblePool.end(),
                candidate.loss,
                [](const InfeasibleCandidate& value, float loss) {
                    return value.loss < loss;
                });
            infeasiblePool.insert(insertion, std::move(candidate));
            if (config.infeasiblePoolSize > 0U
                && infeasiblePool.size() > config.infeasiblePoolSize)
            {
                infeasiblePool.pop_back();
            }
            ++failures;
            if (config.maxConsecutiveFailures > 0U
                && failures >= config.maxConsecutiveFailures)
            {
                break;
            }

            const float sampled = std::min(
                std::abs(RustNormalF32(
                    separator.Random(),
                    static_cast<float>(config.solutionPoolDistributionStddev))),
                0.999F);
            const std::size_t selectedIndex = std::min(
                static_cast<std::size_t>(sampled
                    * static_cast<float>(infeasiblePool.size())),
                infeasiblePool.size() - 1U);
            const auto rolledBack = separator.Rollback(
                infeasiblePool[selectedIndex].solution);
            if (!rolledBack.HasValue())
            {
                return Result<ExplorationResult>::FromError(
                    rolledBack.GetError());
            }
            const auto disrupted = DisruptSolution(separator, config);
            if (!disrupted.HasValue())
            {
                return Result<ExplorationResult>::FromError(
                    disrupted.GetError());
            }
        }
        ++iteration;
    }

    bestMetrics.seed = seed;
    bestMetrics.surrogateRejectCount = totalSurrogateRejectCount;
    bestMetrics.exactCollisionCount = totalExactCollisionCount;
    return Result<ExplorationResult>::FromValue(
        {bestSolution, bestMetrics, std::move(history)});
}
}
