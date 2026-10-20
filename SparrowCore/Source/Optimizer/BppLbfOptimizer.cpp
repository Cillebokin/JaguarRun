#include "SparrowCore/Optimizer/BppLbfOptimizer.h"

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

sparrow::Result<sparrow::BppLbfResult> InvalidBpp(const char* message)
{
    return sparrow::Result<sparrow::BppLbfResult>::FromError(
        {jaguar::ErrorCode::InvalidInput, message});
}
}

namespace sparrow
{
Result<BppLbfResult> SolveBppLbf(
    const jaguar::BPInstance& instance,
    const LbfConfig& config)
{
    const Result<void> configResult = config.Validate();
    if (!configResult.HasValue())
    {
        return Result<BppLbfResult>::FromError(configResult.GetError());
    }
    if (instance.ItemCount() == 0U || instance.BinCount() == 0U
        || instance.TotalDemand() == 0U)
    {
        return InvalidBpp("BPP instance has no demanded items or bins");
    }

    std::vector<OrderedItem> order;
    order.reserve(instance.ItemCount());
    for (std::size_t index = 0; index < instance.ItemCount(); ++index)
    {
        const jaguar::Item& item = instance.GetItem(index);
        const jaguar::Surrogate* surrogate = item.CollisionShape().GetSurrogate();
        if (surrogate == nullptr)
        {
            return InvalidBpp("BPP item has no cached surrogate");
        }
        const double key = config.useSparrowConstructorOrder
            ? surrogate->ConvexHullArea() * item.CollisionShape().Diameter()
            : item.CollisionShape().Diameter();
        if (!std::isfinite(key))
        {
            return InvalidBpp("BPP item order key is not finite");
        }
        order.push_back({index, key});
    }
    std::sort(order.begin(), order.end(), [](const OrderedItem& left,
                                             const OrderedItem& right) {
        if (left.key != right.key)
        {
            return left.key > right.key;
        }
        return left.index < right.index;
    });

    auto problemResult = jaguar::BPProblem::Create(instance);
    if (!problemResult.HasValue())
    {
        return Result<BppLbfResult>::FromError(problemResult.GetError());
    }
    jaguar::BPProblem problem = std::move(problemResult.Value());
    SparrowRng rng(config.seed);
    std::size_t sampleCount = 0U;

    for (const OrderedItem& ordered : order)
    {
        while (problem.RemainingDemand(ordered.index) > 0U)
        {
            const jaguar::BPSolution current = problem.Save();
            std::optional<jaguar::BPPlacement> found;

            for (const jaguar::BPLayoutSnapshot& open : current.layouts)
            {
                jaguar::Layout layout(open.layoutSnapshot.container);
                const auto restored = layout.Restore(open.layoutSnapshot);
                if (!restored.HasValue())
                {
                    return Result<BppLbfResult>::FromError(restored.GetError());
                }
                jaguar::HazardFilter filter;
                const auto minQuality = instance.GetItem(ordered.index).MinQuality();
                filter.irrelevantHandle = [minQuality](
                                              jaguar::HazardHandle,
                                              const jaguar::HazardEntity& entity) {
                    return minQuality.has_value()
                        && entity.kind == jaguar::HazardKind::InferiorQualityZone
                        && entity.quality < *minQuality;
                };
                const auto searched = SearchLbf(
                    layout, instance.GetItem(ordered.index), config, rng, filter);
                if (!searched.HasValue())
                {
                    return Result<BppLbfResult>::FromError(searched.GetError());
                }
                sampleCount += searched.Value().has_value()
                    ? searched.Value()->samples
                    : config.UniformSampleBudget();
                if (searched.Value().has_value())
                {
                    found = jaguar::BPPlacement{
                        jaguar::LayoutTargetKind::Open,
                        open.layoutIndex,
                        open.binIndex,
                        ordered.index,
                        searched.Value()->transform};
                    break;
                }
            }

            if (!found.has_value())
            {
                for (std::size_t binIndex = 0; binIndex < instance.BinCount(); ++binIndex)
                {
                    if (problem.RemainingBinStock(binIndex) == 0U)
                    {
                        continue;
                    }
                    jaguar::Layout layout(instance.GetBin(binIndex).container);
                    jaguar::HazardFilter filter;
                    const auto minQuality = instance.GetItem(ordered.index).MinQuality();
                    filter.irrelevantHandle = [minQuality](
                                                  jaguar::HazardHandle,
                                                  const jaguar::HazardEntity& entity) {
                        return minQuality.has_value()
                            && entity.kind == jaguar::HazardKind::InferiorQualityZone
                            && entity.quality < *minQuality;
                    };
                    const auto searched = SearchLbf(
                        layout, instance.GetItem(ordered.index), config, rng, filter);
                    if (!searched.HasValue())
                    {
                        return Result<BppLbfResult>::FromError(searched.GetError());
                    }
                    sampleCount += searched.Value().has_value()
                        ? searched.Value()->samples
                        : config.UniformSampleBudget();
                    if (searched.Value().has_value())
                    {
                        found = jaguar::BPPlacement{
                            jaguar::LayoutTargetKind::Closed,
                            0U,
                            binIndex,
                            ordered.index,
                            searched.Value()->transform};
                        break;
                    }
                }
            }

            if (!found.has_value())
            {
                break;
            }
            const auto placed = problem.PlaceItem(*found);
            if (!placed.HasValue())
            {
                return Result<BppLbfResult>::FromError(placed.GetError());
            }
        }
    }

    const jaguar::BPSolution solution = problem.Save();
    LbfMetrics metrics;
    metrics.seed = config.seed;
    metrics.sampleCount = sampleCount;
    metrics.placedItemCount = problem.PlacedItemCount();
    metrics.remainingDemand = 0U;
    for (std::size_t index = 0; index < instance.ItemCount(); ++index)
    {
        metrics.remainingDemand += problem.RemainingDemand(index);
    }
    metrics.stripWidth = 0.0;
    metrics.density = solution.Density();
    metrics.feasible = metrics.remainingDemand == 0U
        && jaguar::ValidateBPSolution(problem, solution).HasValue();
    return Result<BppLbfResult>::FromValue({solution, metrics});
}
}
