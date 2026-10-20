#include "SparrowCore/Random/SparrowRng.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace
{
constexpr std::uint64_t SPLIT_MIX_INCREMENT = 0x9E3779B97F4A7C15ULL;
constexpr std::uint64_t SPLIT_MIX_MULTIPLIER_A = 0xBF58476D1CE4E5B9ULL;
constexpr std::uint64_t SPLIT_MIX_MULTIPLIER_B = 0x94D049BB133111EBULL;
constexpr std::uint32_t UNIFORM_MANTISSA_SCALE = 1U << 23U;
constexpr double NORMAL_ZIGGURAT_TAIL = 3.654152885361008796;
constexpr double NORMAL_ZIGGURAT_AREA = 0.00492867323399;

double DoubleFromBits(std::uint64_t bits) noexcept
{
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

struct NormalZigguratTables
{
    std::array<double, 257U> x{};
    std::array<double, 257U> f{};
};

const NormalZigguratTables& GetNormalZigguratTables() noexcept
{
    static const NormalZigguratTables tables = [] {
        NormalZigguratTables result;
        result.x[1U] = NORMAL_ZIGGURAT_TAIL;
        for (std::size_t index = 2U; index < result.x.size(); ++index)
        {
            const double previous = result.x[index - 1U];
            result.x[index] = std::sqrt(
                -2.0 * std::log(
                    NORMAL_ZIGGURAT_AREA / previous
                    + std::exp(-previous * previous / 2.0)));
        }
        result.x[0U] =
            NORMAL_ZIGGURAT_AREA
            / std::exp(-NORMAL_ZIGGURAT_TAIL * NORMAL_ZIGGURAT_TAIL / 2.0);
        // rand_distr 0.6 stores the terminal edge as exact zero.
        result.x.back() = 0.0;
        for (std::size_t index = 0U; index < result.x.size(); ++index)
        {
            result.f[index] =
                std::exp(-result.x[index] * result.x[index] / 2.0);
        }
        return result;
    }();
    return tables;
}

double RustUniform01F64(sparrow::SparrowRng& rng) noexcept
{
    constexpr double scale = 1.0 / 9007199254740992.0;
    return static_cast<double>(rng.NextU64() >> 11U) * scale;
}

double RustOpen01F64(sparrow::SparrowRng& rng) noexcept
{
    constexpr std::uint64_t exponentOne = 0x3ff0000000000000ULL;
    const double oneToTwo = DoubleFromBits(
        exponentOne | (rng.NextU64() >> 12U));
    return oneToTwo
        - (1.0 - std::numeric_limits<double>::epsilon() / 2.0);
}

double RustStandardNormalF64(sparrow::SparrowRng& rng)
{
    constexpr std::uint64_t exponentTwo = 0x4000000000000000ULL;
    const NormalZigguratTables& tables = GetNormalZigguratTables();
    for (;;)
    {
        const std::uint64_t bits = rng.NextU64();
        const std::size_t index = static_cast<std::size_t>(bits & 0xffU);
        const double u = DoubleFromBits(exponentTwo | (bits >> 12U)) - 3.0;
        const double x = u * tables.x[index];
        if (std::abs(x) < tables.x[index + 1U])
        {
            return x;
        }
        if (index == 0U)
        {
            double tailX = 1.0;
            double tailY = 0.0;
            while (-2.0 * tailY < tailX * tailX)
            {
                tailX = std::log(RustOpen01F64(rng)) / NORMAL_ZIGGURAT_TAIL;
                tailY = std::log(RustOpen01F64(rng));
            }
            return u < 0.0 ? tailX - NORMAL_ZIGGURAT_TAIL
                           : NORMAL_ZIGGURAT_TAIL - tailX;
        }

        const double vertical = tables.f[index + 1U]
            + (tables.f[index] - tables.f[index + 1U])
                * RustUniform01F64(rng);
        if (vertical < std::exp(-x * x / 2.0))
        {
            return x;
        }
    }
}

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
    return static_cast<float>(RustStandardNormalF64(*this));
}

std::array<std::uint64_t, 4> SparrowRng::State() const noexcept
{
    return m_State;
}
}
