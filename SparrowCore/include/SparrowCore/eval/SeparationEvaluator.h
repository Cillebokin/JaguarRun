#pragma once

#include "SparrowCore/Export.h"
#include "SparrowCore/Optimizer/SampleEval.h"
#include "SparrowCore/Quantify/CollisionTracker.h"

#include "JaguarCore/Collision/CollisionEngine.h"
#include "JaguarCore/Entities/Item.h"
#include "JaguarCore/Entities/Layout.h"

#include <cstddef>
#include <optional>

namespace sparrow
{
class SPARROWCORE_API SeparationEvaluator final : public SampleEvaluator
{
public:
    SeparationEvaluator(const jaguar::Layout& layout,
                        const jaguar::Item& item,
                        jaguar::PlacedItemHandle movingHandle,
                        const CollisionTracker& tracker);

    SampleEval Evaluate(
        const jaguar::Transform2d& transform,
        const std::optional<SampleEval>& upperBound) override;

    std::size_t EvaluationCount() const noexcept override;

private:
    const jaguar::Item* m_Item = nullptr;
    const CollisionTracker* m_Tracker = nullptr;
    std::optional<std::size_t> m_MovingIndex;
    jaguar::CollisionEnginePtr m_Engine;
    jaguar::HazardFilter m_Filter;
    std::size_t m_EvaluationCount = 0U;
};
}
