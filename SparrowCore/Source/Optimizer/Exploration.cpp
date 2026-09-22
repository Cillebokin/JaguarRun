#include "SparrowCore/Optimizer/Exploration.h"

#include "SparrowCore/Optimizer/Separator.h"

#include "JaguarCore/Assertions.h"

#include <cmath>
#include <utility>

namespace
{
sparrow::Result<sparrow::CollisionTracker> BuildTracker(
    const jaguar::SPSolution& solution)
{
    jaguar::Layout layout(solution.layoutSnapshot.container);
    const auto restored = layout.Restore(solution.layoutSnapshot);
    if (!restored.HasValue())
    {
        return sparrow::Result<sparrow::CollisionTracker>::FromError(
            restored.GetError());
    }
    return sparrow::CollisionTracker::Create(layout);
}

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
        && jaguar::ValidateSPSolution(problem, solution).HasValue();
    return sparrow::Result<sparrow::LbfMetrics>::FromValue(metrics);
}

sparrow::Result<sparrow::SeparatorResult> TrySeparate(
    const jaguar::SPInstance& instance,
    const jaguar::SPSolution& baseSolution,
    double width,
    const sparrow::SeparatorConfig& separatorConfig,
    std::uint64_t seed,
    sparrow::OptimizationTerminator& terminator)
{
    auto problemResult = jaguar::SPProblem::Create(instance);
    if (!problemResult.HasValue())
    {
        return sparrow::Result<sparrow::SeparatorResult>::FromError(
            problemResult.GetError());
    }
    jaguar::SPProblem problem = std::move(problemResult.Value());
    const auto restored = problem.Restore(baseSolution);
    if (!restored.HasValue())
    {
        return sparrow::Result<sparrow::SeparatorResult>::FromError(
            restored.GetError());
    }
    const auto changed = problem.ChangeStripWidth(width);
    if (!changed.HasValue())
    {
        return sparrow::Result<sparrow::SeparatorResult>::FromError(
            changed.GetError());
    }
    auto separator = sparrow::Separator::Create(std::move(problem), seed);
    if (!separator.HasValue())
    {
        return sparrow::Result<sparrow::SeparatorResult>::FromError(
            separator.GetError());
    }
    return separator.Value().Run(separatorConfig, terminator);
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
    if (maxIterations == 0U || maxConsecutiveFailures == 0U
        || !std::isfinite(shrinkStep) || shrinkStep <= 0.0
        || shrinkStep >= 1.0)
    {
        return Result<void>::FromError(
            {jaguar::ErrorCode::InvalidInput,
             "exploration limits or shrinkStep are invalid"});
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
    jaguar::SPSolution bestSolution = initialSolution;
    LbfMetrics bestMetrics = initialMetrics.Value();
    std::size_t failures = 0U;
    for (std::size_t iteration = 0U;
         iteration < config.maxIterations
             && failures < config.maxConsecutiveFailures
             && !terminator.ShouldTerminate(iteration);
         ++iteration)
    {
        const double targetWidth = bestSolution.StripWidth()
            * (1.0 - config.shrinkStep);
        if (!std::isfinite(targetWidth) || targetWidth <= 0.0
            || targetWidth >= bestSolution.StripWidth())
        {
            break;
        }
        const auto candidate = TrySeparate(
            instance,
            bestSolution,
            targetWidth,
            config.separator,
            seed + iteration + 1U,
            terminator);
        if (!candidate.HasValue())
        {
            return Result<ExplorationResult>::FromError(candidate.GetError());
        }
        const auto candidateMetrics = CalculateMetrics(
            instance, candidate.Value().solution, seed);
        if (!candidateMetrics.HasValue())
        {
            return Result<ExplorationResult>::FromError(candidateMetrics.GetError());
        }
        const auto tracker = BuildTracker(candidate.Value().solution);
        if (!tracker.HasValue())
        {
            return Result<ExplorationResult>::FromError(tracker.GetError());
        }
        const OptimizationProgress progress = MakeProgress(
            OptimizationPhase::OPTIMIZATION_PHASE_EXPLORATION,
            iteration,
            candidateMetrics.Value(),
            tracker.Value().TotalLoss());
        if (listener != nullptr)
        {
            listener->OnProgress(progress);
        }
        if (candidateMetrics.Value().feasible
            && candidate.Value().solution.StripWidth() < bestSolution.StripWidth())
        {
            bestSolution = candidate.Value().solution;
            bestMetrics = candidateMetrics.Value();
            failures = 0U;
            if (listener != nullptr)
            {
                listener->OnImprovement(progress);
            }
        }
        else
        {
            ++failures;
        }
    }
    return Result<ExplorationResult>::FromValue({bestSolution, bestMetrics});
}
}
