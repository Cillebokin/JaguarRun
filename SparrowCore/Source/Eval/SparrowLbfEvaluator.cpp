#include "SparrowCore/Eval/SparrowLbfEvaluator.h"

namespace sparrow
{
SparrowLbfEvaluator::SparrowLbfEvaluator(const jaguar::Layout& layout,
                                         const jaguar::Item& item,
                                         const jaguar::HazardFilter& filter)
    : m_Delegate(layout, item, filter)
{
}

SampleEval SparrowLbfEvaluator::Evaluate(
    const jaguar::Transform2d& transform,
    const std::optional<SampleEval>& upperBound)
{
    return m_Delegate.Evaluate(transform, upperBound);
}

std::size_t SparrowLbfEvaluator::EvaluationCount() const noexcept
{
    return m_Delegate.EvaluationCount();
}
}
