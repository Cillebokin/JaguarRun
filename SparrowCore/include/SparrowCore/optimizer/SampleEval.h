#pragma once

#include "SparrowCore/Export.h"

#include "JaguarCore/Geometry/RustF32.h"
#include "JaguarCore/Geometry/Transformation.h"

#include <cstddef>
#include <cmath>
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
    return jaguar::detail::AlmostEqualRustF32(left, right);
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
    const bool approximatelyEqual =
        RustApproxEqualF32(left.loss, right.loss);
    if (!approximatelyEqual &&
        (std::isnan(left.loss) || std::isnan(right.loss)))
    {
        throw std::domain_error("SampleEval comparison requires non-NaN losses");
    }
    return !approximatelyEqual && left.loss < right.loss;
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
