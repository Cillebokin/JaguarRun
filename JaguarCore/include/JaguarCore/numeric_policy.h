#pragma once

#include <cmath>

namespace jaguar
{
struct NumericPolicy
{
    double equalityTolerance = 1.0e-9;
    double geometryTolerance = 1.0e-7;
    double areaTolerance = 1.0e-9;
    double arcChordTolerance = 1.0e-3;

    bool isFinite(double value) const noexcept { return std::isfinite(value); }

    bool nearlyEqual(double lhs, double rhs) const noexcept
    {
        return std::abs(lhs - rhs) <= equalityTolerance;
    }
};
}
