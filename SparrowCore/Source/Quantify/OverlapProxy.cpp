#include "SparrowCore/Quantify/OverlapProxy.h"

#include <algorithm>
#include <cmath>

namespace
{
bool IsFiniteRect(const jaguar::Rect2d& rectangle) noexcept
{
    return std::isfinite(rectangle.xMin)
        && std::isfinite(rectangle.yMin)
        && std::isfinite(rectangle.xMax)
        && std::isfinite(rectangle.yMax)
        && rectangle.xMin < rectangle.xMax
        && rectangle.yMin < rectangle.yMax;
}

float CirclePairPenalty(const jaguar::Circle2d& first,
                        const jaguar::Circle2d& second,
                        const jaguar::Transform2d& firstTransform,
                        const jaguar::Transform2d& secondTransform,
                        float epsilon) noexcept
{
    if (!std::isfinite(first.radius) || !std::isfinite(second.radius)
        || first.radius < 0.0 || second.radius < 0.0
        || !first.center.IsFinite() || !second.center.IsFinite()
        || !firstTransform.IsFinite() || !secondTransform.IsFinite()
        || !std::isfinite(epsilon) || epsilon <= 0.0F)
    {
        return 0.0F;
    }

    const jaguar::Point2d firstCenter = firstTransform.Apply(first.center);
    const jaguar::Point2d secondCenter = secondTransform.Apply(second.center);
    if (!firstCenter.IsFinite() || !secondCenter.IsFinite())
    {
        return 0.0F;
    }

    const double distance = std::sqrt(firstCenter.SquaredDistanceTo(secondCenter));
    const double penetration = first.radius + second.radius - distance;
    const double minimumRadius = std::min(first.radius, second.radius);
    if (!std::isfinite(distance) || !std::isfinite(penetration)
        || !std::isfinite(minimumRadius) || minimumRadius <= 0.0)
    {
        return 0.0F;
    }

    const double epsilonValue = static_cast<double>(epsilon);
    const double decayedPenalty = penetration >= epsilonValue
        ? penetration
        : (epsilonValue * epsilonValue
           / (-penetration + 2.0 * epsilonValue));
    const double result = decayedPenalty * minimumRadius * 3.14159265358979323846;
    if (!std::isfinite(result) || result <= 0.0)
    {
        return 0.0F;
    }
    return static_cast<float>(result);
}
}

namespace sparrow
{
float CalculateOverlapProxy(const jaguar::Surrogate& first,
                            const jaguar::Transform2d& firstTransform,
                            const jaguar::Surrogate& second,
                            const jaguar::Transform2d& secondTransform,
                            float epsilon) noexcept
{
    float total = 0.0F;
    for (const jaguar::Circle2d& firstPole : first.Poles())
    {
        for (const jaguar::Circle2d& secondPole : second.Poles())
        {
            total += CirclePairPenalty(
                firstPole,
                secondPole,
                firstTransform,
                secondTransform,
                epsilon);
            if (!std::isfinite(total))
            {
                return 0.0F;
            }
        }
    }
    return std::max(total, 0.0F);
}

float CalculateContainerPenalty(const jaguar::SimplePolygon& shape,
                                const jaguar::Rect2d& containerBounds) noexcept
{
    const jaguar::Rect2d shapeBounds = shape.BoundingBox();
    if (!IsFiniteRect(shapeBounds) || !IsFiniteRect(containerBounds))
    {
        return 0.0F;
    }

    const double left = std::max(0.0, containerBounds.xMin - shapeBounds.xMin);
    const double right = std::max(0.0, shapeBounds.xMax - containerBounds.xMax);
    const double bottom = std::max(0.0, containerBounds.yMin - shapeBounds.yMin);
    const double top = std::max(0.0, shapeBounds.yMax - containerBounds.yMax);
    const double horizontal = left + right;
    const double vertical = bottom + top;
    const double result = horizontal * shapeBounds.Height()
        + vertical * shapeBounds.Width()
        + horizontal * vertical;
    if (!std::isfinite(result) || result <= 0.0)
    {
        return 0.0F;
    }
    return static_cast<float>(result);
}
}
