#include "JaguarCore/geometry/shape_modification.h"

#include <algorithm>
#include <cmath>

namespace jaguar
{
JAGUARCORE_API bool shapeModificationValid(const SimplePolygon& original,
                                           const SimplePolygon& modified,
                                           ShapeModifyMode mode) noexcept
{
    const double edgeTolerance = modified.diameter() * 1.0e-6;
    for (const Point2d& point : original.vertices())
    {
        bool onModifiedEdge = false;
        for (const Edge2d& edge : modified.edges())
        {
            if (edge.closestPoint(point).squaredDistanceTo(point) <=
                edgeTolerance * edgeTolerance)
            {
                onModifiedEdge = true;
                break;
            }
        }

        if (onModifiedEdge)
            continue;

        const bool insideModified = modified.contains(point);
        if (mode == ShapeModifyMode::Inflate && !insideModified)
            return false;
        if (mode == ShapeModifyMode::Deflate && insideModified)
            return false;
    }
    return true;
}

JAGUARCORE_API Result<SimplePolygon> simplifyShape(const SimplePolygon& shape,
                                                   ShapeModifyMode mode,
                                                   double maxAreaChangeRatio)
{
    if (!std::isfinite(maxAreaChangeRatio) || maxAreaChangeRatio < 0.0)
    {
        return Result<SimplePolygon>::fromError(
            {ErrorCode::InvalidInput, "invalid simplification tolerance"});
    }

    std::vector<Point2d> points = shape.vertices();
    const double originalArea = shape.area();
    const double maxAreaChange = originalArea * maxAreaChangeRatio;

    while (points.size() > 3)
    {
        std::size_t bestIndex = points.size();
        double bestDelta = 0.0;
        bool found = false;

        for (std::size_t i = 0; i < points.size(); ++i)
        {
            std::vector<Point2d> candidate = points;
            candidate.erase(candidate.begin() + static_cast<std::ptrdiff_t>(i));

            const auto candidateShape = SimplePolygon::create(candidate);
            if (!candidateShape.hasValue())
                continue;

            if (!shapeModificationValid(shape, candidateShape.value(), mode))
                continue;

            const double delta = candidateShape.value().area() - originalArea;
            const bool preservesMode =
                mode == ShapeModifyMode::Inflate ? delta >= -1.0e-12 : delta <= 1.0e-12;
            const double absoluteDelta = std::abs(delta);
            if (!preservesMode || absoluteDelta > maxAreaChange)
                continue;

            if (!found || absoluteDelta < bestDelta)
            {
                found = true;
                bestIndex = i;
                bestDelta = absoluteDelta;
            }
        }

        if (!found)
            break;

        points.erase(points.begin() + static_cast<std::ptrdiff_t>(bestIndex));
    }

    return SimplePolygon::create(std::move(points));
}
}
