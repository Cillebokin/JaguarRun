#include "SparrowCore/Optimizer/Compression.h"

#include "SparrowCore/Optimizer/Separator.h"

#include "JaguarCore/Assertions.h"

#include <algorithm>
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
Result<void> CompressionConfig::Validate() const
{
    if (maxIterations == 0U || maxConsecutiveFailures == 0U
        || !std::isfinite(shrinkMin) || !std::isfinite(shrinkMax)
        || shrinkMin <= 0.0 || shrinkMax < shrinkMin || shrinkMax >= 1.0
        || !std::isfinite(failureDecay) || failureDecay <= 0.0
        || failureDecay >= 1.0)
    {
        return Result<void>::FromError(
            {jaguar::ErrorCode::InvalidInput,
             "compression limits or shrink range are invalid"});
    }
    return separator.Validate();
}

Result<CompressionResult> Compress(
    const jaguar::SPInstance& instance,
    const jaguar::SPSolution& initialSolution,
    const CompressionConfig& config,
    std::uint64_t seed,
    OptimizationTerminator& terminator,
    OptimizationListener* listener)
{
    const Result<void> configResult = config.Validate();
    if (!configResult.HasValue())
    {
        return Result<CompressionResult>::FromError(configResult.GetError());
    }
    const auto initialMetrics = CalculateMetrics(instance, initialSolution, seed);
    if (!initialMetrics.HasValue())
    {
        return Result<CompressionResult>::FromError(initialMetrics.GetError());
    }
    jaguar::SPSolution bestSolution = initialSolution;
    LbfMetrics bestMetrics = initialMetrics.Value();
    std::size_t failures = 0U;
    double currentStep = config.shrinkMax;
    for (std::size_t iteration = 0U;
         iteration < config.maxIterations
             && failures < config.maxConsecutiveFailures
             && !terminator.ShouldTerminate(iteration);
         ++iteration)
    {
        if (config.decayStrategy == ShrinkDecayStrategy::SHRINK_DECAY_TIME_BASED)
        {
            const double progress = config.maxIterations <= 1U
                ? 1.0
                : static_cast<double>(iteration)
                    / static_cast<double>(config.maxIterations - 1U);
            currentStep = config.shrinkMax
                + (config.shrinkMin - config.shrinkMax) * progress;
        }
        currentStep = std::clamp(currentStep, config.shrinkMin, config.shrinkMax);
        const double targetWidth = bestSolution.StripWidth()
            * (1.0 - currentStep);
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
            return Result<CompressionResult>::FromError(candidate.GetError());
        }
        const auto candidateMetrics = CalculateMetrics(
            instance, candidate.Value().solution, seed);
        if (!candidateMetrics.HasValue())
        {
            return Result<CompressionResult>::FromError(candidateMetrics.GetError());
        }
        const auto tracker = BuildTracker(candidate.Value().solution);
        if (!tracker.HasValue())
        {
            return Result<CompressionResult>::FromError(tracker.GetError());
        }
        const OptimizationProgress progress = MakeProgress(
            OptimizationPhase::OPTIMIZATION_PHASE_COMPRESSION,
            iteration,
            candidateMetrics.Value(),
            tracker.Value().TotalLoss());
        if (listener != nullptr)
        {
            listener->OnProgress(progress);
        }
        if (candidateMetrics.Value().feasible
            && tracker.Value().IsSeparated()
            && candidate.Value().solution.StripWidth() < bestSolution.StripWidth())
        {
            bestSolution = candidate.Value().solution;
            bestMetrics = candidateMetrics.Value();
            failures = 0U;
            if (config.decayStrategy
                == ShrinkDecayStrategy::SHRINK_DECAY_FAILURE_BASED)
            {
                currentStep = std::min(
                    config.shrinkMax,
                    currentStep / config.failureDecay);
            }
            if (listener != nullptr)
            {
                listener->OnImprovement(progress);
            }
        }
        else
        {
            ++failures;
            if (config.decayStrategy
                == ShrinkDecayStrategy::SHRINK_DECAY_FAILURE_BASED)
            {
                currentStep = std::max(
                    config.shrinkMin,
                    currentStep * config.failureDecay);
            }
        }
    }
    return Result<CompressionResult>::FromValue({bestSolution, bestMetrics});
}
}
