#include "SparrowCore/Optimizer/SparrowOptimizer.h"

#include "SparrowCore/Optimizer/Compression.h"
#include "SparrowCore/Optimizer/Exploration.h"
#include "SparrowCore/Optimizer/SparrowLbfBuilder.h"
#include "SparrowCore/Random/SparrowRng.h"

#include "JaguarCore/Assertions.h"

#include <utility>

namespace
{
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

sparrow::Result<sparrow::SppLbfResult> MakeWarmStart(
    const jaguar::SPInstance& instance,
    const jaguar::SPSolution& solution,
    std::uint64_t seed)
{
    auto problemResult = jaguar::SPProblem::Create(instance);
    if (!problemResult.HasValue())
    {
        return sparrow::Result<sparrow::SppLbfResult>::FromError(
            problemResult.GetError());
    }
    jaguar::SPProblem problem = std::move(problemResult.Value());
    const auto restored = problem.Restore(solution);
    if (!restored.HasValue())
    {
        return sparrow::Result<sparrow::SppLbfResult>::FromError(
            restored.GetError());
    }
    std::size_t remainingDemand = 0U;
    for (std::size_t index = 0U; index < instance.ItemCount(); ++index)
    {
        remainingDemand += problem.RemainingDemand(index);
    }
    if (remainingDemand != 0U)
    {
        return sparrow::Result<sparrow::SppLbfResult>::FromError(
            {jaguar::ErrorCode::Infeasible,
             "warm-start solution does not satisfy item demand"});
    }
    const auto validation = jaguar::ValidateSPSolution(problem, solution);
    if (!validation.HasValue())
    {
        return sparrow::Result<sparrow::SppLbfResult>::FromError(
            validation.GetError());
    }
    jaguar::Layout layout(solution.layoutSnapshot.container);
    const auto layoutRestored = layout.Restore(solution.layoutSnapshot);
    if (!layoutRestored.HasValue())
    {
        return sparrow::Result<sparrow::SppLbfResult>::FromError(
            layoutRestored.GetError());
    }
    if (!layout.IsFeasible())
    {
        return sparrow::Result<sparrow::SppLbfResult>::FromError(
            {jaguar::ErrorCode::Infeasible,
             "warm-start solution is geometrically infeasible"});
    }

    sparrow::LbfMetrics metrics;
    metrics.seed = seed;
    metrics.sampleCount = 0U;
    metrics.placedItemCount = problem.PlacedItemCount();
    metrics.remainingDemand = 0U;
    metrics.stripWidth = solution.StripWidth();
    metrics.density = solution.Density();
    metrics.feasible = true;
    return sparrow::Result<sparrow::SppLbfResult>::FromValue(
        {solution, metrics});
}
}

namespace sparrow
{
namespace
{
Result<SparrowOptimizationResult> OptimizeWithTerminator(
    const jaguar::SPInstance& instance,
    const SparrowConfig& config,
    OptimizationTerminator& terminator,
    OptimizationListener* listener)
{
    const Result<void> configResult = config.Validate();
    if (!configResult.HasValue())
    {
        return Result<SparrowOptimizationResult>::FromError(
            configResult.GetError());
    }
    OptimizationHistory history;
    // sparrow derives independent child streams from one root stream in the
    // order construction, exploration, compression. Keep the public seed as
    // the only root and reproduce that phase order here.
    SparrowRng phaseRng(config.seed);
    SparrowConstructorConfig constructorConfig = config.constructor;
    if (!config.warmStart.has_value())
    {
        constructorConfig.seed = phaseRng.NextU64();
    }
    const auto constructed = config.warmStart.has_value()
        ? MakeWarmStart(instance, *config.warmStart, config.seed)
        : ConstructSparrowInitialSolution(instance, constructorConfig);
    if (!constructed.HasValue())
    {
        return Result<SparrowOptimizationResult>::FromError(
            constructed.GetError());
    }
    if (listener != nullptr)
    {
        const OptimizationProgress progress = MakeProgress(
            OptimizationPhase::OPTIMIZATION_PHASE_CONSTRUCTION,
            0U,
            constructed.Value().metrics,
            0.0F);
        listener->OnProgress(progress);
        listener->OnImprovement(progress);
    }

    const std::uint64_t explorationSeed = phaseRng.NextU64();
    const std::uint64_t compressionSeed = phaseRng.NextU64();

    if (listener != nullptr)
        listener->OnPhase(
            OptimizationPhase::OPTIMIZATION_PHASE_EXPLORATION);
    terminator.StartPhase(config.exploration.timeLimit);
    const auto explored = Explore(
        instance,
        constructed.Value().solution,
        config.exploration,
        explorationSeed,
        terminator,
        listener);
    if (!explored.HasValue())
    {
        return Result<SparrowOptimizationResult>::FromError(
            explored.GetError());
    }
    history.insert(history.end(),
                   explored.Value().history.begin(),
                   explored.Value().history.end());
    if (listener != nullptr)
        listener->OnPhase(
            OptimizationPhase::OPTIMIZATION_PHASE_COMPRESSION);
    terminator.StartPhase(config.compression.timeLimit);
    const auto compressed = Compress(
        instance,
        explored.Value().separatorState,
        explored.Value().solution,
        config.compression,
        compressionSeed,
        terminator,
        listener);
    if (!compressed.HasValue())
    {
        return Result<SparrowOptimizationResult>::FromError(
            compressed.GetError());
    }
    history.insert(history.end(),
                   compressed.Value().history.begin(),
                   compressed.Value().history.end());

    auto validationProblemResult = jaguar::SPProblem::Create(instance);
    if (!validationProblemResult.HasValue())
    {
        return Result<SparrowOptimizationResult>::FromError(
            validationProblemResult.GetError());
    }
    jaguar::SPProblem validationProblem =
        std::move(validationProblemResult.Value());
    const auto restored = validationProblem.Restore(compressed.Value().solution);
    if (!restored.HasValue()
        || !jaguar::ValidateSPSolution(
                validationProblem, compressed.Value().solution)
                .HasValue())
    {
        return Result<SparrowOptimizationResult>::FromError(
            {jaguar::ErrorCode::Infeasible,
             "Sparrow optimizer produced an invalid final solution"});
    }
    jaguar::Layout finalLayout(
        compressed.Value().solution.layoutSnapshot.container);
    const auto finalLayoutRestored = finalLayout.Restore(
        compressed.Value().solution.layoutSnapshot);
    if (!finalLayoutRestored.HasValue() || !finalLayout.IsFeasible())
    {
        return Result<SparrowOptimizationResult>::FromError(
            {jaguar::ErrorCode::Infeasible,
             "Sparrow optimizer produced a geometrically infeasible solution"});
    }

    LbfMetrics finalMetrics = compressed.Value().metrics;
    finalMetrics.seed = config.seed;
    finalMetrics.sampleCount = constructed.Value().metrics.sampleCount;
    if (listener != nullptr)
    {
        listener->OnProgress(MakeProgress(
            OptimizationPhase::OPTIMIZATION_PHASE_COMPLETE,
            0U,
            finalMetrics,
            0.0F));
    }
    PublishSolutionReport(
        history,
        listener,
        MakeSolutionReport(
            OptimizationReportType::OPTIMIZATION_REPORT_FINAL,
            OptimizationPhase::OPTIMIZATION_PHASE_COMPLETE,
            0U,
            compressed.Value().solution,
            0.0F,
            finalMetrics.feasible));
    return Result<SparrowOptimizationResult>::FromValue(
        {compressed.Value().solution, finalMetrics, std::move(history)});
}
}

Result<SparrowOptimizationResult> Optimize(
    const jaguar::SPInstance& instance,
    const SparrowConfig& config,
    OptimizationListener* listener)
{
    OptimizationTerminator terminator(config.terminator);
    terminator.Start();
    return OptimizeWithTerminator(instance, config, terminator, listener);
}

Result<SparrowOptimizationResult> Optimize(
    const jaguar::SPInstance& instance,
    const SparrowConfig& config,
    OptimizationTerminator& terminator,
    OptimizationListener* listener)
{
    return OptimizeWithTerminator(instance, config, terminator, listener);
}
}
