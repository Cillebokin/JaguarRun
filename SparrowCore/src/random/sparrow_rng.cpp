#include "SparrowCore/random/sparrow_rng.h"

#include <cmath>
#include <cstdint>

namespace
{
constexpr std::uint64_t kSplitMixIncrement = 0x9E3779B97F4A7C15ULL;
constexpr std::uint64_t kSplitMixMultiplierA = 0xBF58476D1CE4E5B9ULL;
constexpr std::uint64_t kSplitMixMultiplierB = 0x94D049BB133111EBULL;
constexpr std::uint64_t kUniformMantissaScale = 1ULL << 24U;
constexpr float kTwoPi = 6.28318530717958647692F;

std::uint64_t splitMix64Next(std::uint64_t& state) noexcept
{
    state += kSplitMixIncrement;
    std::uint64_t value = state;
    value = (value ^ (value >> 30U)) * kSplitMixMultiplierA;
    value = (value ^ (value >> 27U)) * kSplitMixMultiplierB;
    return value ^ (value >> 31U);
}

std::uint64_t rotateLeft(std::uint64_t value, unsigned int shift) noexcept
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
        value = splitMix64Next(splitMixState);
    }
}

std::uint64_t SparrowRng::nextU64() noexcept
{
    const std::uint64_t result = rotateLeft(m_State[0] + m_State[3], 23U) + m_State[0];
    const std::uint64_t temporary = m_State[1] << 17U;

    m_State[2] ^= m_State[0];
    m_State[3] ^= m_State[1];
    m_State[1] ^= m_State[2];
    m_State[0] ^= m_State[3];
    m_State[2] ^= temporary;
    m_State[3] = rotateLeft(m_State[3], 45U);

    return result;
}

std::uint32_t SparrowRng::nextU32() noexcept
{
    return static_cast<std::uint32_t>(nextU64() >> 32U);
}

float SparrowRng::uniform01F32() noexcept
{
    const std::uint64_t top24Bits = nextU64() >> 40U;
    return static_cast<float>(top24Bits) / static_cast<float>(kUniformMantissaScale);
}

float SparrowRng::normal01F32()
{
    if (m_HasNormalSpare)
    {
        m_HasNormalSpare = false;
        return m_NormalSpare;
    }

    float firstUniform = 0.0F;
    do
    {
        firstUniform = uniform01F32();
    } while (firstUniform <= 0.0F);

    const float secondUniform = uniform01F32();
    const float radius = std::sqrt(-2.0F * std::log(firstUniform));
    const float angle = kTwoPi * secondUniform;

    m_NormalSpare = radius * std::sin(angle);
    m_HasNormalSpare = true;
    return radius * std::cos(angle);
}

std::array<std::uint64_t, 4> SparrowRng::state() const noexcept
{
    return m_State;
}
}
