#include "JaguarCore/geometry/edge.h"

#include <algorithm>
#include <cmath>

namespace jaguar
{
namespace
{
double cross(const Point2d& a, const Point2d& b, const Point2d& c) noexcept
{
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

bool within(double value, double lower, double upper, double tolerance) noexcept
{
    return value >= lower - tolerance && value <= upper + tolerance;
}

bool onSegment(const Point2d& point,
               const Point2d& start,
               const Point2d& end,
               double tolerance) noexcept
{
    return std::abs(cross(start, end, point)) <= tolerance &&
           within(point.x, std::min(start.x, end.x), std::max(start.x, end.x), tolerance) &&
           within(point.y, std::min(start.y, end.y), std::max(start.y, end.y), tolerance);
}
}

JAGUARCORE_API Result<Edge2d> Edge2d::create(Point2d startPoint, Point2d endPoint)
{
    if (!startPoint.isFinite() || !endPoint.isFinite() || startPoint == endPoint)
    {
        return Result<Edge2d>::fromError({ErrorCode::InvalidInput, "invalid edge"});
    }
    return Result<Edge2d>::fromValue(Edge2d{startPoint, endPoint});
}

JAGUARCORE_API Rect2d Edge2d::boundingBox() const noexcept
{
    return Rect2d{std::min(start.x, end.x), std::min(start.y, end.y),
                  std::max(start.x, end.x), std::max(start.y, end.y)};
}

JAGUARCORE_API double Edge2d::length() const noexcept
{
    return std::sqrt(start.squaredDistanceTo(end));
}

JAGUARCORE_API Point2d Edge2d::closestPoint(const Point2d& point) const noexcept
{
    const double dx = end.x - start.x;
    const double dy = end.y - start.y;
    const double lengthSquared = dx * dx + dy * dy;
    if (lengthSquared == 0.0)
        return start;

    const double projection = ((point.x - start.x) * dx + (point.y - start.y) * dy) /
                              lengthSquared;
    const double clamped = std::max(0.0, std::min(1.0, projection));
    return Point2d{start.x + clamped * dx, start.y + clamped * dy};
}

JAGUARCORE_API bool Edge2d::intersects(const Edge2d& other, double tolerance) const noexcept
{
    const Rect2d firstBox = boundingBox();
    const Rect2d secondBox = other.boundingBox();
    if (firstBox.xMax < secondBox.xMin - tolerance ||
        secondBox.xMax < firstBox.xMin - tolerance ||
        firstBox.yMax < secondBox.yMin - tolerance ||
        secondBox.yMax < firstBox.yMin - tolerance)
    {
        return false;
    }

    const double firstStart = cross(start, end, other.start);
    const double firstEnd = cross(start, end, other.end);
    const double secondStart = cross(other.start, other.end, start);
    const double secondEnd = cross(other.start, other.end, end);

    // Match jagua-rs Edge::collides_with: parallel/collinear segments are
    // treated as non-colliding. This deliberately excludes collinear overlap
    // and endpoint-only contact from the proper edge-intersection predicate.
    if (std::abs(firstStart) <= tolerance && std::abs(firstEnd) <= tolerance &&
        std::abs(secondStart) <= tolerance && std::abs(secondEnd) <= tolerance)
        return false;

    const bool properIntersection =
        ((firstStart > tolerance && firstEnd < -tolerance) ||
         (firstStart < -tolerance && firstEnd > tolerance)) &&
        ((secondStart > tolerance && secondEnd < -tolerance) ||
         (secondStart < -tolerance && secondEnd > tolerance));

    if (properIntersection)
        return true;

    return (std::abs(firstStart) <= tolerance && onSegment(other.start, start, end, tolerance)) ||
           (std::abs(firstEnd) <= tolerance && onSegment(other.end, start, end, tolerance)) ||
           (std::abs(secondStart) <= tolerance && onSegment(start, other.start, other.end, tolerance)) ||
           (std::abs(secondEnd) <= tolerance && onSegment(end, other.start, other.end, tolerance));
}
}
