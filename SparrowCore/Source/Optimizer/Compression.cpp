#include "SparrowCore/Optimizer/Compression.h"

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
        const double tolerance = item.CollisionShape().Diameter()
            * relativeTolerance;
        double itemWidth = std::numeric_limits<double>::infinity();
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
    const double result = minimumWidth
        + solution.StripWidth() - containerBounds.Width();
    return std::isfinite(result) && result > 0.0 ? result : 0.0;
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
    if (!std::isfinite(shrinkMin) || !std::isfinite(shrinkMax)
        || shrinkMin <= 0.0 || shrinkMax < shrinkMin || shrinkMax >= 1.0
        || !std::isfinite(failureDecay) || failureDecay <= 0.0
        || failureDecay >= 1.0 || timeLimit.count() < 0)
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

    auto problemResult = jaguar::SPProblem::Create(instance);
    if (!problemResult.HasValue())
    {
        return Result<CompressionResult>::FromError(problemResult.GetError());
    }
    jaguar::SPProblem problem = std::move(problemResult.Value());
    const auto restored = problem.Restore(initialSolution);
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

    jaguar::SPSolution bestSolution = initialSolution;
    LbfMetrics bestMetrics = initialMetrics.Value();
    const double minimumWidth = CalculateMinimumStripWidth(
        instance, initialSolution);
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
        if (config.decayStrategy
            == ShrinkDecayStrategy::SHRINK_DECAY_TIME_BASED)
        {
            double ratio = 0.0;
            if (config.timeLimit.count() > 0)
            {
                const auto elapsed = std::chrono::duration_cast<
                    std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start);
                ratio = static_cast<double>(elapsed.count())
                    / static_cast<double>(config.timeLimit.count());
            }
            step = config.shrinkMax
                + ratio * (config.shrinkMin - config.shrinkMax);
        }
        else
        {
            step = config.shrinkMax * std::pow(
                config.failureDecay, static_cast<double>(failures));
        }
        if (!std::isfinite(step) || step < config.shrinkMin)
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
            listener->OnCompressionProgress(step);
        if (listener != nullptr)
        {
            listener->OnProgress(MakeProgress(
                OptimizationPhase::OPTIMIZATION_PHASE_COMPRESSION,
                iteration,
                bestMetrics,
                0.0F));
        }

        const auto rolledBack = separator.Rollback(bestSolution);
        if (!rolledBack.HasValue())
        {
            return Result<CompressionResult>::FromError(
                rolledBack.GetError());
        }
        const double split = separator.Random().Uniform01F32() * width;
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
        if (listener != nullptr)
        {
            listener->OnProgress(progress);
        }

        if (loss == 0.0F && metrics.feasible)
        {
            bestSolution = separated.Value().solution;
            bestMetrics = metrics;
            if (listener != nullptr)
            {
                listener->OnImprovement(progress);
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
