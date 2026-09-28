#include "JaguarCore/Geometry/Edge.h"
#include "JaguarCore/Geometry/RustF32.h"
#include "JaguarCore/Geometry/Transformation.h"

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

std::optional<Point2d> intersectionPointRustF32(
    const Edge2d& first,
    const Edge2d& second) noexcept
{
    const float x1 = static_cast<float>(first.start.x);
    const float y1 = static_cast<float>(first.start.y);
    const float x2 = static_cast<float>(first.end.x);
    const float y2 = static_cast<float>(first.end.y);
    const float x3 = static_cast<float>(second.start.x);
    const float y3 = static_cast<float>(second.start.y);
    const float x4 = static_cast<float>(second.end.x);
    const float y4 = static_cast<float>(second.end.y);

    const bool xAxisNoOverlap =
        detail::MaxRustF32(detail::MinRustF32(x1, x2),
                           detail::MinRustF32(x3, x4))
        > detail::MinRustF32(detail::MaxRustF32(x1, x2),
                             detail::MaxRustF32(x3, x4));
    const bool yAxisNoOverlap =
        detail::MaxRustF32(detail::MinRustF32(y1, y2),
                           detail::MinRustF32(y3, y4))
        > detail::MinRustF32(detail::MaxRustF32(y1, y2),
                             detail::MaxRustF32(y3, y4));
    if (xAxisNoOverlap || yAxisNoOverlap)
        return std::nullopt;

    const float tNumerator = (x2 - x4) * (y4 - y3)
        - (y2 - y4) * (x4 - x3);
    const float tDenominator = (x2 - x1) * (y4 - y3)
        - (y2 - y1) * (x4 - x3);
    const float uNumerator = (x2 - x4) * (y2 - y1)
        - (y2 - y4) * (x2 - x1);
    const float uDenominator = (x2 - x1) * (y4 - y3)
        - (y2 - y1) * (x4 - x3);
    if (tDenominator == 0.0F || uDenominator == 0.0F)
        return std::nullopt;

    const float t = tNumerator / tDenominator;
    const float u = uNumerator / uDenominator;
    if (!(t >= 0.0F && t <= 1.0F && u >= 0.0F && u <= 1.0F))
        return std::nullopt;

    return Point2d{static_cast<double>(x2 + t * (x1 - x2)),
                   static_cast<double>(y2 + t * (y1 - y2))};
}
}

JAGUARCORE_API std::vector<Edge2d> Rect2d::EdgesRustF32() const
{
    const auto corners = CornersRustF32();
    return {Edge2d{corners[0], corners[1]}, Edge2d{corners[1], corners[2]},
            Edge2d{corners[2], corners[3]}, Edge2d{corners[3], corners[0]}};
}

JAGUARCORE_API std::vector<Edge2d> Rect2d::SidesRustF32() const
{
    return EdgesRustF32();
}

JAGUARCORE_API Result<Edge2d> Edge2d::Create(Point2d startPoint, Point2d endPoint)
{
    const float startX = static_cast<float>(startPoint.x);
    const float startY = static_cast<float>(startPoint.y);
    const float endX = static_cast<float>(endPoint.x);
    const float endY = static_cast<float>(endPoint.y);
    if (startX == endX && startY == endY)
    {
        return Result<Edge2d>::FromError({ErrorCode::InvalidInput, "invalid edge"});
    }
    return Result<Edge2d>::FromValue(
        Edge2d{Point2d{static_cast<double>(startX), static_cast<double>(startY)},
               Point2d{static_cast<double>(endX), static_cast<double>(endY)}});
}

JAGUARCORE_API Rect2d Edge2d::BoundingBox() const noexcept
{
    const float startX = static_cast<float>(start.x);
    const float startY = static_cast<float>(start.y);
    const float endX = static_cast<float>(end.x);
    const float endY = static_cast<float>(end.y);
    return Rect2d{
        static_cast<double>(detail::MinRustF32(startX, endX)),
        static_cast<double>(detail::MinRustF32(startY, endY)),
        static_cast<double>(detail::MaxRustF32(startX, endX)),
        static_cast<double>(detail::MaxRustF32(startY, endY))};
}

JAGUARCORE_API double Edge2d::Length() const noexcept
{
    return std::sqrt(start.SquaredDistanceTo(end));
}

JAGUARCORE_API double Edge2d::LengthRustF32() const noexcept
{
    const float dx = static_cast<float>(end.x) - static_cast<float>(start.x);
    const float dy = static_cast<float>(end.y) - static_cast<float>(start.y);
    return static_cast<double>(std::sqrt(dx * dx + dy * dy));
}

JAGUARCORE_API double Edge2d::XMinRustF32() const noexcept
{
    return static_cast<double>(detail::MinRustF32(
        static_cast<float>(start.x), static_cast<float>(end.x)));
}

JAGUARCORE_API double Edge2d::YMinRustF32() const noexcept
{
    return static_cast<double>(detail::MinRustF32(
        static_cast<float>(start.y), static_cast<float>(end.y)));
}

JAGUARCORE_API double Edge2d::XMaxRustF32() const noexcept
{
    return static_cast<double>(detail::MaxRustF32(
        static_cast<float>(start.x), static_cast<float>(end.x)));
}

JAGUARCORE_API double Edge2d::YMaxRustF32() const noexcept
{
    return static_cast<double>(detail::MaxRustF32(
        static_cast<float>(start.y), static_cast<float>(end.y)));
}

JAGUARCORE_API Point2d Edge2d::CentroidRustF32() const noexcept
{
    const float startX = static_cast<float>(start.x);
    const float startY = static_cast<float>(start.y);
    const float endX = static_cast<float>(end.x);
    const float endY = static_cast<float>(end.y);
    return Point2d{
        static_cast<double>(detail::MidpointRustF32(startX, endX)),
        static_cast<double>(detail::MidpointRustF32(startY, endY))};
}

JAGUARCORE_API Edge2d Edge2d::ExtendedAtFrontRustF32(
    double distance) const noexcept
{
    const float startX = static_cast<float>(start.x);
    const float startY = static_cast<float>(start.y);
    const float endX = static_cast<float>(end.x);
    const float endY = static_cast<float>(end.y);
    const float dx = endX - startX;
    const float dy = endY - startY;
    const float edgeLength = std::sqrt(dx * dx + dy * dy);
    const float extension = static_cast<float>(distance) / edgeLength;
    return Edge2d{
        Point2d{static_cast<double>(startX - dx * extension),
                static_cast<double>(startY - dy * extension)},
        Point2d{static_cast<double>(endX), static_cast<double>(endY)}};
}

JAGUARCORE_API Edge2d Edge2d::ExtendedAtBackRustF32(
    double distance) const noexcept
{
    const float startX = static_cast<float>(start.x);
    const float startY = static_cast<float>(start.y);
    float endX = static_cast<float>(end.x);
    float endY = static_cast<float>(end.y);
    const float dx = endX - startX;
    const float dy = endY - startY;
    const float edgeLength = std::sqrt(dx * dx + dy * dy);
    const float extension = static_cast<float>(distance) / edgeLength;
    endX += dx * extension;
    endY += dy * extension;
    return Edge2d{Point2d{static_cast<double>(startX), static_cast<double>(startY)},
                  Point2d{static_cast<double>(endX), static_cast<double>(endY)}};
}

JAGUARCORE_API Edge2d Edge2d::ScaledRustF32(double factor) const noexcept
{
    const float startX = static_cast<float>(start.x);
    const float startY = static_cast<float>(start.y);
    const float endX = static_cast<float>(end.x);
    const float endY = static_cast<float>(end.y);
    const float dx = endX - startX;
    const float dy = endY - startY;
    const float scaleFactor = static_cast<float>(factor) - 1.0F;
    return Edge2d{
        Point2d{static_cast<double>(startX - dx * scaleFactor / 2.0F),
                static_cast<double>(startY - dy * scaleFactor / 2.0F)},
        Point2d{static_cast<double>(endX + dx * scaleFactor / 2.0F),
                static_cast<double>(endY + dy * scaleFactor / 2.0F)}};
}

JAGUARCORE_API Edge2d Edge2d::ReversedRustF32() const noexcept
{
    return Edge2d{
        Point2d{static_cast<double>(static_cast<float>(end.x)),
                static_cast<double>(static_cast<float>(end.y))},
        Point2d{static_cast<double>(static_cast<float>(start.x)),
                static_cast<double>(static_cast<float>(start.y))}};
}

JAGUARCORE_API void Edge2d::TransformRustF32(
    const Transform2d& transform) noexcept
{
    start = transform.ApplyRustF32(start);
    end = transform.ApplyRustF32(end);
}

JAGUARCORE_API Point2d Edge2d::ClosestPoint(const Point2d& point) const noexcept
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

JAGUARCORE_API Point2d Edge2d::ClosestPointRustF32(
    const Point2d& point) const noexcept
{
    const float x1 = static_cast<float>(start.x);
    const float y1 = static_cast<float>(start.y);
    const float x2 = static_cast<float>(end.x);
    const float y2 = static_cast<float>(end.y);
    const float x = static_cast<float>(point.x);
    const float y = static_cast<float>(point.y);
    const float a = x - x1;
    const float b = y - y1;
    const float c = x2 - x1;
    const float d = y2 - y1;
    const float dot = a * c + b * d;
    const float lengthSquared = c * c + d * d;
    float parameter = -1.0F;
    if (lengthSquared != 0.0F)
        parameter = dot / lengthSquared;

    if (parameter < 0.0F)
        return Point2d{static_cast<double>(x1), static_cast<double>(y1)};
    if (parameter > 1.0F)
        return Point2d{static_cast<double>(x2), static_cast<double>(y2)};
    return Point2d{static_cast<double>(x1 + parameter * c),
                   static_cast<double>(y1 + parameter * d)};
}

JAGUARCORE_API double Edge2d::SquaredDistanceToRustF32(
    const Point2d& point) const noexcept
{
    const Point2d closest = ClosestPointRustF32(point);
    const float dx = static_cast<float>(point.x) -
                     static_cast<float>(closest.x);
    const float dy = static_cast<float>(point.y) -
                     static_cast<float>(closest.y);
    return static_cast<double>(dx * dx + dy * dy);
}

JAGUARCORE_API double Edge2d::DistanceToRustF32(
    const Point2d& point) const noexcept
{
    const float squaredDistance = static_cast<float>(
        SquaredDistanceToRustF32(point));
    return static_cast<double>(std::sqrt(squaredDistance));
}

JAGUARCORE_API bool Edge2d::Intersects(const Edge2d& other, double tolerance) const noexcept
{
    const Rect2d firstBox = BoundingBox();
    const Rect2d secondBox = other.BoundingBox();
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

JAGUARCORE_API bool Edge2d::IntersectsRustF32(const Edge2d& other) const noexcept
{
    return intersectionPointRustF32(*this, other).has_value();
}

JAGUARCORE_API std::optional<Point2d> Edge2d::IntersectionPointRustF32(
    const Edge2d& other) const noexcept
{
    return intersectionPointRustF32(*this, other);
}

JAGUARCORE_API bool Edge2d::IntersectsRustF32(
    const Rect2d& rectangle) const noexcept
{
    const float startX = static_cast<float>(start.x);
    const float startY = static_cast<float>(start.y);
    const float endX = static_cast<float>(end.x);
    const float endY = static_cast<float>(end.y);
    const float rectXMin = static_cast<float>(rectangle.xMin);
    const float rectYMin = static_cast<float>(rectangle.yMin);
    const float rectXMax = static_cast<float>(rectangle.xMax);
    const float rectYMax = static_cast<float>(rectangle.yMax);

    if (detail::MaxRustF32(detail::MinRustF32(startX, endX), rectXMin) >
            detail::MinRustF32(detail::MaxRustF32(startX, endX), rectXMax) ||
        detail::MaxRustF32(detail::MinRustF32(startY, endY), rectYMin) >
            detail::MinRustF32(detail::MaxRustF32(startY, endY), rectYMax))
    {
        return false;
    }

    const auto containsEndpoint = [rectXMin, rectYMin, rectXMax, rectYMax](
                                      float x, float y) {
        return x >= rectXMin && x <= rectXMax &&
               y >= rectYMin && y <= rectYMax;
    };
    if (containsEndpoint(startX, startY) || containsEndpoint(endX, endY))
        return true;

    const float edgeDx = endX - startX;
    const float edgeDy = endY - startY;
    const float cornerX[] = {rectXMax, rectXMin, rectXMin, rectXMax};
    const float cornerY[] = {rectYMax, rectYMax, rectYMin, rectYMin};
    bool allPositive = true;
    bool allNegative = true;
    for (std::size_t index = 0U; index < 4U; ++index)
    {
        const float side = (cornerX[index] - startX) * edgeDy -
                           (cornerY[index] - startY) * edgeDx;
        allPositive = allPositive && side > 0.0F;
        allNegative = allNegative && side < 0.0F;
    }
    return !(allPositive || allNegative);
}
}
