#include "SparrowCore/optimizer/lbf_loss.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
constexpr float kXMultiplier = 10.0F;
}

namespace sparrow
{
LbfLoss LbfLoss::fromBoundingBox(const jaguar::Rect2d& bbox) noexcept
{
    return {static_cast<float>(bbox.xMax), static_cast<float>(bbox.yMax)};
}

float LbfLoss::cost() const noexcept
{
    if (!std::isfinite(xMax) || !std::isfinite(yMax))
    {
        return std::numeric_limits<float>::infinity();
    }
    return xMax * kXMultiplier + yMax;
}

jaguar::Rect2d LbfLoss::tightenSampleBBox(const jaguar::Rect2d& sample) const noexcept
{
    const float xMaxBound = cost() / kXMultiplier;
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
    const float leftCost = left.cost();
    const float rightCost = right.cost();
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
