#include "SparrowCore/Optimizer/Compression.h"
#include "SparrowCore/Optimizer/OptimizationObserver.h"

#include "SparrowCore/Optimizer/Separator.h"

#include "JaguarCore/Assertions.h"

#include <algorithm>
#include <chrono>
#include <cmath>
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

float CalculateMinimumStripWidth(
    const jaguar::SPInstance& instance,
    const jaguar::SPSolution& solution) noexcept
{
    constexpr float relativeTolerance = 1.0e-6F;
    const jaguar::Rect2d containerBounds =
        solution.layoutSnapshot.container.CollisionShape().BoundingBox();
    const float containerWidth = static_cast<float>(containerBounds.Width());
    const float containerHeight = static_cast<float>(containerBounds.Height());
    const float widthInset = static_cast<float>(solution.StripWidth())
        - containerWidth;

    double totalArea = 0.0;
    for (std::size_t index = 0U; index < instance.ItemCount(); ++index)
    {
        totalArea += static_cast<double>(static_cast<float>(
                         instance.GetItem(index).CollisionShape().Area()))
            * static_cast<double>(instance.ItemDemand(index));
    }
    float minimumWidth = static_cast<float>(
        totalArea / static_cast<double>(containerHeight))
        * (1.0F - relativeTolerance);
    for (std::size_t index = 0U; index < instance.ItemCount(); ++index)
    {
        const jaguar::Item& item = instance.GetItem(index);
        const auto& rotation = item.AllowedRotation();
        if (rotation.mode == jaguar::RotationMode::Continuous)
        {
            continue;
        }
        std::vector<float> rotations;
        if (rotation.mode == jaguar::RotationMode::None)
        {
            rotations.push_back(0.0F);
        }
        else
        {
            rotations.reserve(rotation.radians.size());
            for (const double angle : rotation.radians)
            {
                rotations.push_back(static_cast<float>(angle));
            }
        }
        const float tolerance = static_cast<float>(
                                   item.CollisionShape().Diameter())
            * relativeTolerance;
        float itemWidth = std::numeric_limits<float>::infinity();
        for (const float angle : rotations)
        {
            if (!std::isfinite(angle))
            {
                continue;
            }
            const auto transformed = item.CollisionShape().TransformedRustF32(
                jaguar::Transform2d::FromRotationTranslation(
                    static_cast<double>(angle), {0.0, 0.0}));
            if (!transformed.HasValue())
            {
                continue;
            }
            const jaguar::Rect2d bounds = transformed.Value().BoundingBox();
            const float boundsHeight = static_cast<float>(bounds.Height());
            const float boundsWidth = static_cast<float>(bounds.Width());
            if (boundsHeight <= containerHeight + tolerance)
            {
                itemWidth = std::min(
                    itemWidth, std::max(0.0F, boundsWidth - tolerance));
            }
        }
        minimumWidth = std::max(minimumWidth, itemWidth);
    }
    return minimumWidth + widthInset;
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

namespace sparrow::detail
{
float CompressionMinimumStripWidthForTrace(
    const jaguar::SPInstance& instance,
    const jaguar::SPSolution& solution) noexcept
{
    return CalculateMinimumStripWidth(instance, solution);
}
}

namespace sparrow
{
Result<void> CompressionConfig::Validate() const
{
    if (!std::isfinite(shrinkMin) || !std::isfinite(shrinkMax)
        || shrinkMin < 0.0 || shrinkMax < shrinkMin || shrinkMax > 1.0
        || !std::isfinite(failureDecay) || failureDecay < 0.0
        || failureDecay > 1.0 || timeLimit.count() < 0)
    {
        return Result<void>::FromError(
            {jaguar::ErrorCode::InvalidInput,
             "compression limits, shrink range, or phase time limit is invalid"});
    }
    return separator.Validate();
}

Result<CompressionResult> Compress(
    const jaguar::SPInstance& instance,
    const jaguar::SPSolution& initialSolution,
    const CompressionConfig& config,
    std::uint64_t seed,
    OptimizationTerminator& terminator,
    OptimizationListener* listener,
    OptimizationObserver* observer)
{
    return Compress(instance,
                    initialSolution,
                    initialSolution,
                    config,
                    seed,
                    terminator,
                    listener,
                    observer);
}

Result<CompressionResult> Compress(
    const jaguar::SPInstance& instance,
    const jaguar::SPSolution& separatorState,
    const jaguar::SPSolution& initialSolution,
    const CompressionConfig& config,
    std::uint64_t seed,
    OptimizationTerminator& terminator,
    OptimizationListener* listener,
    OptimizationObserver* observer)
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

    auto problemResult = jaguar::SPProblem::Create(instance);
    if (!problemResult.HasValue())
    {
        return Result<CompressionResult>::FromError(problemResult.GetError());
    }
    jaguar::SPProblem problem = std::move(problemResult.Value());
    const auto restored = problem.Restore(separatorState);
    if (!restored.HasValue())
    {
        return Result<CompressionResult>::FromError(restored.GetError());
    }
    auto separatorResult = Separator::Create(std::move(problem), seed);
    if (!separatorResult.HasValue())
    {
        return Result<CompressionResult>::FromError(
            separatorResult.GetError());
    }
    Separator separator = std::move(separatorResult.Value());
    // Rust constructs the compression workers before the first split draw.
    separator.ResetWorkerRngs(config.separator.nWorkers);

    jaguar::SPSolution bestSolution = initialSolution;
    LbfMetrics bestMetrics = initialMetrics.Value();
    const float minimumWidth = CalculateMinimumStripWidth(
        instance, separatorState);
    std::size_t failures = 0U;
    std::size_t iteration = 0U;
    const auto start = std::chrono::steady_clock::now();
    std::size_t totalSurrogateRejectCount =
        bestMetrics.surrogateRejectCount;
    std::size_t totalExactCollisionCount = bestMetrics.exactCollisionCount;
    OptimizationHistory history;

    while (!terminator.ShouldTerminate(iteration)
           && (config.maxIterations == 0U
               || iteration < config.maxIterations))
    {
        double step = config.shrinkMax;
        double minimumStep = config.shrinkMin;
        if (config.decayStrategy
            == ShrinkDecayStrategy::SHRINK_DECAY_TIME_BASED)
        {
            if (config.timeLimit.count() == 0)
            {
                // Rust's elapsed / zero time limit produces a non-finite
                // step, so the compression loop exits before reporting an attempt.
                break;
            }
            double ratio = 0.0;
            const auto elapsed = std::chrono::duration_cast<
                std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - start);
            ratio = static_cast<double>(elapsed.count())
                / static_cast<double>(config.timeLimit.count());
            step = config.shrinkMax
                + ratio * (config.shrinkMin - config.shrinkMax);
        }
        else
        {
            // Rust stores shrink_range and FailureBased's factor as f32, so
            // both the step and its minimum comparison must round in f32.
            const float shrinkMax = static_cast<float>(config.shrinkMax);
            const float shrinkMin = static_cast<float>(config.shrinkMin);
            const float failureDecay =
                static_cast<float>(config.failureDecay);
            const float failureFactor = static_cast<float>(std::pow(
                static_cast<double>(failureDecay),
                static_cast<double>(static_cast<int>(failures))));
            step = static_cast<double>(shrinkMax * failureFactor);
            minimumStep = static_cast<double>(shrinkMin);
        }
        if (!std::isfinite(step) || step < minimumStep)
        {
            break;
        }

        const double width = bestSolution.StripWidth();
        const double nextWidth = width * (1.0 - step);
        if (!std::isfinite(nextWidth) || nextWidth <= 0.0
            || nextWidth >= width
            || (minimumWidth > 0.0 && nextWidth < minimumWidth))
        {
            break;
        }
        if (listener != nullptr)
            listener->OnCompressionProgress(
                static_cast<double>(static_cast<float>(step)));
        if (observer != nullptr)
        {
            observer->OnProgress(MakeProgress(
                OptimizationPhase::OPTIMIZATION_PHASE_COMPRESSION,
                iteration,
                bestMetrics,
                0.0F));
        }

        const auto restoredWidth = separator.ChangeStripWidth(width);
        if (!restoredWidth.HasValue())
        {
            return Result<CompressionResult>::FromError(
                restoredWidth.GetError());
        }
        const auto rolledBack = separator.Rollback(bestSolution);
        if (!rolledBack.HasValue())
        {
            return Result<CompressionResult>::FromError(
                rolledBack.GetError());
        }
        const float split = separator.Random().Uniform01F32()
            * static_cast<float>(width);
        const auto changed = separator.ChangeStripWidth(nextWidth, split);
        if (!changed.HasValue())
        {
            return Result<CompressionResult>::FromError(changed.GetError());
        }
        const auto separated = separator.Run(
            config.separator,
            terminator,
            listener,
            &history,
            OptimizationPhase::OPTIMIZATION_PHASE_COMPRESSION);
        if (!separated.HasValue())
        {
            return Result<CompressionResult>::FromError(
                separated.GetError());
        }

        const float loss = separator.Tracker().TotalLoss();
        const auto metricsResult = CalculateMetrics(
            instance, separated.Value().solution, seed);
        if (!metricsResult.HasValue())
        {
            return Result<CompressionResult>::FromError(
                metricsResult.GetError());
        }
        LbfMetrics metrics = metricsResult.Value();
        metrics.surrogateRejectCount = separated.Value().surrogateRejectCount;
        metrics.exactCollisionCount = separated.Value().exactCollisionCount;
        totalSurrogateRejectCount += metrics.surrogateRejectCount;
        totalExactCollisionCount += metrics.exactCollisionCount;
        const OptimizationProgress progress = MakeProgress(
            OptimizationPhase::OPTIMIZATION_PHASE_COMPRESSION,
            iteration,
            metrics,
            loss);
        if (observer != nullptr)
        {
            observer->OnProgress(progress);
        }

        if (loss == 0.0F && metrics.feasible)
        {
            bestSolution = separated.Value().solution;
            bestMetrics = metrics;
            if (observer != nullptr)
            {
                observer->OnImprovement(progress);
            }
            PublishSolutionReport(
                history,
                listener,
                MakeSolutionReport(
                    OptimizationReportType::OPTIMIZATION_REPORT_COMPRESSION_FEASIBLE,
                    OptimizationPhase::OPTIMIZATION_PHASE_COMPRESSION,
                    iteration,
                    bestSolution,
                    loss,
                    true));
        }
        else
        {
            ++failures;
            if (config.maxConsecutiveFailures > 0U
                && failures >= config.maxConsecutiveFailures)
            {
                break;
            }
        }
        ++iteration;
    }

    bestMetrics.seed = seed;
    bestMetrics.surrogateRejectCount = totalSurrogateRejectCount;
    bestMetrics.exactCollisionCount = totalExactCollisionCount;
    return Result<CompressionResult>::FromValue(
        {bestSolution, bestMetrics, std::move(history)});
}
}
