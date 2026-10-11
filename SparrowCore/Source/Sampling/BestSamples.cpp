#include "SparrowCore/Sampling/BestSamples.h"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace
{
constexpr float ROTATION_THRESHOLD = 0.01745329251994329577F;
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
    if (std::isnan(translationThreshold) || translationThreshold < 0.0F)
    {
        return false;
    }

    // DTransformation stores translation coordinates as f32. Convert each
    // input before subtracting so double-valued C++ transforms follow the same
    // rounding path as Rust at the uniqueness boundary.
    const float leftX = static_cast<float>(left.translation.x);
    const float rightX = static_cast<float>(right.translation.x);
    const float leftY = static_cast<float>(left.translation.y);
    const float rightY = static_cast<float>(right.translation.y);
    const float xDifference = std::abs(leftX - rightX);
    const float yDifference = std::abs(leftY - rightY);
    if (!(xDifference < translationThreshold && yDifference < translationThreshold))
    {
        return false;
    }

    // Match Rust's `rotation % (2*pi)`: fmod preserves the sign and the
    // comparison is intentionally not a circular shortest-distance check.
    constexpr float TWO_PI = 6.28318530717958647692F;
    const float leftRotation = std::fmod(
        static_cast<float>(left.rotationRad), TWO_PI);
    const float rightRotation = std::fmod(
        static_cast<float>(right.rotationRad), TWO_PI);
    return std::abs(leftRotation - rightRotation) < ROTATION_THRESHOLD;
}

bool BestSamples::Report(const jaguar::Transform2d& transform,
                         const SampleEval& evaluation)
{
#if !defined(NDEBUG)
    // Rust Debug evaluates upper_bound() before examining the candidate; for
    // size zero, its `size - 1` index calculation panics on underflow.
    if (m_Capacity == 0U)
    {
        static_cast<void>(UpperBound());
    }
#endif
    if (evaluation.kind == SampleKind::SAMPLE_KIND_INVALID)
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
    // The Rust-compatible approximate-equality comparator is not a strict weak
    // ordering, so it cannot satisfy std::sort's comparison precondition.
    for (std::size_t index = 1U; index < m_Samples.size(); ++index)
    {
        auto sample = std::move(m_Samples[index]);
        std::size_t destination = index;
        while (destination > 0U
               && IsBetterSample(sample.second,
                                 m_Samples[destination - 1U].second))
        {
            m_Samples[destination] = std::move(m_Samples[destination - 1U]);
            --destination;
        }
        m_Samples[destination] = std::move(sample);
    }
    // Rust Release wraps size - 1 to usize::MAX for capacity zero, so the
    // upper bound stays Invalid and capacity logic does not evict or truncate.
    if (m_Capacity != 0U && m_Samples.size() > m_Capacity)
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

const std::vector<std::pair<jaguar::Transform2d, SampleEval>>&
BestSamples::Samples() const noexcept
{
    return m_Samples;
}

std::optional<std::pair<jaguar::Transform2d, SampleEval>> BestSamples::Best() const
{
    if (m_Samples.empty())
    {
        return std::nullopt;
    }
    return m_Samples.front();
}

SampleEval BestSamples::UpperBound() const
{
#if !defined(NDEBUG)
    if (m_Capacity == 0U)
    {
        throw std::underflow_error("BestSamples capacity zero underflows size - 1");
    }
#else
    if (m_Capacity == 0U)
    {
        return {SampleKind::SAMPLE_KIND_INVALID, 0.0F};
    }
#endif
    if (m_Samples.size() < m_Capacity || m_Samples.empty())
    {
        return {SampleKind::SAMPLE_KIND_INVALID, 0.0F};
    }
    return m_Samples.back().second;
}
}
