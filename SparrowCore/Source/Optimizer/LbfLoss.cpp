#include "SparrowCore/Optimizer/LbfLoss.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
constexpr float X_MULTIPLIER = 10.0F;
}

namespace sparrow
{
LbfLoss LbfLoss::FromBoundingBox(const jaguar::Rect2d& bbox) noexcept
{
    return {static_cast<float>(bbox.xMax), static_cast<float>(bbox.yMax)};
}

float LbfLoss::Cost() const noexcept
{
    if (!std::isfinite(xMax) || !std::isfinite(yMax))
    {
        return std::numeric_limits<float>::infinity();
    }
    return xMax * X_MULTIPLIER + yMax;
}

jaguar::Rect2d LbfLoss::TightenSampleBBox(const jaguar::Rect2d& sample) const noexcept
{
    const float xMaxBound = Cost() / X_MULTIPLIER;
    if (!std::isfinite(xMaxBound))
    {
        return sample;
    }

    const float tightenedXMax = std::min(static_cast<float>(sample.xMax), xMaxBound);
    return {sample.xMin,
            sample.yMin,
            static_cast<double>(tightenedXMax),
            sample.yMax};
}

bool operator<(const LbfLoss& left, const LbfLoss& right) noexcept
{
    const float leftCost = left.Cost();
    const float rightCost = right.Cost();
    if (std::isnan(leftCost))
    {
        return false;
    }
    if (std::isnan(rightCost))
    {
        return true;
    }
    return leftCost < rightCost;
}
}
