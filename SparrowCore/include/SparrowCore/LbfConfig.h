#pragma once

#include "SparrowCore/ResultAlias.h"

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace sparrow
{
struct LbfConfig
{
    std::uint64_t seed = 0;
    std::size_t samplesPerItem = 5000;
    // Retained for source compatibility with earlier C++ search settings.
    // jagua-rs/lbf uses localSearchFraction for its local-sampling budget.
    std::size_t nCoordinateDescents = 3;
    double localSearchFraction = 0.2;
    double minTranslationStddevRatio = 0.0005;
    double maxTranslationStddevRatio = 0.01;
    double minRotationStddevRad = 0.5 * 3.14159265358979323846 / 180.0;
    double maxRotationStddevRad = 2.0 * 3.14159265358979323846 / 180.0;
    std::size_t maxWidthGrowthAttempts = 32;
    double widthGrowthRatio = 1.1;
    bool useSparrowConstructorOrder = true;

    // Match jagua-rs/lbf: n_samples is converted to f32 before multiplying
    // by ls_frac, and the result is truncated to an integer budget.
    inline std::size_t LocalSearchSampleBudget() const noexcept
    {
        const float total = static_cast<float>(samplesPerItem);
        const float requested = total * static_cast<float>(localSearchFraction);
        if (!std::isfinite(requested) || requested <= 0.0F)
        {
            return 0U;
        }
        if (requested >= total)
        {
            return samplesPerItem;
        }
        return static_cast<std::size_t>(requested);
    }

    inline std::size_t UniformSampleBudget() const noexcept
    {
        return samplesPerItem - LocalSearchSampleBudget();
    }

    inline Result<void> Validate() const
    {
        const auto invalid = [](const char* message) {
            return Result<void>::FromError({jaguar::ErrorCode::InvalidInput, message});
        };

        if (std::isnan(localSearchFraction) || localSearchFraction > 1.0)
        {
            return invalid("localSearchFraction must not be NaN or greater than one");
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

        return Result<void>::FromValue();
    }
};

struct SparrowConstructorConfig
{
    std::uint64_t seed = 0;
    std::size_t nContainerSamples = 1000;
    std::size_t nFocussedSamples = 0;
    std::size_t nCoordinateDescents = 3;
    // Zero keeps the Rust Sparrow builder's width-only termination behavior.
    // A positive cap is an optional C++ extension.
    std::size_t maxWidthGrowthAttempts = 0;
    double widthGrowthRatio = 1.2;
    double widthCeilingFactor = 2.0;

    inline Result<void> Validate() const
    {
        if (!std::isfinite(widthGrowthRatio) || widthGrowthRatio <= 1.0)
        {
            return Result<void>::FromError(
                {jaguar::ErrorCode::InvalidInput,
                 "Sparrow constructor widthGrowthRatio must be greater than one"});
        }
        if (!std::isfinite(widthCeilingFactor) || widthCeilingFactor <= 1.0)
        {
            return Result<void>::FromError(
                {jaguar::ErrorCode::InvalidInput,
                 "Sparrow constructor widthCeilingFactor must be greater than one"});
        }
        return Result<void>::FromValue();
    }
};
}
