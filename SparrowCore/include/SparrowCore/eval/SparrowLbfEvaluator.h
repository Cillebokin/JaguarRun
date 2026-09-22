#pragma once

#include "SparrowCore/Eval/LbfEvaluator.h"

#include <cstddef>
#include <optional>

namespace sparrow
{
class SPARROWCORE_API SparrowLbfEvaluator final : public SampleEvaluator
{
public:
    SparrowLbfEvaluator(const jaguar::Layout& layout,
                        const jaguar::Item& item,
                        const jaguar::HazardFilter& filter);

    SampleEval Evaluate(
        const jaguar::Transform2d& transform,
        const std::optional<SampleEval>& upperBound) override;

    std::size_t EvaluationCount() const noexcept override;

private:
    LbfEvaluator m_Delegate;
};
}
