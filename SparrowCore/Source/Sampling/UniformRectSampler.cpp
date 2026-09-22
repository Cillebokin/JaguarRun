#include "SparrowCore/Sampling/UniformRectSampler.h"

#include <cmath>
#include <utility>

namespace
{
sparrow::Result<sparrow::UniformRectSampler> InvalidSampler(const char* message)
{
    return sparrow::Result<sparrow::UniformRectSampler>::FromError(
        {jaguar::ErrorCode::InvalidInput, message});
}

float SampleAxis(float minValue, float maxValue, sparrow::SparrowRng& rng) noexcept
{
    const float value = minValue + (maxValue - minValue) * rng.Uniform01F32();
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

Result<UniformRectSampler> UniformRectSampler::Create(
    jaguar::Rect2d bbox, const jaguar::Item& item)
{
    if (!std::isfinite(bbox.xMin) || !std::isfinite(bbox.yMin)
        || !std::isfinite(bbox.xMax) || !std::isfinite(bbox.yMax)
        || bbox.xMin >= bbox.xMax || bbox.yMin >= bbox.yMax)
    {
        return InvalidSampler("uniform sampler bbox must be finite and non-degenerate");
    }

    const float xMin = static_cast<float>(bbox.xMin);
    const float xMax = static_cast<float>(bbox.xMax);
    const float yMin = static_cast<float>(bbox.yMin);
    const float yMax = static_cast<float>(bbox.yMax);
    if (!std::isfinite(xMin) || !std::isfinite(xMax)
        || !std::isfinite(yMin) || !std::isfinite(yMax)
        || xMin >= xMax || yMin >= yMax)
    {
        return InvalidSampler("uniform sampler bbox is not representable as float");
    }

    auto rotationDistribution = RotationDistribution::FromItem(item);
    if (!rotationDistribution.HasValue())
    {
        return Result<UniformRectSampler>::FromError(rotationDistribution.GetError());
    }

    return Result<UniformRectSampler>::FromValue(
        UniformRectSampler{bbox,
                           std::move(rotationDistribution.Value()),
                           xMin,
                           xMax,
                           yMin,
                           yMax});
}

jaguar::Transform2d UniformRectSampler::Sample(SparrowRng& rng) const noexcept
{
    const float rotation = m_RotationDistribution.Sample(rng);
    const float x = SampleAxis(m_XMin, m_XMax, rng);
    const float y = SampleAxis(m_YMin, m_YMax, rng);
    return jaguar::Transform2d::FromRotationTranslation(
        static_cast<double>(rotation),
        {static_cast<double>(x), static_cast<double>(y)});
}

jaguar::Rect2d UniformRectSampler::BoundingBox() const noexcept
{
    return m_Bbox;
}
}
