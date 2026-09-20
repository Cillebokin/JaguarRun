#pragma once

#include "JaguarCore/geometry/edge.h"
#include "JaguarCore/geometry/rect.h"
#include "JaguarCore/geometry/simple_polygon.h"

#include <cstddef>
#include <vector>

namespace jaguar
{
class PolygonSoA;

bool intersectsBoundaryAvx2Impl(const PolygonSoA& polygon,
                                const Edge2d& edge,
                                double tolerance) noexcept;

enum class SoaExecutionMode
{
    Scalar,
    Auto,
    Avx2,
};

bool avx2Supported() noexcept;

class PolygonSoA
{
public:
    explicit PolygonSoA(const SimplePolygon& polygon);

    std::size_t vertexCount() const noexcept;
    std::size_t edgeCount() const noexcept;
    Point2d vertex(std::size_t index) const noexcept;
    Edge2d edge(std::size_t index) const noexcept;
    const Rect2d& boundingBox() const noexcept;
    bool contains(const Point2d& point, double tolerance = 1.0e-9) const noexcept;
    bool intersectsBoundary(const Edge2d& edge,
                            double tolerance = 1.0e-9) const noexcept;
    bool intersectsBoundary(const Edge2d& edge,
                            double tolerance,
                            SoaExecutionMode mode) const noexcept;
    bool intersects(const Edge2d& edge, double tolerance = 1.0e-9) const noexcept;
    bool intersects(const Edge2d& edge,
                    double tolerance,
                    SoaExecutionMode mode) const noexcept;
    bool intersects(const PolygonSoA& other, double tolerance = 1.0e-9) const noexcept;
    bool intersects(const PolygonSoA& other,
                    double tolerance,
                    SoaExecutionMode mode) const noexcept;

private:
    friend bool intersectsBoundaryAvx2Impl(const PolygonSoA& polygon,
                                           const Edge2d& edge,
                                           double tolerance) noexcept;

    bool pointOnBoundary(const Point2d& point, double tolerance) const noexcept;
    bool pointInside(const Point2d& point) const noexcept;
    bool intersectsBoundaryScalar(const Edge2d& edge,
                                  double tolerance) const noexcept;
    std::vector<double> m_StartX;
    std::vector<double> m_StartY;
    std::vector<double> m_EndX;
    std::vector<double> m_EndY;
    std::vector<double> m_EdgeMinX;
    std::vector<double> m_EdgeMaxX;
    std::vector<double> m_EdgeMinY;
    std::vector<double> m_EdgeMaxY;
    Rect2d m_BoundingBox{};
};
}
