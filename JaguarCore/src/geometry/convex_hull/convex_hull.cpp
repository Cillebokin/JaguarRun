#include "JaguarCore/geometry/convex_hull.h"

#include <algorithm>

namespace jaguar
{
namespace
{
double cross(const Point2d& origin, const Point2d& first, const Point2d& second) noexcept
{
    return (first.x - origin.x) * (second.y - origin.y) -
           (first.y - origin.y) * (second.x - origin.x);
}
}

JAGUARCORE_API std::vector<Point2d> convexHull(std::vector<Point2d> points)
{
    if (points.size() <= 1)
        return points;

    std::sort(points.begin(), points.end(), [](const Point2d& lhs, const Point2d& rhs) {
        if (lhs.x != rhs.x)
            return lhs.x < rhs.x;
        return lhs.y < rhs.y;
    });
    points.erase(std::unique(points.begin(), points.end()), points.end());
    if (points.size() <= 2)
        return points;

    std::vector<Point2d> lower;
    for (const Point2d& point : points)
    {
        while (lower.size() >= 2 && cross(lower[lower.size() - 2], lower.back(), point) <= 0.0)
            lower.pop_back();
        lower.push_back(point);
    }

    std::vector<Point2d> upper;
    for (auto it = points.rbegin(); it != points.rend(); ++it)
    {
        while (upper.size() >= 2 &&
               cross(upper[upper.size() - 2], upper.back(), *it) <= 0.0)
            upper.pop_back();
        upper.push_back(*it);
    }

    lower.pop_back();
    upper.pop_back();
    lower.insert(lower.end(), upper.begin(), upper.end());
    return lower;
}
}
