#pragma once

#include <cmath>
#include <cstddef>

namespace sparrow::detail
{
class WorkerLossSelector final
{
public:
    bool Consider(std::size_t workerIndex, float weightedLoss) noexcept
    {
        if (!m_HasBest || IsLess(weightedLoss, m_BestWeightedLoss))
        {
            m_HasBest = true;
            m_BestWorkerIndex = workerIndex;
            m_BestWeightedLoss = weightedLoss;
            return true;
        }
        return false;
    }

    bool HasBest() const noexcept
    {
        return m_HasBest;
    }

    std::size_t BestWorkerIndex() const noexcept
    {
        return m_BestWorkerIndex;
    }

private:
    static bool IsLess(float candidate, float incumbent) noexcept
    {
        if (std::isnan(candidate))
        {
            return false;
        }
        if (std::isnan(incumbent))
        {
            return true;
        }
        return candidate < incumbent;
    }

    bool m_HasBest = false;
    std::size_t m_BestWorkerIndex = 0U;
    float m_BestWeightedLoss = 0.0F;
};
}
