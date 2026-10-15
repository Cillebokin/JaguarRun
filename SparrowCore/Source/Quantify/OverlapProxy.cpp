#include "SparrowCore/Quantify/OverlapProxy.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

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
    const jaguar::Point2d firstCenter = firstTransform.ApplyRustF32(first.center);
    const jaguar::Point2d secondCenter = secondTransform.ApplyRustF32(second.center);

    const float dx = static_cast<float>(firstCenter.x) -
                     static_cast<float>(secondCenter.x);
    const float dy = static_cast<float>(firstCenter.y) -
                     static_cast<float>(secondCenter.y);
    const float distance = std::sqrt(dx * dx + dy * dy);
    const float firstRadius = static_cast<float>(first.radius);
    const float secondRadius = static_cast<float>(second.radius);
    const float penetration = firstRadius + secondRadius - distance;
    const float minimumRadius = std::min(firstRadius, secondRadius);

    const float decayedPenalty = penetration >= epsilon
        ? penetration
        : (epsilon * epsilon / (-penetration + 2.0F * epsilon));
    return decayedPenalty * minimumRadius;
}

const jaguar::Surrogate& RequireSurrogate(const jaguar::SimplePolygon& shape)
{
    const jaguar::Surrogate* surrogate = shape.GetSurrogate();
    if (surrogate == nullptr)
    {
        throw std::logic_error("surrogate not generated");
    }

    return *surrogate;
}

float ShapePenalty(const jaguar::SimplePolygon& first,
                   const jaguar::SimplePolygon& second)
{
    const jaguar::Surrogate& firstSurrogate = RequireSurrogate(first);
    const jaguar::Surrogate& secondSurrogate = RequireSurrogate(second);
    const float firstArea = static_cast<float>(firstSurrogate.ConvexHullArea());
    const float secondArea = static_cast<float>(secondSurrogate.ConvexHullArea());
    return std::sqrt(std::sqrt(firstArea) * std::sqrt(secondArea));
}

float RectCentroidDistance(const jaguar::Rect2d& first,
                           const jaguar::Rect2d& second) noexcept
{
    const jaguar::Point2d firstCentroid = first.CentroidRustF32();
    const jaguar::Point2d secondCentroid = second.CentroidRustF32();
    const float firstX = static_cast<float>(firstCentroid.x);
    const float firstY = static_cast<float>(firstCentroid.y);
    const float secondX = static_cast<float>(secondCentroid.x);
    const float secondY = static_cast<float>(secondCentroid.y);
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
                            float epsilon)
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
        }
    }
    total *= rustPi;
#ifndef NDEBUG
    if (!std::isnormal(total))
        throw std::domain_error("assertion failed: total_overlap.is_normal()");
#endif
    return total;
}

float CalculatePolygonCollisionLoss(const jaguar::SimplePolygon& first,
                                    const jaguar::SimplePolygon& second)
{
    const float firstDiameter = static_cast<float>(first.Diameter());
    const float secondDiameter = static_cast<float>(second.Diameter());
    const float diameter = std::max(firstDiameter, secondDiameter);

    constexpr float epsilonDiameterRatio = 0.01F;
    const float epsilon = static_cast<float>(diameter)
        * epsilonDiameterRatio;

    const jaguar::Surrogate& firstSurrogate = RequireSurrogate(first);
    const jaguar::Surrogate& secondSurrogate = RequireSurrogate(second);

    const float rawProxy = CalculateOverlapProxy(
        firstSurrogate,
        jaguar::Transform2d::Identity(),
        secondSurrogate,
        jaguar::Transform2d::Identity(),
        epsilon);
    const float overlapProxy = rawProxy + epsilon * epsilon;
#ifndef NDEBUG
    if (!std::isnormal(overlapProxy))
        throw std::domain_error("assertion failed: overlap_proxy.is_normal()");
#endif

    const float shapePenalty = ShapePenalty(first, second);
    const float result = std::sqrt(overlapProxy) * shapePenalty;
    return result;
}

float CalculateContainerPenalty(const jaguar::SimplePolygon& shape,
                                const jaguar::Rect2d& containerBounds)
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

#ifndef NDEBUG
    if (!std::isnormal(overlap))
        throw std::domain_error("assertion failed: overlap.is_normal()");
#endif

    const float shapePenalty = ShapePenalty(shape, shape);
    return 2.0F * std::sqrt(overlap) * shapePenalty;
}
}
