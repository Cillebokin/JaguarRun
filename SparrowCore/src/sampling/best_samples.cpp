#include "SparrowCore/sampling/best_samples.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace
{
constexpr float kTwoPi = 6.28318530717958647692F;
constexpr float kRotationThreshold = 0.01745329251994329577F;

float normalizedRotation(float rotation) noexcept
{
    const float value = std::fmod(rotation, kTwoPi);
    return value < 0.0F ? value + kTwoPi : value;
}
}

namespace sparrow
{
BestSamples::BestSamples(std::size_t capacity, float uniqueThreshold)
    : m_Capacity(capacity),
      m_UniqueThreshold(uniqueThreshold)
{
}

bool BestSamples::similar(const jaguar::Transform2d& left,
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

    const float leftRotation = normalizedRotation(static_cast<float>(left.rotationRad));
    const float rightRotation = normalizedRotation(static_cast<float>(right.rotationRad));
    return std::abs(leftRotation - rightRotation) < kRotationThreshold;
}

bool BestSamples::report(const jaguar::Transform2d& transform,
                         const SampleEval& evaluation)
{
    if (m_Capacity == 0U || evaluation.kind == SampleKind::Invalid
        || !std::isfinite(evaluation.loss))
    {
        return false;
    }

    if (m_Samples.size() == m_Capacity && !betterSample(evaluation, upperBound()))
    {
        return false;
    }

    std::vector<std::size_t> similarIndices;
    for (std::size_t index = 0; index < m_Samples.size(); ++index)
    {
        if (similar(m_Samples[index].first, transform, m_UniqueThreshold))
        {
            similarIndices.push_back(index);
        }
    }

    if (!similarIndices.empty())
    {
        bool betterThanAllSimilar = true;
        for (const std::size_t index : similarIndices)
        {
            if (!betterSample(evaluation, m_Samples[index].second))
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
            if (!similar(sample.first, transform, m_UniqueThreshold))
            {
                retained.push_back(sample);
            }
        }
        m_Samples = std::move(retained);
    }

    m_Samples.emplace_back(transform, evaluation);
    std::sort(m_Samples.begin(), m_Samples.end(),
              [](const auto& left, const auto& right) {
                  return betterSample(left.second, right.second);
              });
    if (m_Samples.size() > m_Capacity)
    {
        m_Samples.resize(m_Capacity);
    }
    return true;
}

std::size_t BestSamples::size() const noexcept
{
    return m_Samples.size();
}

std::size_t BestSamples::capacity() const noexcept
{
    return m_Capacity;
}

std::optional<std::pair<jaguar::Transform2d, SampleEval>> BestSamples::best() const
{
    if (m_Samples.empty())
    {
        return std::nullopt;
    }
    return m_Samples.front();
}

SampleEval BestSamples::upperBound() const noexcept
{
    if (m_Samples.size() < m_Capacity || m_Samples.empty())
    {
        return {SampleKind::Invalid, 0.0F};
    }
    return m_Samples.back().second;
}
}
