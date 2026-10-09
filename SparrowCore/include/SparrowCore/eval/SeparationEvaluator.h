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
// The layout, item, and tracker must outlive evaluation and remain unchanged
// while this evaluator is in use, matching Rust's shared-borrow contract.
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
    std::size_t SurrogateRejectCount() const noexcept;
    std::size_t ExactCollisionCount() const noexcept;

private:
    const jaguar::Layout* m_Layout = nullptr;
    const jaguar::Item* m_Item = nullptr;
    const CollisionTracker* m_Tracker = nullptr;
    std::optional<std::size_t> m_MovingIndex;
    std::optional<jaguar::HazardHandle> m_MovingHazardHandle;
    jaguar::CollisionEnginePtr m_Engine;
    jaguar::HazardFilter m_Filter;
    std::size_t m_EvaluationCount = 0U;
    std::size_t m_SurrogateRejectCount = 0U;
    std::size_t m_ExactCollisionCount = 0U;
};
}
