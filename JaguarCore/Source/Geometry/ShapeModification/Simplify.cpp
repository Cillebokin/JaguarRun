#include "JaguarCore/Geometry/ShapeModification.h"

#include <algorithm>
#include <cmath>

namespace jaguar
{
JAGUARCORE_API bool ShapeModificationValid(const SimplePolygon& original,
                                           const SimplePolygon& modified,
                                           ShapeModifyMode mode) noexcept
{
    const double edgeTolerance = modified.Diameter() * 1.0e-6;
    for (const Point2d& point : original.Vertices())
    {
        bool onModifiedEdge = false;
        for (const Edge2d& edge : modified.Edges())
        {
            if (edge.ClosestPoint(point).SquaredDistanceTo(point) <=
                edgeTolerance * edgeTolerance)
            {
                onModifiedEdge = true;
                break;
            }
        }

        if (onModifiedEdge)
            continue;

        const bool insideModified = modified.Contains(point);
        if (mode == ShapeModifyMode::Inflate && !insideModified)
            return false;
        if (mode == ShapeModifyMode::Deflate && insideModified)
            return false;
    }
    return true;
}

JAGUARCORE_API Result<SimplePolygon> SimplifyShape(const SimplePolygon& shape,
                                                   ShapeModifyMode mode,
                                                   double maxAreaChangeRatio)
{
    if (!std::isfinite(maxAreaChangeRatio) || maxAreaChangeRatio < 0.0)
    {
        return Result<SimplePolygon>::FromError(
            {ErrorCode::InvalidInput, "invalid simplification tolerance"});
    }

    std::vector<Point2d> points = shape.Vertices();
    const double originalArea = shape.Area();
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

            const auto candidateShape = SimplePolygon::Create(candidate);
            if (!candidateShape.HasValue())
                continue;

            if (!ShapeModificationValid(shape, candidateShape.Value(), mode))
                continue;

            const double delta = candidateShape.Value().Area() - originalArea;
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

    return SimplePolygon::Create(std::move(points));
}
}
