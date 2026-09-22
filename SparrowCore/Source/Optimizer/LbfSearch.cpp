#include "SparrowCore/Optimizer/LbfSearch.h"

#include "SparrowCore/Eval/LbfEvaluator.h"
#include "SparrowCore/Sampling/LocalSearchSampler.h"
#include "SparrowCore/Sampling/UniformRectSampler.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace
{
using SearchResultValue = std::optional<sparrow::SearchResult>;

sparrow::Result<SearchResultValue> InvalidSearch(const char* message)
{
    return sparrow::Result<SearchResultValue>::FromError(
        {jaguar::ErrorCode::InvalidInput, message});
}

bool IsFiniteTransform(const jaguar::Transform2d& transform) noexcept
{
    return std::isfinite(transform.rotationRad)
        && std::isfinite(transform.translation.x)
        && std::isfinite(transform.translation.y);
}

std::size_t CalculateLocalBudget(const sparrow::LbfConfig& config) noexcept
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

sparrow::LbfLoss LossFromShape(const jaguar::SimplePolygon& shape)
{
    return sparrow::LbfLoss::FromBoundingBox(shape.BoundingBox());
}
}

namespace sparrow
{
Result<std::optional<SearchResult>> SearchLbf(
    const jaguar::Layout& layout,
    const jaguar::Item& item,
    const LbfConfig& config,
    SparrowRng& rng,
    const jaguar::HazardFilter& filter)
{
    const Result<void> configResult = config.Validate();
    if (!configResult.HasValue())
    {
        return Result<std::optional<SearchResult>>::FromError(configResult.GetError());
    }
    if (item.CollisionShape().GetSurrogate() == nullptr)
    {
        return InvalidSearch("LBF item has no cached surrogate");
    }
    if (!std::isfinite(config.localSearchFraction))
    {
        return InvalidSearch("LBF local-search fraction is not finite");
    }

    const std::size_t lsSamples = CalculateLocalBudget(config);
    const std::size_t uniformSamples = config.samplesPerItem - lsSamples;
    const jaguar::Rect2d containerBounds = layout.GetContainer().Bounds();
    if (!std::isfinite(containerBounds.xMin)
        || !std::isfinite(containerBounds.yMin)
        || !std::isfinite(containerBounds.xMax)
        || !std::isfinite(containerBounds.yMax)
        || containerBounds.xMin >= containerBounds.xMax
        || containerBounds.yMin >= containerBounds.yMax)
    {
        return InvalidSearch("LBF container bounds are invalid");
    }

    LbfEvaluator evaluator(layout, item, filter);
    auto samplerResult = UniformRectSampler::Create(containerBounds, item);
    if (!samplerResult.HasValue())
    {
        return Result<std::optional<SearchResult>>::FromError(
            samplerResult.GetError());
    }
    UniformRectSampler sampler = std::move(samplerResult.Value());

    std::optional<SearchResult> best;
    for (std::size_t index = 0; index < uniformSamples; ++index)
    {
        const jaguar::Transform2d candidate = sampler.Sample(rng);
        const auto transformedResult = item.CollisionShape().Transformed(candidate);
        if (!transformedResult.HasValue())
        {
            continue;
        }
        const jaguar::SimplePolygon transformed = transformedResult.Value();
        const LbfLoss cost = LossFromShape(transformed);
        if (best.has_value() && !(cost < best->loss))
        {
            continue;
        }

        // LbfLoss is the reference-search objective. It is deliberately
        // different from Sparrow's evaluator loss, so the exact evaluator is
        // called without an incompatible upper bound.
        const SampleEval evaluation = evaluator.Evaluate(candidate, std::nullopt);
        if (evaluation.kind != SampleKind::SAMPLE_KIND_CLEAR)
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
        const jaguar::Rect2d tightened = cost.TightenSampleBBox(sampler.BoundingBox());
        const auto tightenedSampler = UniformRectSampler::Create(tightened, item);
        if (tightenedSampler.HasValue())
        {
            sampler = std::move(tightenedSampler.Value());
        }
    }

    if (!best.has_value())
    {
        return Result<std::optional<SearchResult>>::FromValue(std::nullopt);
    }

    const auto localSamplerResult = LocalSearchSampler::Create(
        item, best->transform, config, containerBounds);
    if (!localSamplerResult.HasValue())
    {
        return Result<std::optional<SearchResult>>::FromError(
            localSamplerResult.GetError());
    }
    LocalSearchSampler localSampler = std::move(localSamplerResult.Value());
    for (std::size_t index = 0; index < lsSamples; ++index)
    {
        const jaguar::Transform2d candidate = localSampler.Sample(rng);
        const auto transformedResult = item.CollisionShape().Transformed(candidate);
        if (!transformedResult.HasValue())
        {
            continue;
        }
        const jaguar::SimplePolygon transformed = transformedResult.Value();
        const LbfLoss cost = LossFromShape(transformed);
        if (cost < best->loss)
        {
            const SampleEval evaluation = evaluator.Evaluate(candidate, std::nullopt);
            if (evaluation.kind == SampleKind::SAMPLE_KIND_CLEAR)
            {
                best->transform = candidate;
                best->loss = cost;
                localSampler.ShiftMean(candidate);
            }
        }
        localSampler.DecayStddev(
            lsSamples == 0U
                ? 1.0F
                : static_cast<float>(index) / static_cast<float>(lsSamples));
    }

    best->samples = uniformSamples + lsSamples;
    return Result<std::optional<SearchResult>>::FromValue(std::move(best));
}
}
