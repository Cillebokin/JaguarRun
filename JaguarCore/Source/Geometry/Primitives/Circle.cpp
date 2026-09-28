#include "JaguarCore/Geometry/Circle.h"
#include "JaguarCore/Geometry/Edge.h"
#include "JaguarCore/Geometry/RustF32.h"
#include "JaguarCore/Geometry/Transformation.h"

#include <cmath>

namespace jaguar
{
namespace
{
constexpr float rustPi = 3.14159265358979323846F;
}

JAGUARCORE_API Result<Circle2d> Circle2d::Create(Point2d centerPoint, double radiusValue)
{
    const float centerX = static_cast<float>(centerPoint.x);
    const float centerY = static_cast<float>(centerPoint.y);
    const float radiusF32 = static_cast<float>(radiusValue);
    if (!std::isfinite(centerX) || !std::isfinite(centerY) ||
        !std::isfinite(radiusF32) || radiusF32 < 0.0F)
    {
        return Result<Circle2d>::FromError({ErrorCode::InvalidInput, "invalid circle"});
    }
    return Result<Circle2d>::FromValue(
        Circle2d{Point2d{static_cast<double>(centerX),
                         static_cast<double>(centerY)},
                 static_cast<double>(radiusF32)});
}

JAGUARCORE_API Result<Circle2d> Circle2d::BoundingCircleRustF32(
    const std::vector<Circle2d>& circles)
{
    if (circles.empty())
        return Result<Circle2d>::FromError(
            {ErrorCode::InvalidInput, "no circles provided"});

    Circle2d bounding = circles.front();
    for (std::size_t index = 1U; index < circles.size(); ++index)
    {
        const Circle2d& circle = circles[index];
        const float startX = static_cast<float>(bounding.center.x);
        const float startY = static_cast<float>(bounding.center.y);
        const float endX = static_cast<float>(circle.center.x);
        const float endY = static_cast<float>(circle.center.y);
        const float startRadius = static_cast<float>(bounding.radius);
        const float endRadius = static_cast<float>(circle.radius);
        float dx = endX - startX;
        float dy = endY - startY;
        const float centerDistance = std::sqrt(dx * dx + dy * dy);
        if (startRadius < centerDistance + endRadius)
        {
            float extendedStartX = startX;
            float extendedStartY = startY;
            float extendedEndX = endX;
            float extendedEndY = endY;

            float edgeLength = centerDistance;
            extendedStartX -= dx * (startRadius / edgeLength);
            extendedStartY -= dy * (startRadius / edgeLength);

            dx = extendedEndX - extendedStartX;
            dy = extendedEndY - extendedStartY;
            edgeLength = std::sqrt(dx * dx + dy * dy);
            extendedEndX += dx * (endRadius / edgeLength);
            extendedEndY += dy * (endRadius / edgeLength);

            const float centerX =
                detail::MidpointRustF32(extendedStartX, extendedEndX);
            const float centerY =
                detail::MidpointRustF32(extendedStartY, extendedEndY);
            dx = extendedEndX - extendedStartX;
            dy = extendedEndY - extendedStartY;
            const float radius = std::sqrt(dx * dx + dy * dy) * 0.5F;
            bounding = Circle2d{Point2d{static_cast<double>(centerX),
                                        static_cast<double>(centerY)},
                                static_cast<double>(radius)};
        }
    }
    return Result<Circle2d>::FromValue(std::move(bounding));
}

JAGUARCORE_API double Circle2d::Area() const noexcept
{
    return std::acos(-1.0) * radius * radius;
}

JAGUARCORE_API double Circle2d::AreaRustF32() const noexcept
{
    const float radiusF32 = static_cast<float>(radius);
    return static_cast<double>(radiusF32 * radiusF32 * rustPi);
}

JAGUARCORE_API Rect2d Circle2d::BoundingBox() const noexcept
{
    return Rect2d{center.x - radius, center.y - radius, center.x + radius, center.y + radius};
}

JAGUARCORE_API Rect2d Circle2d::BoundingBoxRustF32() const noexcept
{
    const float centerX = static_cast<float>(center.x);
    const float centerY = static_cast<float>(center.y);
    const float radiusF32 = static_cast<float>(radius);
    return Rect2d{static_cast<double>(centerX - radiusF32),
                  static_cast<double>(centerY - radiusF32),
                  static_cast<double>(centerX + radiusF32),
                  static_cast<double>(centerY + radiusF32)};
}

JAGUARCORE_API double Circle2d::DiameterRustF32() const noexcept
{
    return static_cast<double>(static_cast<float>(radius) * 2.0F);
}

JAGUARCORE_API void Circle2d::TransformRustF32(
    const Transform2d& transform) noexcept
{
    center = transform.ApplyRustF32(center);
}

JAGUARCORE_API bool Circle2d::Contains(const Point2d& point) const noexcept
{
    return center.SquaredDistanceTo(point) <= radius * radius;
}

JAGUARCORE_API bool Circle2d::ContainsRustF32(const Point2d& point) const noexcept
{
    const float dx = static_cast<float>(point.x) - static_cast<float>(center.x);
    const float dy = static_cast<float>(point.y) - static_cast<float>(center.y);
    const float radiusF32 = static_cast<float>(radius);
    return dx * dx + dy * dy <= radiusF32 * radiusF32;
}

JAGUARCORE_API bool Circle2d::IntersectsRustF32(
    const Circle2d& other) const noexcept
{
    const float dx = static_cast<float>(center.x) -
                     static_cast<float>(other.center.x);
    const float dy = static_cast<float>(center.y) -
                     static_cast<float>(other.center.y);
    const float radiusSum = static_cast<float>(radius) +
                            static_cast<float>(other.radius);
    return dx * dx + dy * dy <= radiusSum * radiusSum;
}

JAGUARCORE_API bool Circle2d::IntersectsRustF32(
    const Edge2d& edge) const noexcept
{
    const float squaredDistance = static_cast<float>(
        edge.SquaredDistanceToRustF32(center));
    const float radiusF32 = static_cast<float>(radius);
    return squaredDistance <= radiusF32 * radiusF32;
}

JAGUARCORE_API bool Circle2d::IntersectsRustF32(
    const Rect2d& rectangle) const noexcept
{
    const float centerX = static_cast<float>(center.x);
    const float centerY = static_cast<float>(center.y);
    const float rectXMin = static_cast<float>(rectangle.xMin);
    const float rectYMin = static_cast<float>(rectangle.yMin);
    const float rectXMax = static_cast<float>(rectangle.xMax);
    const float rectYMax = static_cast<float>(rectangle.yMax);
    const float nearestX = detail::MaxRustF32(
        rectXMin, detail::MinRustF32(centerX, rectXMax));
    const float nearestY = detail::MaxRustF32(
        rectYMin, detail::MinRustF32(centerY, rectYMax));
    const float dx = nearestX - centerX;
    const float dy = nearestY - centerY;
    const float radiusF32 = static_cast<float>(radius);
    return dx * dx + dy * dy <= radiusF32 * radiusF32;
}

JAGUARCORE_API SeparationDistance2d Circle2d::SeparationDistanceRustF32(
    const Point2d& point) const noexcept
{
    const float dx = static_cast<float>(point.x) - static_cast<float>(center.x);
    const float dy = static_cast<float>(point.y) - static_cast<float>(center.y);
    const float centerDistance = std::sqrt(dx * dx + dy * dy);
    const float radiusF32 = static_cast<float>(radius);
    if (centerDistance <= radiusF32)
        return {GeoPosition::Interior,
                static_cast<double>(radiusF32 - centerDistance)};
    return {GeoPosition::Exterior,
            static_cast<double>(centerDistance - radiusF32)};
}

JAGUARCORE_API SeparationDistance2d Circle2d::SeparationDistanceRustF32(
    const Circle2d& other) const noexcept
{
    const float dx = static_cast<float>(center.x) -
                     static_cast<float>(other.center.x);
    const float dy = static_cast<float>(center.y) -
                     static_cast<float>(other.center.y);
    const float squaredCenterDistance = dx * dx + dy * dy;
    const float radiusSum = static_cast<float>(radius) +
                            static_cast<float>(other.radius);
    const float squaredRadiusSum = radiusSum * radiusSum;
    if (squaredCenterDistance < squaredRadiusSum)
    {
        return {GeoPosition::Interior,
                static_cast<double>(std::sqrt(squaredRadiusSum) -
                                    std::sqrt(squaredCenterDistance))};
    }
    return {GeoPosition::Exterior,
            static_cast<double>(std::sqrt(squaredCenterDistance) -
                                std::sqrt(squaredRadiusSum))};
}

JAGUARCORE_API SeparationDistance2d Circle2d::SeparationDistanceRustF32(
    const Edge2d& edge) const noexcept
{
    const float edgeDistance = static_cast<float>(edge.DistanceToRustF32(center));
    const float radiusF32 = static_cast<float>(radius);
    if (edgeDistance < radiusF32)
        return {GeoPosition::Interior,
                static_cast<double>(radiusF32 - edgeDistance)};
    return {GeoPosition::Exterior,
            static_cast<double>(edgeDistance - radiusF32)};
}

JAGUARCORE_API double Circle2d::DistanceToRustF32(
    const Point2d& point) const noexcept
{
    const float dx = static_cast<float>(point.x) - static_cast<float>(center.x);
    const float dy = static_cast<float>(point.y) - static_cast<float>(center.y);
    const float squaredDistance = dx * dx + dy * dy;
    const float radiusF32 = static_cast<float>(radius);
    if (squaredDistance < radiusF32 * radiusF32)
        return 0.0;
    return static_cast<double>(std::sqrt(squaredDistance) - radiusF32);
}

JAGUARCORE_API double Circle2d::DistanceToRustF32(
    const Circle2d& other) const noexcept
{
    const SeparationDistance2d separation = SeparationDistanceRustF32(other);
    return separation.first == GeoPosition::Interior ? 0.0 : separation.second;
}

JAGUARCORE_API double Circle2d::DistanceToRustF32(
    const Edge2d& edge) const noexcept
{
    const SeparationDistance2d separation = SeparationDistanceRustF32(edge);
    return separation.first == GeoPosition::Interior ? 0.0 : separation.second;
}

JAGUARCORE_API double Circle2d::SquaredDistanceToRustF32(
    const Point2d& point) const noexcept
{
    const float distance = static_cast<float>(DistanceToRustF32(point));
    return static_cast<double>(distance * distance);
}

JAGUARCORE_API double Circle2d::SquaredDistanceToRustF32(
    const Circle2d& other) const noexcept
{
    const float distance = static_cast<float>(DistanceToRustF32(other));
    return static_cast<double>(distance * distance);
}

JAGUARCORE_API double Circle2d::SquaredDistanceToRustF32(
    const Edge2d& edge) const noexcept
{
    const float distance = static_cast<float>(DistanceToRustF32(edge));
    return static_cast<double>(distance * distance);
}

JAGUARCORE_API SeparationDistance2d Circle2d::SquaredSeparationDistanceRustF32(
    const Point2d& point) const noexcept
{
    SeparationDistance2d separation = SeparationDistanceRustF32(point);
    const float distance = static_cast<float>(separation.second);
    separation.second = static_cast<double>(distance * distance);
    return separation;
}

JAGUARCORE_API SeparationDistance2d Circle2d::SquaredSeparationDistanceRustF32(
    const Circle2d& other) const noexcept
{
    SeparationDistance2d separation = SeparationDistanceRustF32(other);
    const float distance = static_cast<float>(separation.second);
    separation.second = static_cast<double>(distance * distance);
    return separation;
}

JAGUARCORE_API SeparationDistance2d Circle2d::SquaredSeparationDistanceRustF32(
    const Edge2d& edge) const noexcept
{
    SeparationDistance2d separation = SeparationDistanceRustF32(edge);
    const float distance = static_cast<float>(separation.second);
    separation.second = static_cast<double>(distance * distance);
    return separation;
}
}
