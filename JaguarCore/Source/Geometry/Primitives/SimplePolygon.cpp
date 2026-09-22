#include "JaguarCore/Geometry/SimplePolygon.h"

#include "JaguarCore/Geometry/ConvexHull.h"
#include "JaguarCore/FailFast/Surrogate.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace jaguar
{
namespace
{
double signedArea(const std::vector<Point2d>& points) noexcept
{
    double sum = 0.0;
    for (std::size_t i = 0; i < points.size(); ++i)
    {
        const std::size_t next = (i + 1) % points.size();
        sum += points[i].x * points[next].y - points[next].x * points[i].y;
    }
    return 0.5 * sum;
}

Rect2d makeBoundingBox(const std::vector<Point2d>& points)
{
    double xMin = std::numeric_limits<double>::max();
    double yMin = std::numeric_limits<double>::max();
    double xMax = std::numeric_limits<double>::lowest();
    double yMax = std::numeric_limits<double>::lowest();

    for (const Point2d& point : points)
    {
        xMin = std::min(xMin, point.x);
        yMin = std::min(yMin, point.y);
        xMax = std::max(xMax, point.x);
        yMax = std::max(yMax, point.y);
    }
    return Rect2d{xMin, yMin, xMax, yMax};
}

Point2d makeCentroid(const std::vector<Point2d>& points, double area)
{
    double x = 0.0;
    double y = 0.0;
    for (std::size_t i = 0; i < points.size(); ++i)
    {
        const std::size_t next = (i + 1) % points.size();
        const double factor = points[i].x * points[next].y - points[next].x * points[i].y;
        x += (points[i].x + points[next].x) * factor;
        y += (points[i].y + points[next].y) * factor;
    }
    const double denominator = 6.0 * area;
    return Point2d{x / denominator, y / denominator};
}

double makeDiameter(const std::vector<Point2d>& points) noexcept
{
    const std::vector<Point2d> hull = ConvexHull(points);
    double maxSquaredDistance = 0.0;
    for (std::size_t i = 0; i < hull.size(); ++i)
    {
        for (std::size_t j = i + 1; j < hull.size(); ++j)
            maxSquaredDistance = std::max(maxSquaredDistance, hull[i].SquaredDistanceTo(hull[j]));
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
            if (!second.HasValue() || first.Value().Intersects(second.Value()))
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

    const double rawArea = signedArea(points);
    if (!std::isfinite(rawArea))
        return Result<SimplePolygon>::FromError(
            {ErrorCode::InvalidInput, "polygon area is non-finite"});
    if (std::abs(rawArea) <= policy.areaTolerance)
        return Result<SimplePolygon>::FromError(
            {ErrorCode::ZeroArea, "simple polygon has no area"});

    if (rawArea < 0.0)
        std::reverse(points.begin(), points.end());

    const double area = std::abs(rawArea);
    const Rect2d boundingBox = makeBoundingBox(points);
    const Point2d centroid = makeCentroid(points, area);
    const double diameter = makeDiameter(points);
    if (!std::isfinite(area) || !centroid.IsFinite() ||
        !std::isfinite(boundingBox.xMin) || !std::isfinite(boundingBox.yMin) ||
        !std::isfinite(boundingBox.xMax) || !std::isfinite(boundingBox.yMax) ||
        !std::isfinite(diameter))
    {
        return Result<SimplePolygon>::FromError(
            {ErrorCode::InvalidInput, "polygon geometry contains a non-finite value"});
    }
    return Result<SimplePolygon>::FromValue(
        SimplePolygon{std::move(points), boundingBox, centroid, area, diameter});
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
}
