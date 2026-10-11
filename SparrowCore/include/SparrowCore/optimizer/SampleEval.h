#pragma once

#include "SparrowCore/Export.h"

#include "JaguarCore/Geometry/Transformation.h"

#include <cstddef>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <stdexcept>

namespace sparrow
{
enum class SampleKind
{
    SAMPLE_KIND_INVALID,
    SAMPLE_KIND_COLLISION,
    SAMPLE_KIND_CLEAR,
};

struct SampleEval
{
    SampleKind kind = SampleKind::SAMPLE_KIND_INVALID;
    float loss = 0.0F;
};

inline int SampleKindRank(SampleKind kind) noexcept
{
    switch (kind)
    {
    case SampleKind::SAMPLE_KIND_CLEAR:
        return 0;
    case SampleKind::SAMPLE_KIND_COLLISION:
        return 1;
    case SampleKind::SAMPLE_KIND_INVALID:
        return 2;
    }
    return 2;
}

inline bool RustApproxEqualF32(float left, float right) noexcept
{
    if (left == right)
    {
        return true;
    }

    if (std::abs(left - right) <= std::numeric_limits<float>::epsilon())
    {
        return true;
    }

    std::uint32_t leftBits = 0U;
    std::uint32_t rightBits = 0U;
    std::memcpy(&leftBits, &left, sizeof(leftBits));
    std::memcpy(&rightBits, &right, sizeof(rightBits));
    constexpr std::uint32_t signBit = 1U << 31U;
    const std::uint32_t leftOrdered = (leftBits & signBit) != 0U
        ? ~leftBits
        : leftBits ^ signBit;
    const std::uint32_t rightOrdered = (rightBits & signBit) != 0U
        ? ~rightBits
        : rightBits ^ signBit;
    const std::uint64_t ulpDistance = leftOrdered >= rightOrdered
        ? static_cast<std::uint64_t>(leftOrdered - rightOrdered)
        : static_cast<std::uint64_t>(rightOrdered - leftOrdered);
    return ulpDistance <= 4U;
}

inline bool IsBetterSample(const SampleEval& left, const SampleEval& right)
{
    const int leftRank = SampleKindRank(left.kind);
    const int rightRank = SampleKindRank(right.kind);
    if (leftRank != rightRank)
    {
        return leftRank < rightRank;
    }
    if (left.kind == SampleKind::SAMPLE_KIND_INVALID)
    {
        return false;
    }
    if (std::isnan(left.loss) || std::isnan(right.loss))
    {
        throw std::domain_error("SampleEval comparison requires non-NaN losses");
    }
    return !RustApproxEqualF32(left.loss, right.loss)
        && left.loss < right.loss;
}

inline bool operator<(const SampleEval& left, const SampleEval& right)
{
    return IsBetterSample(left, right);
}

inline bool operator==(const SampleEval& left, const SampleEval& right) noexcept
{
    if (left.kind != right.kind)
    {
        return false;
    }
    if (left.kind == SampleKind::SAMPLE_KIND_INVALID)
    {
        return true;
    }
    return left.loss == right.loss;
}

inline bool operator!=(const SampleEval& left, const SampleEval& right) noexcept
{
    return !(left == right);
}

class SPARROWCORE_API SampleEvaluator
{
public:
    virtual ~SampleEvaluator() = default;

    virtual SampleEval Evaluate(
        const jaguar::Transform2d& transform,
        const std::optional<SampleEval>& upperBound) = 0;

    virtual std::size_t EvaluationCount() const noexcept = 0;
};
}
