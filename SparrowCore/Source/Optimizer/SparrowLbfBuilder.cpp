#include "SparrowCore/Optimizer/SparrowLbfBuilder.h"

#include "SparrowCore/Eval/SparrowLbfEvaluator.h"
#include "SparrowCore/Optimizer/LbfSearch.h"
#include "SparrowCore/Sampling/CoordinateDescent.h"

#include "JaguarCore/Assertions.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace
{
struct OrderedItem
{
    std::size_t index = 0U;
    double key = 0.0;
};

sparrow::Result<sparrow::SppLbfResult> InvalidConstruction(const char* message)
{
    return sparrow::Result<sparrow::SppLbfResult>::FromError(
        {jaguar::ErrorCode::InvalidInput, message});
}

sparrow::Result<sparrow::SppLbfResult> InfeasibleConstruction(const char* message)
{
    return sparrow::Result<sparrow::SppLbfResult>::FromError(
        {jaguar::ErrorCode::Infeasible, message});
}

sparrow::LbfConfig MakeSearchConfig(
    const sparrow::SparrowConstructorConfig& config)
{
    sparrow::LbfConfig result;
    result.seed = config.seed;
    result.samplesPerItem = config.nContainerSamples + config.nFocussedSamples;
    result.localSearchFraction = result.samplesPerItem == 0U
        ? 0.0
        : static_cast<double>(config.nFocussedSamples)
              / static_cast<double>(result.samplesPerItem);
    result.maxWidthGrowthAttempts = config.maxWidthGrowthAttempts;
    result.widthGrowthRatio = config.widthGrowthRatio;
    result.useSparrowConstructorOrder = true;
    return result;
}

bool IsFiniteWidth(double width) noexcept
{
    return std::isfinite(width) && width > 0.0;
}

sparrow::Result<std::optional<sparrow::SearchResult>> FindPlacement(
    const jaguar::SPProblem& problem,
    const jaguar::Item& item,
    const sparrow::SparrowConstructorConfig& constructorConfig,
    const sparrow::LbfConfig& searchConfig,
    sparrow::SparrowRng& rng)
{
    const jaguar::SPSolution snapshot = problem.Save();
    jaguar::Layout layout(snapshot.layoutSnapshot.container);
    const auto restored = layout.Restore(snapshot.layoutSnapshot);
    if (!restored.HasValue())
    {
        return sparrow::Result<std::optional<sparrow::SearchResult>>::FromError(
            restored.GetError());
    }

    jaguar::HazardFilter filter;
    const auto minQuality = item.MinQuality();
    filter.irrelevantHandle = [minQuality](
                                  jaguar::HazardHandle,
                                  const jaguar::HazardEntity& entity) {
        return minQuality.has_value()
            && entity.kind == jaguar::HazardKind::InferiorQualityZone
            && entity.quality < *minQuality;
    };

    auto searched = sparrow::SearchLbf(layout, item, searchConfig, rng, filter);
    if (!searched.HasValue() || !searched.Value().has_value())
    {
        if (!searched.HasValue())
        {
            return searched;
        }
        return sparrow::Result<std::optional<sparrow::SearchResult>>::FromValue(
            std::nullopt);
    }

    sparrow::SearchResult best = *searched.Value();
    sparrow::SparrowLbfEvaluator evaluator(layout, item, filter);
    sparrow::SampleEval bestEvaluation =
        evaluator.Evaluate(best.transform, std::nullopt);
    if (bestEvaluation.kind != sparrow::SampleKind::SAMPLE_KIND_CLEAR)
    {
        return sparrow::Result<std::optional<sparrow::SearchResult>>::FromValue(
            std::nullopt);
    }

    const jaguar::Rect2d bounds = item.CollisionShape().BoundingBox();
    const float minimumDimension = static_cast<float>(
        std::min(bounds.Width(), bounds.Height()));
    if (std::isfinite(minimumDimension) && minimumDimension > 0.0F)
    {
        sparrow::CoordinateDescentConfig descentConfig;
        descentConfig.translationStepInit = minimumDimension * 0.25F;
        descentConfig.translationStepLimit = minimumDimension * 0.02F;
        descentConfig.rotationStepInit = 5.0F * 3.14159265358979323846F / 180.0F;
        descentConfig.rotationStepLimit = 1.0F * 3.14159265358979323846F / 180.0F;
        descentConfig.rotationWiggle =
            item.AllowedRotation().mode == jaguar::RotationMode::Continuous;
        descentConfig.maxEvaluations = std::max<std::size_t>(64U, searchConfig.samplesPerItem);

        for (std::size_t index = 0U;
             index < constructorConfig.nCoordinateDescents;
             ++index)
        {
            const auto refined = sparrow::RefineCoordinateDescent(
                {best.transform, bestEvaluation},
                evaluator,
                descentConfig,
                rng);
            if (!refined.HasValue())
            {
                if (refined.GetError().code == jaguar::ErrorCode::TimeLimit)
                {
                    break;
                }
                return sparrow::Result<std::optional<sparrow::SearchResult>>::FromError(
                    refined.GetError());
            }
            if (refined.Value().second.kind
                    == sparrow::SampleKind::SAMPLE_KIND_CLEAR
                && sparrow::IsBetterSample(
                       refined.Value().second, bestEvaluation))
            {
                best.transform = refined.Value().first;
                bestEvaluation = refined.Value().second;
                const auto transformed = item.CollisionShape().Transformed(
                    best.transform);
                if (transformed.HasValue())
                {
                    best.loss = sparrow::LbfLoss::FromBoundingBox(
                        transformed.Value().BoundingBox());
                }
            }
        }
    }
    best.samples += evaluator.EvaluationCount();
    return sparrow::Result<std::optional<sparrow::SearchResult>>::FromValue(
        std::move(best));
}
}

namespace sparrow
{
Result<SppLbfResult> ConstructSparrowInitialSolution(
    const jaguar::SPInstance& instance,
    const SparrowConstructorConfig& config)
{
    const Result<void> configResult = config.Validate();
    if (!configResult.HasValue())
    {
        return Result<SppLbfResult>::FromError(configResult.GetError());
    }
    if (instance.ItemCount() == 0U || instance.TotalDemand() == 0U)
    {
        return InvalidConstruction("Sparrow constructor received an empty instance");
    }

    std::vector<OrderedItem> itemOrder;
    itemOrder.reserve(instance.TotalDemand());
    double widthCeiling = 0.0;
    for (std::size_t index = 0U; index < instance.ItemCount(); ++index)
    {
        const jaguar::Item& item = instance.GetItem(index);
        const jaguar::Surrogate* surrogate = item.CollisionShape().GetSurrogate();
        const double diameter = item.CollisionShape().Diameter();
        if (surrogate == nullptr || !std::isfinite(diameter) || diameter <= 0.0)
        {
            return InvalidConstruction("Sparrow item geometry is invalid");
        }
        const double key = surrogate->ConvexHullArea() * diameter;
        if (!std::isfinite(key))
        {
            return InvalidConstruction("Sparrow item order key is not finite");
        }
        itemOrder.push_back({index, key});
        widthCeiling += diameter * static_cast<double>(instance.ItemDemand(index));
    }
    std::sort(itemOrder.begin(), itemOrder.end(), [](const OrderedItem& left,
                                                     const OrderedItem& right) {
        if (left.key != right.key)
        {
            return left.key > right.key;
        }
        return left.index < right.index;
    });
    widthCeiling *= config.widthCeilingFactor;
    if (!IsFiniteWidth(widthCeiling))
    {
        return InvalidConstruction("Sparrow width ceiling is invalid");
    }

    auto problemResult = jaguar::SPProblem::Create(instance);
    if (!problemResult.HasValue())
    {
        return Result<SppLbfResult>::FromError(problemResult.GetError());
    }
    jaguar::SPProblem problem = std::move(problemResult.Value());
    SparrowRng rng(config.seed);
    const LbfConfig searchConfig = MakeSearchConfig(config);
    std::size_t sampleCount = 0U;
    std::size_t widthAttempts = 0U;

    for (const OrderedItem& ordered : itemOrder)
    {
        while (problem.RemainingDemand(ordered.index) > 0U)
        {
            const auto placement = FindPlacement(
                problem,
                instance.GetItem(ordered.index),
                config,
                searchConfig,
                rng);
            if (!placement.HasValue())
            {
                return Result<SppLbfResult>::FromError(placement.GetError());
            }
            if (!placement.Value().has_value())
            {
                if (widthAttempts >= config.maxWidthGrowthAttempts)
                {
                    return InfeasibleConstruction(
                        "Sparrow constructor width growth limit exceeded");
                }
                const double nextWidth = problem.StripWidth() * config.widthGrowthRatio;
                if (!IsFiniteWidth(nextWidth) || nextWidth > widthCeiling)
                {
                    return InfeasibleConstruction(
                        "Sparrow constructor width ceiling exceeded");
                }
                const auto changed = problem.ChangeStripWidth(nextWidth);
                if (!changed.HasValue())
                {
                    return Result<SppLbfResult>::FromError(changed.GetError());
                }
                ++widthAttempts;
                continue;
            }

            const SearchResult& found = *placement.Value();
            const auto placed = problem.PlaceItem({ordered.index, found.transform});
            if (!placed.HasValue())
            {
                return Result<SppLbfResult>::FromError(placed.GetError());
            }
            sampleCount += found.samples;
        }
    }

    const auto fitted = problem.FitStrip();
    if (!fitted.HasValue())
    {
        return Result<SppLbfResult>::FromError(fitted.GetError());
    }

    const jaguar::SPSolution solution = problem.Save();
    LbfMetrics metrics;
    metrics.seed = config.seed;
    metrics.sampleCount = sampleCount;
    metrics.placedItemCount = problem.PlacedItemCount();
    metrics.remainingDemand = 0U;
    for (std::size_t index = 0U; index < instance.ItemCount(); ++index)
    {
        metrics.remainingDemand += problem.RemainingDemand(index);
    }
    metrics.stripWidth = solution.StripWidth();
    metrics.density = solution.Density();
    metrics.feasible = metrics.remainingDemand == 0U
        && jaguar::ValidateSPSolution(problem, solution).HasValue();
    if (!metrics.feasible)
    {
        return InfeasibleConstruction("Sparrow constructor produced an invalid solution");
    }
    return Result<SppLbfResult>::FromValue({solution, metrics});
}
}
