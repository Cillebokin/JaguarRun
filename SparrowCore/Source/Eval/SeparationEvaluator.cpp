#include "SparrowCore/Eval/SeparationEvaluator.h"

#include <algorithm>
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
            const jaguar::Hazard hazard{
                {jaguar::HazardKind::PlacedItem,
                 placement.placedItem.GetItemIndex(),
                 0,
                 0},
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
    m_Filter.irrelevant = [](const jaguar::HazardEntity& entity) {
        return entity.kind == jaguar::HazardKind::Hole;
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

    jaguar::HazardCollector collisions;
    const auto surrogateCollision = m_Engine->CollectSurrogateCollisionsUntil(
        *surrogate,
        *legalTransform,
        collisions,
        m_Filter,
        [](const jaguar::HazardEntity&) { return false; });
    if (!surrogateCollision.HasValue())
    {
        return {SampleKind::SAMPLE_KIND_INVALID, 0.0F};
    }
    const bool surrogateHit = !collisions.Empty();
    const auto transformed = m_Item->CollisionShape().TransformedRustF32(*legalTransform);
    if (!transformed.HasValue())
    {
        return {SampleKind::SAMPLE_KIND_INVALID, 0.0F};
    }
    if (surrogateHit)
    {
        ++m_SurrogateRejectCount;
    }
    jaguar::HazardCollector exactCollisions;
    m_Engine->CollectPolygonCollisionsUntil(
        transformed.Value(),
        exactCollisions,
        m_Filter,
        [](const jaguar::HazardEntity&) { return false; });
    if (!exactCollisions.Empty())
    {
        ++m_ExactCollisionCount;
    }
    for (const auto& entry : exactCollisions.Entries())
    {
        collisions.Insert(entry.first, entry.second);
    }
    if (collisions.Empty())
    {
        return {SampleKind::SAMPLE_KIND_CLEAR, 0.0F};
    }
    if (upperBound.has_value()
        && upperBound->kind == SampleKind::SAMPLE_KIND_CLEAR)
    {
        return {SampleKind::SAMPLE_KIND_INVALID, 0.0F};
    }

    const auto loss = m_Tracker->EvaluateCandidateLoss(
        *m_Layout, *m_Item, *legalTransform, *m_MovingIndex);
    if (!loss.HasValue() || !std::isfinite(loss.Value()))
    {
        return {SampleKind::SAMPLE_KIND_INVALID, 0.0F};
    }
    if (upperBound.has_value()
        && upperBound->kind == SampleKind::SAMPLE_KIND_COLLISION
        && std::isfinite(upperBound->loss)
        && loss.Value() > upperBound->loss)
    {
        return {SampleKind::SAMPLE_KIND_INVALID, 0.0F};
    }
    return {SampleKind::SAMPLE_KIND_COLLISION, std::max(loss.Value(), 1.0e-6F)};
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
