#include "SparrowCore/Optimizer/LbfLoss.h"

#include <cmath>

namespace
{
constexpr float X_MULTIPLIER = 10.0F;

float RustF32Min(float left, float right) noexcept
{
    // Rust f32::min ignores a single NaN, keeps the left operand when both
    // are NaN, and keeps the right operand for equal numeric values.
    if (std::isnan(left))
    {
        return std::isnan(right) ? left : right;
    }
    if (std::isnan(right))
    {
        return left;
    }
    return left < right ? left : right;
}
}

namespace sparrow
{
LbfLoss LbfLoss::FromBoundingBox(const jaguar::Rect2d& bbox) noexcept
{
    return {static_cast<float>(bbox.xMax), static_cast<float>(bbox.yMax)};
}

float LbfLoss::Cost() const noexcept
{
    return xMax * X_MULTIPLIER + yMax;
}

jaguar::Rect2d LbfLoss::TightenSampleBBox(const jaguar::Rect2d& sample) const noexcept
{
    const float xMaxBound = Cost() / X_MULTIPLIER;
    const float tightenedXMax = RustF32Min(static_cast<float>(sample.xMax), xMaxBound);
    return {sample.xMin,
            sample.yMin,
            static_cast<double>(tightenedXMax),
            sample.yMax};
}

bool operator<(const LbfLoss& left, const LbfLoss& right) noexcept
{
    const float leftCost = left.Cost();
    const float rightCost = right.Cost();
    return leftCost < rightCost;
}
}
