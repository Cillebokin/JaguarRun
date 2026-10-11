#include "SparrowCore/Random/SparrowRng.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace
{
constexpr std::uint64_t SPLIT_MIX_INCREMENT = 0x9E3779B97F4A7C15ULL;
constexpr std::uint64_t SPLIT_MIX_MULTIPLIER_A = 0xBF58476D1CE4E5B9ULL;
constexpr std::uint64_t SPLIT_MIX_MULTIPLIER_B = 0x94D049BB133111EBULL;
constexpr std::uint32_t UNIFORM_MANTISSA_SCALE = 1U << 23U;
constexpr float TWO_PI = 6.28318530717958647692F;

std::uint64_t SplitMix64Next(std::uint64_t& state) noexcept
{
    state += SPLIT_MIX_INCREMENT;
    std::uint64_t value = state;
    value = (value ^ (value >> 30U)) * SPLIT_MIX_MULTIPLIER_A;
    value = (value ^ (value >> 27U)) * SPLIT_MIX_MULTIPLIER_B;
    return value ^ (value >> 31U);
}

std::uint64_t RotateLeft(std::uint64_t value, unsigned int shift) noexcept
{
    return (value << shift) | (value >> (64U - shift));
}
}

namespace sparrow
{
SparrowRng::SparrowRng(std::uint64_t seed) noexcept
{
    std::uint64_t splitMixState = seed;
    for (std::uint64_t& value : m_State)
    {
        value = SplitMix64Next(splitMixState);
    }
}

std::uint64_t SparrowRng::NextU64() noexcept
{
    const std::uint64_t result = RotateLeft(m_State[0] + m_State[3], 23U) + m_State[0];
    const std::uint64_t temporary = m_State[1] << 17U;

    m_State[2] ^= m_State[0];
    m_State[3] ^= m_State[1];
    m_State[1] ^= m_State[2];
    m_State[0] ^= m_State[3];
    m_State[2] ^= temporary;
    m_State[3] = RotateLeft(m_State[3], 45U);

    return result;
}

std::uint32_t SparrowRng::NextU32() noexcept
{
    return static_cast<std::uint32_t>(NextU64() >> 32U);
}

std::uint32_t SparrowRng::UniformIndex(std::uint32_t upperExclusive)
{
    if (upperExclusive == 0U)
    {
        throw std::invalid_argument("uniform index range must be non-empty");
    }

    const std::uint64_t product =
        static_cast<std::uint64_t>(NextU32()) * upperExclusive;
    std::uint32_t result = static_cast<std::uint32_t>(product >> 32U);
    const std::uint32_t low = static_cast<std::uint32_t>(product);
    const std::uint32_t threshold = 0U - upperExclusive;
    if (low > threshold)
    {
        const std::uint32_t nextHigh = static_cast<std::uint32_t>(
            (static_cast<std::uint64_t>(NextU32()) * upperExclusive) >> 32U);
        result += static_cast<std::uint32_t>(
            low > std::numeric_limits<std::uint32_t>::max() - nextHigh);
    }
    return result;
}

float SparrowRng::Uniform01F32() noexcept
{
    const std::uint32_t top23Bits = NextU32() >> 9U;
    return static_cast<float>(top23Bits) / static_cast<float>(UNIFORM_MANTISSA_SCALE);
}

float SparrowRng::Normal01F32()
{
    if (m_HasNormalSpare)
    {
        m_HasNormalSpare = false;
        return m_NormalSpare;
    }

    float firstUniform = 0.0F;
    do
    {
        firstUniform = Uniform01F32();
    } while (firstUniform <= 0.0F);

    const float secondUniform = Uniform01F32();
    const float radius = std::sqrt(-2.0F * std::log(firstUniform));
    const float angle = TWO_PI * secondUniform;

    m_NormalSpare = radius * std::sin(angle);
    m_HasNormalSpare = true;
    return radius * std::cos(angle);
}

std::array<std::uint64_t, 4> SparrowRng::State() const noexcept
{
    return m_State;
}
}
