#include "JaguarCore/Geometry/SimplePolygon.h"

#include "JaguarCore/Geometry/ConvexHull.h"
#include "JaguarCore/Geometry/PointOfInaccessibility.h"
#include "JaguarCore/Geometry/RustF32.h"
#include "JaguarCore/FailFast/Surrogate.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace jaguar
{
namespace
{
float rustF32(double value) noexcept
{
    return static_cast<float>(value);
}

bool normalizeRustF32(std::vector<Point2d>& points) noexcept
{
    for (Point2d& point : points)
    {
        point.x = static_cast<double>(rustF32(point.x));
        point.y = static_cast<double>(rustF32(point.y));
        if (!std::isfinite(point.x) || !std::isfinite(point.y))
            return false;
    }
    return true;
}

float signedAreaRustF32(const std::vector<Point2d>& points) noexcept
{
    float sum = 0.0F;
    for (std::size_t i = 0; i < points.size(); ++i)
    {
        const std::size_t next = (i + 1) % points.size();
        const float x = rustF32(points[i].x);
        const float y = rustF32(points[i].y);
        const float nextX = rustF32(points[next].x);
        const float nextY = rustF32(points[next].y);
        sum += (y + nextY) * (x - nextX);
    }
    return 0.5F * sum;
}

Rect2d makeBoundingBox(const std::vector<Point2d>& points)
{
    float xMin = std::numeric_limits<float>::max();
    float yMin = std::numeric_limits<float>::max();
    float xMax = std::numeric_limits<float>::lowest();
    float yMax = std::numeric_limits<float>::lowest();

    for (const Point2d& point : points)
    {
        const float x = rustF32(point.x);
        const float y = rustF32(point.y);
        xMin = detail::MinRustF32(xMin, x);
        yMin = detail::MinRustF32(yMin, y);
        xMax = detail::MaxRustF32(xMax, x);
        yMax = detail::MaxRustF32(yMax, y);
    }
    return Rect2d{static_cast<double>(xMin), static_cast<double>(yMin),
                  static_cast<double>(xMax), static_cast<double>(yMax)};
}

Point2d makeCentroidRustF32(const std::vector<Point2d>& points, float area)
{
    float x = 0.0F;
    float y = 0.0F;
    for (std::size_t i = 0; i < points.size(); ++i)
    {
        const std::size_t next = (i + 1) % points.size();
        const float pointX = rustF32(points[i].x);
        const float pointY = rustF32(points[i].y);
        const float nextX = rustF32(points[next].x);
        const float nextY = rustF32(points[next].y);
        const float factor = pointX * nextY - nextX * pointY;
        x += (pointX + nextX) * factor;
        y += (pointY + nextY) * factor;
    }
    const float denominator = 6.0F * area;
    return Point2d{static_cast<double>(x / denominator),
                   static_cast<double>(y / denominator)};
}

float rustCross(const Point2d& origin,
                const Point2d& first,
                const Point2d& second) noexcept
{
    const float firstX = rustF32(first.x) - rustF32(origin.x);
    const float firstY = rustF32(first.y) - rustF32(origin.y);
    const float secondX = rustF32(second.x) - rustF32(origin.x);
    const float secondY = rustF32(second.y) - rustF32(origin.y);
    return firstX * secondY - firstY * secondX;
}

std::vector<Point2d> convexHullRustF32(std::vector<Point2d> points)
{
    if (points.size() <= 1U)
        return points;

    std::stable_sort(points.begin(), points.end(), [](const Point2d& left,
                                                      const Point2d& right) {
        return rustF32(left.x) < rustF32(right.x);
    });

    const auto grow = [](std::vector<Point2d> hull,
                         const Point2d& next) {
        while (hull.size() >= 2U &&
               rustCross(hull[hull.size() - 2U], hull.back(), next) <= 0.0F)
        {
            hull.pop_back();
        }
        hull.push_back(next);
        return hull;
    };

    std::vector<Point2d> lower;
    for (const Point2d& point : points)
        lower = grow(std::move(lower), point);

    std::vector<Point2d> upper;
    for (auto it = points.rbegin(); it != points.rend(); ++it)
        upper = grow(std::move(upper), *it);

    lower.pop_back();
    upper.pop_back();
    lower.insert(lower.end(), upper.begin(), upper.end());
    return lower;
}

float makeDiameterRustF32(const std::vector<Point2d>& points) noexcept
{
    const std::vector<Point2d> hull = convexHullRustF32(points);
    float maxSquaredDistance = 0.0F;
    for (std::size_t i = 0; i < hull.size(); ++i)
    {
        for (std::size_t j = i + 1; j < hull.size(); ++j)
        {
            const float dx = rustF32(hull[i].x) - rustF32(hull[j].x);
            const float dy = rustF32(hull[i].y) - rustF32(hull[j].y);
            maxSquaredDistance = std::max(maxSquaredDistance,
                                          dx * dx + dy * dy);
        }
    }
    return std::sqrt(maxSquaredDistance);
}

bool hasSelfIntersection(const std::vector<Point2d>& points) noexcept
{
    const std::size_t count = points.size();
    for (std::size_t i = 0; i < count; ++i)
    {
        const std::size_t iNext = (i + 1) % count;
        const auto first = Edge2d::Create(points[i], points[iNext]);
        if (!first.HasValue())
            return true;

        for (std::size_t j = i + 1; j < count; ++j)
        {
            const std::size_t jNext = (j + 1) % count;
            if (i == j || iNext == j || jNext == i)
                continue;

            const auto second = Edge2d::Create(points[j], points[jNext]);
            if (!second.HasValue() ||
                first.Value().IntersectsRustF32(second.Value()))
                return true;
        }
    }
    return false;
}

}

SimplePolygon::SimplePolygon(std::vector<Point2d> points,
                             Rect2d boundingBox,
                             Point2d centroid,
                             double area,
                             double diameter)
    : m_Vertices(std::move(points)),
      m_BoundingBox(boundingBox),
      m_Centroid(centroid),
      m_Area(area),
      m_Diameter(diameter)
{
}

JAGUARCORE_API Result<SimplePolygon> SimplePolygon::Create(
    std::vector<Point2d> points, NumericPolicy policy)
{
    if (!policy.IsValid())
        return Result<SimplePolygon>::FromError(
            {ErrorCode::InvalidInput, "numeric policy contains an invalid tolerance"});

    if (points.size() < 3)
        return Result<SimplePolygon>::FromError(
            {ErrorCode::InvalidPolygon, "simple polygon needs at least three points"});

    for (const Point2d& point : points)
    {
        if (!point.IsFinite())
            return Result<SimplePolygon>::FromError(
                {ErrorCode::InvalidInput, "polygon contains a non-finite point"});
    }

    if (!normalizeRustF32(points))
        return Result<SimplePolygon>::FromError(
            {ErrorCode::InvalidInput,
             "polygon contains a value outside the f32 numeric range"});

    for (std::size_t i = 0; i < points.size(); ++i)
    {
        for (std::size_t j = i + 1; j < points.size(); ++j)
        {
            if (points[i] == points[j])
                return Result<SimplePolygon>::FromError(
                    {ErrorCode::InvalidPolygon, "simple polygon contains duplicate points"});
        }
    }

    if (hasSelfIntersection(points))
        return Result<SimplePolygon>::FromError(
            {ErrorCode::SelfIntersection, "simple polygon contains intersecting edges"});

    const float rawArea = signedAreaRustF32(points);
    if (!std::isfinite(rawArea))
        return Result<SimplePolygon>::FromError(
            {ErrorCode::InvalidInput, "polygon area is non-finite"});
    if (std::abs(rawArea) <= policy.areaTolerance)
        return Result<SimplePolygon>::FromError(
            {ErrorCode::ZeroArea, "simple polygon has no area"});

    if (rawArea < 0.0F)
        std::reverse(points.begin(), points.end());

    const float area = std::abs(rawArea);
    const Rect2d boundingBox = makeBoundingBox(points);
    const Point2d centroid = makeCentroidRustF32(points, area);
    const float diameter = makeDiameterRustF32(points);
    if (!std::isfinite(area) ||
        !std::isfinite(boundingBox.xMin) || !std::isfinite(boundingBox.yMin) ||
        !std::isfinite(boundingBox.xMax) || !std::isfinite(boundingBox.yMax) ||
        !std::isfinite(diameter))
    {
        return Result<SimplePolygon>::FromError(
            {ErrorCode::InvalidInput, "polygon geometry contains a non-finite value"});
    }
    SimplePolygon polygon{std::move(points), boundingBox, centroid, area, diameter};
    const auto poi = ComputePointOfInaccessibility(polygon);
    if (!poi.HasValue())
        return Result<SimplePolygon>::FromError(poi.GetError());
    polygon.m_PointOfInaccessibility = poi.Value();
    return Result<SimplePolygon>::FromValue(std::move(polygon));
}

JAGUARCORE_API const std::vector<Point2d>& SimplePolygon::Vertices() const noexcept
{
    return m_Vertices;
}

JAGUARCORE_API const Rect2d& SimplePolygon::BoundingBox() const noexcept
{
    return m_BoundingBox;
}

JAGUARCORE_API Point2d SimplePolygon::Centroid() const noexcept
{
    return m_Centroid;
}

JAGUARCORE_API double SimplePolygon::Area() const noexcept
{
    return m_Area;
}

JAGUARCORE_API double SimplePolygon::Diameter() const noexcept
{
    return m_Diameter;
}

JAGUARCORE_API std::size_t SimplePolygon::VertexCount() const noexcept
{
    return m_Vertices.size();
}

JAGUARCORE_API const Circle2d& SimplePolygon::PointOfInaccessibility() const noexcept
{
    return m_PointOfInaccessibility;
}

JAGUARCORE_API std::vector<Edge2d> SimplePolygon::Edges() const
{
    std::vector<Edge2d> result;
    result.reserve(m_Vertices.size());
    for (std::size_t i = 0; i < m_Vertices.size(); ++i)
    {
        const auto edge = Edge2d::Create(m_Vertices[i], m_Vertices[(i + 1) % m_Vertices.size()]);
        if (edge.HasValue())
            result.push_back(edge.Value());
    }
    return result;
}

JAGUARCORE_API Result<SimplePolygon> SimplePolygon::Transformed(
    const Transform2d& transform) const
{
    if (!transform.IsFinite())
    {
        return Result<SimplePolygon>::FromError(
            {ErrorCode::InvalidInput, "transform contains a non-finite value"});
    }

    std::vector<Point2d> transformedPoints;
    transformedPoints.reserve(m_Vertices.size());
    for (const Point2d& point : m_Vertices)
        transformedPoints.push_back(transform.Apply(point));

    const auto result = SimplePolygon::Create(std::move(transformedPoints));
    if (!result.HasValue())
        return Result<SimplePolygon>::FromError(result.GetError());

    SimplePolygon transformedPolygon = result.Value();
    if (m_Surrogate)
    {
        const auto transformedSurrogate = m_Surrogate->Transformed(transform);
        if (!transformedSurrogate.HasValue())
            return Result<SimplePolygon>::FromError(transformedSurrogate.GetError());
        transformedPolygon.m_Surrogate = std::make_shared<const Surrogate>(
            transformedSurrogate.Value());
    }
    return Result<SimplePolygon>::FromValue(std::move(transformedPolygon));
}

JAGUARCORE_API Result<SimplePolygon> SimplePolygon::TransformedRustF32(
    const Transform2d& transform) const
{
    if (!transform.IsFinite())
    {
        return Result<SimplePolygon>::FromError(
            {ErrorCode::InvalidInput, "transform contains a non-finite value"});
    }

    SimplePolygon transformedPolygon = *this;
    for (Point2d& point : transformedPolygon.m_Vertices)
        point = transform.ApplyRustF32(point);

    const Rect2d boundingBox = makeBoundingBox(transformedPolygon.m_Vertices);
    const auto validBoundingBox = Rect2d::Create(
        boundingBox.xMin, boundingBox.yMin, boundingBox.xMax, boundingBox.yMax);
    if (!validBoundingBox.HasValue())
        return Result<SimplePolygon>::FromError(validBoundingBox.GetError());
    transformedPolygon.m_BoundingBox = validBoundingBox.Value();
    transformedPolygon.m_Centroid = makeCentroidRustF32(
        transformedPolygon.m_Vertices, static_cast<float>(m_Area));
    transformedPolygon.m_PointOfInaccessibility.center =
        transform.ApplyRustF32(m_PointOfInaccessibility.center);
    transformedPolygon.m_PointOfInaccessibility.radius =
        m_PointOfInaccessibility.radius;
    if (m_Surrogate)
    {
        const auto transformedSurrogate =
            m_Surrogate->TransformedRustF32(transform);
        if (!transformedSurrogate.HasValue())
            return Result<SimplePolygon>::FromError(
                transformedSurrogate.GetError());
        transformedPolygon.m_Surrogate = std::make_shared<const Surrogate>(
            transformedSurrogate.Value());
    }
    return Result<SimplePolygon>::FromValue(std::move(transformedPolygon));
}

JAGUARCORE_API Result<void> SimplePolygon::GenerateSurrogate(
    const SurrogateConfig& config)
{
    if (m_Surrogate && m_Surrogate->Config() == config)
        return Result<void>::FromValue();

    const auto generated = Surrogate::Create(*this, config);
    if (!generated.HasValue())
        return Result<void>::FromError(generated.GetError());

    m_Surrogate = std::make_shared<const Surrogate>(generated.Value());
    return Result<void>::FromValue();
}

JAGUARCORE_API const Surrogate* SimplePolygon::GetSurrogate() const noexcept
{
    return m_Surrogate.get();
}

JAGUARCORE_API bool SimplePolygon::Contains(const Point2d& point, double tolerance) const noexcept
{
    if (point.x < m_BoundingBox.xMin - tolerance ||
        point.x > m_BoundingBox.xMax + tolerance ||
        point.y < m_BoundingBox.yMin - tolerance ||
        point.y > m_BoundingBox.yMax + tolerance)
        return false;

    const auto polygonEdges = Edges();
    for (const Edge2d& edge : polygonEdges)
    {
        const Point2d closest = edge.ClosestPoint(point);
        if (closest.SquaredDistanceTo(point) <= tolerance * tolerance)
            return true;
    }

    bool inside = false;
    for (std::size_t i = 0, j = m_Vertices.size() - 1; i < m_Vertices.size(); j = i++)
    {
        const Point2d& current = m_Vertices[i];
        const Point2d& previous = m_Vertices[j];
        const bool crosses = ((current.y > point.y) != (previous.y > point.y));
        if (!crosses)
            continue;

        const double xIntersection =
            (previous.x - current.x) * (point.y - current.y) /
                (previous.y - current.y) +
            current.x;
        if (point.x < xIntersection)
            inside = !inside;
    }
    return inside;
}

JAGUARCORE_API bool SimplePolygon::ContainsRustF32(
    const Point2d& point) const noexcept
{
    const float xMin = static_cast<float>(m_BoundingBox.xMin);
    const float yMin = static_cast<float>(m_BoundingBox.yMin);
    const float xMax = static_cast<float>(m_BoundingBox.xMax);
    const float yMax = static_cast<float>(m_BoundingBox.yMax);
    const float pointX = static_cast<float>(point.x);
    const float pointY = static_cast<float>(point.y);
    if (pointX < xMin || pointX > xMax || pointY < yMin || pointY > yMax)
        return false;

    const float pointOutsideX = xMax + (xMax - xMin);
    const Edge2d ray{Point2d{static_cast<double>(pointX),
                             static_cast<double>(pointY)},
                     Point2d{static_cast<double>(pointOutsideX),
                             static_cast<double>(pointY)}};
    bool intersections = false;
    for (std::size_t index = 0U; index < m_Vertices.size(); ++index)
    {
        const std::size_t next = (index + 1U) % m_Vertices.size();
        const float startX = static_cast<float>(m_Vertices[index].x);
        const float startY = static_cast<float>(m_Vertices[index].y);
        const float endX = static_cast<float>(m_Vertices[next].x);
        const float endY = static_cast<float>(m_Vertices[next].y);
        if ((detail::AlmostEqualRustF32(startY, pointY) &&
             detail::GreaterRustF32(startX, pointX))
            || (detail::AlmostEqualRustF32(endY, pointY) &&
                detail::GreaterRustF32(endX, pointX)))
        {
            if (detail::LessRustF32(startY, pointY) ||
                detail::LessRustF32(endY, pointY))
                intersections = !intersections;
        }
        else if (ray.IntersectsRustF32(Edge2d{
                     Point2d{static_cast<double>(startX),
                             static_cast<double>(startY)},
                     Point2d{static_cast<double>(endX),
                             static_cast<double>(endY)}}))
        {
            intersections = !intersections;
        }
    }
    return intersections;
}

JAGUARCORE_API bool SimplePolygon::Intersects(const Edge2d& edge, double tolerance) const noexcept
{
    if (!m_BoundingBox.Intersects(edge.BoundingBox()))
        return false;

    for (const Edge2d& polygonEdge : Edges())
    {
        if (polygonEdge.Intersects(edge, tolerance))
            return true;
    }
    return Contains(edge.start, tolerance) || Contains(edge.end, tolerance);
}

JAGUARCORE_API bool SimplePolygon::IntersectsRustF32(
    const Edge2d& edge) const noexcept
{
    const Rect2d firstBox = BoundingBox();
    const Rect2d secondBox = edge.BoundingBox();
    if (static_cast<float>(firstBox.xMax) < static_cast<float>(secondBox.xMin)
        || static_cast<float>(secondBox.xMax) < static_cast<float>(firstBox.xMin)
        || static_cast<float>(firstBox.yMax) < static_cast<float>(secondBox.yMin)
        || static_cast<float>(secondBox.yMax) < static_cast<float>(firstBox.yMin))
    {
        return false;
    }
    for (const Edge2d& polygonEdge : Edges())
    {
        if (polygonEdge.IntersectsRustF32(edge))
            return true;
    }
    return ContainsRustF32(edge.start) || ContainsRustF32(edge.end);
}

JAGUARCORE_API double SimplePolygon::DistanceTo(const Point2d& point) const noexcept
{
    if (Contains(point))
        return 0.0;

    double distanceSquared = std::numeric_limits<double>::max();
    for (const Edge2d& edge : Edges())
    {
        const Point2d closest = edge.ClosestPoint(point);
        distanceSquared = std::min(distanceSquared, closest.SquaredDistanceTo(point));
    }
    return std::sqrt(distanceSquared);
}

JAGUARCORE_API double SimplePolygon::DistanceToRustF32(
    const Point2d& point) const noexcept
{
    return static_cast<double>(std::sqrt(static_cast<float>(
        SquaredDistanceToRustF32(point))));
}

JAGUARCORE_API double SimplePolygon::SquaredDistanceToRustF32(
    const Point2d& point) const noexcept
{
    if (ContainsRustF32(point))
        return 0.0;

    float distanceSquared = std::numeric_limits<float>::max();
    for (const Edge2d& edge : Edges())
    {
        distanceSquared = std::min(
            distanceSquared,
            static_cast<float>(edge.SquaredDistanceToRustF32(point)));
    }
    return static_cast<double>(distanceSquared);
}

JAGUARCORE_API SeparationDistance2d SimplePolygon::SeparationDistanceRustF32(
    const Point2d& point) const noexcept
{
    const SeparationDistance2d separation =
        SquaredSeparationDistanceRustF32(point);
    return {separation.first,
            static_cast<double>(std::sqrt(static_cast<float>(separation.second)))};
}

JAGUARCORE_API SeparationDistance2d
SimplePolygon::SquaredSeparationDistanceRustF32(const Point2d& point) const noexcept
{
    float distanceSquared = std::numeric_limits<float>::max();
    for (const Edge2d& edge : Edges())
    {
        distanceSquared = std::min(
            distanceSquared,
            static_cast<float>(edge.SquaredDistanceToRustF32(point)));
    }
    const GeoPosition position = ContainsRustF32(point)
                                     ? GeoPosition::Interior
                                     : GeoPosition::Exterior;
    return {position, static_cast<double>(distanceSquared)};
}
}
