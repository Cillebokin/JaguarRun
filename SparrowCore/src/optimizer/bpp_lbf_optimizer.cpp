#include "SparrowCore/optimizer/bpp_lbf_optimizer.h"

#include "JaguarCore/assertions.h"

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

sparrow::Result<sparrow::BppLbfResult> invalidBpp(const char* message)
{
    return sparrow::Result<sparrow::BppLbfResult>::fromError(
        {jaguar::ErrorCode::InvalidInput, message});
}
}

namespace sparrow
{
Result<BppLbfResult> solveBppLbf(
    const jaguar::BPInstance& instance,
    const LbfConfig& config)
{
    const Result<void> configResult = config.validate();
    if (!configResult.hasValue())
    {
        return Result<BppLbfResult>::fromError(configResult.error());
    }
    if (instance.itemCount() == 0U || instance.binCount() == 0U
        || instance.totalDemand() == 0U)
    {
        return invalidBpp("BPP instance has no demanded items or bins");
    }

    std::vector<OrderedItem> order;
    order.reserve(instance.itemCount());
    for (std::size_t index = 0; index < instance.itemCount(); ++index)
    {
        const jaguar::Item& item = instance.item(index);
        const jaguar::Surrogate* surrogate = item.collisionShape().surrogate();
        if (surrogate == nullptr)
        {
            return invalidBpp("BPP item has no cached surrogate");
        }
        const double key = config.useSparrowConstructorOrder
            ? surrogate->convexHullArea() * item.collisionShape().diameter()
            : item.collisionShape().diameter();
        if (!std::isfinite(key))
        {
            return invalidBpp("BPP item order key is not finite");
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

    auto problemResult = jaguar::BPProblem::create(instance);
    if (!problemResult.hasValue())
    {
        return Result<BppLbfResult>::fromError(problemResult.error());
    }
    jaguar::BPProblem problem = std::move(problemResult.value());
    SparrowRng rng(config.seed);
    std::size_t sampleCount = 0U;

    for (const OrderedItem& ordered : order)
    {
        while (problem.remainingDemand(ordered.index) > 0U)
        {
            const jaguar::BPSolution current = problem.save();
            std::optional<jaguar::BPPlacement> found;

            for (const jaguar::BPLayoutSnapshot& open : current.layouts)
            {
                jaguar::Layout layout(open.layoutSnapshot.container);
                const auto restored = layout.restore(open.layoutSnapshot);
                if (!restored.hasValue())
                {
                    return Result<BppLbfResult>::fromError(restored.error());
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
                const auto searched = searchLbf(
                    layout, instance.item(ordered.index), config, rng, filter);
                if (!searched.hasValue())
                {
                    return Result<BppLbfResult>::fromError(searched.error());
                }
                sampleCount += searched.value().has_value()
                    ? searched.value()->samples
                    : config.samplesPerItem;
                if (searched.value().has_value())
                {
                    found = jaguar::BPPlacement{
                        jaguar::LayoutTargetKind::Open,
                        open.layoutIndex,
                        open.binIndex,
                        ordered.index,
                        searched.value()->transform};
                    break;
                }
            }

            if (!found.has_value())
            {
                for (std::size_t binIndex = 0; binIndex < instance.binCount(); ++binIndex)
                {
                    if (problem.remainingBinStock(binIndex) == 0U)
                    {
                        continue;
                    }
                    jaguar::Layout layout(instance.bin(binIndex).container);
                    jaguar::HazardFilter filter;
                    const auto minQuality = instance.item(ordered.index).minQuality();
                    filter.irrelevantHandle = [minQuality](
                                                  jaguar::HazardHandle,
                                                  const jaguar::HazardEntity& entity) {
                        return minQuality.has_value()
                            && entity.kind == jaguar::HazardKind::InferiorQualityZone
                            && entity.quality < *minQuality;
                    };
                    const auto searched = searchLbf(
                        layout, instance.item(ordered.index), config, rng, filter);
                    if (!searched.hasValue())
                    {
                        return Result<BppLbfResult>::fromError(searched.error());
                    }
                    sampleCount += searched.value().has_value()
                        ? searched.value()->samples
                        : config.samplesPerItem;
                    if (searched.value().has_value())
                    {
                        found = jaguar::BPPlacement{
                            jaguar::LayoutTargetKind::Closed,
                            0U,
                            binIndex,
                            ordered.index,
                            searched.value()->transform};
                        break;
                    }
                }
            }

            if (!found.has_value())
            {
                break;
            }
            const auto placed = problem.placeItem(*found);
            if (!placed.hasValue())
            {
                return Result<BppLbfResult>::fromError(placed.error());
            }
        }
    }

    const jaguar::BPSolution solution = problem.save();
    LbfMetrics metrics;
    metrics.seed = config.seed;
    metrics.sampleCount = sampleCount;
    metrics.placedItemCount = problem.placedItemCount();
    metrics.remainingDemand = 0U;
    for (std::size_t index = 0; index < instance.itemCount(); ++index)
    {
        metrics.remainingDemand += problem.remainingDemand(index);
    }
    metrics.stripWidth = 0.0;
    metrics.density = solution.density();
    metrics.feasible = metrics.remainingDemand == 0U
        && jaguar::validateBPSolution(problem, solution).hasValue();
    return Result<BppLbfResult>::fromValue({solution, metrics});
}
}
