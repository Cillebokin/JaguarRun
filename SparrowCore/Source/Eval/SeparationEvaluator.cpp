#include "SparrowCore/Eval/SeparationEvaluator.h"

#include <cmath>
#include <limits>
#include <memory>

namespace
{
bool IsFiniteTransform(const jaguar::Transform2d& transform) noexcept
{
    return transform.IsFinite();
}

std::optional<jaguar::Transform2d> LegalizeTransform(
    const jaguar::Transform2d& transform,
    const jaguar::Item& item)
{
    if (!IsFiniteTransform(transform))
    {
        return std::nullopt;
    }
    jaguar::Transform2d result = transform;
    switch (item.AllowedRotation().mode)
    {
    case jaguar::RotationMode::None:
        result.rotationRad = 0.0;
        break;
    case jaguar::RotationMode::Discrete:
    {
        if (item.AllowedRotation().radians.empty())
        {
            return std::nullopt;
        }
        double nearest = std::numeric_limits<double>::infinity();
        double selected = 0.0;
        for (const double allowed : item.AllowedRotation().radians)
        {
            if (!std::isfinite(allowed))
            {
                return std::nullopt;
            }
            const double distance = std::abs(result.rotationRad - allowed);
            if (distance < nearest)
            {
                nearest = distance;
                selected = allowed;
            }
        }
        result.rotationRad = selected;
        break;
    }
    case jaguar::RotationMode::Continuous:
        break;
    }
    return result.IsFinite() ? std::optional<jaguar::Transform2d>(result)
                             : std::nullopt;
}
}

namespace sparrow
{
SeparationEvaluator::SeparationEvaluator(
    const jaguar::Layout& layout,
    const jaguar::Item& item,
    jaguar::PlacedItemHandle movingHandle,
    const CollisionTracker& tracker)
    : m_Layout(&layout),
      m_Item(&item),
      m_Tracker(&tracker),
      m_MovingIndex(tracker.IndexForHandle(movingHandle))
{
    const jaguar::LayoutSnapshot snapshot = layout.Save();
    m_Engine = snapshot.container.CreateCollisionEngine();
    if (m_Engine)
    {
        for (const jaguar::LayoutPlacementSnapshot& placement : snapshot.placements)
        {
            // jagua-rs 的 PlacedItem 实体含 pk（每次放置唯一）；评估引擎
            // 是私有实例，用布局句柄+变换保证实体唯一性，同 Layout::MakeHazard。
            const jaguar::Hazard hazard{
                {jaguar::HazardKind::PlacedItem,
                 placement.placedItem.GetItemIndex(),
                 0,
                 0,
                 placement.handle,
                 placement.placedItem.Transform()},
                std::make_shared<const jaguar::SimplePolygon>(
                    placement.placedItem.CollisionShape()),
                true};
            const auto registered = m_Engine->RegisterHazard(hazard);
            if (!registered.HasValue())
            {
                m_Engine.reset();
                break;
            }
            if (placement.handle == movingHandle)
            {
                m_MovingHazardHandle = registered.Value();
            }
        }
        if (!m_MovingHazardHandle.has_value())
        {
            m_Engine.reset();
        }
    }
    const std::optional<jaguar::HazardHandle> movingHazardHandle =
        m_MovingHazardHandle;
    m_Filter.irrelevantHandle = [movingHazardHandle](
                                    jaguar::HazardHandle handle,
                                    const jaguar::HazardEntity&) {
        return movingHazardHandle.has_value()
            && handle == *movingHazardHandle;
    };
}

SampleEval SeparationEvaluator::Evaluate(
    const jaguar::Transform2d& transform,
    const std::optional<SampleEval>& upperBound)
{
    ++m_EvaluationCount;
    if (m_Item == nullptr || m_Tracker == nullptr || !m_Engine
        || !m_MovingIndex.has_value())
    {
        return {SampleKind::SAMPLE_KIND_INVALID, 0.0F};
    }
    const auto legalTransform = LegalizeTransform(transform, *m_Item);
    if (!legalTransform.has_value())
    {
        return {SampleKind::SAMPLE_KIND_INVALID, 0.0F};
    }
    const jaguar::Surrogate* surrogate = m_Item->CollisionShape().GetSurrogate();
    if (surrogate == nullptr)
    {
        return {SampleKind::SAMPLE_KIND_INVALID, 0.0F};
    }

    const auto transformed =
        m_Item->CollisionShape().TransformedRustF32(*legalTransform);
    if (!transformed.HasValue())
    {
        return {SampleKind::SAMPLE_KIND_INVALID, 0.0F};
    }

    float lossBound = std::numeric_limits<float>::infinity();
    if (upperBound.has_value())
    {
        if (upperBound->kind == SampleKind::SAMPLE_KIND_COLLISION)
        {
            lossBound = upperBound->loss;
        }
        else if (upperBound->kind == SampleKind::SAMPLE_KIND_CLEAR)
        {
            lossBound = 0.0F;
        }
    }
    float accumulatedLoss = 0.0F;
    bool lossEvaluationFailed = false;
    const auto addHazardLoss = [&](const jaguar::HazardEntity& hazard) {
        const float remaining = lossBound == std::numeric_limits<float>::infinity()
            ? std::numeric_limits<float>::infinity()
            : lossBound - accumulatedLoss;
        const auto extraLoss = m_Tracker->EvaluateCandidateHazardLoss(
            transformed.Value(), *m_MovingIndex, hazard);
        if (!extraLoss.HasValue())
        {
            lossEvaluationFailed = true;
            return true;
        }
        if (hazard.kind == jaguar::HazardKind::PlacedItem
            && !(extraLoss.Value() <= remaining))
        {
            return true;
        }
        accumulatedLoss += extraLoss.Value();
        return accumulatedLoss > lossBound;
    };

    jaguar::HazardCollector collisions;
    const auto surrogateCollision = m_Engine->CollectSurrogateCollisionsUntil(
        *surrogate,
        *legalTransform,
        collisions,
        m_Filter,
        addHazardLoss);
    if (!surrogateCollision.HasValue())
    {
        return {SampleKind::SAMPLE_KIND_INVALID, 0.0F};
    }
    const bool surrogateHit = !collisions.Empty();
    if (surrogateHit)
    {
        ++m_SurrogateRejectCount;
    }
    if (surrogateCollision.Value() || lossEvaluationFailed)
    {
        return {SampleKind::SAMPLE_KIND_INVALID, 0.0F};
    }

    jaguar::HazardCollector exactCollisions;
    const bool stoppedDuringExactCheck = m_Engine->CollectPolygonCollisionsUntil(
        transformed.Value(),
        exactCollisions,
        m_Filter,
        [&](const jaguar::HazardEntity& hazard) {
            if (collisions.ContainsEntity(hazard))
            {
                return false;
            }
            return addHazardLoss(hazard);
        });
    if (!exactCollisions.Empty())
    {
        ++m_ExactCollisionCount;
    }
    if (stoppedDuringExactCheck || lossEvaluationFailed)
    {
        return {SampleKind::SAMPLE_KIND_INVALID, 0.0F};
    }
    for (const auto& entry : exactCollisions.Entries())
    {
        collisions.Insert(entry.first, entry.second);
    }
    if (collisions.Empty())
    {
        return {SampleKind::SAMPLE_KIND_CLEAR, 0.0F};
    }
    return {SampleKind::SAMPLE_KIND_COLLISION, accumulatedLoss};
}

std::size_t SeparationEvaluator::EvaluationCount() const noexcept
{
    return m_EvaluationCount;
}

std::size_t SeparationEvaluator::SurrogateRejectCount() const noexcept
{
    return m_SurrogateRejectCount;
}

std::size_t SeparationEvaluator::ExactCollisionCount() const noexcept
{
    return m_ExactCollisionCount;
}
}
