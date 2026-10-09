#include "SparrowCore/Sampling/UniformRectSampler.h"

#include <cmath>
#include <utility>

namespace
{
using UniformRectSamplerResult =
    sparrow::Result<std::optional<sparrow::UniformRectSampler>>;

UniformRectSamplerResult InvalidSampler(const char* message)
{
    return UniformRectSamplerResult::FromError(
        {jaguar::ErrorCode::InvalidInput, message});
}

UniformRectSamplerResult NoFitSampler()
{
    return UniformRectSamplerResult::FromValue(std::nullopt);
}

constexpr float TWO_PI = 6.28318530717958647692F;
constexpr std::size_t ROT_N_SAMPLES = 16U;

float SampleAxis(float minValue, float maxValue, sparrow::SparrowRng& rng) noexcept
{
    if (minValue == maxValue)
    {
        return minValue;
    }
    // Match rand 0.10's sample_single multiply-add, including upper-endpoint rounding.
    return minValue + (maxValue - minValue) * rng.Uniform01F32();
}
}

namespace sparrow
{
UniformRectSampler::UniformRectSampler(jaguar::Rect2d bbox,
                                       std::vector<RotationEntry> entries)
    : m_Bbox(bbox),
      m_Entries(std::move(entries))
{
}

Result<std::optional<UniformRectSampler>> UniformRectSampler::Create(
    jaguar::Rect2d bbox, const jaguar::Item& item)
{
    return Create(bbox, item, bbox);
}

Result<std::optional<UniformRectSampler>> UniformRectSampler::Create(
    jaguar::Rect2d sampleBbox,
    const jaguar::Item& item,
    jaguar::Rect2d containerBbox)
{
    const auto validRect = [](const jaguar::Rect2d& rect) {
        return std::isfinite(rect.xMin) && std::isfinite(rect.yMin)
            && std::isfinite(rect.xMax) && std::isfinite(rect.yMax)
            && rect.xMin <= rect.xMax && rect.yMin <= rect.yMax;
    };
    if (!validRect(sampleBbox) || !validRect(containerBbox))
    {
        return NoFitSampler();
    }

    const float sampleXMin = static_cast<float>(sampleBbox.xMin);
    const float sampleXMax = static_cast<float>(sampleBbox.xMax);
    const float sampleYMin = static_cast<float>(sampleBbox.yMin);
    const float sampleYMax = static_cast<float>(sampleBbox.yMax);
    const float containerXMin = static_cast<float>(containerBbox.xMin);
    const float containerXMax = static_cast<float>(containerBbox.xMax);
    const float containerYMin = static_cast<float>(containerBbox.yMin);
    const float containerYMax = static_cast<float>(containerBbox.yMax);
    if (!std::isfinite(sampleXMin) || !std::isfinite(sampleXMax)
        || !std::isfinite(sampleYMin) || !std::isfinite(sampleYMax)
        || !std::isfinite(containerXMin) || !std::isfinite(containerXMax)
        || !std::isfinite(containerYMin) || !std::isfinite(containerYMax))
    {
        // Rust receives these bounds as f32; an overflowing f64 -> f32 cast
        // therefore has the same no-fit result as non-finite Rust bounds.
        return NoFitSampler();
    }

    std::vector<float> rotations;
    switch (item.AllowedRotation().mode)
    {
    case jaguar::RotationMode::None:
        rotations.push_back(0.0F);
        break;
    case jaguar::RotationMode::Discrete:
        for (const double angle : item.AllowedRotation().radians)
        {
            const float value = static_cast<float>(angle);
            if (!std::isfinite(angle) || !std::isfinite(value))
            {
                return InvalidSampler("uniform sampler rotation is not finite");
            }
            rotations.push_back(value);
        }
        break;
    case jaguar::RotationMode::Continuous:
        rotations.reserve(ROT_N_SAMPLES);
        for (std::size_t index = 0U; index < ROT_N_SAMPLES; ++index)
        {
            rotations.push_back(TWO_PI
                               * static_cast<float>(index)
                               / static_cast<float>(ROT_N_SAMPLES));
        }
        break;
    }
    if (rotations.empty())
    {
        return NoFitSampler();
    }

    std::vector<RotationEntry> entries;
    entries.reserve(rotations.size());
    for (const float rotation : rotations)
    {
        const auto rotated = item.CollisionShape().TransformedRustF32(
            jaguar::Transform2d::FromRotationTranslation(
                static_cast<double>(rotation), {0.0, 0.0}));
        if (!rotated.HasValue())
        {
            return Result<std::optional<UniformRectSampler>>::FromError(
                rotated.GetError());
        }
        const jaguar::Rect2d rotatedBounds = rotated.Value().BoundingBox();
        const float feasibleXMin = containerXMin
            - static_cast<float>(rotatedBounds.xMin);
        const float feasibleXMax = containerXMax
            - static_cast<float>(rotatedBounds.xMax);
        const float feasibleYMin = containerYMin
            - static_cast<float>(rotatedBounds.yMin);
        const float feasibleYMax = containerYMax
            - static_cast<float>(rotatedBounds.yMax);
        const float xMin = std::max(feasibleXMin, sampleXMin);
        const float xMax = std::min(feasibleXMax, sampleXMax);
        const float yMin = std::max(feasibleYMin, sampleYMin);
        const float yMax = std::min(feasibleYMax, sampleYMax);
        if (std::isfinite(xMin) && std::isfinite(xMax)
            && std::isfinite(yMin) && std::isfinite(yMax)
            && xMin <= xMax && yMin <= yMax)
        {
            entries.push_back({rotation, xMin, xMax, yMin, yMax});
        }
    }
    if (entries.empty())
    {
        return NoFitSampler();
    }

    return Result<std::optional<UniformRectSampler>>::FromValue(
        UniformRectSampler{sampleBbox, std::move(entries)});
}

jaguar::Transform2d UniformRectSampler::Sample(SparrowRng& rng) const noexcept
{
    const RotationEntry& entry =
        m_Entries[rng.UniformIndex(static_cast<std::uint32_t>(m_Entries.size()))];
    const float x = SampleAxis(entry.xMin, entry.xMax, rng);
    const float y = SampleAxis(entry.yMin, entry.yMax, rng);
    return jaguar::Transform2d::FromRotationTranslation(
        static_cast<double>(entry.rotation),
        {static_cast<double>(x), static_cast<double>(y)});
}

jaguar::Rect2d UniformRectSampler::BoundingBox() const noexcept
{
    return m_Bbox;
}
}
