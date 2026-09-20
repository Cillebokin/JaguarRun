#include "SparrowCore/eval/lbf_evaluator.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace
{
constexpr float kTwoPi = 6.28318530717958647692F;

bool finiteTransform(const jaguar::Transform2d& transform) noexcept
{
    return std::isfinite(transform.rotationRad)
        && std::isfinite(transform.translation.x)
        && std::isfinite(transform.translation.y);
}

float normalizedRotation(float rotation) noexcept
{
    const float value = std::fmod(rotation, kTwoPi);
    return value < 0.0F ? value + kTwoPi : value;
}

float shortestRotationDistance(float left, float right) noexcept
{
    const float delta = normalizedRotation(left - right);
    return std::min(delta, kTwoPi - delta);
}

std::optional<jaguar::Transform2d> legalizeTransform(
    const jaguar::Transform2d& transform, const jaguar::Item& item)
{
    if (!finiteTransform(transform))
    {
        return std::nullopt;
    }

    float rotation = static_cast<float>(transform.rotationRad);
    switch (item.allowedRotation().mode)
    {
    case jaguar::RotationMode::None:
        rotation = 0.0F;
        break;
    case jaguar::RotationMode::Discrete:
    {
        if (item.allowedRotation().radians.empty())
        {
            return std::nullopt;
        }
        float bestRotation = 0.0F;
        float bestDistance = std::numeric_limits<float>::infinity();
        for (const double allowed : item.allowedRotation().radians)
        {
            const float allowedAsFloat = static_cast<float>(allowed);
            if (!std::isfinite(allowed) || !std::isfinite(allowedAsFloat))
            {
                return std::nullopt;
            }
            const float distance = shortestRotationDistance(rotation, allowedAsFloat);
            if (distance < bestDistance)
            {
                bestDistance = distance;
                bestRotation = allowedAsFloat;
            }
        }
        rotation = bestRotation;
        break;
    }
    case jaguar::RotationMode::Continuous:
        break;
    }

    if (!std::isfinite(rotation))
    {
        return std::nullopt;
    }
    const float translationX = static_cast<float>(transform.translation.x);
    const float translationY = static_cast<float>(transform.translation.y);
    if (!std::isfinite(translationX) || !std::isfinite(translationY))
    {
        return std::nullopt;
    }
    return jaguar::Transform2d::fromRotationTranslation(
        static_cast<double>(rotation),
        {static_cast<double>(translationX), static_cast<double>(translationY)});
}

std::optional<float> lbfLoss(const jaguar::SimplePolygon& shape)
{
    const jaguar::Point2d centroid = shape.centroid();
    const jaguar::Rect2d bbox = shape.boundingBox();
    const float poiX = static_cast<float>(centroid.x);
    const float poiY = static_cast<float>(centroid.y);
    const float bboxX = static_cast<float>(bbox.xMin);
    const float bboxY = static_cast<float>(bbox.yMin);
    if (!std::isfinite(poiX) || !std::isfinite(poiY)
        || !std::isfinite(bboxX) || !std::isfinite(bboxY))
    {
        return std::nullopt;
    }
    const float value = 10.0F * (poiX + bboxX) + (poiY + bboxY);
    if (!std::isfinite(value))
    {
        return std::nullopt;
    }
    return value;
}

jaguar::HazardFilter makeEffectiveFilter(const jaguar::HazardFilter& source)
{
    const auto entityFilter = source.irrelevant;
    const auto handleFilter = source.irrelevantHandle;
    jaguar::HazardFilter effective;
    effective.irrelevant = [entityFilter](const jaguar::HazardEntity& entity) {
        if (entity.kind == jaguar::HazardKind::Hole)
        {
            return false;
        }
        return entityFilter && entityFilter(entity);
    };
    effective.irrelevantHandle = [entityFilter, handleFilter](
                                     jaguar::HazardHandle handle,
                                     const jaguar::HazardEntity& entity) {
        if (entity.kind == jaguar::HazardKind::Hole)
        {
            return false;
        }
        if (handleFilter && handleFilter(handle, entity))
        {
            return true;
        }
        return entityFilter && entityFilter(entity);
    };
    return effective;
}
}

namespace sparrow
{
LbfEvaluator::LbfEvaluator(const jaguar::Layout& layout,
                           const jaguar::Item& item,
                           const jaguar::HazardFilter& filter)
    : m_Item(&item),
      m_Filter(makeEffectiveFilter(filter))
{
    const jaguar::LayoutSnapshot snapshot = layout.save();
    m_Engine = snapshot.container.createCollisionEngine();
    if (m_Engine)
    {
        if (!m_Engine->restore(snapshot.cdeSnapshot).hasValue())
        {
            m_Engine.reset();
        }
    }
}

SampleEval LbfEvaluator::evaluate(
    const jaguar::Transform2d& transform,
    const std::optional<SampleEval>& upperBound)
{
    ++m_EvaluationCount;
    if (m_Item == nullptr || !m_Engine)
    {
        return {SampleKind::Invalid, 0.0F};
    }

    const auto legalTransform = legalizeTransform(transform, *m_Item);
    if (!legalTransform.has_value())
    {
        return {SampleKind::Invalid, 0.0F};
    }

    const jaguar::Surrogate* surrogate = m_Item->collisionShape().surrogate();
    if (surrogate == nullptr)
    {
        return {SampleKind::Invalid, 0.0F};
    }

    // Match jagua-rs/Sparrow fail-fast semantics: a surrogate hit is an
    // invalid sample; exact polygon evaluation is only reached after it clears.
    const bool surrogateMayCollide = m_Engine->detectSurrogateCollision(
        *surrogate, *legalTransform, m_Filter);
    if (surrogateMayCollide)
    {
        return {SampleKind::Invalid, 0.0F};
    }

    const jaguar::SimplePolygon transformed =
        m_Item->collisionShape().transformed(*legalTransform);
    const auto loss = lbfLoss(transformed);
    if (!loss.has_value())
    {
        return {SampleKind::Invalid, 0.0F};
    }

    if (upperBound.has_value()
        && (upperBound->kind == SampleKind::Clear
            || upperBound->kind == SampleKind::Collision)
        && std::isfinite(upperBound->loss)
        && *loss >= upperBound->loss)
    {
        return {SampleKind::Invalid, 0.0F};
    }

    if (m_Engine->detectPolygonCollision(transformed, m_Filter))
    {
        return {SampleKind::Invalid, 0.0F};
    }
    return {SampleKind::Clear, *loss};
}

std::size_t LbfEvaluator::evaluationCount() const noexcept
{
    return m_EvaluationCount;
}
}
