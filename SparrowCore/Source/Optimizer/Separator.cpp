#include "SparrowCore/Optimizer/Separator.h"

#include <algorithm>
#include <utility>
#include <vector>

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

bool SameTransform(const jaguar::Transform2d& left,
                   const jaguar::Transform2d& right) noexcept
{
    return left.rotationRad == right.rotationRad
        && left.translation == right.translation;
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

jaguar::SPSolution Separator::SaveSolution() const
{
    return m_Problem.Save();
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
                                        OptimizationTerminator& terminator)
{
    const Result<void> configResult = config.Validate();
    if (!configResult.HasValue())
    {
        return Result<SeparatorResult>::FromError(configResult.GetError());
    }
    jaguar::SPSolution bestSolution = m_Problem.Save();
    float bestLoss = m_Tracker.TotalWeightedLoss();
    std::size_t noImprovement = 0U;
    std::size_t strikes = 0U;

    for (std::size_t iteration = 0U;
         !terminator.ShouldTerminate(iteration)
             && !m_Tracker.IsSeparated()
             && strikes < config.strikeLimit
             && noImprovement < config.noImprovementLimit;
         ++iteration)
    {
        const jaguar::SPSolution currentSolution = m_Problem.Save();
        jaguar::Layout layout(currentSolution.layoutSnapshot.container);
        const auto restored = layout.Restore(currentSolution.layoutSnapshot);
        if (!restored.HasValue())
        {
            return Result<SeparatorResult>::FromError(restored.GetError());
        }

        std::vector<std::size_t> candidates;
        candidates.reserve(m_Tracker.ItemCount());
        for (std::size_t index = 0U; index < m_Tracker.ItemCount(); ++index)
        {
            if (m_Tracker.WeightedItemLoss(index) > 0.0F)
            {
                candidates.push_back(index);
            }
        }
        std::sort(candidates.begin(), candidates.end(),
                  [this](std::size_t left, std::size_t right) {
                      return m_Tracker.WeightedItemLoss(left)
                          > m_Tracker.WeightedItemLoss(right);
                  });

        bool improved = false;
        for (const std::size_t index : candidates)
        {
            const jaguar::LayoutSnapshot snapshot = layout.Save();
            if (index >= snapshot.placements.size())
            {
                continue;
            }
            const auto move = FindBestSeparatorMove(
                layout,
                m_Tracker,
                snapshot.placements[index].handle,
                config,
                m_Rng);
            if (!move.HasValue() || !move.Value().transform.has_value())
            {
                continue;
            }
            const jaguar::Transform2d candidate = *move.Value().transform;
            if (SameTransform(candidate, snapshot.placements[index].placedItem.Transform())
                && move.Value().evaluation.kind
                       != SampleKind::SAMPLE_KIND_CLEAR)
            {
                continue;
            }
            const auto removed = m_Problem.RemoveItem(snapshot.placements[index].handle);
            if (!removed.HasValue())
            {
                continue;
            }
            const auto placed = m_Problem.PlaceItem({removed.Value().itemIndex, candidate});
            if (!placed.HasValue())
            {
                const auto rollback = m_Problem.Restore(currentSolution);
                if (!rollback.HasValue())
                {
                    return Result<SeparatorResult>::FromError(rollback.GetError());
                }
                const auto rollbackTracker = RebuildTracker();
                if (!rollbackTracker.HasValue())
                {
                    return Result<SeparatorResult>::FromError(
                        rollbackTracker.GetError());
                }
                continue;
            }
            const auto rebuilt = RebuildTracker();
            if (!rebuilt.HasValue())
            {
                return Result<SeparatorResult>::FromError(rebuilt.GetError());
            }
            const float candidateLoss = m_Tracker.TotalWeightedLoss();
            if (candidateLoss < bestLoss)
            {
                bestLoss = candidateLoss;
                bestSolution = m_Problem.Save();
                improved = true;
                break;
            }

            const auto rollback = m_Problem.Restore(currentSolution);
            if (!rollback.HasValue())
            {
                return Result<SeparatorResult>::FromError(rollback.GetError());
            }
            const auto rollbackTracker = RebuildTracker();
            if (!rollbackTracker.HasValue())
            {
                return Result<SeparatorResult>::FromError(rollbackTracker.GetError());
            }
        }

        if (improved)
        {
            noImprovement = 0U;
            strikes = 0U;
        }
        else
        {
            ++noImprovement;
            ++strikes;
            m_Tracker.UpdateWeights();
        }
    }

    const auto restored = m_Problem.Restore(bestSolution);
    if (!restored.HasValue())
    {
        return Result<SeparatorResult>::FromError(restored.GetError());
    }
    const auto rebuilt = RebuildTracker();
    if (!rebuilt.HasValue())
    {
        return Result<SeparatorResult>::FromError(rebuilt.GetError());
    }
    return Result<SeparatorResult>::FromValue(
        {bestSolution, m_Tracker.Save()});
}
}
