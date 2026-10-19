#include "SparrowCore/Optimizer/Separator.h"
#include "SparrowCore/Random/SparrowRng.h"
#include "SeparatorRandom.h"
#include "WorkerRoundCollector.h"

#ifndef NDEBUG
#include <cassert>
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <future>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace
{
sparrow::Result<jaguar::Layout> BuildLayout(
    const jaguar::SPSolution& solution)
{
    jaguar::Layout layout(solution.layoutSnapshot.container);
    const auto restored = layout.Restore(solution.layoutSnapshot);
    if (!restored.HasValue())
    {
        return sparrow::Result<jaguar::Layout>::FromError(
            restored.GetError());
    }
    return sparrow::Result<jaguar::Layout>::FromValue(std::move(layout));
}

sparrow::Result<sparrow::CollisionTracker> BuildTracker(
    const jaguar::SPSolution& solution)
{
    auto layout = BuildLayout(solution);
    if (!layout.HasValue())
    {
        return sparrow::Result<sparrow::CollisionTracker>::FromError(
            layout.GetError());
    }
    return sparrow::CollisionTracker::Create(layout.Value());
}

sparrow::Result<void> RestoreTracker(
    sparrow::CollisionTracker& tracker,
    const sparrow::CollisionTrackerSnapshot& snapshot,
    const jaguar::SPSolution& solution)
{
    auto layout = BuildLayout(solution);
    if (!layout.HasValue())
    {
        return sparrow::Result<void>::FromError(layout.GetError());
    }
    return tracker.Restore(snapshot, layout.Value());
}

sparrow::Result<void> RestoreTrackerButKeepWeights(
    sparrow::CollisionTracker& tracker,
    const sparrow::CollisionTrackerSnapshot& snapshot,
    const jaguar::SPSolution& solution)
{
    auto layout = BuildLayout(solution);
    if (!layout.HasValue())
    {
        return sparrow::Result<void>::FromError(layout.GetError());
    }
    return tracker.RestoreButKeepWeights(snapshot, layout.Value());
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

void Shuffle(std::vector<std::size_t>& values,
             sparrow::SparrowRng& rng) noexcept
{
    if (values.size() <= 1U)
    {
        return;
    }

    if (values.size() < std::numeric_limits<std::uint32_t>::max())
    {
        std::uint32_t n = 0U;
        std::uint32_t chunk = 0U;
        std::uint8_t chunkRemaining = 1U;
        for (std::size_t index = 0U; index < values.size(); ++index)
        {
            const std::uint32_t nextN = n + 1U;
            std::uint8_t nextChunkRemaining = 0U;
            if (chunkRemaining != 0U)
            {
                nextChunkRemaining = static_cast<std::uint8_t>(
                    chunkRemaining - 1U);
            }
            else
            {
                std::uint32_t bound = nextN;
                std::uint32_t current = nextN + 1U;
                std::uint8_t remaining = 1U;
                while (current <= std::numeric_limits<std::uint32_t>::max()
                                     / bound)
                {
                    bound *= current;
                    ++current;
                    ++remaining;
                }
                chunk = rng.UniformIndex(bound);
                nextChunkRemaining = static_cast<std::uint8_t>(
                    remaining - 1U);
            }

            std::uint32_t selected = 0U;
            if (nextChunkRemaining == 0U)
            {
                selected = chunk;
            }
            else
            {
                selected = chunk % nextN;
                chunk /= nextN;
            }
            std::swap(values[index], values[selected]);
            chunkRemaining = nextChunkRemaining;
            n = nextN;
        }
        return;
    }

    for (std::size_t index = 0U; index < values.size(); ++index)
    {
        const std::size_t selected = static_cast<std::size_t>(BoundedIndex(
            rng, static_cast<std::uint64_t>(index + 1U)));
        std::swap(values[index], values[selected]);
    }
}

struct WorkerRoundResult
{
    jaguar::SPSolution solution;
    sparrow::CollisionTrackerSnapshot trackerSnapshot;
    float weightedLoss = std::numeric_limits<float>::infinity();
    std::size_t surrogateRejectCount = 0U;
    std::size_t exactCollisionCount = 0U;
    std::size_t evaluationCount = 0U;
    std::size_t moveCount = 0U;
};

struct WorkerTaskResult
{
    std::size_t index = 0U;
    sparrow::SparrowRng rng{0U};
    sparrow::Result<WorkerRoundResult> result =
        sparrow::Result<WorkerRoundResult>::FromError(
        {jaguar::ErrorCode::InvalidInput, "worker task has no result"});
    std::exception_ptr exception;
};

sparrow::Result<WorkerRoundResult> RunWorkerRound(
    const jaguar::SPInstance& instance,
    const jaguar::SPSolution& masterSolution,
    const sparrow::CollisionTrackerSnapshot& masterTrackerSnapshot,
    const sparrow::SeparatorConfig& config,
    sparrow::SparrowRng& rng)
{
    auto problemResult = jaguar::SPProblem::Create(instance);
    if (!problemResult.HasValue())
    {
        return sparrow::Result<WorkerRoundResult>::FromError(
            problemResult.GetError());
    }
    jaguar::SPProblem problem = std::move(problemResult.Value());
    const auto restored = problem.Restore(masterSolution);
    if (!restored.HasValue())
    {
        return sparrow::Result<WorkerRoundResult>::FromError(
            restored.GetError());
    }

    auto layout = BuildLayout(masterSolution);
    if (!layout.HasValue())
    {
        return sparrow::Result<WorkerRoundResult>::FromError(
            layout.GetError());
    }
    auto trackerResult = sparrow::CollisionTracker::Create(layout.Value());
    if (!trackerResult.HasValue())
    {
        return sparrow::Result<WorkerRoundResult>::FromError(
            trackerResult.GetError());
    }
    sparrow::CollisionTracker tracker = std::move(trackerResult.Value());
    const auto trackerRestored = tracker.Restore(
        masterTrackerSnapshot, layout.Value());
    if (!trackerRestored.HasValue())
    {
        return sparrow::Result<WorkerRoundResult>::FromError(
            trackerRestored.GetError());
    }
#ifndef NDEBUG
    tracker.DebugAssertMatchesLayout(layout.Value());
#endif

    std::vector<std::size_t> candidates;
    candidates.reserve(tracker.ItemCount());
    for (std::size_t index = 0U; index < tracker.ItemCount(); ++index)
    {
        if (tracker.ItemLoss(index) > 0.0F)
        {
            candidates.push_back(index);
        }
    }
    Shuffle(candidates, rng);

    std::size_t surrogateRejectCount = 0U;
    std::size_t exactCollisionCount = 0U;
    std::size_t evaluationCount = 0U;
    std::size_t moveCount = 0U;
    for (const std::size_t index : candidates)
    {
        if (index >= tracker.ItemCount() || tracker.ItemLoss(index) <= 0.0F)
        {
            continue;
        }
        const jaguar::LayoutSnapshot snapshot = layout.Value().Save();
        if (index >= snapshot.placements.size())
        {
            continue;
        }
        const jaguar::PlacedItemHandle previousHandle =
            snapshot.placements[index].handle;
        const auto move = sparrow::FindBestSeparatorMove(
            layout.Value(), tracker, previousHandle, config, rng);
        if (!move.HasValue())
        {
            return sparrow::Result<WorkerRoundResult>::FromError(
                move.GetError());
        }
        surrogateRejectCount += move.Value().surrogateRejectCount;
        exactCollisionCount += move.Value().exactCollisionCount;
        evaluationCount += move.Value().evaluationCount;
        if (!move.Value().transform.has_value())
        {
            continue;
        }
#ifndef NDEBUG
        tracker.DebugAssertMatchesLayout(layout.Value());
#endif
        ++moveCount;

        const auto removed = problem.RemoveItem(previousHandle);
        if (!removed.HasValue())
        {
            return sparrow::Result<WorkerRoundResult>::FromError(
                removed.GetError());
        }
        const auto placed = problem.PlaceItem({
            removed.Value().itemIndex,
            *move.Value().transform});
        if (!placed.HasValue())
        {
            return sparrow::Result<WorkerRoundResult>::FromError(
                placed.GetError());
        }

        const jaguar::SPSolution updatedSolution = problem.Save();
        const auto layoutUpdated = layout.Value().Restore(
            updatedSolution.layoutSnapshot);
        if (!layoutUpdated.HasValue())
        {
            return sparrow::Result<WorkerRoundResult>::FromError(
                layoutUpdated.GetError());
        }
        const auto trackerUpdated = tracker.UpdateMovedItem(
            layout.Value(), previousHandle, placed.Value());
        if (!trackerUpdated.HasValue())
        {
            return sparrow::Result<WorkerRoundResult>::FromError(
                trackerUpdated.GetError());
        }
    }

    WorkerRoundResult result{
        problem.Save(),
        tracker.Save(),
        tracker.TotalWeightedLoss(),
        surrogateRejectCount,
        exactCollisionCount,
        evaluationCount,
        moveCount};
    return sparrow::Result<WorkerRoundResult>::FromValue(std::move(result));
}
}

namespace sparrow::detail
{
void ShuffleSliceRandom(std::vector<std::size_t>& values,
                        SparrowRng& rng) noexcept
{
    Shuffle(values, rng);
}
}

namespace sparrow
{
Separator::Separator(jaguar::SPProblem problem,
                     CollisionTracker tracker,
                     std::uint64_t seed)
    : m_Problem(std::move(problem)),
      m_Tracker(std::move(tracker)),
      m_Rng(seed)
{
}

Result<Separator> Separator::Create(jaguar::SPProblem problem,
                                    std::uint64_t seed)
{
    const auto tracker = BuildTracker(problem.Save());
    if (!tracker.HasValue())
    {
        return Result<Separator>::FromError(tracker.GetError());
    }
    return Result<Separator>::FromValue(
        Separator{std::move(problem), std::move(tracker.Value()), seed});
}

Result<void> Separator::RebuildTracker()
{
    const auto tracker = BuildTracker(m_Problem.Save());
    if (!tracker.HasValue())
    {
        return Result<void>::FromError(tracker.GetError());
    }
    m_Tracker = std::move(tracker.Value());
    return Result<void>::FromValue();
}

void Separator::ResetWorkerRngs(std::size_t workerCount)
{
    m_WorkerRngs.clear();
    m_WorkerRngs.reserve(workerCount);
    for (std::size_t index = 0U; index < workerCount; ++index)
    {
        m_WorkerRngs.emplace_back(m_Rng.NextU64());
    }
}

Result<void> Separator::ChangeStripWidth(
    double newWidth,
    std::optional<double> splitPosition)
{
    const jaguar::SPSolution previous = m_Problem.Save();
    const double oldWidth = previous.StripWidth();
    const double split = splitPosition.value_or(oldWidth * 0.5);
    const double delta = newWidth - oldWidth;
    if (!std::isfinite(newWidth) || !std::isfinite(split)
        || !std::isfinite(delta))
    {
        return Result<void>::FromError(
            {jaguar::ErrorCode::InvalidInput,
             "separator strip width or split position is invalid"});
    }

    for (const jaguar::LayoutPlacementSnapshot& placement
         : previous.layoutSnapshot.placements)
    {
        if (placement.placedItem.CollisionShape().Centroid().x <= split)
        {
            continue;
        }
        jaguar::Transform2d shifted = placement.placedItem.Transform();
        shifted.translation.x += delta;
        const auto moved = MoveItem(placement.handle, shifted);
        if (!moved.HasValue())
        {
            const auto restored = m_Problem.Restore(previous);
            if (restored.HasValue())
            {
                RebuildTracker();
            }
            return Result<void>::FromError(moved.GetError());
        }
    }

    const auto changed = m_Problem.ChangeStripWidth(newWidth);
    if (!changed.HasValue())
    {
        const auto restored = m_Problem.Restore(previous);
        if (restored.HasValue())
        {
            RebuildTracker();
        }
        return changed;
    }
    const auto rebuilt = RebuildTracker();
    if (!rebuilt.HasValue())
    {
        return rebuilt;
    }
    ResetWorkerRngs(m_WorkerRngs.size());
    return Result<void>::FromValue();
}

Result<void> Separator::Rollback(const jaguar::SPSolution& solution)
{
    const auto restored = m_Problem.Restore(solution);
    if (!restored.HasValue())
    {
        return restored;
    }
    return RebuildTracker();
}

Result<jaguar::PlacedItemHandle> Separator::MoveItem(
    jaguar::PlacedItemHandle handle,
    const jaguar::Transform2d& transform)
{
    if (!transform.IsFinite())
    {
        return Result<jaguar::PlacedItemHandle>::FromError(
            {jaguar::ErrorCode::InvalidInput,
             "separator move transform is invalid"});
    }
    const jaguar::SPSolution previous = m_Problem.Save();
#ifndef NDEBUG
    const auto layoutBeforeMove = BuildLayout(previous);
    assert(layoutBeforeMove.HasValue());
    if (layoutBeforeMove.HasValue())
    {
        m_Tracker.DebugAssertMatchesLayout(layoutBeforeMove.Value());
    }
#endif
    const auto removed = m_Problem.RemoveItem(handle);
    if (!removed.HasValue())
    {
        return Result<jaguar::PlacedItemHandle>::FromError(
            removed.GetError());
    }
    const auto placed = m_Problem.PlaceItem({removed.Value().itemIndex, transform});
    if (!placed.HasValue())
    {
        m_Problem.Restore(previous);
        return Result<jaguar::PlacedItemHandle>::FromError(
            placed.GetError());
    }

    const jaguar::SPSolution updated = m_Problem.Save();
    auto layout = BuildLayout(updated);
    if (!layout.HasValue())
    {
        m_Problem.Restore(previous);
        RebuildTracker();
        return Result<jaguar::PlacedItemHandle>::FromError(
            layout.GetError());
    }
    const auto updatedTracker = m_Tracker.UpdateMovedItem(
        layout.Value(), handle, placed.Value());
    if (!updatedTracker.HasValue())
    {
        m_Problem.Restore(previous);
        RebuildTracker();
        return Result<jaguar::PlacedItemHandle>::FromError(
            updatedTracker.GetError());
    }
    return placed;
}

jaguar::SPSolution Separator::SaveSolution() const
{
    return m_Problem.Save();
}

SparrowRng& Separator::Random() noexcept
{
    return m_Rng;
}

const CollisionTracker& Separator::Tracker() const noexcept
{
    return m_Tracker;
}

const jaguar::SPProblem& Separator::Problem() const noexcept
{
    return m_Problem;
}

Result<SeparatorResult> Separator::Run(const SeparatorConfig& config,
                                        OptimizationTerminator& terminator,
                                        OptimizationListener* listener,
                                        OptimizationHistory* history,
                                        OptimizationPhase phase)
{
    const Result<void> configResult = config.Validate();
    if (!configResult.HasValue())
    {
        return Result<SeparatorResult>::FromError(configResult.GetError());
    }

    if (m_WorkerRngs.size() != config.nWorkers)
    {
        ResetWorkerRngs(config.nWorkers);
    }

    jaguar::SPSolution bestSolution = m_Problem.Save();
    CollisionTrackerSnapshot bestTracker = m_Tracker.Save();
    float minLoss = m_Tracker.TotalLoss();
    std::size_t surrogateRejectCount = 0U;
    std::size_t exactCollisionCount = 0U;
    std::size_t totalEvaluationCount = 0U;
    std::size_t totalMoveCount = 0U;
    std::size_t strikes = 0U;
    std::size_t iteration = 0U;
    const auto start = std::chrono::steady_clock::now();

    const auto reportProgress = [&](std::size_t reportIteration,
                                    float minimumLoss) {
        if (listener == nullptr)
            return;
        listener->OnSeparationProgress(
            {m_Problem.StripWidth(),
             m_Problem.Density() * 100.0,
             reportIteration,
             minimumLoss});
    };

    reportProgress(0U, minLoss);

    while (!terminator.ShouldTerminate(iteration)
           && strikes < config.strikeLimit)
    {
        const float initialStrikeLoss = m_Tracker.TotalLoss();
        std::size_t noImprovement = 0U;

        while (!terminator.ShouldTerminate(iteration)
               && noImprovement < config.noImprovementLimit)
        {
            const jaguar::SPSolution masterSolution = m_Problem.Save();
            const CollisionTrackerSnapshot masterTracker = m_Tracker.Save();
            detail::WorkerRoundCollector<WorkerRoundResult> workerCollector;

            const jaguar::SPInstance& workerInstance = m_Problem.Instance();
            std::vector<std::future<WorkerTaskResult>> workerTasks;
            workerTasks.reserve(m_WorkerRngs.size());
            for (std::size_t workerIndex = 0U;
                 workerIndex < m_WorkerRngs.size();
                 ++workerIndex)
            {
                const SparrowRng initialWorkerRng =
                    m_WorkerRngs[workerIndex];
                workerTasks.emplace_back(std::async(
                    std::launch::async,
                    [&workerInstance,
                     &masterSolution,
                     &masterTracker,
                     &config,
                     workerIndex,
                     workerRng = initialWorkerRng]() mutable {
                        WorkerTaskResult task;
                        task.index = workerIndex;
                        try
                        {
                            task.result = RunWorkerRound(
                                workerInstance,
                                masterSolution,
                                masterTracker,
                                config,
                                workerRng);
                        }
                        catch (...)
                        {
                            task.exception = std::current_exception();
                        }
                        task.rng = workerRng;
                        return task;
                    }));
            }

            std::exception_ptr workerException;
            std::optional<jaguar::Error> workerError;
            for (std::future<WorkerTaskResult>& workerTask : workerTasks)
            {
                WorkerTaskResult task = workerTask.get();
                m_WorkerRngs[task.index] = task.rng;
                if (task.exception != nullptr)
                {
                    if (workerException == nullptr)
                        workerException = task.exception;
                    continue;
                }
                const auto& workerResult = task.result;
                if (!workerResult.HasValue())
                {
                    if (!workerError.has_value())
                        workerError = workerResult.GetError();
                    continue;
                }
                WorkerRoundResult worker = std::move(task.result.Value());
                workerCollector.Add(task.index, std::move(worker));
            }

            if (workerException != nullptr)
                std::rethrow_exception(workerException);
            if (workerError.has_value())
            {
                return Result<SeparatorResult>::FromError(*workerError);
            }

            if (!workerCollector.HasBest())
            {
                return Result<SeparatorResult>::FromError(
                    {jaguar::ErrorCode::InvalidInput,
                     "separator produced no worker result"});
            }

            surrogateRejectCount += workerCollector.SurrogateRejectCount();
            exactCollisionCount += workerCollector.ExactCollisionCount();
            totalEvaluationCount += workerCollector.EvaluationCount();
            totalMoveCount += workerCollector.MoveCount();
            std::optional<WorkerRoundResult> bestWorker =
                workerCollector.TakeBest();

            const auto restored = m_Problem.Restore(bestWorker->solution);
            if (!restored.HasValue())
            {
                return Result<SeparatorResult>::FromError(
                    restored.GetError());
            }
            const auto trackerRestored = RestoreTracker(
                m_Tracker,
                bestWorker->trackerSnapshot,
                bestWorker->solution);
            if (!trackerRestored.HasValue())
            {
                return Result<SeparatorResult>::FromError(
                    trackerRestored.GetError());
            }

            const float loss = m_Tracker.TotalLoss();
            if (loss == 0.0F)
            {
                bestSolution = m_Problem.Save();
                bestTracker = m_Tracker.Save();
                reportProgress(iteration + 1U, loss);
                break;
            }
            if (loss < minLoss)
            {
                if (loss < minLoss * 0.98F)
                {
                    noImprovement = 0U;
                }
                bestSolution = m_Problem.Save();
                bestTracker = m_Tracker.Save();
                minLoss = loss;
                if (listener != nullptr || history != nullptr)
                {
                    const auto report = MakeSolutionReport(
                        OptimizationReportType::OPTIMIZATION_REPORT_EXPLORATION_IMPROVING,
                        phase,
                        iteration,
                        bestSolution,
                        loss,
                        false);
                    if (history != nullptr)
                        PublishSolutionReport(*history, listener, report);
                    else
                        listener->OnSolution(report);
                }
            }
            else
            {
                ++noImprovement;
            }

            m_Tracker.UpdateWeights();
            reportProgress(iteration + 1U, minLoss);
            ++iteration;
        }

        if (m_Tracker.IsSeparated())
        {
            break;
        }
        if (initialStrikeLoss * 0.98F <= minLoss)
        {
            ++strikes;
        }
        else
        {
            strikes = 0U;
        }

        const auto restored = m_Problem.Restore(bestSolution);
        if (!restored.HasValue())
        {
            return Result<SeparatorResult>::FromError(restored.GetError());
        }
        const auto trackerRestored = RestoreTrackerButKeepWeights(
            m_Tracker, bestTracker, bestSolution);
        if (!trackerRestored.HasValue())
        {
            return Result<SeparatorResult>::FromError(
                trackerRestored.GetError());
        }
    }

    const auto restored = m_Problem.Restore(bestSolution);
    if (!restored.HasValue())
    {
        return Result<SeparatorResult>::FromError(restored.GetError());
    }
    const auto trackerRestored = RestoreTrackerButKeepWeights(
        m_Tracker, bestTracker, bestSolution);
    if (!trackerRestored.HasValue())
    {
        return Result<SeparatorResult>::FromError(
            trackerRestored.GetError());
    }
    const float elapsedSeconds = std::chrono::duration<float>(
        std::chrono::steady_clock::now() - start).count();
    if (listener != nullptr)
    {
        listener->OnSeparationResult(
            {m_Tracker.IsSeparated(),
             elapsedSeconds,
             totalEvaluationCount,
             totalMoveCount,
             iteration});
    }
    return Result<SeparatorResult>::FromValue(
        {bestSolution,
         m_Tracker.Save(),
         elapsedSeconds,
         surrogateRejectCount,
         exactCollisionCount,
         totalEvaluationCount,
         totalMoveCount,
         iteration});
}
}
