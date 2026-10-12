#include "SparrowCore/Optimizer/SparrowOptimizer.h"

#include "SparrowCore/Optimizer/Compression.h"
#include "SparrowCore/Optimizer/Exploration.h"
#include "SparrowCore/Optimizer/OptimizationObserver.h"
#include "SparrowCore/Optimizer/SparrowLbfBuilder.h"
#include "SparrowCore/Random/SparrowRng.h"

#include "JaguarCore/Assertions.h"

#include <algorithm>
#include <random>
#include <stdexcept>
#include <utility>

namespace
{
std::uint64_t GenerateRandomSeed()
{
    std::random_device entropySource;
    std::uniform_int_distribution<std::uint64_t> seedDistribution;
    return seedDistribution(entropySource);
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
    // Rust restores warm starts without requiring complete demand or a
    // collision-free layout; keep those facts in metrics instead of rejecting.
    jaguar::Layout layout(solution.layoutSnapshot.container);
    const auto layoutRestored = layout.Restore(solution.layoutSnapshot);
    if (!layoutRestored.HasValue())
    {
        return sparrow::Result<sparrow::SppLbfResult>::FromError(
            layoutRestored.GetError());
    }
    const auto restoredHazards =
        layout.Save().cdeSnapshot.dynamicHazards;
    for (const jaguar::LayoutPlacementSnapshot& placement :
         solution.layoutSnapshot.placements)
    {
        jaguar::HazardEntity expectedHazard;
        expectedHazard.kind = jaguar::HazardKind::PlacedItem;
        expectedHazard.itemIndex = placement.placedItem.GetItemIndex();
        expectedHazard.placedItemHandle = placement.handle;
        expectedHazard.transform = placement.placedItem.Transform();
        const bool hasOwnHazard = std::any_of(
            restoredHazards.begin(), restoredHazards.end(),
            [&expectedHazard](const jaguar::Hazard& hazard) {
                return hazard.entity == expectedHazard;
            });
        if (!hasOwnHazard)
        {
            // Rust's next warm-start step constructs its collision tracker
            // from the restored CDE and panics when an item has no own hazard.
            throw std::logic_error("HazardEntity not found in collector");
        }
    }
    sparrow::LbfMetrics metrics;
    metrics.seed = seed;
    metrics.sampleCount = 0U;
    metrics.placedItemCount = problem.PlacedItemCount();
    metrics.remainingDemand = remainingDemand;
    metrics.stripWidth = solution.StripWidth();
    metrics.density = solution.Density();
    metrics.feasible = remainingDemand == 0U
        && jaguar::ValidateSPSolution(problem, solution).HasValue()
        && layout.IsFeasible();
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
    OptimizationListener* listener,
    OptimizationObserver* observer)
{
    const Result<void> configResult = config.Validate();
    if (!configResult.HasValue())
    {
        return Result<SparrowOptimizationResult>::FromError(
            configResult.GetError());
    }
    const std::uint64_t effectiveSeed =
        config.seed.has_value() ? *config.seed : GenerateRandomSeed();
    OptimizationHistory history;
    // sparrow derives independent child streams from one root stream in the
    // order construction, exploration, compression. Keep the public seed as
    // the only root and reproduce that phase order here.
    SparrowRng phaseRng(effectiveSeed);
    SparrowConstructorConfig constructorConfig = config.constructor;
    if (!config.warmStart.has_value())
    {
        constructorConfig.seed = phaseRng.NextU64();
    }
    const auto constructed = config.warmStart.has_value()
        ? MakeWarmStart(instance, *config.warmStart, effectiveSeed)
        : ConstructSparrowInitialSolution(instance, constructorConfig);
    if (!constructed.HasValue())
    {
        return Result<SparrowOptimizationResult>::FromError(
            constructed.GetError());
    }
    if (observer != nullptr)
    {
        const OptimizationProgress progress = MakeProgress(
            OptimizationPhase::OPTIMIZATION_PHASE_CONSTRUCTION,
            0U,
            constructed.Value().metrics,
            0.0F);
        observer->OnProgress(progress);
        observer->OnImprovement(progress);
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
        listener,
        observer);
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
        listener,
        observer);
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
    // A stopped Rust run may return its restorable partial/infeasible seed.
    // Validate snapshot structure here and leave solution quality in metrics.
    if (!restored.HasValue())
    {
        return Result<SparrowOptimizationResult>::FromError(
            {jaguar::ErrorCode::Infeasible,
             "Sparrow optimizer produced an invalid final solution"});
    }
    jaguar::Layout finalLayout(
        compressed.Value().solution.layoutSnapshot.container);
    const auto finalLayoutRestored = finalLayout.Restore(
        compressed.Value().solution.layoutSnapshot);
    if (!finalLayoutRestored.HasValue())
    {
        return Result<SparrowOptimizationResult>::FromError(
            {jaguar::ErrorCode::Infeasible,
             "Sparrow optimizer produced a geometrically infeasible solution"});
    }

    LbfMetrics finalMetrics = compressed.Value().metrics;
    finalMetrics.seed = effectiveSeed;
    finalMetrics.sampleCount = constructed.Value().metrics.sampleCount;
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
    OptimizationListener* listener,
    OptimizationObserver* observer)
{
    OptimizationTerminator terminator(config.terminator);
    terminator.Start();
    return OptimizeWithTerminator(
        instance, config, terminator, listener, observer);
}

Result<SparrowOptimizationResult> Optimize(
    const jaguar::SPInstance& instance,
    const SparrowConfig& config,
    OptimizationTerminator& terminator,
    OptimizationListener* listener,
    OptimizationObserver* observer)
{
    return OptimizeWithTerminator(
        instance, config, terminator, listener, observer);
}
}
