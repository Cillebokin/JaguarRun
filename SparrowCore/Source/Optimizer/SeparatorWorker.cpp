#include "SparrowCore/Optimizer/SeparatorWorker.h"

#include "SparrowCore/Eval/SeparationEvaluator.h"
#include "SparrowCore/LbfConfig.h"
#include "SparrowCore/Sampling/BestSamples.h"
#include "SparrowCore/Sampling/CoordinateDescent.h"
#include "SparrowCore/Sampling/LocalSearchSampler.h"
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

sparrow::LbfConfig MakeLocalConfig(const sparrow::SeparatorConfig& config)
{
    sparrow::LbfConfig result;
    result.samplesPerItem = std::max<std::size_t>(1U, config.nFocussedSamples);
    result.localSearchFraction = 0.0;
    result.minTranslationStddevRatio = 0.0005;
    result.maxTranslationStddevRatio = 0.05;
    result.minRotationStddevRad = 0.5 * 3.14159265358979323846 / 180.0;
    result.maxRotationStddevRad = 5.0 * 3.14159265358979323846 / 180.0;
    return result;
}
}

namespace sparrow
{
Result<void> SeparatorConfig::Validate() const
{
    if (strikeLimit == 0U || noImprovementLimit == 0U)
    {
        return Result<void>::FromError(
            {jaguar::ErrorCode::InvalidInput,
             "separator limits must be positive"});
    }
    if (nContainerSamples == 0U && nFocussedSamples == 0U)
    {
        return Result<void>::FromError(
            {jaguar::ErrorCode::InvalidInput,
             "separator needs a positive sample budget"});
    }
    return Result<void>::FromValue();
}

Result<SeparatorWorkerResult> FindBestSeparatorMove(
    const jaguar::Layout& layout,
    const CollisionTracker& tracker,
    jaguar::PlacedItemHandle movingHandle,
    const SeparatorConfig& config,
    SparrowRng& rng)
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
    BestSamples best(
        std::max<std::size_t>(1U,
                              config.nContainerSamples + config.nFocussedSamples),
        0.01F);
    const SampleEval current = evaluator.Evaluate(movingItem.Transform(), std::nullopt);
    best.Report(movingItem.Transform(), current);

    const jaguar::Rect2d bounds = layout.GetContainer().Bounds();
    const auto containerSampler = UniformRectSampler::Create(
        bounds, movingItem.GetItem());
    if (!containerSampler.HasValue())
    {
        return Result<SeparatorWorkerResult>::FromError(
            containerSampler.GetError());
    }
    UniformRectSampler sampler = containerSampler.Value();
    for (std::size_t index = 0U; index < config.nContainerSamples; ++index)
    {
        const jaguar::Transform2d candidate = sampler.Sample(rng);
        const SampleEval evaluation = evaluator.Evaluate(candidate, best.UpperBound());
        best.Report(candidate, evaluation);
    }

    const std::optional<std::pair<jaguar::Transform2d, SampleEval>> bestSample =
        best.Best();
    if (bestSample.has_value() && config.nFocussedSamples > 0U)
    {
        const LbfConfig localConfig = MakeLocalConfig(config);
        const auto localSamplerResult = LocalSearchSampler::Create(
            movingItem.GetItem(),
            bestSample->first,
            localConfig,
            bounds);
        if (localSamplerResult.HasValue())
        {
            LocalSearchSampler localSampler = localSamplerResult.Value();
            for (std::size_t index = 0U;
                 index < config.nFocussedSamples;
                 ++index)
            {
                const jaguar::Transform2d candidate = localSampler.Sample(rng);
                const SampleEval evaluation = evaluator.Evaluate(
                    candidate, best.UpperBound());
                if (best.Report(candidate, evaluation))
                {
                    localSampler.ShiftMean(candidate);
                }
                localSampler.DecayStddev(
                    static_cast<float>(index + 1U)
                    / static_cast<float>(config.nFocussedSamples));
            }
        }
    }

    std::optional<std::pair<jaguar::Transform2d, SampleEval>> refinedBest =
        best.Best();
    if (refinedBest.has_value() && config.nCoordinateDescents > 0U)
    {
        const jaguar::Rect2d itemBounds =
            movingItem.GetItem().CollisionShape().BoundingBox();
        const float minimumDimension = static_cast<float>(std::min(
            itemBounds.Width(), itemBounds.Height()));
        if (std::isfinite(minimumDimension) && minimumDimension > 0.0F)
        {
            CoordinateDescentConfig descentConfig;
            descentConfig.translationStepInit = minimumDimension * 0.25F;
            descentConfig.translationStepLimit = minimumDimension * 0.02F;
            descentConfig.rotationStepInit =
                5.0F * 3.14159265358979323846F / 180.0F;
            descentConfig.rotationStepLimit =
                1.0F * 3.14159265358979323846F / 180.0F;
            descentConfig.rotationWiggle =
                movingItem.GetItem().AllowedRotation().mode
                == jaguar::RotationMode::Continuous;
            descentConfig.maxEvaluations = std::max<std::size_t>(
                64U,
                config.nContainerSamples + config.nFocussedSamples);
            for (std::size_t index = 0U;
                 index < config.nCoordinateDescents;
                 ++index)
            {
                const auto refined = RefineCoordinateDescent(
                    *refinedBest,
                    evaluator,
                    descentConfig,
                    rng);
                if (!refined.HasValue())
                {
                    break;
                }
                if (IsBetterSample(refined.Value().second, refinedBest->second))
                {
                    refinedBest = refined.Value();
                    best.Report(refined.Value().first, refined.Value().second);
                }
            }
        }
    }

    SeparatorWorkerResult result;
    result.evaluationCount = evaluator.EvaluationCount();
    if (refinedBest.has_value())
    {
        result.transform = refinedBest->first;
        result.evaluation = refinedBest->second;
    }
    return Result<SeparatorWorkerResult>::FromValue(std::move(result));
}
}
