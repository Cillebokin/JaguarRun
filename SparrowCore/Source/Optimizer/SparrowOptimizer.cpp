#include "SparrowCore/Optimizer/SparrowOptimizer.h"

#include "SparrowCore/Optimizer/Compression.h"
#include "SparrowCore/Optimizer/Exploration.h"
#include "SparrowCore/Optimizer/SparrowLbfBuilder.h"

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
}

namespace sparrow
{
Result<SparrowOptimizationResult> Optimize(
    const jaguar::SPInstance& instance,
    const SparrowConfig& config,
    OptimizationListener* listener)
{
    const Result<void> configResult = config.Validate();
    if (!configResult.HasValue())
    {
        return Result<SparrowOptimizationResult>::FromError(
            configResult.GetError());
    }
    OptimizationTerminator terminator(config.terminator);
    terminator.Start();

    const auto constructed = ConstructSparrowInitialSolution(
        instance, config.constructor);
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

    const auto explored = Explore(
        instance,
        constructed.Value().solution,
        config.exploration,
        config.seed,
        terminator,
        listener);
    if (!explored.HasValue())
    {
        return Result<SparrowOptimizationResult>::FromError(
            explored.GetError());
    }
    const auto compressed = Compress(
        instance,
        explored.Value().solution,
        config.compression,
        config.seed,
        terminator,
        listener);
    if (!compressed.HasValue())
    {
        return Result<SparrowOptimizationResult>::FromError(
            compressed.GetError());
    }

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
    return Result<SparrowOptimizationResult>::FromValue(
        {compressed.Value().solution, finalMetrics});
}
}
