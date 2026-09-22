#include "SparrowCore/Sampling/BestSamples.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace
{
constexpr float TWO_PI = 6.28318530717958647692F;
constexpr float ROTATION_THRESHOLD = 0.01745329251994329577F;

float NormalizeRotation(float rotation) noexcept
{
    const float value = std::fmod(rotation, TWO_PI);
    return value < 0.0F ? value + TWO_PI : value;
}
}

namespace sparrow
{
BestSamples::BestSamples(std::size_t capacity, float uniqueThreshold)
    : m_Capacity(capacity),
      m_UniqueThreshold(uniqueThreshold)
{
}

bool BestSamples::IsSimilar(const jaguar::Transform2d& left,
                          const jaguar::Transform2d& right,
                          float translationThreshold) noexcept
{
    if (!std::isfinite(translationThreshold) || translationThreshold < 0.0F)
    {
        return false;
    }

    const float xDifference = static_cast<float>(
        std::abs(left.translation.x - right.translation.x));
    const float yDifference = static_cast<float>(
        std::abs(left.translation.y - right.translation.y));
    if (!(xDifference < translationThreshold && yDifference < translationThreshold))
    {
        return false;
    }

    const float leftRotation = NormalizeRotation(static_cast<float>(left.rotationRad));
    const float rightRotation = NormalizeRotation(static_cast<float>(right.rotationRad));
    return std::abs(leftRotation - rightRotation) < ROTATION_THRESHOLD;
}

bool BestSamples::Report(const jaguar::Transform2d& transform,
                         const SampleEval& evaluation)
{
    if (m_Capacity == 0U || evaluation.kind == SampleKind::SAMPLE_KIND_INVALID
        || !std::isfinite(evaluation.loss))
    {
        return false;
    }

    if (m_Samples.size() == m_Capacity && !IsBetterSample(evaluation, UpperBound()))
    {
        return false;
    }

    std::vector<std::size_t> similarIndices;
    for (std::size_t index = 0; index < m_Samples.size(); ++index)
    {
        if (IsSimilar(m_Samples[index].first, transform, m_UniqueThreshold))
        {
            similarIndices.push_back(index);
        }
    }

    if (!similarIndices.empty())
    {
        bool betterThanAllSimilar = true;
        for (const std::size_t index : similarIndices)
        {
            if (!IsBetterSample(evaluation, m_Samples[index].second))
            {
                betterThanAllSimilar = false;
                break;
            }
        }
        if (!betterThanAllSimilar)
        {
            return false;
        }
        std::vector<std::pair<jaguar::Transform2d, SampleEval>> retained;
        retained.reserve(m_Samples.size());
        for (const auto& sample : m_Samples)
        {
            if (!IsSimilar(sample.first, transform, m_UniqueThreshold))
            {
                retained.push_back(sample);
            }
        }
        m_Samples = std::move(retained);
    }

    m_Samples.emplace_back(transform, evaluation);
    std::sort(m_Samples.begin(), m_Samples.end(),
              [](const auto& left, const auto& right) {
                  return IsBetterSample(left.second, right.second);
              });
    if (m_Samples.size() > m_Capacity)
    {
        m_Samples.resize(m_Capacity);
    }
    return true;
}

std::size_t BestSamples::Size() const noexcept
{
    return m_Samples.size();
}

std::size_t BestSamples::Capacity() const noexcept
{
    return m_Capacity;
}

std::optional<std::pair<jaguar::Transform2d, SampleEval>> BestSamples::Best() const
{
    if (m_Samples.empty())
    {
        return std::nullopt;
    }
    return m_Samples.front();
}

SampleEval BestSamples::UpperBound() const noexcept
{
    if (m_Samples.size() < m_Capacity || m_Samples.empty())
    {
        return {SampleKind::SAMPLE_KIND_INVALID, 0.0F};
    }
    return m_Samples.back().second;
}
}
