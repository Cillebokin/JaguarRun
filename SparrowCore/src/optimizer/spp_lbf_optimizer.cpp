#include "SparrowCore/optimizer/spp_lbf_optimizer.h"

#include "JaguarCore/assertions.h"

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

sparrow::Result<sparrow::SppLbfResult> invalidSpp(const char* message)
{
    return sparrow::Result<sparrow::SppLbfResult>::fromError(
        {jaguar::ErrorCode::InvalidInput, message});
}

sparrow::Result<sparrow::SppLbfResult> infeasibleSpp(const char* message)
{
    return sparrow::Result<sparrow::SppLbfResult>::fromError(
        {jaguar::ErrorCode::Infeasible, message});
}
}

namespace sparrow
{
Result<SppLbfResult> solveSppLbf(
    const jaguar::SPInstance& instance,
    const LbfConfig& config)
{
    const Result<void> configResult = config.validate();
    if (!configResult.hasValue())
    {
        return Result<SppLbfResult>::fromError(configResult.error());
    }
    if (instance.itemCount() == 0U || instance.totalDemand() == 0U)
    {
        return invalidSpp("SPP instance has no demanded items");
    }

    std::vector<OrderedItem> order;
    order.reserve(instance.itemCount());
    for (std::size_t index = 0; index < instance.itemCount(); ++index)
    {
        const jaguar::Item& item = instance.item(index);
        const jaguar::Surrogate* surrogate = item.collisionShape().surrogate();
        if (surrogate == nullptr || !std::isfinite(item.collisionShape().diameter()))
        {
            return invalidSpp("SPP item has no valid surrogate or diameter");
        }
        const double key = config.useSparrowConstructorOrder
            ? surrogate->convexHullArea() * item.collisionShape().diameter()
            : item.collisionShape().diameter();
        if (!std::isfinite(key))
        {
            return invalidSpp("SPP item order key is not finite");
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

    auto problemResult = jaguar::SPProblem::create(instance);
    if (!problemResult.hasValue())
    {
        return Result<SppLbfResult>::fromError(problemResult.error());
    }
    jaguar::SPProblem problem = std::move(problemResult.value());
    SparrowRng rng(config.seed);
    std::size_t sampleCount = 0U;
    std::size_t widthGrowthAttempts = 0U;
    const std::size_t initialDemand = instance.totalDemand();

    for (const OrderedItem& ordered : order)
    {
        while (problem.remainingDemand(ordered.index) > 0U)
        {
            const jaguar::SPSolution current = problem.save();
            jaguar::Layout currentLayout(current.layoutSnapshot.container);
            const auto restored = currentLayout.restore(current.layoutSnapshot);
            if (!restored.hasValue())
            {
                return Result<SppLbfResult>::fromError(restored.error());
            }

            jaguar::HazardFilter filter;
            const auto minQuality = instance.item(ordered.index).minQuality();
            filter.irrelevantHandle = [minQuality](
                                          jaguar::HazardHandle,
                                          const jaguar::HazardEntity& entity) {
                return minQuality.has_value()
                    && entity.kind == jaguar::HazardKind::InferiorQualityZone
                    && entity.quality < *minQuality;
            };

            const auto search = searchLbf(
                currentLayout,
                instance.item(ordered.index),
                config,
                rng,
                filter);
            if (!search.hasValue())
            {
                return Result<SppLbfResult>::fromError(search.error());
            }
            if (!search.value().has_value())
            {
                const std::size_t localSamples = static_cast<std::size_t>(std::floor(
                    static_cast<double>(config.samplesPerItem)
                    * config.localSearchFraction));
                sampleCount += config.samplesPerItem - localSamples;
                if (widthGrowthAttempts >= config.maxWidthGrowthAttempts)
                {
                    return infeasibleSpp("SPP strip width growth limit exceeded");
                }
                const double nextWidth = problem.stripWidth() * config.widthGrowthRatio;
                const auto changed = problem.changeStripWidth(nextWidth);
                if (!changed.hasValue())
                {
                    return Result<SppLbfResult>::fromError(changed.error());
                }
                ++widthGrowthAttempts;
                continue;
            }

            const SearchResult& placement = *search.value();
            const auto placed = problem.placeItem({ordered.index, placement.transform});
            if (!placed.hasValue())
            {
                return Result<SppLbfResult>::fromError(placed.error());
            }
            sampleCount += placement.samples;
        }
    }

    if (problem.placedItemCount() != initialDemand)
    {
        return infeasibleSpp("SPP demand was not completely placed");
    }
    const auto fitted = problem.fitStrip();
    if (!fitted.hasValue())
    {
        return Result<SppLbfResult>::fromError(fitted.error());
    }

    const jaguar::SPSolution solution = problem.save();
    LbfMetrics metrics;
    metrics.seed = config.seed;
    metrics.sampleCount = sampleCount;
    metrics.placedItemCount = problem.placedItemCount();
    metrics.remainingDemand = 0U;
    metrics.stripWidth = solution.stripWidth();
    metrics.density = solution.density();
    metrics.feasible = jaguar::validateSPSolution(problem, solution).hasValue();
    if (!metrics.feasible)
    {
        return infeasibleSpp("SPP solution validation failed");
    }
    return Result<SppLbfResult>::fromValue({solution, metrics});
}
}
