#include "SparrowCore/Optimizer/SeparatorWorker.h"
#include "SeparatorWorkerTrace.h"

#include "SparrowCore/Eval/SeparationEvaluator.h"
#include "SparrowCore/Sampling/BestSamples.h"
#include "SparrowCore/Sampling/CoordinateDescent.h"
#include "SparrowCore/Sampling/UniformRectSampler.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace
{
sparrow::Result<sparrow::SeparatorWorkerResult> InvalidWorker(const char* message)
{
    return sparrow::Result<sparrow::SeparatorWorkerResult>::FromError(
        {jaguar::ErrorCode::InvalidInput, message});
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

class TracingEvaluator final : public sparrow::SampleEvaluator
{
public:
    TracingEvaluator(
        sparrow::SeparationEvaluator& delegate,
        const sparrow::SeparatorSearchTraceCallback* callback)
        : m_Delegate(delegate),
          m_Callback(callback)
    {
    }

    void SetPhase(sparrow::SeparatorSearchTracePhase phase) noexcept
    {
        m_Phase = phase;
    }

    sparrow::SeparatorSearchTracePhase Phase() const noexcept
    {
        return m_Phase;
    }

    sparrow::SampleEval Evaluate(
        const jaguar::Transform2d& transform,
        const std::optional<sparrow::SampleEval>& upperBound) override
    {
        const sparrow::SampleEval evaluation =
            m_Delegate.Evaluate(transform, upperBound);
        if (m_Callback != nullptr)
        {
            sparrow::SeparatorSearchTraceEvent event;
            event.type = sparrow::SeparatorSearchTraceEvent::Type::Evaluation;
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
    sparrow::SeparationEvaluator& m_Delegate;
    const sparrow::SeparatorSearchTraceCallback* m_Callback = nullptr;
    sparrow::SeparatorSearchTracePhase m_Phase =
        sparrow::SeparatorSearchTracePhase::ReferencePlacement;
};
}

namespace sparrow
{
Result<void> SeparatorConfig::Validate() const
{
    if (nWorkers == 0U)
    {
        return Result<void>::FromError(
            {jaguar::ErrorCode::InvalidInput,
             "separator worker count must be positive"});
    }
    return Result<void>::FromValue();
}

namespace
{
Result<SeparatorWorkerResult> FindBestSeparatorMoveImpl(
    const jaguar::Layout& layout,
    const CollisionTracker& tracker,
    jaguar::PlacedItemHandle movingHandle,
    const SeparatorConfig& config,
    SparrowRng& rng,
    const SeparatorSearchTraceCallback* callback)
{
    const Result<void> configResult = config.Validate();
    if (!configResult.HasValue())
    {
        return Result<SeparatorWorkerResult>::FromError(configResult.GetError());
    }
    const std::optional<std::size_t> movingIndex =
        tracker.IndexForHandle(movingHandle);
    if (!movingIndex.has_value() || *movingIndex >= tracker.ItemCount())
    {
        return InvalidWorker("separator handle is not in the tracker");
    }
    const jaguar::LayoutSnapshot snapshot = layout.Save();
    if (*movingIndex >= snapshot.placements.size())
    {
        return InvalidWorker("separator layout snapshot is inconsistent");
    }
    const jaguar::PlacedItem& movingItem =
        snapshot.placements[*movingIndex].placedItem;
    SeparationEvaluator evaluator(
        layout, movingItem.GetItem(), movingHandle, tracker);
    TracingEvaluator tracingEvaluator(evaluator, callback);
    const jaguar::Rect2d itemBounds = movingItem.GetItem().CollisionShape().BoundingBox();
    const float minimumDimension = static_cast<float>(std::min(
        itemBounds.Width(), itemBounds.Height()));
    if (!std::isfinite(minimumDimension) || minimumDimension <= 0.0F)
    {
        return InvalidWorker("separator item minimum dimension is invalid");
    }
    BestSamples best(
        config.nCoordinateDescents,
        minimumDimension * 0.05F);
    tracingEvaluator.SetPhase(SeparatorSearchTracePhase::ReferencePlacement);
    const SampleEval current = tracingEvaluator.Evaluate(
        movingItem.Transform(), std::nullopt);
    const auto report = [&](const jaguar::Transform2d& transform,
                            const SampleEval& evaluation) {
        const bool accepted = best.Report(transform, evaluation);
        if (callback != nullptr)
        {
            SeparatorSearchTraceEvent event;
            event.type = SeparatorSearchTraceEvent::Type::Report;
            event.phase = tracingEvaluator.Phase();
            event.transform = transform;
            event.evaluation = evaluation;
            event.accepted = accepted;
            event.retained = best.Samples();
            (*callback)(event);
        }
        return accepted;
    };
    report(movingItem.Transform(), current);

    const jaguar::Rect2d bounds =
        layout.GetContainer().CollisionShape().BoundingBox();
    const auto focusedSampler = UniformRectSampler::Create(
        movingItem.CollisionShape().BoundingBox(),
        movingItem.GetItem(),
        bounds);
    const auto containerSampler = UniformRectSampler::Create(
        bounds, movingItem.GetItem(), bounds);
    if (focusedSampler.HasValue())
    {
        tracingEvaluator.SetPhase(SeparatorSearchTracePhase::FocusedSample);
        const UniformRectSampler sampler = focusedSampler.Value();
        for (std::size_t index = 0U;
             index < config.nFocussedSamples;
             ++index)
        {
            const jaguar::Transform2d candidate = sampler.Sample(rng);
            report(candidate,
                   tracingEvaluator.Evaluate(candidate, best.UpperBound()));
        }
    }

    if (containerSampler.HasValue())
    {
        tracingEvaluator.SetPhase(SeparatorSearchTracePhase::ContainerSample);
        const UniformRectSampler sampler = containerSampler.Value();
        for (std::size_t index = 0U;
             index < config.nContainerSamples;
             ++index)
        {
            const jaguar::Transform2d candidate = sampler.Sample(rng);
            report(candidate,
                   tracingEvaluator.Evaluate(candidate, best.UpperBound()));
        }
    }

    const auto starts = best.Samples();
    tracingEvaluator.SetPhase(SeparatorSearchTracePhase::PreRefine);
    const CoordinateDescentConfig preConfig = MakeRefinementConfig(
        movingItem.GetItem(), false);
    for (const auto& start : starts)
    {
        const auto refined = RefineCoordinateDescent(
            start, tracingEvaluator, preConfig, rng);
        if (refined.HasValue())
        {
            report(refined.Value().first, refined.Value().second);
        }
    }

    const auto bestSample = best.Best();
    std::optional<std::pair<jaguar::Transform2d, SampleEval>> finalSample;
    if (bestSample.has_value())
    {
        tracingEvaluator.SetPhase(SeparatorSearchTracePhase::FinalRefine);
        const CoordinateDescentConfig finalConfig = MakeRefinementConfig(
            movingItem.GetItem(), true);
        const auto refined = RefineCoordinateDescent(
            *bestSample, tracingEvaluator, finalConfig, rng);
        if (refined.HasValue())
        {
            finalSample = refined.Value();
        }
    }

    const auto refinedBest = best.Best();

    SeparatorWorkerResult result;
    result.evaluationCount = evaluator.EvaluationCount();
    result.surrogateRejectCount = evaluator.SurrogateRejectCount();
    result.exactCollisionCount = evaluator.ExactCollisionCount();
    if (finalSample.has_value())
    {
        result.transform = finalSample->first;
        result.evaluation = finalSample->second;
    }
    else if (refinedBest.has_value())
    {
        result.transform = refinedBest->first;
        result.evaluation = refinedBest->second;
    }
    if (callback != nullptr)
    {
        SeparatorSearchTraceEvent event;
        event.type = SeparatorSearchTraceEvent::Type::Result;
        event.phase = SeparatorSearchTracePhase::FinalRefine;
        event.evaluationCount = result.evaluationCount;
        event.present = result.transform.has_value();
        if (result.transform.has_value())
        {
            event.transform = *result.transform;
            event.evaluation = result.evaluation;
        }
        (*callback)(event);
    }
    return Result<SeparatorWorkerResult>::FromValue(std::move(result));
}
}

Result<SeparatorWorkerResult> FindBestSeparatorMove(
    const jaguar::Layout& layout,
    const CollisionTracker& tracker,
    jaguar::PlacedItemHandle movingHandle,
    const SeparatorConfig& config,
    SparrowRng& rng)
{
    return FindBestSeparatorMoveImpl(
        layout, tracker, movingHandle, config, rng, nullptr);
}

Result<SeparatorWorkerResult> FindBestSeparatorMoveWithTrace(
    const jaguar::Layout& layout,
    const CollisionTracker& tracker,
    jaguar::PlacedItemHandle movingHandle,
    const SeparatorConfig& config,
    SparrowRng& rng,
    const SeparatorSearchTraceCallback& callback)
{
    return FindBestSeparatorMoveImpl(
        layout, tracker, movingHandle, config, rng, &callback);
}
}
