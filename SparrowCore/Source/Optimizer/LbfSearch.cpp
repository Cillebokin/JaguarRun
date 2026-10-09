#include "SparrowCore/Optimizer/LbfSearch.h"
#include "LbfSearchTrace.h"

#include "SparrowCore/Eval/LbfEvaluator.h"
#include "SparrowCore/Sampling/BestSamples.h"
#include "SparrowCore/Sampling/CoordinateDescent.h"
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

sparrow::CoordinateDescentConfig MakeRefinementConfig(
    const jaguar::Item& item, bool finalRefinement)
{
    const jaguar::Rect2d bounds = item.CollisionShape().BoundingBox();
    const float minimumDimension = static_cast<float>(
        std::min(bounds.Width(), bounds.Height()));
    sparrow::CoordinateDescentConfig result;
    if (!std::isfinite(minimumDimension) || minimumDimension <= 0.0F)
    {
        return result;
    }
    if (finalRefinement)
    {
        result.translationStepInit = minimumDimension * 0.01F;
        result.translationStepLimit = minimumDimension * 0.001F;
        result.rotationStepInit = 0.5F
            * 3.14159265358979323846F / 180.0F;
        result.rotationStepLimit = 0.05F
            * 3.14159265358979323846F / 180.0F;
    }
    else
    {
        result.translationStepInit = minimumDimension * 0.25F;
        result.translationStepLimit = minimumDimension * 0.02F;
        result.rotationStepInit = 5.0F
            * 3.14159265358979323846F / 180.0F;
        result.rotationStepLimit = 1.0F
            * 3.14159265358979323846F / 180.0F;
    }
    result.rotationWiggle =
        item.AllowedRotation().mode == jaguar::RotationMode::Continuous;
    return result;
}

sparrow::LbfLoss LossFromShape(const jaguar::SimplePolygon& shape)
{
    return sparrow::LbfLoss::FromBoundingBox(shape.BoundingBox());
}

class TracingEvaluator final : public sparrow::SampleEvaluator
{
public:
    TracingEvaluator(const jaguar::Layout& layout,
                     const jaguar::Item& item,
                     const jaguar::HazardFilter& filter,
                     const sparrow::LbfSearchTraceCallback* callback)
        : m_Delegate(layout, item, filter),
          m_Callback(callback)
    {
    }

    void SetPhase(sparrow::LbfSearchTracePhase phase) noexcept
    {
        m_Phase = phase;
    }

    sparrow::SampleEval Evaluate(
        const jaguar::Transform2d& transform,
        const std::optional<sparrow::SampleEval>& upperBound) override
    {
        const sparrow::SampleEval evaluation =
            m_Delegate.Evaluate(transform, upperBound);
        if (m_Callback != nullptr)
        {
            sparrow::LbfSearchTraceEvent event;
            event.type = sparrow::LbfSearchTraceEvent::Type::Evaluation;
            event.phase = m_Phase;
            event.transform = transform;
            event.evaluation = evaluation;
            (*m_Callback)(event);
        }
        return evaluation;
    }

    std::size_t EvaluationCount() const noexcept override
    {
        return m_Delegate.EvaluationCount();
    }

private:
    sparrow::LbfEvaluator m_Delegate;
    const sparrow::LbfSearchTraceCallback* m_Callback = nullptr;
    sparrow::LbfSearchTracePhase m_Phase =
        sparrow::LbfSearchTracePhase::ContainerSample;
};
}

namespace sparrow
{
namespace
{
Result<std::optional<SearchResult>> SearchLbfImpl(
    const jaguar::Layout& layout,
    const jaguar::Item& item,
    const LbfConfig& config,
    SparrowRng& rng,
    const jaguar::HazardFilter& filter,
    const LbfSearchTraceCallback* callback)
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
    const jaguar::Rect2d containerBounds =
        layout.GetContainer().CollisionShape().BoundingBox();
    if (!std::isfinite(containerBounds.xMin)
        || !std::isfinite(containerBounds.yMin)
        || !std::isfinite(containerBounds.xMax)
        || !std::isfinite(containerBounds.yMax)
        || containerBounds.xMin >= containerBounds.xMax
        || containerBounds.yMin >= containerBounds.yMax)
    {
        return InvalidSearch("LBF container bounds are invalid");
    }

    TracingEvaluator evaluator(layout, item, filter, callback);
    const auto samplerResult = UniformRectSampler::Create(
        containerBounds, item, containerBounds);
    if (!samplerResult.HasValue())
    {
        // UniformBBoxSampler::new returns None when no legal rotation can fit
        // the current container; the SPP builder interprets that as a width
        // growth opportunity rather than as malformed input.
        return Result<std::optional<SearchResult>>::FromValue(std::nullopt);
    }
    const UniformRectSampler sampler = samplerResult.Value();
    const jaguar::Rect2d itemBounds = item.CollisionShape().BoundingBox();
    const float itemMinimumDimension = static_cast<float>(std::min(
        itemBounds.Width(), itemBounds.Height()));
    if (!std::isfinite(itemMinimumDimension) || itemMinimumDimension <= 0.0F)
    {
        return InvalidSearch("LBF item minimum dimension is invalid");
    }

    BestSamples best(
        config.nCoordinateDescents,
        itemMinimumDimension * 0.05F);
    std::vector<LbfLoss> tighteningHistory;
    std::optional<LbfLoss> bestLoss;
    LbfSearchTracePhase evaluatorPhase = LbfSearchTracePhase::ContainerSample;
    const auto recordTightening = [&](const jaguar::Transform2d& candidate,
                                      const SampleEval& evaluation) {
        if (evaluation.kind != SampleKind::SAMPLE_KIND_CLEAR)
        {
            return;
        }
        const auto transformed = item.CollisionShape().TransformedRustF32(candidate);
        if (!transformed.HasValue())
        {
            return;
        }
        const LbfLoss loss = LossFromShape(transformed.Value());
        if (!bestLoss.has_value() || loss < *bestLoss)
        {
            bestLoss = loss;
            tighteningHistory.push_back(loss);
        }
    };
    const auto report = [&](const jaguar::Transform2d& candidate,
                            const SampleEval& evaluation) {
        const bool accepted = best.Report(candidate, evaluation);
        if (callback != nullptr)
        {
            LbfSearchTraceEvent event;
            event.type = LbfSearchTraceEvent::Type::Report;
            event.phase = evaluatorPhase;
            event.transform = candidate;
            event.evaluation = evaluation;
            event.accepted = accepted;
            event.retained = best.Samples();
            (*callback)(event);
        }
        if (!accepted)
        {
            return;
        }
        recordTightening(candidate, evaluation);
    };

    // With no reference placement this is the container-wide phase of the
    // Rust search. The public localSearchFraction remains source-compatible,
    // but is not used as a Gaussian replacement for focused bbox sampling.
    for (std::size_t index = 0U; index < config.samplesPerItem; ++index)
    {
        const jaguar::Transform2d candidate = sampler.Sample(rng);
        report(candidate, evaluator.Evaluate(candidate, best.UpperBound()));
    }

    const auto starts = best.Samples();
    const CoordinateDescentConfig preConfig = MakeRefinementConfig(item, false);
    evaluatorPhase = LbfSearchTracePhase::PreRefine;
    evaluator.SetPhase(evaluatorPhase);
    for (const auto& start : starts)
    {
        const auto refined = RefineCoordinateDescent(
            start, evaluator, preConfig, rng);
        if (refined.HasValue())
        {
            report(refined.Value().first, refined.Value().second);
        }
    }

    const auto bestSample = best.Best();
    if (!bestSample.has_value())
    {
        return Result<std::optional<SearchResult>>::FromValue(std::nullopt);
    }
    const CoordinateDescentConfig finalConfig = MakeRefinementConfig(item, true);
    evaluatorPhase = LbfSearchTracePhase::FinalRefine;
    evaluator.SetPhase(evaluatorPhase);
    const auto final = RefineCoordinateDescent(
        *bestSample, evaluator, finalConfig, rng);
    const auto finalSample = final.HasValue() ? final.Value() : *bestSample;
    recordTightening(finalSample.first, finalSample.second);
    const auto transformed = item.CollisionShape().TransformedRustF32(
        finalSample.first);
    if (!transformed.HasValue())
    {
        return Result<std::optional<SearchResult>>::FromValue(std::nullopt);
    }
    SearchResult result;
    result.transform = finalSample.first;
    result.loss = LossFromShape(transformed.Value());
    result.samples = evaluator.EvaluationCount();
    result.tighteningHistory = std::move(tighteningHistory);
    if (callback != nullptr)
    {
        LbfSearchTraceEvent event;
        event.type = LbfSearchTraceEvent::Type::Result;
        event.phase = LbfSearchTracePhase::FinalRefine;
        event.transform = finalSample.first;
        event.evaluation = finalSample.second;
        event.evaluationCount = result.samples;
        (*callback)(event);
    }
    return Result<std::optional<SearchResult>>::FromValue(
        std::optional<SearchResult>{std::move(result)});
}
}

Result<std::optional<SearchResult>> SearchLbf(
    const jaguar::Layout& layout,
    const jaguar::Item& item,
    const LbfConfig& config,
    SparrowRng& rng,
    const jaguar::HazardFilter& filter)
{
    return SearchLbfImpl(layout, item, config, rng, filter, nullptr);
}

Result<std::optional<SearchResult>> SearchLbfWithTrace(
    const jaguar::Layout& layout,
    const jaguar::Item& item,
    const LbfConfig& config,
    SparrowRng& rng,
    const jaguar::HazardFilter& filter,
    const LbfSearchTraceCallback& callback)
{
    return SearchLbfImpl(layout, item, config, rng, filter, &callback);
}
}
