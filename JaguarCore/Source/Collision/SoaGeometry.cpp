#include "SoaGeometry.h"

#include <algorithm>
#include <cmath>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace jaguar
{
namespace
{
double cross(const Point2d& a, const Point2d& b, const Point2d& c) noexcept
{
    return (b.x - a.x) * (c.y - a.y) -
           (b.y - a.y) * (c.x - a.x);
}

bool onSegment(const Point2d& point,
               const Point2d& start,
               const Point2d& end,
               double tolerance) noexcept
{
    return std::abs(cross(start, end, point)) <= tolerance &&
           point.x >= std::min(start.x, end.x) - tolerance &&
           point.x <= std::max(start.x, end.x) + tolerance &&
           point.y >= std::min(start.y, end.y) - tolerance &&
           point.y <= std::max(start.y, end.y) + tolerance;
}

bool runtimeAvx2Supported() noexcept
{
#if defined(_MSC_VER) && defined(JAGUARCORE_AVX2_COMPILED)
    int cpuInfo[4] = {};
    __cpuid(cpuInfo, 0);
    if (cpuInfo[0] < 7)
        return false;

    __cpuidex(cpuInfo, 1, 0);
    const bool osxsave = (cpuInfo[2] & (1 << 27)) != 0;
    const bool avx = (cpuInfo[2] & (1 << 28)) != 0;
    if (!osxsave || !avx)
        return false;

    const unsigned __int64 xcr0 = _xgetbv(0);
    if ((xcr0 & 0x6) != 0x6)
        return false;

    __cpuidex(cpuInfo, 7, 0);
    return (cpuInfo[1] & (1 << 5)) != 0;
#else
    return false;
#endif
}
}

bool avx2Supported() noexcept
{
    static const bool supported = runtimeAvx2Supported();
    return supported;
}

PolygonSoA::PolygonSoA(const SimplePolygon& polygon)
    : m_BoundingBox(polygon.BoundingBox())
{
    const auto& vertices = polygon.Vertices();
    const std::size_t count = vertices.size();
    m_StartX.reserve(count);
    m_StartY.reserve(count);
    m_EndX.reserve(count);
    m_EndY.reserve(count);
    m_EdgeMinX.reserve(count);
    m_EdgeMaxX.reserve(count);
    m_EdgeMinY.reserve(count);
    m_EdgeMaxY.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        const Point2d& start = vertices[i];
        const Point2d& end = vertices[(i + 1) % count];
        m_StartX.push_back(start.x);
        m_StartY.push_back(start.y);
        m_EndX.push_back(end.x);
        m_EndY.push_back(end.y);
        m_EdgeMinX.push_back(std::min(start.x, end.x));
        m_EdgeMaxX.push_back(std::max(start.x, end.x));
        m_EdgeMinY.push_back(std::min(start.y, end.y));
        m_EdgeMaxY.push_back(std::max(start.y, end.y));
    }
}

std::size_t PolygonSoA::VertexCount() const noexcept
{
    return m_StartX.size();
}

std::size_t PolygonSoA::edgeCount() const noexcept
{
    return m_StartX.size();
}

Point2d PolygonSoA::vertex(std::size_t index) const noexcept
{
    return {m_StartX[index], m_StartY[index]};
}

Edge2d PolygonSoA::edge(std::size_t index) const noexcept
{
    return Edge2d{{m_StartX[index], m_StartY[index]},
                  {m_EndX[index], m_EndY[index]}};
}

const Rect2d& PolygonSoA::BoundingBox() const noexcept
{
    return m_BoundingBox;
}

bool PolygonSoA::pointOnBoundary(const Point2d& point,
                                double tolerance) const noexcept
{
    for (std::size_t i = 0; i < edgeCount(); ++i)
    {
        if (point.x < m_EdgeMinX[i] - tolerance ||
            point.x > m_EdgeMaxX[i] + tolerance ||
            point.y < m_EdgeMinY[i] - tolerance ||
            point.y > m_EdgeMaxY[i] + tolerance)
            continue;
        if (onSegment(point, {m_StartX[i], m_StartY[i]},
                      {m_EndX[i], m_EndY[i]}, tolerance))
            return true;
    }
    return false;
}

bool PolygonSoA::pointInside(const Point2d& point) const noexcept
{
    bool inside = false;
    for (std::size_t i = 0, j = edgeCount() - 1; i < edgeCount(); j = i++)
    {
        const bool crosses =
            ((m_StartY[i] > point.y) != (m_StartY[j] > point.y));
        if (!crosses)
            continue;
        const double xIntersection =
            (m_StartX[j] - m_StartX[i]) * (point.y - m_StartY[i]) /
                (m_StartY[j] - m_StartY[i]) +
            m_StartX[i];
        if (point.x < xIntersection)
            inside = !inside;
    }
    return inside;
}

bool PolygonSoA::Contains(const Point2d& point, double tolerance) const noexcept
{
    if (!m_BoundingBox.Contains(point) &&
        (point.x < m_BoundingBox.xMin - tolerance ||
         point.x > m_BoundingBox.xMax + tolerance ||
         point.y < m_BoundingBox.yMin - tolerance ||
         point.y > m_BoundingBox.yMax + tolerance))
        return false;
    return pointOnBoundary(point, tolerance) || pointInside(point);
}

bool PolygonSoA::Intersects(const Edge2d& edge, double tolerance) const noexcept
{
    return Intersects(edge, tolerance, SoaExecutionMode::Auto);
}

bool PolygonSoA::Intersects(const Edge2d& edge,
                            double tolerance,
                            SoaExecutionMode mode) const noexcept
{
    if (!m_BoundingBox.Intersects(edge.BoundingBox()))
        return false;
    if (intersectsBoundary(edge, tolerance, mode))
        return true;
    return Contains(edge.start, tolerance) || Contains(edge.end, tolerance);
}

bool PolygonSoA::intersectsBoundary(const Edge2d& edge,
                                    double tolerance) const noexcept
{
    return intersectsBoundary(edge, tolerance, SoaExecutionMode::Auto);
}

bool PolygonSoA::intersectsBoundary(const Edge2d& edge,
                                    double tolerance,
                                    SoaExecutionMode mode) const noexcept
{
    if (!m_BoundingBox.Intersects(edge.BoundingBox()))
        return false;
    if (avx2Supported() &&
        (mode == SoaExecutionMode::Avx2 || mode == SoaExecutionMode::Auto))
        return intersectsBoundaryAvx2Impl(*this, edge, tolerance);
    return intersectsBoundaryScalar(edge, tolerance);
}

bool PolygonSoA::intersectsBoundaryScalar(const Edge2d& edge,
                                          double tolerance) const noexcept
{
    for (std::size_t i = 0; i < edgeCount(); ++i)
    {
        const Rect2d edgeBounds = edge.BoundingBox();
        if (m_EdgeMaxX[i] < edgeBounds.xMin - tolerance ||
            edgeBounds.xMax < m_EdgeMinX[i] - tolerance ||
            m_EdgeMaxY[i] < edgeBounds.yMin - tolerance ||
            edgeBounds.yMax < m_EdgeMinY[i] - tolerance)
            continue;
        if (this->edge(i).Intersects(edge, tolerance))
            return true;
    }
    return false;
}

bool PolygonSoA::Intersects(const PolygonSoA& other, double tolerance) const noexcept
{
    return Intersects(other, tolerance, SoaExecutionMode::Auto);
}

bool PolygonSoA::Intersects(const PolygonSoA& other,
                            double tolerance,
                            SoaExecutionMode mode) const noexcept
{
    if (!m_BoundingBox.Intersects(other.m_BoundingBox))
        return false;
    for (std::size_t i = 0; i < edgeCount(); ++i)
    {
        const Edge2d edge = this->edge(i);
        if (other.Intersects(edge, tolerance, mode))
            return true;
    }
    return Contains(other.vertex(0)) || other.Contains(vertex(0));
}
}
