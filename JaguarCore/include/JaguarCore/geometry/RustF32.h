#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace jaguar
{
namespace detail
{
inline float ToDegreesRustF32(double radians) noexcept
{
    constexpr float degreesPerRadian =
        57.2957795130823208767981548141051703F;
    return static_cast<float>(radians) * degreesPerRadian;
}

inline float MidpointRustF32(float first, float second) noexcept
{
#if defined(_M_X64) || defined(__x86_64__)
    // Match Rust's native x86_64 f32::midpoint path and its single f32 rounding.
    return static_cast<float>(
        (static_cast<double>(first) + static_cast<double>(second)) * 0.5);
#else
    const float halfMax = std::numeric_limits<float>::max() * 0.5F;
    if (std::signbit(first) != std::signbit(second) ||
        (std::fabs(first) <= halfMax && std::fabs(second) <= halfMax))
    {
        return (first + second) * 0.5F;
    }
    return first * 0.5F + second * 0.5F;
#endif
}

inline float MinRustF32(float first, float second) noexcept
{
    return std::fmin(first, second);
}

inline float MaxRustF32(float first, float second) noexcept
{
    return std::fmax(first, second);
}

inline std::uint32_t OrderedRustF32Bits(float value) noexcept
{
    std::uint32_t bits = 0U;
    std::memcpy(&bits, &value, sizeof(bits));
    if ((bits & 0x80000000U) != 0U)
        return ~bits;
    return bits ^ 0x80000000U;
}

inline bool AlmostEqualRustF32(float first, float second) noexcept
{
    if (first == second)
        return true;
    if (std::fabs(first - second) <= std::numeric_limits<float>::epsilon())
        return true;

    const std::uint32_t firstBits = OrderedRustF32Bits(first);
    const std::uint32_t secondBits = OrderedRustF32Bits(second);
    const std::uint32_t wrappedDifference = firstBits - secondBits;
    const std::uint32_t absoluteDifference =
        (wrappedDifference & 0x80000000U) == 0U
        ? wrappedDifference
        : (wrappedDifference == 0x80000000U
               ? 0x7fffffffU
               : ~wrappedDifference + 1U);
    return absoluteDifference <= 4U;
}

inline bool LessEqualRustF32(float first, float second) noexcept
{
    return first < second || AlmostEqualRustF32(first, second);
}

inline bool LessRustF32(float first, float second) noexcept
{
    return first < second && !AlmostEqualRustF32(first, second);
}

inline bool GreaterRustF32(float first, float second) noexcept
{
    return first > second && !AlmostEqualRustF32(first, second);
}
}
}
