#include "SparrowCore/Eval/SeparationEvaluator.h"

#include <algorithm>
#include <cmath>
#include <limits>

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
    : m_Item(&item),
      m_Tracker(&tracker),
      m_MovingIndex(tracker.IndexForHandle(movingHandle))
{
    const jaguar::LayoutSnapshot snapshot = layout.Save();
    m_Engine = snapshot.container.CreateCollisionEngine();
    if (m_Engine)
    {
        if (!m_Engine->Restore(snapshot.cdeSnapshot).HasValue())
        {
            m_Engine.reset();
        }
    }
    m_Filter.irrelevantHandle = [movingHandle](
                                    jaguar::PlacedItemHandle handle,
                                    const jaguar::HazardEntity&) {
        return handle == movingHandle;
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

    const auto surrogateCollision = m_Engine->DetectSurrogateCollision(
        *surrogate, *legalTransform, m_Filter);
    if (!surrogateCollision.HasValue())
    {
        return {SampleKind::SAMPLE_KIND_INVALID, 0.0F};
    }
    const auto transformed = m_Item->CollisionShape().Transformed(*legalTransform);
    if (!transformed.HasValue())
    {
        return {SampleKind::SAMPLE_KIND_INVALID, 0.0F};
    }
    if (!surrogateCollision.Value()
        && !m_Engine->DetectPolygonCollision(transformed.Value(), m_Filter))
    {
        return {SampleKind::SAMPLE_KIND_CLEAR, 0.0F};
    }
    if (surrogateCollision.Value()
        && !m_Engine->DetectPolygonCollision(transformed.Value(), m_Filter))
    {
        return {SampleKind::SAMPLE_KIND_CLEAR, 0.0F};
    }

    const auto loss = m_Tracker->EvaluateCandidateLoss(
        *m_Item, *legalTransform, *m_MovingIndex);
    if (!loss.HasValue() || !std::isfinite(loss.Value()))
    {
        return {SampleKind::SAMPLE_KIND_INVALID, 0.0F};
    }
    if (upperBound.has_value()
        && upperBound->kind == SampleKind::SAMPLE_KIND_COLLISION
        && std::isfinite(upperBound->loss)
        && loss.Value() >= upperBound->loss)
    {
        return {SampleKind::SAMPLE_KIND_INVALID, 0.0F};
    }
    return {SampleKind::SAMPLE_KIND_COLLISION, std::max(loss.Value(), 1.0e-6F)};
}

std::size_t SeparationEvaluator::EvaluationCount() const noexcept
{
    return m_EvaluationCount;
}
}
