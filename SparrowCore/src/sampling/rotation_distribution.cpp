#include "SparrowCore/sampling/rotation_distribution.h"

#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>

namespace
{
constexpr float kTwoPi = 6.28318530717958647692F;

sparrow::Result<sparrow::RotationDistribution> invalidRotation(const char* message)
{
    return sparrow::Result<sparrow::RotationDistribution>::fromError(
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

Result<RotationDistribution> RotationDistribution::fromItem(const jaguar::Item& item)
{
    const jaguar::RotationRange& allowedRotation = item.allowedRotation();
    switch (allowedRotation.mode)
    {
    case jaguar::RotationMode::None:
        return Result<RotationDistribution>::fromValue(
            RotationDistribution{Kind::None, {}});
    case jaguar::RotationMode::Continuous:
        return Result<RotationDistribution>::fromValue(
            RotationDistribution{Kind::Continuous, {}});
    case jaguar::RotationMode::Discrete:
    {
        if (allowedRotation.radians.empty())
        {
            return invalidRotation("discrete rotation set must not be empty");
        }

        std::vector<float> radians;
        radians.reserve(allowedRotation.radians.size());
        for (const double angle : allowedRotation.radians)
        {
            const float angleAsFloat = static_cast<float>(angle);
            if (!std::isfinite(angle) || !std::isfinite(angleAsFloat))
            {
                return invalidRotation("discrete rotation angle must be finite");
            }
            radians.push_back(angleAsFloat);
        }
        return Result<RotationDistribution>::fromValue(
            RotationDistribution{Kind::Discrete, std::move(radians)});
    }
    }

    return invalidRotation("unknown rotation mode");
}

float RotationDistribution::sample(SparrowRng& rng) const noexcept
{
    switch (m_Kind)
    {
    case Kind::None:
        return 0.0F;
    case Kind::Continuous:
        return kTwoPi * rng.uniform01F32();
    case Kind::Discrete:
    {
        const std::uint64_t bound =
            static_cast<std::uint64_t>(m_DiscreteRadians.size());
        const std::uint64_t threshold =
            (std::numeric_limits<std::uint64_t>::max() % bound + 1U) % bound;
        std::uint64_t randomValue = 0;
        do
        {
            randomValue = rng.nextU64();
        } while (randomValue < threshold);
        const std::uint64_t index = randomValue % bound;
        return m_DiscreteRadians[static_cast<std::size_t>(index)];
    }
    }

    return 0.0F;
}
}
