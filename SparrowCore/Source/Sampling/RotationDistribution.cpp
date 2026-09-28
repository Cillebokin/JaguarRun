#include "SparrowCore/Sampling/RotationDistribution.h"

#include <cmath>
#include <cstddef>
#include <utility>

namespace
{
constexpr float TWO_PI = 6.28318530717958647692F;
constexpr std::size_t ROT_N_SAMPLES = 16U;

sparrow::Result<sparrow::RotationDistribution> InvalidRotation(const char* message)
{
    return sparrow::Result<sparrow::RotationDistribution>::FromError(
        {jaguar::ErrorCode::InvalidInput, message});
}
}

namespace sparrow
{
RotationDistribution::RotationDistribution(Kind kind,
                                           std::vector<float> discreteRadians)
    : m_Kind(kind),
      m_DiscreteRadians(std::move(discreteRadians))
{
}

Result<RotationDistribution> RotationDistribution::FromItem(const jaguar::Item& item)
{
    const jaguar::RotationRange& allowedRotation = item.AllowedRotation();
    switch (allowedRotation.mode)
    {
    case jaguar::RotationMode::None:
        return Result<RotationDistribution>::FromValue(
            RotationDistribution{Kind::ROTATION_KIND_NONE, {}});
    case jaguar::RotationMode::Continuous:
    {
        std::vector<float> radians;
        radians.reserve(ROT_N_SAMPLES);
        for (std::size_t index = 0U; index < ROT_N_SAMPLES; ++index)
        {
            radians.push_back(TWO_PI * static_cast<float>(index)
                              / static_cast<float>(ROT_N_SAMPLES));
        }
        return Result<RotationDistribution>::FromValue(
            RotationDistribution{Kind::ROTATION_KIND_CONTINUOUS,
                                  std::move(radians)});
    }
    case jaguar::RotationMode::Discrete:
    {
        if (allowedRotation.radians.empty())
        {
            return InvalidRotation("discrete rotation set must not be empty");
        }

        std::vector<float> radians;
        radians.reserve(allowedRotation.radians.size());
        for (const double angle : allowedRotation.radians)
        {
            const float angleAsFloat = static_cast<float>(angle);
            if (!std::isfinite(angle) || !std::isfinite(angleAsFloat))
            {
                return InvalidRotation("discrete rotation angle must be finite");
            }
            radians.push_back(angleAsFloat);
        }
        return Result<RotationDistribution>::FromValue(
            RotationDistribution{Kind::ROTATION_KIND_DISCRETE, std::move(radians)});
    }
    }

    return InvalidRotation("unknown rotation mode");
}

float RotationDistribution::Sample(SparrowRng& rng) const noexcept
{
    switch (m_Kind)
    {
    case Kind::ROTATION_KIND_NONE:
        return 0.0F;
    case Kind::ROTATION_KIND_CONTINUOUS:
    {
        const auto index = rng.UniformIndex(
            static_cast<std::uint32_t>(m_DiscreteRadians.size()));
        return m_DiscreteRadians[index];
    }
    case Kind::ROTATION_KIND_DISCRETE:
    {
        const auto index = rng.UniformIndex(
            static_cast<std::uint32_t>(m_DiscreteRadians.size()));
        return m_DiscreteRadians[static_cast<std::size_t>(index)];
    }
    }

    return 0.0F;
}
}
