#pragma once

#include "WorkerLossSelector.h"

#include <cstddef>
#include <optional>
#include <utility>

namespace sparrow::detail
{
template <typename WorkerRound>
class WorkerRoundCollector final
{
public:
    void Add(std::size_t workerIndex, WorkerRound worker)
    {
        m_SurrogateRejectCount += worker.surrogateRejectCount;
        m_ExactCollisionCount += worker.exactCollisionCount;
        m_EvaluationCount += worker.evaluationCount;
        m_MoveCount += worker.moveCount;
        if (m_Selector.Consider(workerIndex, worker.weightedLoss))
        {
            m_BestWorker.emplace(std::move(worker));
        }
    }

    bool HasBest() const noexcept
    {
        return m_BestWorker.has_value();
    }

    std::size_t BestWorkerIndex() const noexcept
    {
        return m_Selector.BestWorkerIndex();
    }

    std::optional<WorkerRound> TakeBest()
    {
        std::optional<WorkerRound> result = std::move(m_BestWorker);
        m_BestWorker.reset();
        return result;
    }

    std::size_t SurrogateRejectCount() const noexcept
    {
        return m_SurrogateRejectCount;
    }

    std::size_t ExactCollisionCount() const noexcept
    {
        return m_ExactCollisionCount;
    }

    std::size_t EvaluationCount() const noexcept
    {
        return m_EvaluationCount;
    }

    std::size_t MoveCount() const noexcept
    {
        return m_MoveCount;
    }

private:
    WorkerLossSelector m_Selector;
    std::optional<WorkerRound> m_BestWorker;
    std::size_t m_SurrogateRejectCount = 0U;
    std::size_t m_ExactCollisionCount = 0U;
    std::size_t m_EvaluationCount = 0U;
    std::size_t m_MoveCount = 0U;
};
}
