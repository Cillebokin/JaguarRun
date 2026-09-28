#pragma once

#include <cmath>

namespace jaguar
{
// 二维点
struct Point2d
{
    double x = 0.0; // X 坐标
    double y = 0.0; // Y 坐标

    // 判断坐标是否为有限值
    inline bool IsFinite() const noexcept
    { return std::isfinite(x) && std::isfinite(y); }

    // 计算到另一个点的距离平方
    inline double SquaredDistanceTo(const Point2d& other) const noexcept
    {
        const double dx = x - other.x;
        const double dy = y - other.y;
        return dx * dx + dy * dy;
    }

    inline double SquaredDistanceToRustF32(const Point2d& other) const noexcept
    {
        const float dx = static_cast<float>(x) - static_cast<float>(other.x);
        const float dy = static_cast<float>(y) - static_cast<float>(other.y);
        return static_cast<double>(dx * dx + dy * dy);
    }

    inline double DistanceToRustF32(const Point2d& other) const noexcept
    {
        return static_cast<double>(std::sqrt(static_cast<float>(
            SquaredDistanceToRustF32(other))));
    }
};

// 判断两个点是否相等
inline bool operator==(const Point2d& lhs, const Point2d& rhs) noexcept
{
    return lhs.x == rhs.x && lhs.y == rhs.y;
}

// 判断两个点是否不等
inline bool operator!=(const Point2d& lhs, const Point2d& rhs) noexcept
{
    return !(lhs == rhs);
}
}
