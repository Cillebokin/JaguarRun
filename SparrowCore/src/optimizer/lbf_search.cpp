#include "SparrowCore/optimizer/lbf_search.h"

#include "SparrowCore/eval/lbf_evaluator.h"
#include "SparrowCore/sampling/local_search_sampler.h"
#include "SparrowCore/sampling/uniform_rect_sampler.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace
{
using SearchResultValue = std::optional<sparrow::SearchResult>;

sparrow::Result<SearchResultValue> invalidSearch(const char* message)
{
    return sparrow::Result<SearchResultValue>::fromError(
        {jaguar::ErrorCode::InvalidInput, message});
}

bool finiteTransform(const jaguar::Transform2d& transform) noexcept
{
    return std::isfinite(transform.rotationRad)
        && std::isfinite(transform.translation.x)
        && std::isfinite(transform.translation.y);
}

std::size_t localBudget(const sparrow::LbfConfig& config) noexcept
{
    const double value = static_cast<double>(config.samplesPerItem)
        * config.localSearchFraction;
    if (!std::isfinite(value) || value <= 0.0)
    {
        return 0U;
    }
    const double floored = std::floor(value);
    if (floored >= static_cast<double>(config.samplesPerItem))
    {
        return config.samplesPerItem;
    }
    return static_cast<std::size_t>(floored);
}

sparrow::LbfLoss lossFromShape(const jaguar::SimplePolygon& shape)
{
    return sparrow::LbfLoss::fromBoundingBox(shape.boundingBox());
}
}

namespace sparrow
{
Result<std::optional<SearchResult>> searchLbf(
    const jaguar::Layout& layout,
    const jaguar::Item& item,
    const LbfConfig& config,
    SparrowRng& rng,
    const jaguar::HazardFilter& filter)
{
    const Result<void> configResult = config.validate();
    if (!configResult.hasValue())
    {
        return Result<std::optional<SearchResult>>::fromError(configResult.error());
    }
    if (item.collisionShape().surrogate() == nullptr)
    {
        return invalidSearch("LBF item has no cached surrogate");
    }
    if (!std::isfinite(config.localSearchFraction))
    {
        return invalidSearch("LBF local-search fraction is not finite");
    }

    const std::size_t lsSamples = localBudget(config);
    const std::size_t uniformSamples = config.samplesPerItem - lsSamples;
    const jaguar::Rect2d containerBounds = layout.container().bounds();
    if (!std::isfinite(containerBounds.xMin)
        || !std::isfinite(containerBounds.yMin)
        || !std::isfinite(containerBounds.xMax)
        || !std::isfinite(containerBounds.yMax)
        || containerBounds.xMin >= containerBounds.xMax
        || containerBounds.yMin >= containerBounds.yMax)
    {
        return invalidSearch("LBF container bounds are invalid");
    }

    LbfEvaluator evaluator(layout, item, filter);
    auto samplerResult = UniformRectSampler::create(containerBounds, item);
    if (!samplerResult.hasValue())
    {
        return Result<std::optional<SearchResult>>::fromError(
            samplerResult.error());
    }
    UniformRectSampler sampler = std::move(samplerResult.value());

    std::optional<SearchResult> best;
    for (std::size_t index = 0; index < uniformSamples; ++index)
    {
        const jaguar::Transform2d candidate = sampler.sample(rng);
        const jaguar::SimplePolygon transformed =
            item.collisionShape().transformed(candidate);
        const LbfLoss cost = lossFromShape(transformed);
        if (best.has_value() && !(cost < best->loss))
        {
            continue;
        }

        // LbfLoss is the reference-search objective. It is deliberately
        // different from Sparrow's evaluator loss, so the exact evaluator is
        // called without an incompatible upper bound.
        const SampleEval evaluation = evaluator.evaluate(candidate, std::nullopt);
        if (evaluation.kind != SampleKind::Clear)
        {
            continue;
        }
        if (!best.has_value())
        {
            best = SearchResult{candidate, cost, index + 1U, {}};
        }
        else
        {
            best->transform = candidate;
            best->loss = cost;
            best->samples = index + 1U;
        }
        best->tighteningHistory.push_back(cost);
        const jaguar::Rect2d tightened = cost.tightenSampleBBox(sampler.bbox());
        const auto tightenedSampler = UniformRectSampler::create(tightened, item);
        if (tightenedSampler.hasValue())
        {
            sampler = std::move(tightenedSampler.value());
        }
    }

    if (!best.has_value())
    {
        return Result<std::optional<SearchResult>>::fromValue(std::nullopt);
    }

    const auto localSamplerResult = LocalSearchSampler::create(
        item, best->transform, config, containerBounds);
    if (!localSamplerResult.hasValue())
    {
        return Result<std::optional<SearchResult>>::fromError(
            localSamplerResult.error());
    }
    LocalSearchSampler localSampler = std::move(localSamplerResult.value());
    for (std::size_t index = 0; index < lsSamples; ++index)
    {
        const jaguar::Transform2d candidate = localSampler.sample(rng);
        const jaguar::SimplePolygon transformed =
            item.collisionShape().transformed(candidate);
        const LbfLoss cost = lossFromShape(transformed);
        if (cost < best->loss)
        {
            const SampleEval evaluation = evaluator.evaluate(candidate, std::nullopt);
            if (evaluation.kind == SampleKind::Clear)
            {
                best->transform = candidate;
                best->loss = cost;
                localSampler.shiftMean(candidate);
            }
        }
        localSampler.decayStddev(
            lsSamples == 0U
                ? 1.0F
                : static_cast<float>(index) / static_cast<float>(lsSamples));
    }

    best->samples = uniformSamples + lsSamples;
    return Result<std::optional<SearchResult>>::fromValue(std::move(best));
}
}
