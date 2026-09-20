#pragma once

#include "JaguarCore/export.h"
#include "JaguarCore/geometry/edge.h"
#include "JaguarCore/geometry/rect.h"
#include "JaguarCore/geometry/transformation.h"
#include "JaguarCore/result.h"

#include <memory>
#include <vector>

namespace jaguar
{
struct SurrogateConfig;
class Surrogate;

class SimplePolygon
{
public:
    static JAGUARCORE_API Result<SimplePolygon> create(std::vector<Point2d> points);

    JAGUARCORE_API const std::vector<Point2d>& vertices() const noexcept;
    JAGUARCORE_API const Rect2d& boundingBox() const noexcept;
    JAGUARCORE_API Point2d centroid() const noexcept;
    JAGUARCORE_API double area() const noexcept;
    JAGUARCORE_API double diameter() const noexcept;
    JAGUARCORE_API std::size_t vertexCount() const noexcept;
    JAGUARCORE_API std::vector<Edge2d> edges() const;
    JAGUARCORE_API SimplePolygon transformed(const Transform2d& transform) const;
    JAGUARCORE_API Result<void> generateSurrogate(const SurrogateConfig& config);
    JAGUARCORE_API const Surrogate* surrogate() const noexcept;
    JAGUARCORE_API bool contains(const Point2d& point, double tolerance = 1.0e-9) const noexcept;
    JAGUARCORE_API bool intersects(const Edge2d& edge, double tolerance = 1.0e-9) const noexcept;
    JAGUARCORE_API double distanceTo(const Point2d& point) const noexcept;

private:
    explicit SimplePolygon(std::vector<Point2d> points,
                            Rect2d boundingBox,
                            Point2d centroid,
                            double area,
                            double diameter);

    std::vector<Point2d> m_Vertices;
    Rect2d m_BoundingBox;
    Point2d m_Centroid;
    double m_Area = 0.0;
    double m_Diameter = 0.0;
    std::shared_ptr<const Surrogate> m_Surrogate;
};
}
