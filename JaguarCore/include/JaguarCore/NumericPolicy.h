#pragma once

#include <cmath>

namespace jaguar
{
// 数值计算容差策略
struct NumericPolicy
{
    // 默认数值相等判断容差
    inline static constexpr double DefaultEqualityTolerance() noexcept
    { return 1.0e-9; }

    // 默认几何计算容差
    inline static constexpr double DefaultGeometryTolerance() noexcept
    { return 1.0e-7; }

    // 默认面积判断容差
    inline static constexpr double DefaultAreaTolerance() noexcept
    { return 0.0; }

    // 默认圆弧弦长误差容差
    inline static constexpr double DefaultArcChordTolerance() noexcept
    { return 1.0e-3; }

    double equalityTolerance = DefaultEqualityTolerance(); // 数值相等判断容差
    double geometryTolerance = DefaultGeometryTolerance(); // 几何计算容差
    double areaTolerance = DefaultAreaTolerance();         // 面积判断容差
    double arcChordTolerance = DefaultArcChordTolerance(); // 圆弧弦长误差容差

    // 检查所有容差是否有效
    inline bool IsValid() const noexcept
    {
        return std::isfinite(equalityTolerance) && equalityTolerance >= 0.0 &&
               std::isfinite(geometryTolerance) && geometryTolerance >= 0.0 &&
               std::isfinite(areaTolerance) && areaTolerance >= 0.0 &&
               std::isfinite(arcChordTolerance) && arcChordTolerance >= 0.0;
    }

    // 判断数值是否为有限值
    inline bool IsFinite(double value) const noexcept
    {
        return std::isfinite(value);
    }

    // 判断两个数值是否近似相等
    inline bool NearlyEqual(double lhs, double rhs) const noexcept
    {
        return std::abs(lhs - rhs) <= equalityTolerance;
    }
};
}
