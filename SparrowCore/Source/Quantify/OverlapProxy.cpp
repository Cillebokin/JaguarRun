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

constexpr float rustPi = 3.14159265358979323846F;

float CirclePairContribution(const jaguar::Circle2d& first,
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

    const jaguar::Point2d firstCenter = firstTransform.ApplyRustF32(first.center);
    const jaguar::Point2d secondCenter = secondTransform.ApplyRustF32(second.center);
    if (!firstCenter.IsFinite() || !secondCenter.IsFinite())
    {
        return 0.0F;
    }

    const float dx = static_cast<float>(firstCenter.x) -
                     static_cast<float>(secondCenter.x);
    const float dy = static_cast<float>(firstCenter.y) -
                     static_cast<float>(secondCenter.y);
    const float distance = std::sqrt(dx * dx + dy * dy);
    const float firstRadius = static_cast<float>(first.radius);
    const float secondRadius = static_cast<float>(second.radius);
    const float penetration = firstRadius + secondRadius - distance;
    const float minimumRadius = std::min(firstRadius, secondRadius);
    if (!std::isfinite(distance) || !std::isfinite(penetration)
        || !std::isfinite(minimumRadius) || minimumRadius <= 0.0F)
    {
        return 0.0F;
    }

    const float decayedPenalty = penetration >= epsilon
        ? penetration
        : (epsilon * epsilon / (-penetration + 2.0F * epsilon));
    const float result = decayedPenalty * minimumRadius;
    if (!std::isfinite(result) || result <= 0.0F)
    {
        return 0.0F;
    }
    return result;
}

float ShapePenalty(const jaguar::SimplePolygon& first,
                   const jaguar::SimplePolygon& second) noexcept
{
    const jaguar::Surrogate* firstSurrogate = first.GetSurrogate();
    const jaguar::Surrogate* secondSurrogate = second.GetSurrogate();
    if (firstSurrogate == nullptr || secondSurrogate == nullptr)
    {
        return 0.0F;
    }
    const float firstArea = static_cast<float>(firstSurrogate->ConvexHullArea());
    const float secondArea = static_cast<float>(secondSurrogate->ConvexHullArea());
    if (!std::isfinite(firstArea) || !std::isfinite(secondArea)
        || firstArea <= 0.0F || secondArea <= 0.0F)
    {
        return 0.0F;
    }
    const float result = std::sqrt(std::sqrt(firstArea)
                                   * std::sqrt(secondArea));
    return std::isfinite(result) && result > 0.0F ? result : 0.0F;
}

float RectCentroidDistance(const jaguar::Rect2d& first,
                           const jaguar::Rect2d& second) noexcept
{
    const float firstX = (static_cast<float>(first.xMin)
                          + static_cast<float>(first.xMax)) * 0.5F;
    const float firstY = (static_cast<float>(first.yMin)
                          + static_cast<float>(first.yMax)) * 0.5F;
    const float secondX = (static_cast<float>(second.xMin)
                           + static_cast<float>(second.xMax)) * 0.5F;
    const float secondY = (static_cast<float>(second.yMin)
                           + static_cast<float>(second.yMax)) * 0.5F;
    const float dx = firstX - secondX;
    const float dy = firstY - secondY;
    return std::sqrt(dx * dx + dy * dy);
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
            total += CirclePairContribution(
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
    total *= rustPi;
    if (!std::isfinite(total))
        return 0.0F;
    return std::max(total, 0.0F);
}

float CalculatePolygonCollisionLoss(const jaguar::SimplePolygon& first,
                                    const jaguar::SimplePolygon& second) noexcept
{
    const jaguar::Surrogate* firstSurrogate = first.GetSurrogate();
    const jaguar::Surrogate* secondSurrogate = second.GetSurrogate();
    if (firstSurrogate == nullptr || secondSurrogate == nullptr)
    {
        return 0.0F;
    }

    const float firstDiameter = static_cast<float>(first.Diameter());
    const float secondDiameter = static_cast<float>(second.Diameter());
    const float diameter = std::max(firstDiameter, secondDiameter);
    if (!std::isfinite(diameter) || diameter <= 0.0F)
    {
        return 0.0F;
    }

    constexpr float epsilonDiameterRatio = 0.01F;
    const float epsilon = static_cast<float>(diameter)
        * epsilonDiameterRatio;
    if (!std::isfinite(epsilon) || epsilon <= 0.0F)
    {
        return 0.0F;
    }

    const float rawProxy = CalculateOverlapProxy(
        *firstSurrogate,
        jaguar::Transform2d::Identity(),
        *secondSurrogate,
        jaguar::Transform2d::Identity(),
        epsilon);
    const float overlapProxy = rawProxy + epsilon * epsilon;
    const float shapePenalty = ShapePenalty(first, second);
    if (!std::isfinite(overlapProxy) || overlapProxy <= 0.0F
        || shapePenalty <= 0.0F)
    {
        return 0.0F;
    }

    const float result = std::sqrt(overlapProxy) * shapePenalty;
    if (!std::isfinite(result) || result <= 0.0F)
    {
        return 0.0F;
    }
    return result;
}

float CalculateContainerPenalty(const jaguar::SimplePolygon& shape,
                                const jaguar::Rect2d& containerBounds) noexcept
{
    const jaguar::Rect2d shapeBounds = shape.BoundingBox();
    if (!IsFiniteRect(shapeBounds) || !IsFiniteRect(containerBounds))
    {
        return 0.0F;
    }

    const float shapeXMin = static_cast<float>(shapeBounds.xMin);
    const float shapeYMin = static_cast<float>(shapeBounds.yMin);
    const float shapeXMax = static_cast<float>(shapeBounds.xMax);
    const float shapeYMax = static_cast<float>(shapeBounds.yMax);
    const float shapeWidth = shapeXMax - shapeXMin;
    const float shapeHeight = shapeYMax - shapeYMin;
    const float shapeArea = shapeWidth * shapeHeight;
    if (!std::isfinite(shapeArea) || shapeArea <= 0.0F)
    {
        return 0.0F;
    }

    const float containerXMin = static_cast<float>(containerBounds.xMin);
    const float containerYMin = static_cast<float>(containerBounds.yMin);
    const float containerXMax = static_cast<float>(containerBounds.xMax);
    const float containerYMax = static_cast<float>(containerBounds.yMax);
    const float intersectionXMin = std::max(shapeXMin, containerXMin);
    const float intersectionYMin = std::max(shapeYMin, containerYMin);
    const float intersectionXMax = std::min(shapeXMax, containerXMax);
    const float intersectionYMax = std::min(shapeYMax, containerYMax);

    float overlap = 0.0F;
    if (intersectionXMin < intersectionXMax
        && intersectionYMin < intersectionYMax)
    {
        const float intersectionArea =
            (intersectionXMax - intersectionXMin)
            * (intersectionYMax - intersectionYMin);
        overlap = (shapeArea - intersectionArea)
            + 0.0001F * shapeArea;
    }
    else
    {
        overlap = shapeArea
            + RectCentroidDistance(shapeBounds, containerBounds);
    }

    const float shapePenalty = ShapePenalty(shape, shape);
    if (!std::isfinite(overlap) || overlap <= 0.0F
        || !std::isfinite(shapePenalty) || shapePenalty <= 0.0F)
    {
        return 0.0F;
    }

    const float result = 2.0F * std::sqrt(overlap) * shapePenalty;
    if (!std::isfinite(result) || result <= 0.0F)
    {
        return 0.0F;
    }
    return result;
}
}
