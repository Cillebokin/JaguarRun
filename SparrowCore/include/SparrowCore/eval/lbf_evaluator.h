#pragma once

#include "SparrowCore/export.h"
#include "SparrowCore/optimizer/sample_eval.h"

#include "JaguarCore/collision/collision_engine.h"
#include "JaguarCore/collision/hazard_filter.h"
#include "JaguarCore/entities/item.h"
#include "JaguarCore/entities/layout.h"

#include <cstddef>
#include <memory>
#include <optional>

namespace sparrow
{
class SPARROWCORE_API LbfEvaluator final : public SampleEvaluator
{
public:
    LbfEvaluator(const jaguar::Layout& layout,
                 const jaguar::Item& item,
                 const jaguar::HazardFilter& filter);

    SampleEval evaluate(
        const jaguar::Transform2d& transform,
        const std::optional<SampleEval>& upperBound) override;

    std::size_t evaluationCount() const noexcept override;

private:
    const jaguar::Item* m_Item = nullptr;
    std::unique_ptr<jaguar::CollisionEngine> m_Engine;
    jaguar::HazardFilter m_Filter;
    std::size_t m_EvaluationCount = 0;
};
}
