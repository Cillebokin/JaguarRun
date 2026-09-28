#include "JaguarCore/Geometry/ShapeModification.h"

#include "StraightSkeleton.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

namespace jaguar
{
namespace
{
struct Line2d
{
    Point2d start{};
    Point2d end{};
};

struct NormalizedOffset
{
    ShapeModifyMode mode = ShapeModifyMode::Inflate;
    double distance = 0.0;
};

NormalizedOffset normalizeOffsetRustF32(ShapeModifyMode mode,
                                        double distance) noexcept
{
    const double f32Distance =
        static_cast<double>(static_cast<float>(distance));
    const double signedDistance =
        mode == ShapeModifyMode::Inflate ? f32Distance : -f32Distance;
    return {signedDistance < 0.0 ? ShapeModifyMode::Deflate
                                 : ShapeModifyMode::Inflate,
            std::abs(signedDistance)};
}

Result<SimplePolygon> reimportOffsetPolygon(const SimplePolygon& polygon)
{
    return detail::createRustImportedOffsetPolygon(polygon.Vertices());
}

std::optional<Point2d> intersectLines(const Line2d& first, const Line2d& second)
{
    const double firstDx = first.end.x - first.start.x;
    const double firstDy = first.end.y - first.start.y;
    const double secondDx = second.end.x - second.start.x;
    const double secondDy = second.end.y - second.start.y;
    const double denominator = firstDx * secondDy - firstDy * secondDx;
    if (std::abs(denominator) <= 1.0e-12)
        return std::nullopt;

    const double dx = second.start.x - first.start.x;
    const double dy = second.start.y - first.start.y;
    const double t = (dx * secondDy - dy * secondDx) / denominator;
    return Point2d{first.start.x + t * firstDx, first.start.y + t * firstDy};
}

Point2d outwardNormal(const Point2d& start, const Point2d& end)
{
    const double dx = end.x - start.x;
    const double dy = end.y - start.y;
    const double length = std::sqrt(dx * dx + dy * dy);
    if (length <= 1.0e-12)
        return {};
    return Point2d{dy / length, -dx / length};
}

double turnAt(const Point2d& previous,
              const Point2d& current,
              const Point2d& next) noexcept
{
    const double firstX = current.x - previous.x;
    const double firstY = current.y - previous.y;
    const double secondX = next.x - current.x;
    const double secondY = next.y - current.y;
    return firstX * secondY - firstY * secondX;
}

void appendRoundedJoin(std::vector<Point2d>& points,
                       const Point2d& center,
                       const Point2d& previousNormal,
                       const Point2d& nextNormal,
                       double radius,
                       double turn)
{
    constexpr double maxAngularStep = 0.1;
    const double pi = std::acos(-1.0);
    const double twoPi = 2.0 * pi;
    const double startAngle = std::atan2(previousNormal.y, previousNormal.x);
    const double endAngle = std::atan2(nextNormal.y, nextNormal.x);
    double delta = endAngle - startAngle;

    if (turn > 0.0)
    {
        while (delta <= 0.0)
            delta += twoPi;
        if (delta > pi)
            delta -= twoPi;
    }
    else
    {
        while (delta >= 0.0)
            delta -= twoPi;
        if (delta < -pi)
            delta += twoPi;
    }

    const std::size_t steps = std::max<std::size_t>(
        1, static_cast<std::size_t>(std::ceil(std::abs(delta) / maxAngularStep)));
    for (std::size_t step = 0; step <= steps; ++step)
    {
        const double ratio = static_cast<double>(step) /
                             static_cast<double>(steps);
        const double angle = startAngle + delta * ratio;
        points.push_back({center.x + radius * std::cos(angle),
                          center.y + radius * std::sin(angle)});
    }
}

bool samePolygonCycle(const std::vector<Point2d>& first,
                      const std::vector<Point2d>& second)
{
    if (first.size() != second.size())
        return false;
    if (first.empty())
        return true;

    for (std::size_t start = 0; start < second.size(); ++start)
    {
        if (!(second[start] == first.front()))
            continue;
        bool matches = true;
        for (std::size_t i = 0; i < first.size(); ++i)
        {
            if (!(first[i] == second[(start + i) % second.size()]))
            {
                matches = false;
                break;
            }
        }
        if (matches)
            return true;
    }
    return false;
}

bool preservesModificationMode(const SimplePolygon& original,
                               const SimplePolygon& modified,
                               ShapeModifyMode mode)
{
    if (modified.Area() == original.Area())
    {
        return samePolygonCycle(original.Vertices(), modified.Vertices());
    }

    constexpr double tolerance = 1.0e-8;
    if (mode == ShapeModifyMode::Inflate)
    {
        if (modified.Area() < original.Area())
            return false;
        for (const Point2d& point : original.Vertices())
        {
            if (!modified.Contains(point, tolerance))
                return false;
        }
        return true;
    }

    if (modified.Area() > original.Area())
        return false;
    for (const Point2d& point : modified.Vertices())
    {
        if (!original.Contains(point, tolerance))
            return false;
    }
    return true;
}

Result<SimplePolygon> fallbackOffsetShape(const SimplePolygon& shape,
                                          ShapeModifyMode mode,
                                          double distance)
{
    const double signedDistance =
        mode == ShapeModifyMode::Inflate ? distance : -distance;
    const auto& points = shape.Vertices();
    std::vector<Point2d> offsetPoints;
    offsetPoints.reserve(points.size() * 4);

    for (std::size_t i = 0; i < points.size(); ++i)
    {
        const std::size_t previous = (i + points.size() - 1) % points.size();
        const std::size_t next = (i + 1) % points.size();

        const Point2d previousNormal = outwardNormal(points[previous], points[i]);
        const Point2d nextNormal = outwardNormal(points[i], points[next]);

        const Line2d previousLine{
            {points[previous].x + previousNormal.x * signedDistance,
             points[previous].y + previousNormal.y * signedDistance},
            {points[i].x + previousNormal.x * signedDistance,
             points[i].y + previousNormal.y * signedDistance}};
        const Line2d nextLine{
            {points[i].x + nextNormal.x * signedDistance,
             points[i].y + nextNormal.y * signedDistance},
            {points[next].x + nextNormal.x * signedDistance,
             points[next].y + nextNormal.y * signedDistance}};

        const double turn = turnAt(points[previous], points[i], points[next]);
        const bool roundedJoin =
            (signedDistance > 0.0 && turn > 1.0e-12) ||
            (signedDistance < 0.0 && turn < -1.0e-12);
        if (roundedJoin)
        {
            // The rounded join follows the offset side of the two incident
            // edges.  For an inward offset the signed distance reverses the
            // normals; using the outward normals unchanged would place a
            // concave deflate join on the exterior of the source polygon.
            const double normalSign = signedDistance < 0.0 ? -1.0 : 1.0;
            appendRoundedJoin(
                offsetPoints, points[i],
                {previousNormal.x * normalSign,
                 previousNormal.y * normalSign},
                {nextNormal.x * normalSign,
                 nextNormal.y * normalSign},
                std::abs(signedDistance), turn);
        }
        else
        {
            const auto intersection = intersectLines(previousLine, nextLine);
            if (intersection.has_value())
            {
                offsetPoints.push_back(*intersection);
            }
            else
            {
                offsetPoints.push_back(
                    {points[i].x +
                         (previousNormal.x + nextNormal.x) * signedDistance * 0.5,
                     points[i].y +
                         (previousNormal.y + nextNormal.y) * signedDistance * 0.5});
            }
        }
    }

    const auto result =
        detail::createRustImportedOffsetPolygon(std::move(offsetPoints));
    if (!result.HasValue())
        return result;
    if (!preservesModificationMode(shape, result.Value(), mode))
    {
        return Result<SimplePolygon>::FromError(
            {ErrorCode::OffsetFailed,
             "offset does not preserve the requested inflate/deflate relation"});
    }
    return result;
}

}

JAGUARCORE_API Result<SimplePolygon> OffsetShape(const SimplePolygon& shape,
                                                 ShapeModifyMode mode,
                                                 double distance)
{
    const NormalizedOffset offset = normalizeOffsetRustF32(mode, distance);
    if (!std::isfinite(offset.distance))
    {
        return Result<SimplePolygon>::FromError(
            {ErrorCode::OffsetFailed,
             "Offset resulted in an empty polygon"});
    }
    if (offset.distance == 0.0)
        return reimportOffsetPolygon(shape);

    const auto skeletonOffsets = OffsetShapes(
        shape, offset.mode, offset.distance);
    if (skeletonOffsets.HasValue())
    {
        if (skeletonOffsets.Value().empty())
        {
            return Result<SimplePolygon>::FromError(
                {ErrorCode::OffsetFailed,
                 "Offset resulted in an empty polygon"});
        }
        return reimportOffsetPolygon(skeletonOffsets.Value().front());
    }

    return fallbackOffsetShape(shape, offset.mode, offset.distance);
}

JAGUARCORE_API Result<std::vector<SimplePolygon>> OffsetShapes(
    const SimplePolygon& shape, ShapeModifyMode mode, double distance)
{
    const NormalizedOffset offset = normalizeOffsetRustF32(mode, distance);
    if (!std::isfinite(offset.distance))
    {
        return Result<std::vector<SimplePolygon>>::FromError(
            {ErrorCode::OffsetFailed,
             "Offset resulted in an empty polygon"});
    }
    if (offset.distance == 0.0)
        return Result<std::vector<SimplePolygon>>::FromValue({shape});

    const auto skeletonOffsets = detail::roundedStraightSkeletonOffsets(
        shape,
        offset.mode == ShapeModifyMode::Inflate
            ? offset.distance
            : -offset.distance);
    if (!skeletonOffsets.HasValue())
        return skeletonOffsets;

    if (skeletonOffsets.Value().empty())
    {
        if (offset.mode == ShapeModifyMode::Deflate &&
            offset.distance >= shape.PointOfInaccessibility().radius)
        {
            return skeletonOffsets;
        }

        const auto fallback =
            fallbackOffsetShape(shape, offset.mode, offset.distance);
        if (fallback.HasValue())
        {
            return Result<std::vector<SimplePolygon>>::FromValue(
                {fallback.Value()});
        }
        if (offset.mode == ShapeModifyMode::Inflate)
        {
            return Result<std::vector<SimplePolygon>>::FromError(
                fallback.GetError());
        }
        return skeletonOffsets;
    }

    for (const SimplePolygon& polygon : skeletonOffsets.Value())
    {
        if (!preservesModificationMode(shape, polygon, offset.mode))
        {
            return Result<std::vector<SimplePolygon>>::FromError(
                {ErrorCode::OffsetFailed,
                 "straight-skeleton offset does not preserve the requested relation"});
        }
    }
    return skeletonOffsets;
}
}
