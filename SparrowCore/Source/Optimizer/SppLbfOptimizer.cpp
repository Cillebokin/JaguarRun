#include "SparrowCore/Optimizer/SppLbfOptimizer.h"

#include "LbfOptimizerTrace.h"
#include "JaguarCore/Assertions.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <utility>
#include <vector>

namespace
{
struct OrderedItem
{
    std::size_t index = 0U;
    double key = 0.0;
};

sparrow::Result<sparrow::SppLbfResult> InvalidSpp(const char* message)
{
    return sparrow::Result<sparrow::SppLbfResult>::FromError(
        {jaguar::ErrorCode::InvalidInput, message});
}

sparrow::Result<sparrow::SppLbfResult> InfeasibleSpp(const char* message)
{
    return sparrow::Result<sparrow::SppLbfResult>::FromError(
        {jaguar::ErrorCode::Infeasible, message});
}

bool StripWidthIsInCheck(const jaguar::SPInstance& instance,
                         float stripWidth) noexcept
{
    float diametersOfAllItems = 0.0F;
    for (std::size_t index = 0U; index < instance.ItemCount(); ++index)
    {
        const float diameter = static_cast<float>(
            instance.GetItem(index).CollisionShape().Diameter());
        const float demand = static_cast<float>(instance.ItemDemand(index));
        diametersOfAllItems += diameter * demand;
    }
    return stripWidth < 2.0F * diametersOfAllItems;
}
}

namespace sparrow
{
Result<SppLbfResult> SolveSppLbfWithDiagnostics(
    const jaguar::SPInstance& instance,
    const LbfConfig& config,
    const LbfDiagnosticCallback& callback)
{
    const Result<void> configResult = config.Validate();
    if (!configResult.HasValue())
    {
        return Result<SppLbfResult>::FromError(configResult.GetError());
    }
    if (instance.ItemCount() == 0U || instance.TotalDemand() == 0U)
    {
        return InvalidSpp("SPP instance has no demanded items");
    }

    std::vector<OrderedItem> order;
    order.reserve(instance.ItemCount());
    for (std::size_t index = 0; index < instance.ItemCount(); ++index)
    {
        const jaguar::Item& item = instance.GetItem(index);
        const jaguar::Surrogate* surrogate = item.CollisionShape().GetSurrogate();
        if (surrogate == nullptr || !std::isfinite(item.CollisionShape().Diameter()))
        {
            return InvalidSpp("SPP item has no valid surrogate or diameter");
        }
        const double key = config.useSparrowConstructorOrder
            ? surrogate->ConvexHullArea() * item.CollisionShape().Diameter()
            : item.CollisionShape().Diameter();
        if (!std::isfinite(key))
        {
            return InvalidSpp("SPP item order key is not finite");
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

    auto problemResult = jaguar::SPProblem::Create(instance);
    if (!problemResult.HasValue())
    {
        return Result<SppLbfResult>::FromError(problemResult.GetError());
    }
    jaguar::SPProblem problem = std::move(problemResult.Value());
    SparrowRng rng(config.seed);
    std::size_t sampleCount = 0U;
    std::size_t widthGrowthAttempts = 0U;
    const std::size_t initialDemand = instance.TotalDemand();

    for (const OrderedItem& ordered : order)
    {
        while (problem.RemainingDemand(ordered.index) > 0U)
        {
            const jaguar::SPSolution current = problem.Save();
            jaguar::Layout currentLayout(current.layoutSnapshot.container);
            const auto restored = currentLayout.Restore(current.layoutSnapshot);
            if (!restored.HasValue())
            {
                return Result<SppLbfResult>::FromError(restored.GetError());
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

            const auto search = callback
                ? SearchLbfWithDiagnostics(
                      currentLayout,
                      instance.GetItem(ordered.index),
                      config,
                      rng,
                      filter,
                      callback)
                : SearchLbf(
                      currentLayout,
                      instance.GetItem(ordered.index),
                      config,
                      rng,
                      filter);
            if (!search.HasValue())
            {
                return Result<SppLbfResult>::FromError(search.GetError());
            }
            if (!search.Value().has_value())
            {
                sampleCount += config.UniformSampleBudget();
                if (widthGrowthAttempts >= config.maxWidthGrowthAttempts)
                {
                    return InfeasibleSpp("SPP strip width growth limit exceeded");
                }
                const float nextWidth = static_cast<float>(problem.StripWidth())
                    * static_cast<float>(config.widthGrowthRatio);
                const auto changed = problem.ChangeStripWidth(
                    static_cast<double>(nextWidth));
                if (!changed.HasValue())
                {
                    return Result<SppLbfResult>::FromError(changed.GetError());
                }
                if (!StripWidthIsInCheck(
                        instance, static_cast<float>(problem.StripWidth())))
                {
                    return InfeasibleSpp(
                        " strip width is running away, check if all items fit in the height of the strip");
                }
                ++widthGrowthAttempts;
                continue;
            }

            const SearchResult& placement = *search.Value();
            const auto placed = problem.PlaceItem({ordered.index, placement.transform});
            if (!placed.HasValue())
            {
                return Result<SppLbfResult>::FromError(placed.GetError());
            }
            sampleCount += placement.samples;
        }
    }

    if (problem.PlacedItemCount() != initialDemand)
    {
        return InfeasibleSpp("SPP demand was not completely placed");
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
    metrics.stripWidth = solution.StripWidth();
    metrics.density = solution.Density();
    metrics.feasible = jaguar::ValidateSPSolution(problem, solution).HasValue();
    if (!metrics.feasible)
    {
        return InfeasibleSpp("SPP solution validation failed");
    }
    return Result<SppLbfResult>::FromValue({solution, metrics});
}

Result<SppLbfResult> SolveSppLbf(
    const jaguar::SPInstance& instance,
    const LbfConfig& config)
{
    return SolveSppLbfWithDiagnostics(instance, config, {});
}
}
