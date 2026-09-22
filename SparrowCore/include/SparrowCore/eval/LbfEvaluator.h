#pragma once

#include "SparrowCore/Export.h"
#include "SparrowCore/Optimizer/SampleEval.h"

#include "JaguarCore/Collision/CollisionEngine.h"
#include "JaguarCore/Collision/HazardFilter.h"
#include "JaguarCore/Entities/Item.h"
#include "JaguarCore/Entities/Layout.h"

#include <cstddef>
#include <optional>

namespace sparrow
{
class SPARROWCORE_API LbfEvaluator final : public SampleEvaluator
{
public:
    LbfEvaluator(const jaguar::Layout& layout,
                 const jaguar::Item& item,
                 const jaguar::HazardFilter& filter);

    SampleEval Evaluate(
        const jaguar::Transform2d& transform,
        const std::optional<SampleEval>& upperBound) override;

    std::size_t EvaluationCount() const noexcept override;

private:
    const jaguar::Item* m_Item = nullptr;
    jaguar::CollisionEnginePtr m_Engine;
    jaguar::HazardFilter m_Filter;
    std::size_t m_EvaluationCount = 0;
};
}
