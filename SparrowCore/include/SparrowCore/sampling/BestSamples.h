#pragma once

#include "SparrowCore/Export.h"
#include "SparrowCore/Optimizer/SampleEval.h"

#include "JaguarCore/Geometry/Transformation.h"

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

    bool Report(const jaguar::Transform2d& transform, const SampleEval& evaluation);

    std::size_t Size() const noexcept;
    std::size_t Capacity() const noexcept;
    const std::vector<std::pair<jaguar::Transform2d, SampleEval>>& Samples() const noexcept;
    std::optional<std::pair<jaguar::Transform2d, SampleEval>> Best() const;
    SampleEval UpperBound() const noexcept;

private:
    static bool IsSimilar(const jaguar::Transform2d& left,
                        const jaguar::Transform2d& right,
                        float translationThreshold) noexcept;

    std::size_t m_Capacity = 0;
    float m_UniqueThreshold = 0.0F;
    std::vector<std::pair<jaguar::Transform2d, SampleEval>> m_Samples;
};
}
