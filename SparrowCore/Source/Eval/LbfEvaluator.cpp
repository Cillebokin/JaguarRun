#include "SparrowCore/Eval/LbfEvaluator.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace
{
constexpr float TWO_PI = 6.28318530717958647692F;

bool IsFiniteTransform(const jaguar::Transform2d& transform) noexcept
{
    return std::isfinite(transform.rotationRad)
        && std::isfinite(transform.translation.x)
        && std::isfinite(transform.translation.y);
}

float NormalizeRotation(float rotation) noexcept
{
    const float value = std::fmod(rotation, TWO_PI);
    return value < 0.0F ? value + TWO_PI : value;
}

std::optional<jaguar::Transform2d> LegalizeTransform(
    const jaguar::Transform2d& transform, const jaguar::Item& item)
{
    if (!IsFiniteTransform(transform))
    {
        return std::nullopt;
    }

    float rotation = static_cast<float>(transform.rotationRad);
    switch (item.AllowedRotation().mode)
    {
    case jaguar::RotationMode::None:
        rotation = 0.0F;
        break;
    case jaguar::RotationMode::Discrete:
    {
        if (item.AllowedRotation().radians.empty())
        {
            return std::nullopt;
        }
        // 对齐 sparrow convert_sample_to_closest_feasible
        // (sample/uniform_sampler.rs)：键为
        // normalize_rotation(rotation - allowed) ∈ [0, 2π) 的前向距离，
        // 不是环形最短距；min_by_key 平局取首个（严格 < 保留先者）。
        float bestRotation = 0.0F;
        float bestDistance = std::numeric_limits<float>::infinity();
        for (const double allowed : item.AllowedRotation().radians)
        {
            const float allowedAsFloat = static_cast<float>(allowed);
            if (!std::isfinite(allowed) || !std::isfinite(allowedAsFloat))
            {
                return std::nullopt;
            }
            const float distance = NormalizeRotation(rotation - allowedAsFloat);
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
    return jaguar::Transform2d::FromRotationTranslation(
        static_cast<double>(rotation),
        {static_cast<double>(translationX), static_cast<double>(translationY)});
}

std::optional<float> CalculateLbfLoss(const jaguar::SimplePolygon& shape)
{
    const jaguar::Point2d poi = shape.PointOfInaccessibility().center;
    const jaguar::Rect2d bbox = shape.BoundingBox();
    const float poiX = static_cast<float>(poi.x);
    const float poiY = static_cast<float>(poi.y);
    const float bboxX = static_cast<float>(bbox.xMax);
    const float bboxY = static_cast<float>(bbox.yMax);
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

jaguar::HazardFilter MakeEffectiveFilter(const jaguar::HazardFilter& source)
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
      m_Filter(MakeEffectiveFilter(filter))
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
}

SampleEval LbfEvaluator::Evaluate(
    const jaguar::Transform2d& transform,
    const std::optional<SampleEval>&)
{
    ++m_EvaluationCount;
    if (m_Item == nullptr || !m_Engine)
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

    // Match jagua-rs/Sparrow fail-fast semantics: a surrogate hit is an
    // invalid sample; exact polygon evaluation is only reached after it clears.
    const auto surrogateCollision = m_Engine->DetectSurrogateCollision(
        *surrogate, *legalTransform, m_Filter);
    if (!surrogateCollision.HasValue() || surrogateCollision.Value())
    {
        return {SampleKind::SAMPLE_KIND_INVALID, 0.0F};
    }

    const auto transformedResult =
        m_Item->CollisionShape().TransformedRustF32(*legalTransform);
    if (!transformedResult.HasValue())
    {
        return {SampleKind::SAMPLE_KIND_INVALID, 0.0F};
    }
    const jaguar::SimplePolygon transformed = transformedResult.Value();
    const auto loss = CalculateLbfLoss(transformed);
    if (!loss.has_value())
    {
        return {SampleKind::SAMPLE_KIND_INVALID, 0.0F};
    }

    if (m_Engine->DetectPolygonCollision(transformed, m_Filter))
    {
        return {SampleKind::SAMPLE_KIND_INVALID, 0.0F};
    }
    return {SampleKind::SAMPLE_KIND_CLEAR, *loss};
}

std::size_t LbfEvaluator::EvaluationCount() const noexcept
{
    return m_EvaluationCount;
}
}
