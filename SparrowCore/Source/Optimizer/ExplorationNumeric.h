#pragma once

#include <cmath>

namespace sparrow::detail
{
inline bool IsRustF32Distinct(
    double candidateArea,
    double firstArea,
    double candidateDiameter,
    double firstDiameter) noexcept
{
    const float firstAreaF32 = static_cast<float>(firstArea);
    const float candidateAreaF32 = static_cast<float>(candidateArea);
    const float firstDiameterF32 = static_cast<float>(firstDiameter);
    const float candidateDiameterF32 = static_cast<float>(candidateDiameter);
    const float areaTolerance = firstAreaF32 * 0.01F;
    const float diameterTolerance = firstDiameterF32 * 0.01F;
    return std::abs(candidateAreaF32 - firstAreaF32) > areaTolerance
        && std::abs(candidateDiameterF32 - firstDiameterF32)
            > diameterTolerance;
}
}
