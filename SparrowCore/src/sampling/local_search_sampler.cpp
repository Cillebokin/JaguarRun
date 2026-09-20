#include "SparrowCore/sampling/local_search_sampler.h"

#include "SparrowCore/sampling/rotation_distribution.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace
{
sparrow::Result<sparrow::LocalSearchSampler> invalidLocalSearch(const char* message)
{
    return sparrow::Result<sparrow::LocalSearchSampler>::fromError(
        {jaguar::ErrorCode::InvalidInput, message});
}

bool finiteTransform(const jaguar::Transform2d& transform) noexcept
{
    return std::isfinite(transform.rotationRad)
        && std::isfinite(transform.translation.x)
        && std::isfinite(transform.translation.y);
}

bool representableAsFloat(double value) noexcept
{
    const float converted = static_cast<float>(value);
    return std::isfinite(value) && std::isfinite(converted);
}

float decayedStddev(float initial, float limit, float progress) noexcept
{
    if (progress <= 0.0F || initial == limit)
    {
        return initial;
    }
    if (progress >= 1.0F)
    {
        return limit;
    }
    if (initial <= 0.0F)
    {
        return 0.0F;
    }

    const float ratio = limit / initial;
    const float value = initial * std::pow(ratio, progress);
    return std::isfinite(value) ? value : limit;
}
}

namespace sparrow
{
LocalSearchSampler::LocalSearchSampler(jaguar::RotationMode rotationMode,
                                       float discreteRotation,
                                       float meanRotation,
                                       float meanX,
                                       float meanY,
                                       float translationStddevInit,
                                       float translationStddevLimit,
                                       float rotationStddevInit,
                                       float rotationStddevLimit)
    : m_RotationMode(rotationMode),
      m_DiscreteRotation(discreteRotation),
      m_MeanRotation(meanRotation),
      m_MeanX(meanX),
      m_MeanY(meanY),
      m_TranslationStddev(translationStddevInit),
      m_RotationStddev(rotationStddevInit),
      m_TranslationStddevInit(translationStddevInit),
      m_TranslationStddevLimit(translationStddevLimit),
      m_RotationStddevInit(rotationStddevInit),
      m_RotationStddevLimit(rotationStddevLimit)
{
}

Result<LocalSearchSampler> LocalSearchSampler::create(
    const jaguar::Item& item,
    const jaguar::Transform2d& reference,
    const LbfConfig& config,
    const jaguar::Rect2d& containerBounds)
{
    const Result<void> configResult = config.validate();
    if (!configResult.hasValue())
    {
        return Result<LocalSearchSampler>::fromError(configResult.error());
    }
    if (!finiteTransform(reference))
    {
        return invalidLocalSearch("local search reference must be finite");
    }
    if (!std::isfinite(containerBounds.xMin)
        || !std::isfinite(containerBounds.yMin)
        || !std::isfinite(containerBounds.xMax)
        || !std::isfinite(containerBounds.yMax)
        || containerBounds.xMin >= containerBounds.xMax
        || containerBounds.yMin >= containerBounds.yMax)
    {
        return invalidLocalSearch("local search container bounds are invalid");
    }

    const auto rotationValidation = RotationDistribution::fromItem(item);
    if (!rotationValidation.hasValue())
    {
        return Result<LocalSearchSampler>::fromError(rotationValidation.error());
    }

    const double maxDimension = std::max(containerBounds.xMax - containerBounds.xMin,
                                          containerBounds.yMax - containerBounds.yMin);
    const double translationInit = maxDimension * config.maxTranslationStddevRatio;
    const double translationLimit = maxDimension * config.minTranslationStddevRatio;
    const double rotationInit = config.maxRotationStddevRad;
    const double rotationLimit = config.minRotationStddevRad;
    if (!std::isfinite(maxDimension) || maxDimension <= 0.0
        || !std::isfinite(translationInit) || !std::isfinite(translationLimit)
        || !std::isfinite(rotationInit) || !std::isfinite(rotationLimit)
        || translationInit < 0.0 || translationLimit < 0.0
        || rotationInit < 0.0 || rotationLimit < 0.0
        || !representableAsFloat(translationInit)
        || !representableAsFloat(translationLimit)
        || !representableAsFloat(rotationInit)
        || !representableAsFloat(rotationLimit)
        || !representableAsFloat(reference.translation.x)
        || !representableAsFloat(reference.translation.y)
        || !representableAsFloat(reference.rotationRad))
    {
        return invalidLocalSearch("local search values are not finite float values");
    }

    const float meanX = static_cast<float>(reference.translation.x);
    const float meanY = static_cast<float>(reference.translation.y);
    float meanRotation = static_cast<float>(reference.rotationRad);
    float discreteRotation = meanRotation;
    if (item.allowedRotation().mode == jaguar::RotationMode::None)
    {
        meanRotation = 0.0F;
        discreteRotation = 0.0F;
    }

    return Result<LocalSearchSampler>::fromValue(
        LocalSearchSampler{item.allowedRotation().mode,
                            discreteRotation,
                            meanRotation,
                            meanX,
                            meanY,
                            static_cast<float>(translationInit),
                            static_cast<float>(translationLimit),
                            static_cast<float>(rotationInit),
                            static_cast<float>(rotationLimit)});
}

jaguar::Transform2d LocalSearchSampler::sample(SparrowRng& rng)
{
    float rotation = 0.0F;
    if (m_RotationMode == jaguar::RotationMode::Continuous)
    {
        rotation = m_MeanRotation + m_RotationStddev * rng.normal01F32();
    }
    else if (m_RotationMode == jaguar::RotationMode::Discrete)
    {
        rotation = m_DiscreteRotation;
    }

    const float x = m_MeanX + m_TranslationStddev * rng.normal01F32();
    const float y = m_MeanY + m_TranslationStddev * rng.normal01F32();
    return jaguar::Transform2d::fromRotationTranslation(
        static_cast<double>(rotation),
        {static_cast<double>(x), static_cast<double>(y)});
}

void LocalSearchSampler::shiftMean(const jaguar::Transform2d& transform)
{
    if (!finiteTransform(transform))
    {
        return;
    }

    const float x = static_cast<float>(transform.translation.x);
    const float y = static_cast<float>(transform.translation.y);
    const float rotation = static_cast<float>(transform.rotationRad);
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(rotation))
    {
        return;
    }

    m_MeanX = x;
    m_MeanY = y;
    if (m_RotationMode == jaguar::RotationMode::Continuous)
    {
        m_MeanRotation = rotation;
    }
}

void LocalSearchSampler::decayStddev(float progress)
{
    if (!std::isfinite(progress))
    {
        progress = 0.0F;
    }
    progress = std::clamp(progress, 0.0F, 1.0F);
    m_TranslationStddev = decayedStddev(
        m_TranslationStddevInit, m_TranslationStddevLimit, progress);
    m_RotationStddev = decayedStddev(
        m_RotationStddevInit, m_RotationStddevLimit, progress);
}
}
