#include "SparrowCore/Sampling/LocalSearchSampler.h"

#include "SparrowCore/Sampling/RotationDistribution.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace
{
sparrow::Result<sparrow::LocalSearchSampler> InvalidLocalSearch(const char* message)
{
    return sparrow::Result<sparrow::LocalSearchSampler>::FromError(
        {jaguar::ErrorCode::InvalidInput, message});
}

bool IsFiniteTransform(const jaguar::Transform2d& transform) noexcept
{
    return std::isfinite(transform.rotationRad)
        && std::isfinite(transform.translation.x)
        && std::isfinite(transform.translation.y);
}

bool IsRepresentableAsFloat(double value) noexcept
{
    const float converted = static_cast<float>(value);
    return std::isfinite(value) && std::isfinite(converted);
}

float CalculateDecayedStddev(float initial, float limit, float progress) noexcept
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

Result<LocalSearchSampler> LocalSearchSampler::Create(
    const jaguar::Item& item,
    const jaguar::Transform2d& reference,
    const LbfConfig& config,
    const jaguar::Rect2d& containerBounds)
{
    const Result<void> configResult = config.Validate();
    if (!configResult.HasValue())
    {
        return Result<LocalSearchSampler>::FromError(configResult.GetError());
    }
    if (!IsFiniteTransform(reference))
    {
        return InvalidLocalSearch("local search reference must be finite");
    }
    if (!std::isfinite(containerBounds.xMin)
        || !std::isfinite(containerBounds.yMin)
        || !std::isfinite(containerBounds.xMax)
        || !std::isfinite(containerBounds.yMax)
        || containerBounds.xMin >= containerBounds.xMax
        || containerBounds.yMin >= containerBounds.yMax)
    {
        return InvalidLocalSearch("local search container bounds are invalid");
    }

    const auto rotationValidation = RotationDistribution::FromItem(item);
    if (!rotationValidation.HasValue())
    {
        return Result<LocalSearchSampler>::FromError(rotationValidation.GetError());
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
        || !IsRepresentableAsFloat(translationInit)
        || !IsRepresentableAsFloat(translationLimit)
        || !IsRepresentableAsFloat(rotationInit)
        || !IsRepresentableAsFloat(rotationLimit)
        || !IsRepresentableAsFloat(reference.translation.x)
        || !IsRepresentableAsFloat(reference.translation.y)
        || !IsRepresentableAsFloat(reference.rotationRad))
    {
        return InvalidLocalSearch("local search values are not finite float values");
    }

    const float meanX = static_cast<float>(reference.translation.x);
    const float meanY = static_cast<float>(reference.translation.y);
    float meanRotation = static_cast<float>(reference.rotationRad);
    float discreteRotation = meanRotation;
    if (item.AllowedRotation().mode == jaguar::RotationMode::None)
    {
        meanRotation = 0.0F;
        discreteRotation = 0.0F;
    }

    return Result<LocalSearchSampler>::FromValue(
        LocalSearchSampler{item.AllowedRotation().mode,
                            discreteRotation,
                            meanRotation,
                            meanX,
                            meanY,
                            static_cast<float>(translationInit),
                            static_cast<float>(translationLimit),
                            static_cast<float>(rotationInit),
                            static_cast<float>(rotationLimit)});
}

jaguar::Transform2d LocalSearchSampler::Sample(SparrowRng& rng)
{
    float rotation = 0.0F;
    if (m_RotationMode == jaguar::RotationMode::Continuous)
    {
        rotation = m_MeanRotation + m_RotationStddev * rng.Normal01F32();
    }
    else if (m_RotationMode == jaguar::RotationMode::Discrete)
    {
        rotation = m_DiscreteRotation;
    }

    const float x = m_MeanX + m_TranslationStddev * rng.Normal01F32();
    const float y = m_MeanY + m_TranslationStddev * rng.Normal01F32();
    return jaguar::Transform2d::FromRotationTranslation(
        static_cast<double>(rotation),
        {static_cast<double>(x), static_cast<double>(y)});
}

void LocalSearchSampler::ShiftMean(const jaguar::Transform2d& transform)
{
    if (!IsFiniteTransform(transform))
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

void LocalSearchSampler::DecayStddev(float progress)
{
    if (!std::isfinite(progress))
    {
        progress = 0.0F;
    }
    progress = std::clamp(progress, 0.0F, 1.0F);
    m_TranslationStddev = CalculateDecayedStddev(
        m_TranslationStddevInit, m_TranslationStddevLimit, progress);
    m_RotationStddev = CalculateDecayedStddev(
        m_RotationStddevInit, m_RotationStddevLimit, progress);
}
}
