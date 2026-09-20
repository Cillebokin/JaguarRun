#include "JaguarCore/geometry/simple_polygon.h"

#include "JaguarCore/geometry/convex_hull.h"
#include "JaguarCore/fail_fast/surrogate.h"

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
    const std::vector<Point2d> hull = convexHull(points);
    double maxSquaredDistance = 0.0;
    for (std::size_t i = 0; i < hull.size(); ++i)
    {
        for (std::size_t j = i + 1; j < hull.size(); ++j)
            maxSquaredDistance = std::max(maxSquaredDistance, hull[i].squaredDistanceTo(hull[j]));
    }
    return std::sqrt(maxSquaredDistance);
}

bool hasSelfIntersection(const std::vector<Point2d>& points) noexcept
{
    const std::size_t count = points.size();
    for (std::size_t i = 0; i < count; ++i)
    {
        const std::size_t iNext = (i + 1) % count;
        const auto first = Edge2d::create(points[i], points[iNext]);
        if (!first.hasValue())
            return true;

        for (std::size_t j = i + 1; j < count; ++j)
        {
            const std::size_t jNext = (j + 1) % count;
            if (i == j || iNext == j || jNext == i)
                continue;

            const auto second = Edge2d::create(points[j], points[jNext]);
            if (!second.hasValue() || first.value().intersects(second.value()))
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

JAGUARCORE_API Result<SimplePolygon> SimplePolygon::create(std::vector<Point2d> points)
{
    if (points.size() < 3)
        return Result<SimplePolygon>::fromError(
            {ErrorCode::InvalidPolygon, "simple polygon needs at least three points"});

    for (const Point2d& point : points)
    {
        if (!point.isFinite())
            return Result<SimplePolygon>::fromError(
                {ErrorCode::InvalidInput, "polygon contains a non-finite point"});
    }

    for (std::size_t i = 0; i < points.size(); ++i)
    {
        for (std::size_t j = i + 1; j < points.size(); ++j)
        {
            if (points[i] == points[j])
                return Result<SimplePolygon>::fromError(
                    {ErrorCode::InvalidPolygon, "simple polygon contains duplicate points"});
        }
    }

    if (hasSelfIntersection(points))
        return Result<SimplePolygon>::fromError(
            {ErrorCode::SelfIntersection, "simple polygon contains intersecting edges"});

    const double rawArea = signedArea(points);
    if (std::abs(rawArea) <= 1.0e-12)
        return Result<SimplePolygon>::fromError(
            {ErrorCode::ZeroArea, "simple polygon has no area"});

    if (rawArea < 0.0)
        std::reverse(points.begin(), points.end());

    const double area = std::abs(rawArea);
    const Rect2d boundingBox = makeBoundingBox(points);
    const Point2d centroid = makeCentroid(points, area);
    const double diameter = makeDiameter(points);
    return Result<SimplePolygon>::fromValue(
        SimplePolygon{std::move(points), boundingBox, centroid, area, diameter});
}

JAGUARCORE_API const std::vector<Point2d>& SimplePolygon::vertices() const noexcept
{
    return m_Vertices;
}

JAGUARCORE_API const Rect2d& SimplePolygon::boundingBox() const noexcept
{
    return m_BoundingBox;
}

JAGUARCORE_API Point2d SimplePolygon::centroid() const noexcept
{
    return m_Centroid;
}

JAGUARCORE_API double SimplePolygon::area() const noexcept
{
    return m_Area;
}

JAGUARCORE_API double SimplePolygon::diameter() const noexcept
{
    return m_Diameter;
}

JAGUARCORE_API std::size_t SimplePolygon::vertexCount() const noexcept
{
    return m_Vertices.size();
}

JAGUARCORE_API std::vector<Edge2d> SimplePolygon::edges() const
{
    std::vector<Edge2d> result;
    result.reserve(m_Vertices.size());
    for (std::size_t i = 0; i < m_Vertices.size(); ++i)
    {
        const auto edge = Edge2d::create(m_Vertices[i], m_Vertices[(i + 1) % m_Vertices.size()]);
        if (edge.hasValue())
            result.push_back(edge.value());
    }
    return result;
}

JAGUARCORE_API SimplePolygon SimplePolygon::transformed(const Transform2d& transform) const
{
    std::vector<Point2d> transformedPoints;
    transformedPoints.reserve(m_Vertices.size());
    for (const Point2d& point : m_Vertices)
        transformedPoints.push_back(transform.apply(point));

    const auto result = SimplePolygon::create(std::move(transformedPoints));
    SimplePolygon transformedPolygon = result.value();
    if (m_Surrogate)
    {
        transformedPolygon.m_Surrogate = std::make_shared<const Surrogate>(
            m_Surrogate->transformed(transform));
    }
    return transformedPolygon;
}

JAGUARCORE_API Result<void> SimplePolygon::generateSurrogate(
    const SurrogateConfig& config)
{
    if (m_Surrogate && m_Surrogate->config() == config)
        return Result<void>::fromValue();

    const auto generated = Surrogate::create(*this, config);
    if (!generated.hasValue())
        return Result<void>::fromError(generated.error());

    m_Surrogate = std::make_shared<const Surrogate>(generated.value());
    return Result<void>::fromValue();
}

JAGUARCORE_API const Surrogate* SimplePolygon::surrogate() const noexcept
{
    return m_Surrogate.get();
}

JAGUARCORE_API bool SimplePolygon::contains(const Point2d& point, double tolerance) const noexcept
{
    if (point.x < m_BoundingBox.xMin - tolerance ||
        point.x > m_BoundingBox.xMax + tolerance ||
        point.y < m_BoundingBox.yMin - tolerance ||
        point.y > m_BoundingBox.yMax + tolerance)
        return false;

    const auto polygonEdges = edges();
    for (const Edge2d& edge : polygonEdges)
    {
        const Point2d closest = edge.closestPoint(point);
        if (closest.squaredDistanceTo(point) <= tolerance * tolerance)
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

JAGUARCORE_API bool SimplePolygon::intersects(const Edge2d& edge, double tolerance) const noexcept
{
    if (!m_BoundingBox.intersects(edge.boundingBox()))
        return false;

    for (const Edge2d& polygonEdge : edges())
    {
        if (polygonEdge.intersects(edge, tolerance))
            return true;
    }
    return contains(edge.start, tolerance) || contains(edge.end, tolerance);
}

JAGUARCORE_API double SimplePolygon::distanceTo(const Point2d& point) const noexcept
{
    if (contains(point))
        return 0.0;

    double distanceSquared = std::numeric_limits<double>::max();
    for (const Edge2d& edge : edges())
    {
        const Point2d closest = edge.closestPoint(point);
        distanceSquared = std::min(distanceSquared, closest.squaredDistanceTo(point));
    }
    return std::sqrt(distanceSquared);
}
}
