#include "JaguarCore/Geometry/ShapeModification.h"

#include "StraightSkeleton.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
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

struct OverlayPoint
{
    std::int32_t x = 0;
    std::int32_t y = 0;

    bool operator==(const OverlayPoint& other) const noexcept
    {
        return x == other.x && y == other.y;
    }

    bool operator!=(const OverlayPoint& other) const noexcept
    {
        return !(*this == other);
    }
};

struct OverlayAdapter
{
    Point2d offset{};
    double dirScale = 1.0;
    double invScale = 1.0;

    std::optional<OverlayPoint> toInt(const Point2d& point) const noexcept
    {
        const double x = std::round((point.x - offset.x) * dirScale);
        const double y = std::round((point.y - offset.y) * dirScale);
        if (!std::isfinite(x) || !std::isfinite(y) ||
            x < std::numeric_limits<std::int32_t>::min() ||
            x > std::numeric_limits<std::int32_t>::max() ||
            y < std::numeric_limits<std::int32_t>::min() ||
            y > std::numeric_limits<std::int32_t>::max())
        {
            return std::nullopt;
        }
        return OverlayPoint{static_cast<std::int32_t>(x),
                           static_cast<std::int32_t>(y)};
    }

    Point2d toFloat(const OverlayPoint& point) const noexcept
    {
        return {static_cast<double>(point.x) * invScale + offset.x,
                static_cast<double>(point.y) * invScale + offset.y};
    }
};

struct OverlaySection
{
    OverlayPoint a{};
    OverlayPoint b{};
    OverlayPoint aTop{};
    OverlayPoint bTop{};
    Point2d direction{};
};

bool preservesModificationMode(const SimplePolygon& original,
                               const SimplePolygon& modified,
                               ShapeModifyMode mode);

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

std::optional<OverlayAdapter> makeOverlayAdapter(const SimplePolygon& shape,
                                                  double distance)
{
    const Rect2d& bounds = shape.BoundingBox();
    const double extra = std::abs(1.1 * distance) + std::abs(1.1 * distance);
    const double minX = bounds.xMin - extra;
    const double maxX = bounds.xMax + extra;
    const double minY = bounds.yMin - extra;
    const double maxY = bounds.yMax + extra;
    const double halfWidth = (maxX - minX) * 0.5;
    const double halfHeight = (maxY - minY) * 0.5;
    const double maxHalfExtent = std::max(halfWidth, halfHeight);

    OverlayAdapter adapter;
    adapter.offset = {minX + halfWidth, minY + halfHeight};
    if (maxHalfExtent != 0.0)
    {
        // i_float::FloatNumber::to_i32 rounds to the nearest integer,
        // with halfway values away from zero; it does not truncate log2.
        const auto exponent =
            static_cast<int>(std::round(std::log2(maxHalfExtent)));
        const int scaleExponent = 29 - exponent;
        adapter.dirScale = std::exp2(static_cast<double>(scaleExponent));
        adapter.invScale = std::exp2(-static_cast<double>(scaleExponent));
    }
    if (!std::isfinite(adapter.offset.x) ||
        !std::isfinite(adapter.offset.y) ||
        !std::isfinite(adapter.dirScale) ||
        !std::isfinite(adapter.invScale))
    {
        return std::nullopt;
    }
    return adapter;
}

bool isCounterClockwiseAndNonCollinear(
    const std::vector<OverlayPoint>& points) noexcept
{
    if (points.size() < 3U)
        return false;

    long double signedAreaTwice = 0.0L;
    for (std::size_t i = 0U; i < points.size(); ++i)
    {
        const OverlayPoint& previous = points[(i + points.size() - 1U) % points.size()];
        const OverlayPoint& current = points[i];
        const OverlayPoint& next = points[(i + 1U) % points.size()];
        const std::int64_t firstX =
            static_cast<std::int64_t>(current.x) - previous.x;
        const std::int64_t firstY =
            static_cast<std::int64_t>(current.y) - previous.y;
        const std::int64_t secondX =
            static_cast<std::int64_t>(next.x) - current.x;
        const std::int64_t secondY =
            static_cast<std::int64_t>(next.y) - current.y;
        if (firstX * secondY - firstY * secondX == 0)
            return false;
        signedAreaTwice += static_cast<long double>(current.x) * next.y -
                           static_cast<long double>(current.y) * next.x;
    }
    return signedAreaTwice > 0.0L;
}

bool isConvexCounterClockwise(const SimplePolygon& shape, double distance)
{
    if (shape.VertexCount() < 3U)
        return false;

    const auto adapter = makeOverlayAdapter(shape, distance);
    if (!adapter.has_value())
        return false;

    std::vector<OverlayPoint> points;
    points.reserve(shape.VertexCount());
    for (const Point2d& point : shape.Vertices())
    {
        const auto converted = adapter->toInt(point);
        if (!converted.has_value())
            return false;
        if (points.empty() || points.back() != *converted)
            points.push_back(*converted);
    }
    if (points.size() > 1U && points.front() == points.back())
        points.pop_back();
    if (points.size() < 3U)
        return false;

    long double signedAreaTwice = 0.0L;
    bool hasPositiveTurn = false;
    for (std::size_t i = 0U; i < points.size(); ++i)
    {
        const OverlayPoint& previous = points[(i + points.size() - 1U) % points.size()];
        const OverlayPoint& current = points[i];
        const OverlayPoint& next = points[(i + 1U) % points.size()];
        const long double firstX =
            static_cast<long double>(current.x) - previous.x;
        const long double firstY =
            static_cast<long double>(current.y) - previous.y;
        const long double secondX =
            static_cast<long double>(next.x) - current.x;
        const long double secondY =
            static_cast<long double>(next.y) - current.y;
        const long double turn = firstX * secondY - firstY * secondX;
        if (turn < 0.0L)
            return false;
        hasPositiveTurn = hasPositiveTurn || turn > 0.0L;
        signedAreaTwice +=
            static_cast<long double>(current.x) * next.y -
            static_cast<long double>(current.y) * next.x;
    }
    return hasPositiveTurn && signedAreaTwice > 0.0L;
}

std::optional<SimplePolygon> rotateConvexContourToIOverlayStart(
    const SimplePolygon& polygon,
    const SimplePolygon& source,
    double distance)
{
    if (polygon.VertexCount() < 3U)
        return std::nullopt;

    const auto adapter = makeOverlayAdapter(source, distance);
    if (!adapter.has_value())
        return std::nullopt;

    std::vector<Point2d> ordered = polygon.Vertices();
    std::vector<OverlayPoint> quantized;
    quantized.reserve(ordered.size());
    for (const Point2d& point : ordered)
    {
        const auto converted = adapter->toInt(point);
        if (!converted.has_value())
            return std::nullopt;
        quantized.push_back(*converted);
    }

    if (ordered.size() > 1U && quantized.front() == quantized.back())
    {
        ordered.pop_back();
        quantized.pop_back();
    }
    bool removed = true;
    while (removed && ordered.size() > 3U)
    {
        removed = false;
        for (std::size_t i = 0U; i < ordered.size(); ++i)
        {
            const std::size_t previous =
                (i + ordered.size() - 1U) % ordered.size();
            const std::size_t next = (i + 1U) % ordered.size();
            const OverlayPoint& a = quantized[previous];
            const OverlayPoint& b = quantized[i];
            const OverlayPoint& c = quantized[next];
            const long double firstX =
                static_cast<long double>(b.x) - a.x;
            const long double firstY =
                static_cast<long double>(b.y) - a.y;
            const long double secondX =
                static_cast<long double>(c.x) - b.x;
            const long double secondY =
                static_cast<long double>(c.y) - b.y;
            const long double between =
                (static_cast<long double>(b.x) - a.x) *
                    (static_cast<long double>(b.x) - c.x) +
                (static_cast<long double>(b.y) - a.y) *
                    (static_cast<long double>(b.y) - c.y);
            if (b == a || b == c ||
                (firstX * secondY - firstY * secondX == 0.0L &&
                 between <= 0.0L))
            {
                ordered.erase(ordered.begin() +
                              static_cast<std::ptrdiff_t>(i));
                quantized.erase(quantized.begin() +
                                static_cast<std::ptrdiff_t>(i));
                removed = true;
                break;
            }
        }
    }
    if (ordered.size() < 3U)
        return std::nullopt;

    std::size_t first = 0U;
    for (std::size_t i = 1U; i < quantized.size(); ++i)
    {
        if (quantized[i].x < quantized[first].x ||
            (quantized[i].x == quantized[first].x &&
             quantized[i].y > quantized[first].y))
        {
            first = i;
        }
    }
    const std::int32_t minX = quantized[first].x;
    const auto minXCount = std::count_if(
        quantized.begin(), quantized.end(), [minX](const OverlayPoint& point) {
            return point.x == minX;
        });
    const std::size_t start = minXCount == 1U
                                  ? (first + ordered.size() - 1U) % ordered.size()
                                  : first;
    std::rotate(ordered.begin(), ordered.begin() + start, ordered.end());
    const auto imported = detail::createRustImportedOffsetPolygon(
        std::move(ordered));
    if (!imported.HasValue())
        return std::nullopt;
    return imported.Value();
}

// A simple counter-clockwise exterior expanded by round joins keeps the same
// outline traversal when its offset contour does not self-intersect.
std::optional<SimplePolygon> counterClockwiseInflateWithIOverlayOutlineRules(
    const SimplePolygon& shape, double distance)
{
    if (shape.VertexCount() < 3U || !(distance > 0.0))
        return std::nullopt;

    const auto adapter = makeOverlayAdapter(shape, distance);
    if (!adapter.has_value())
        return std::nullopt;

    std::vector<OverlayPoint> source;
    source.reserve(shape.VertexCount());
    for (const Point2d& point : shape.Vertices())
    {
        const auto converted = adapter->toInt(point);
        if (!converted.has_value())
            return std::nullopt;
        if (source.empty() || source.back() != *converted)
            source.push_back(*converted);
    }
    if (source.size() > 1U && source.front() == source.back())
        source.pop_back();
    if (!isCounterClockwiseAndNonCollinear(source))
        return std::nullopt;

    constexpr double roundRatio = 0.1;
    const double limitDotProduct = std::cos(roundRatio);
    const double signedRadius = -distance;
    const double rotationDirection = signedRadius >= 0.0 ? -1.0 : 1.0;

    std::vector<OverlaySection> sections;
    sections.reserve(source.size());
    for (std::size_t i = 0U; i < source.size(); ++i)
    {
        const OverlayPoint a = source[i];
        const OverlayPoint b = source[(i + 1U) % source.size()];
        const Point2d floatA = adapter->toFloat(a);
        const Point2d floatB = adapter->toFloat(b);
        const double dx = floatB.x - floatA.x;
        const double dy = floatB.y - floatA.y;
        const double length = std::sqrt(dx * dx + dy * dy);
        if (!(length > 0.0) || !std::isfinite(length))
            return std::nullopt;

        const Point2d direction{dx / length, dy / length};
        const Point2d normal{-direction.y * signedRadius,
                             direction.x * signedRadius};
        const auto aTop = adapter->toInt(
            {floatA.x + normal.x, floatA.y + normal.y});
        const auto bTop = adapter->toInt(
            {floatB.x + normal.x, floatB.y + normal.y});
        if (!aTop.has_value() || !bTop.has_value())
            return std::nullopt;
        sections.push_back({a, b, *aTop, *bTop, direction});
    }

    std::vector<std::optional<OverlayPoint>> concaveJoins(sections.size());
    for (std::size_t i = 0U; i < sections.size(); ++i)
    {
        const OverlaySection& previous =
            sections[(i + sections.size() - 1U) % sections.size()];
        const OverlaySection& next = sections[i];
        const double turn =
            previous.direction.x * next.direction.y -
            previous.direction.y * next.direction.x;
        if (turn < 0.0)
        {
            const Point2d previousOffsetEnd =
                adapter->toFloat(previous.bTop);
            const Point2d nextOffsetStart = adapter->toFloat(next.aTop);
            const double deltaX = nextOffsetStart.x - previousOffsetEnd.x;
            const double deltaY = nextOffsetStart.y - previousOffsetEnd.y;
            const double parameter =
                (deltaX * next.direction.y - deltaY * next.direction.x) /
                turn;
            concaveJoins[i] = adapter->toInt(
                {previousOffsetEnd.x + previous.direction.x * parameter,
                 previousOffsetEnd.y + previous.direction.y * parameter});
            if (!concaveJoins[i].has_value())
                return std::nullopt;
        }
    }

    std::vector<OverlayPoint> outline;
    outline.reserve(source.size() * 18U);
    for (std::size_t i = 0U; i < sections.size(); ++i)
    {
        const OverlaySection& previous =
            sections[(i + sections.size() - 1U) % sections.size()];
        const OverlaySection& next = sections[i];

        const double dot = previous.direction.x * next.direction.x +
                           previous.direction.y * next.direction.y;
        if (concaveJoins[i].has_value())
        {
            outline.push_back(*concaveJoins[i]);
        }
        else if (limitDotProduct < dot)
        {
            outline.push_back(previous.bTop);
            outline.push_back(next.aTop);
        }
        else
        {
            const double angle = std::acos(dot);
            const auto segmentCount = static_cast<std::size_t>(
                angle / roundRatio + 0.5);
            if (segmentCount == 0U)
                return std::nullopt;

            outline.push_back(previous.bTop);
            const double deltaAngle = angle / static_cast<double>(segmentCount);
            const double rotationAngle = rotationDirection * deltaAngle;
            const double rotationSin = std::sin(rotationAngle);
            const double rotationCos = std::cos(rotationAngle);
            const double centerX = adapter->toFloat(previous.b).x;
            const double centerY = adapter->toFloat(previous.b).y;
            double vectorX = -previous.direction.y;
            double vectorY = previous.direction.x;
            OverlayPoint last = previous.bTop;
            for (std::size_t step = 1U; step < segmentCount; ++step)
            {
                const double rotatedX =
                    rotationCos * vectorX - rotationSin * vectorY;
                const double rotatedY =
                    rotationSin * vectorX + rotationCos * vectorY;
                vectorX = rotatedX;
                vectorY = rotatedY;
                const Point2d point{
                    centerX + vectorX * signedRadius,
                    centerY + vectorY * signedRadius};
                const auto converted = adapter->toInt(point);
                if (!converted.has_value())
                    return std::nullopt;
                if (last != *converted)
                {
                    outline.push_back(*converted);
                    last = *converted;
                }
            }
            if (last != next.aTop)
                outline.push_back(next.aTop);
        }
        const std::size_t nextCorner = (i + 1U) % sections.size();
        outline.push_back(concaveJoins[nextCorner].value_or(next.bTop));
    }

    if (outline.size() < 3U)
        return std::nullopt;

    // Match i_overlay's tested left-top contour start. A vertical leftmost
    // edge starts at its upper endpoint; with a unique leftmost point, the
    // extracted contour starts at its predecessor in the CCW ring.
    auto first = std::min_element(
        outline.begin(), outline.end(), [](const OverlayPoint& left,
                                           const OverlayPoint& right) {
            return left.x < right.x ||
                   (left.x == right.x && left.y > right.y);
        });
    const std::int32_t minX = first->x;
    const auto minXCount = std::count_if(
        outline.begin(), outline.end(), [minX](const OverlayPoint& point) {
            return point.x == minX;
        });
    if (minXCount == 1U)
    {
        first = first == outline.begin() ? outline.end() - 1 : first - 1;
    }
    std::rotate(outline.begin(), first, outline.end());

    std::vector<Point2d> points;
    points.reserve(outline.size());
    for (const OverlayPoint& point : outline)
        points.push_back(adapter->toFloat(point));
    const auto polygon = detail::createRustImportedOffsetPolygon(std::move(points));
    if (!polygon.HasValue())
        return std::nullopt;
    if (!preservesModificationMode(shape, polygon.Value(), ShapeModifyMode::Inflate))
        return std::nullopt;
    return polygon.Value();
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

    if (offset.mode == ShapeModifyMode::Inflate)
    {
        const auto convexOffset =
            counterClockwiseInflateWithIOverlayOutlineRules(
                shape, offset.distance);
        if (convexOffset.has_value())
        {
            return Result<std::vector<SimplePolygon>>::FromValue(
                {*convexOffset});
        }
    }

    auto skeletonOffsets = detail::roundedStraightSkeletonOffsets(
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

    if (offset.mode == ShapeModifyMode::Deflate &&
        isConvexCounterClockwise(shape, offset.distance))
    {
        std::vector<SimplePolygon> ordered;
        ordered.reserve(skeletonOffsets.Value().size());
        for (const SimplePolygon& polygon : skeletonOffsets.Value())
        {
            if (samePolygonCycle(shape.Vertices(), polygon.Vertices()))
            {
                ordered.push_back(polygon);
                continue;
            }

            const auto canonical =
                rotateConvexContourToIOverlayStart(
                    polygon, shape, offset.distance);
            ordered.push_back(canonical.value_or(polygon));
        }
        skeletonOffsets = Result<std::vector<SimplePolygon>>::FromValue(
            std::move(ordered));
    }
    return skeletonOffsets;
}
}
