#pragma once

#include "SparrowCore/export.h"

#include "JaguarCore/geometry/transformation.h"

#include <cstddef>
#include <cmath>
#include <optional>

namespace sparrow
{
enum class SampleKind
{
    Invalid,
    Collision,
    Clear,
};

struct SampleEval
{
    SampleKind kind = SampleKind::Invalid;
    float loss = 0.0F;
};

inline int sampleKindRank(SampleKind kind) noexcept
{
    switch (kind)
    {
    case SampleKind::Clear:
        return 0;
    case SampleKind::Collision:
        return 1;
    case SampleKind::Invalid:
        return 2;
    }
    return 2;
}

inline bool betterSample(const SampleEval& left, const SampleEval& right) noexcept
{
    const int leftRank = sampleKindRank(left.kind);
    const int rightRank = sampleKindRank(right.kind);
    if (leftRank != rightRank)
    {
        return leftRank < rightRank;
    }
    if (left.kind == SampleKind::Invalid)
    {
        return false;
    }
    if (std::isnan(left.loss))
    {
        return false;
    }
    if (std::isnan(right.loss))
    {
        return true;
    }
    return left.loss < right.loss;
}

inline bool operator<(const SampleEval& left, const SampleEval& right) noexcept
{
    return betterSample(left, right);
}

inline bool operator==(const SampleEval& left, const SampleEval& right) noexcept
{
    if (left.kind != right.kind)
    {
        return false;
    }
    if (left.kind == SampleKind::Invalid)
    {
        return true;
    }
    if (std::isnan(left.loss) || std::isnan(right.loss))
    {
        return std::isnan(left.loss) && std::isnan(right.loss);
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

    virtual SampleEval evaluate(
        const jaguar::Transform2d& transform,
        const std::optional<SampleEval>& upperBound) = 0;

    virtual std::size_t evaluationCount() const noexcept = 0;
};
}
