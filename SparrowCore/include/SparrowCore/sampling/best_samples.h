#pragma once

#include "SparrowCore/export.h"
#include "SparrowCore/optimizer/sample_eval.h"

#include "JaguarCore/geometry/transformation.h"

#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace sparrow
{
class SPARROWCORE_API BestSamples
{
public:
    BestSamples(std::size_t capacity, float uniqueThreshold);

    bool report(const jaguar::Transform2d& transform, const SampleEval& evaluation);

    std::size_t size() const noexcept;
    std::size_t capacity() const noexcept;
    std::optional<std::pair<jaguar::Transform2d, SampleEval>> best() const;
    SampleEval upperBound() const noexcept;

private:
    static bool similar(const jaguar::Transform2d& left,
                        const jaguar::Transform2d& right,
                        float translationThreshold) noexcept;

    std::size_t m_Capacity = 0;
    float m_UniqueThreshold = 0.0F;
    std::vector<std::pair<jaguar::Transform2d, SampleEval>> m_Samples;
};
}
