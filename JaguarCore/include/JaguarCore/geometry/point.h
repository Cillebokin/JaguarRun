#pragma once

#include <cmath>

namespace jaguar
{
struct Point2d
{
    double x = 0.0;
    double y = 0.0;

    bool isFinite() const noexcept { return std::isfinite(x) && std::isfinite(y); }

    double squaredDistanceTo(const Point2d& other) const noexcept
    {
        const double dx = x - other.x;
        const double dy = y - other.y;
        return dx * dx + dy * dy;
    }
};

inline bool operator==(const Point2d& lhs, const Point2d& rhs) noexcept
{
    return lhs.x == rhs.x && lhs.y == rhs.y;
}

inline bool operator!=(const Point2d& lhs, const Point2d& rhs) noexcept
{
    return !(lhs == rhs);
}
}
