#include "JaguarCore/Geometry/PointOfInaccessibility.h"

#include "JaguarCore/Geometry/SimplePolygon.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <deque>
#include <limits>
#include <optional>

namespace jaguar
{
namespace
{
struct Node
{
    Rect2d box;
    float distance = 0.0F;
    std::size_t level = 0U;
};

Rect2d InflateToSquareRustF32(const Rect2d& source) noexcept
{
    const float xMin = static_cast<float>(source.xMin);
    const float yMin = static_cast<float>(source.yMin);
    const float xMax = static_cast<float>(source.xMax);
    const float yMax = static_cast<float>(source.yMax);
    const float width = xMax - xMin;
    const float height = yMax - yMin;
    float dx = 0.0F;
    float dy = 0.0F;
    if (height < width)
        dy = (width - height) / 2.0F;
    else if (width < height)
        dx = (height - width) / 2.0F;
    return Rect2d{static_cast<double>(xMin - dx),
                  static_cast<double>(yMin - dy),
                  static_cast<double>(xMax + dx),
                  static_cast<double>(yMax + dy)};
}

float DiameterRustF32(const Rect2d& box) noexcept
{
    const float width = static_cast<float>(box.xMax) -
                        static_cast<float>(box.xMin);
    const float height = static_cast<float>(box.yMax) -
                         static_cast<float>(box.yMin);
    return std::sqrt(width * width + height * height);
}

float ClearanceRustF32(const SimplePolygon& shape,
                       const Point2d& center,
                       const std::vector<Circle2d>& excluded) noexcept
{
    float distanceToBorder = std::numeric_limits<float>::max();
    for (const Edge2d& edge : shape.Edges())
    {
        distanceToBorder = std::min(
            distanceToBorder,
            static_cast<float>(edge.DistanceToRustF32(center)));
    }

    for (const Circle2d& circle : excluded)
    {
        const float centerX = static_cast<float>(circle.center.x);
        const float centerY = static_cast<float>(circle.center.y);
        const float radius = static_cast<float>(circle.radius);
        const float x = static_cast<float>(center.x);
        const float y = static_cast<float>(center.y);
        const float dx = x - centerX;
        const float dy = y - centerY;
        const float separation = std::fabs(std::sqrt(dx * dx + dy * dy) - radius);
        distanceToBorder = std::min(distanceToBorder, separation);
    }

    bool centroidInside = shape.ContainsRustF32(center);
    for (const Circle2d& circle : excluded)
    {
        if (circle.ContainsRustF32(center))
        {
            centroidInside = false;
            break;
        }
    }
    return centroidInside ? distanceToBorder : -distanceToBorder;
}
}

JAGUARCORE_API Result<Circle2d> ComputePointOfInaccessibility(
    const SimplePolygon& shape,
    const std::vector<Circle2d>& excluded)
{
    const Rect2d root = InflateToSquareRustF32(shape.BoundingBox());
    const Point2d rootCenter = root.CentroidRustF32();
    const float rootRadius = DiameterRustF32(root) / 2.0F;

    std::deque<Node> queue;
    queue.push_back(Node{root,
                         ClearanceRustF32(shape, rootCenter, excluded),
                         10U});

    std::optional<Circle2d> best;
    while (!queue.empty())
    {
        const Node current = queue.front();
        queue.pop_front();
        const double bestRadius = best.has_value() ? best->radius : 0.0;
        const Point2d center = current.box.CentroidRustF32();
        if (static_cast<double>(current.distance) > bestRadius)
        {
            const auto candidate = Circle2d::Create(
                center, static_cast<double>(current.distance));
            if (!candidate.HasValue())
                return Result<Circle2d>::FromError(candidate.GetError());
            best = candidate.Value();
        }

        const float radius = current.level == 10U
            ? rootRadius
            : DiameterRustF32(current.box) / 2.0F;
        if (static_cast<double>(current.distance + radius) <= bestRadius
            || current.level == 0U)
        {
            continue;
        }

        const auto quadrants = current.box.QuadrantsRustF32();
        // jagua-rs Rect::quadrants() is ordered [top-right, top-left,
        // bottom-left, bottom-right]. Preserve that FIFO order.
        for (const Rect2d& quadrant : quadrants)
        {
            queue.push_back(Node{quadrant,
                                 ClearanceRustF32(
                                     shape, quadrant.CentroidRustF32(), excluded),
                                 current.level - 1U});
        }
    }

    if (!best.has_value())
    {
        return Result<Circle2d>::FromError(
            {ErrorCode::InvalidPolygon,
             "point of inaccessibility could not be found"});
    }
    return Result<Circle2d>::FromValue(*best);
}
}
