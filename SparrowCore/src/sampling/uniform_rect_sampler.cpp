#include "SparrowCore/sampling/uniform_rect_sampler.h"

#include <cmath>
#include <utility>

namespace
{
sparrow::Result<sparrow::UniformRectSampler> invalidSampler(const char* message)
{
    return sparrow::Result<sparrow::UniformRectSampler>::fromError(
        {jaguar::ErrorCode::InvalidInput, message});
}

float sampleAxis(float minValue, float maxValue, sparrow::SparrowRng& rng) noexcept
{
    const float value = minValue + (maxValue - minValue) * rng.uniform01F32();
    if (value >= maxValue)
    {
        return std::nextafter(maxValue, minValue);
    }
    return value;
}
}

namespace sparrow
{
UniformRectSampler::UniformRectSampler(jaguar::Rect2d bbox,
                                       RotationDistribution rotationDistribution,
                                       float xMin,
                                       float xMax,
                                       float yMin,
                                       float yMax)
    : m_Bbox(bbox),
      m_RotationDistribution(std::move(rotationDistribution)),
      m_XMin(xMin),
      m_XMax(xMax),
      m_YMin(yMin),
      m_YMax(yMax)
{
}

Result<UniformRectSampler> UniformRectSampler::create(
    jaguar::Rect2d bbox, const jaguar::Item& item)
{
    if (!std::isfinite(bbox.xMin) || !std::isfinite(bbox.yMin)
        || !std::isfinite(bbox.xMax) || !std::isfinite(bbox.yMax)
        || bbox.xMin >= bbox.xMax || bbox.yMin >= bbox.yMax)
    {
        return invalidSampler("uniform sampler bbox must be finite and non-degenerate");
    }

    const float xMin = static_cast<float>(bbox.xMin);
    const float xMax = static_cast<float>(bbox.xMax);
    const float yMin = static_cast<float>(bbox.yMin);
    const float yMax = static_cast<float>(bbox.yMax);
    if (!std::isfinite(xMin) || !std::isfinite(xMax)
        || !std::isfinite(yMin) || !std::isfinite(yMax)
        || xMin >= xMax || yMin >= yMax)
    {
        return invalidSampler("uniform sampler bbox is not representable as float");
    }

    auto rotationDistribution = RotationDistribution::fromItem(item);
    if (!rotationDistribution.hasValue())
    {
        return Result<UniformRectSampler>::fromError(rotationDistribution.error());
    }

    return Result<UniformRectSampler>::fromValue(
        UniformRectSampler{bbox,
                           std::move(rotationDistribution.value()),
                           xMin,
                           xMax,
                           yMin,
                           yMax});
}

jaguar::Transform2d UniformRectSampler::sample(SparrowRng& rng) const noexcept
{
    const float rotation = m_RotationDistribution.sample(rng);
    const float x = sampleAxis(m_XMin, m_XMax, rng);
    const float y = sampleAxis(m_YMin, m_YMax, rng);
    return jaguar::Transform2d::fromRotationTranslation(
        static_cast<double>(rotation),
        {static_cast<double>(x), static_cast<double>(y)});
}

jaguar::Rect2d UniformRectSampler::bbox() const noexcept
{
    return m_Bbox;
}
}
