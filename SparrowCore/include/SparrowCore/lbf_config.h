#pragma once

#include "SparrowCore/result_alias.h"

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace sparrow
{
struct LbfConfig
{
    std::uint64_t seed = 0;
    std::size_t samplesPerItem = 5000;
    double localSearchFraction = 0.2;
    double minTranslationStddevRatio = 0.0005;
    double maxTranslationStddevRatio = 0.01;
    double minRotationStddevRad = 0.5 * 3.14159265358979323846 / 180.0;
    double maxRotationStddevRad = 2.0 * 3.14159265358979323846 / 180.0;
    std::size_t maxWidthGrowthAttempts = 32;
    double widthGrowthRatio = 1.1;
    bool useSparrowConstructorOrder = true;

    Result<void> validate() const
    {
        const auto invalid = [](const char* message) {
            return Result<void>::fromError({jaguar::ErrorCode::InvalidInput, message});
        };

        if (samplesPerItem == 0)
        {
            return invalid("samplesPerItem must be positive");
        }
        if (!std::isfinite(localSearchFraction)
            || localSearchFraction < 0.0
            || localSearchFraction > 1.0)
        {
            return invalid("localSearchFraction must be finite and within [0, 1]");
        }
        if (!std::isfinite(minTranslationStddevRatio)
            || !std::isfinite(maxTranslationStddevRatio)
            || minTranslationStddevRatio < 0.0
            || maxTranslationStddevRatio < 0.0
            || minTranslationStddevRatio > maxTranslationStddevRatio)
        {
            return invalid("translation standard deviation range is invalid");
        }
        if (!std::isfinite(minRotationStddevRad)
            || !std::isfinite(maxRotationStddevRad)
            || minRotationStddevRad < 0.0
            || maxRotationStddevRad < 0.0
            || minRotationStddevRad > maxRotationStddevRad)
        {
            return invalid("rotation standard deviation range is invalid");
        }
        if (!std::isfinite(widthGrowthRatio) || widthGrowthRatio <= 1.0)
        {
            return invalid("widthGrowthRatio must be finite and greater than one");
        }

        return Result<void>::fromValue();
    }
};
}
