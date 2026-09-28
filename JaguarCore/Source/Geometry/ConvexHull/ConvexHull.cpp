#include "JaguarCore/Geometry/ConvexHull.h"

#include <algorithm>
#include <cmath>

namespace jaguar
{
namespace
{
float rustF32(double value) noexcept
{
    return static_cast<float>(value);
}

float cross(const Point2d& origin, const Point2d& first,
            const Point2d& second) noexcept
{
    return (rustF32(first.x) - rustF32(origin.x)) *
               (rustF32(second.y) - rustF32(origin.y)) -
           (rustF32(first.y) - rustF32(origin.y)) *
               (rustF32(second.x) - rustF32(origin.x));
}

bool orderedFloatLess(float left, float right) noexcept
{
    const bool leftNan = std::isnan(left);
    const bool rightNan = std::isnan(right);
    if (leftNan)
        return false;
    if (rightNan)
        return true;
    return left < right;
}
}

JAGUARCORE_API std::vector<Point2d> ConvexHull(std::vector<Point2d> points)
{
    for (Point2d& point : points)
    {
        point.x = static_cast<double>(rustF32(point.x));
        point.y = static_cast<double>(rustF32(point.y));
    }

    if (points.empty())
        return points;

    std::stable_sort(points.begin(), points.end(), [](const Point2d& lhs,
                                                       const Point2d& rhs) {
        return orderedFloatLess(rustF32(lhs.x), rustF32(rhs.x));
    });

    std::vector<Point2d> lower;
    for (const Point2d& point : points)
    {
        while (lower.size() >= 2 &&
               cross(lower[lower.size() - 2], lower.back(), point) <= 0.0F)
            lower.pop_back();
        lower.push_back(point);
    }

    std::vector<Point2d> upper;
    for (auto it = points.rbegin(); it != points.rend(); ++it)
    {
        while (upper.size() >= 2 &&
               cross(upper[upper.size() - 2], upper.back(), *it) <= 0.0F)
            upper.pop_back();
        upper.push_back(*it);
    }

    lower.pop_back();
    upper.pop_back();
    lower.insert(lower.end(), upper.begin(), upper.end());
    return lower;
}
}
