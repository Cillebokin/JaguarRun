#pragma once

#include "JaguarCore/Export.h"
#include "JaguarCore/Geometry/Edge.h"
#include "JaguarCore/Geometry/Rect.h"
#include "JaguarCore/Geometry/Transformation.h"
#include "JaguarCore/NumericPolicy.h"
#include "JaguarCore/Result.h"

#include <memory>
#include <vector>

namespace jaguar
{
struct SurrogateConfig;
class Surrogate;

class SimplePolygon
{
public:
    static JAGUARCORE_API Result<SimplePolygon> Create(
        std::vector<Point2d> points, NumericPolicy policy = {});

    JAGUARCORE_API const std::vector<Point2d>& Vertices() const noexcept;
    JAGUARCORE_API const Rect2d& BoundingBox() const noexcept;
    JAGUARCORE_API Point2d Centroid() const noexcept;
    JAGUARCORE_API double Area() const noexcept;
    JAGUARCORE_API double Diameter() const noexcept;
    JAGUARCORE_API std::size_t VertexCount() const noexcept;
    JAGUARCORE_API std::vector<Edge2d> Edges() const;
    JAGUARCORE_API Result<SimplePolygon> Transformed(const Transform2d& transform) const;
    JAGUARCORE_API Result<void> GenerateSurrogate(const SurrogateConfig& config);
    JAGUARCORE_API const Surrogate* GetSurrogate() const noexcept;
    JAGUARCORE_API bool Contains(
        const Point2d& point,
        double tolerance = NumericPolicy::DefaultEqualityTolerance()) const noexcept;
    JAGUARCORE_API bool Intersects(
        const Edge2d& edge,
        double tolerance = NumericPolicy::DefaultEqualityTolerance()) const noexcept;
    JAGUARCORE_API double DistanceTo(const Point2d& point) const noexcept;

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
