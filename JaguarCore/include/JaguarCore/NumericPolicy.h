#pragma once

#include <cmath>

namespace jaguar
{
struct NumericPolicy
{
    inline static constexpr double DefaultEqualityTolerance() noexcept
    {
        return 1.0e-9;
    }

    inline static constexpr double DefaultGeometryTolerance() noexcept
    {
        return 1.0e-7;
    }

    inline static constexpr double DefaultAreaTolerance() noexcept
    {
        return 1.0e-9;
    }

    inline static constexpr double DefaultArcChordTolerance() noexcept
    {
        return 1.0e-3;
    }

    double equalityTolerance = DefaultEqualityTolerance();
    double geometryTolerance = DefaultGeometryTolerance();
    double areaTolerance = DefaultAreaTolerance();
    double arcChordTolerance = DefaultArcChordTolerance();

    inline bool IsValid() const noexcept
    {
        return std::isfinite(equalityTolerance) && equalityTolerance >= 0.0 &&
               std::isfinite(geometryTolerance) && geometryTolerance >= 0.0 &&
               std::isfinite(areaTolerance) && areaTolerance >= 0.0 &&
               std::isfinite(arcChordTolerance) && arcChordTolerance >= 0.0;
    }

    inline bool IsFinite(double value) const noexcept
    {
        return std::isfinite(value);
    }

    inline bool NearlyEqual(double lhs, double rhs) const noexcept
    {
        return std::abs(lhs - rhs) <= equalityTolerance;
    }
};
}
