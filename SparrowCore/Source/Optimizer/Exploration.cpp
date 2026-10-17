#include "SparrowCore/Optimizer/Exploration.h"
#include "SparrowCore/Optimizer/OptimizationObserver.h"
#include "SparrowCore/Optimizer/Separator.h"
#include "ExplorationNumeric.h"
#include "ExplorationNormalSampler.h"
#include "ExplorationTrace.h"

#include "JaguarCore/Assertions.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{
sparrow::Result<sparrow::LbfMetrics> CalculateMetrics(
    const jaguar::SPInstance& instance,
    const jaguar::SPSolution& solution,
    std::uint64_t seed)
{
    auto problemResult = jaguar::SPProblem::Create(instance);
    if (!problemResult.HasValue())
    {
        return sparrow::Result<sparrow::LbfMetrics>::FromError(
            problemResult.GetError());
    }
    jaguar::SPProblem problem = std::move(problemResult.Value());
    const auto restored = problem.Restore(solution);
    if (!restored.HasValue())
    {
        return sparrow::Result<sparrow::LbfMetrics>::FromError(
            restored.GetError());
    }
    jaguar::Layout layout(solution.layoutSnapshot.container);
    const auto layoutRestored = layout.Restore(solution.layoutSnapshot);
    if (!layoutRestored.HasValue())
    {
        return sparrow::Result<sparrow::LbfMetrics>::FromError(
            layoutRestored.GetError());
    }
    sparrow::LbfMetrics metrics;
    metrics.seed = seed;
    metrics.placedItemCount = problem.PlacedItemCount();
    for (std::size_t index = 0U; index < instance.ItemCount(); ++index)
    {
        metrics.remainingDemand += problem.RemainingDemand(index);
    }
    metrics.stripWidth = solution.StripWidth();
    metrics.density = solution.Density();
    metrics.feasible = metrics.remainingDemand == 0U
        && jaguar::ValidateSPSolution(problem, solution).HasValue()
        && layout.IsFeasible();
    return sparrow::Result<sparrow::LbfMetrics>::FromValue(metrics);
}

float CalculateMinimumStripWidth(
    const jaguar::SPInstance& instance,
    const jaguar::SPSolution& solution) noexcept
{
    constexpr float relativeTolerance = 1.0e-6F;
    const jaguar::Rect2d containerBounds =
        solution.layoutSnapshot.container.CollisionShape().BoundingBox();
    const float containerWidth = static_cast<float>(containerBounds.Width());
    const float containerHeight = static_cast<float>(containerBounds.Height());
    if (!std::isfinite(containerWidth)
        || !std::isfinite(containerHeight)
        || containerWidth <= 0.0F
        || containerHeight <= 0.0F)
    {
        return 0.0F;
    }

    double totalArea = 0.0;
    for (std::size_t index = 0U; index < instance.ItemCount(); ++index)
    {
        const float itemArea = static_cast<float>(
            instance.GetItem(index).CollisionShape().Area());
        totalArea += static_cast<double>(itemArea)
            * static_cast<double>(instance.ItemDemand(index));
    }
    float minimumWidth = static_cast<float>(
        totalArea / static_cast<double>(containerHeight))
        * (1.0F - relativeTolerance);

    for (std::size_t index = 0U; index < instance.ItemCount(); ++index)
    {
        const jaguar::Item& item = instance.GetItem(index);
        const auto& rotation = item.AllowedRotation();
        if (rotation.mode == jaguar::RotationMode::Continuous)
        {
            continue;
        }

        std::vector<float> rotations;
        if (rotation.mode == jaguar::RotationMode::None)
        {
            rotations.push_back(0.0F);
        }
        else
        {
            rotations.reserve(rotation.radians.size());
            for (const double angle : rotation.radians)
            {
                rotations.push_back(static_cast<float>(angle));
            }
        }
        float itemWidth = std::numeric_limits<float>::infinity();
        const float tolerance = static_cast<float>(
            item.CollisionShape().Diameter()) * relativeTolerance;
        for (const float angle : rotations)
        {
            if (!std::isfinite(angle))
            {
                continue;
            }
            const auto transformed = item.CollisionShape().TransformedRustF32(
                jaguar::Transform2d::FromRotationTranslation(angle, {0.0, 0.0}));
            if (!transformed.HasValue())
            {
                continue;
            }
            const jaguar::Rect2d bounds = transformed.Value().BoundingBox();
            const float boundsWidth = static_cast<float>(bounds.Width());
            const float boundsHeight = static_cast<float>(bounds.Height());
            if (boundsHeight <= containerHeight + tolerance)
            {
                itemWidth = std::min(
                    itemWidth,
                    std::max(0.0F, boundsWidth - tolerance));
            }
        }
        minimumWidth = std::max(minimumWidth, itemWidth);
    }

    const float widthInset = static_cast<float>(solution.StripWidth())
        - containerWidth;
    const float result = minimumWidth + widthInset;
    return result > 0.0F ? result : 0.0F;
}

double NormalizeRotation(double value) noexcept
{
    constexpr double twoPi = 6.28318530717958647692;
    const double normalized = std::fmod(value, twoPi);
    return normalized < 0.0 ? normalized + twoPi : normalized;
}

sparrow::Result<jaguar::Transform2d> LegalizeTransform(
    jaguar::Transform2d transform,
    const jaguar::Item& item)
{
    switch (item.AllowedRotation().mode)
    {
    case jaguar::RotationMode::None:
        transform.rotationRad = 0.0;
        break;
    case jaguar::RotationMode::Discrete:
    {
        if (item.AllowedRotation().radians.empty())
        {
            return sparrow::Result<jaguar::Transform2d>::FromError(
                {jaguar::ErrorCode::Infeasible,
                 "called `Option::unwrap()` on a `None` value"});
        }
        double nearest = std::numeric_limits<double>::infinity();
        double selected = item.AllowedRotation().radians.front();
        for (const double allowed : item.AllowedRotation().radians)
        {
            if (!std::isfinite(allowed))
            {
                continue;
            }
            const double distance = std::abs(
                NormalizeRotation(transform.rotationRad - allowed));
            if (distance < nearest)
            {
                nearest = distance;
                selected = allowed;
            }
        }
        transform.rotationRad = selected;
        break;
    }
    case jaguar::RotationMode::Continuous:
        break;
    }
    return sparrow::Result<jaguar::Transform2d>::FromValue(transform);
}

std::size_t LeadingZeroes(std::size_t value) noexcept
{
    constexpr std::size_t bits = std::numeric_limits<std::size_t>::digits;
    if (value == 0U)
    {
        return bits;
    }
    std::size_t count = 0U;
    for (std::size_t bit = bits; bit > 0U; --bit)
    {
        if ((value & (std::size_t{1U} << (bit - 1U))) != 0U)
        {
            break;
        }
        ++count;
    }
    return count;
}

std::uint32_t LeadingZeroes(std::uint32_t value) noexcept
{
    if (value == 0U)
    {
        return 32U;
    }
    std::uint32_t count = 0U;
    for (std::uint32_t bit = 32U; bit > 0U; --bit)
    {
        if ((value & (std::uint32_t{1U} << (bit - 1U))) != 0U)
        {
            break;
        }
        ++count;
    }
    return count;
}

// rand 0.10 IteratorRandom::choose on a Filter iterator uses CoinFlipper
// reservoir sampling. A bounded index draw has the same target distribution
// but consumes/maps RNG bits differently, so mirror it for Rust trace parity.
class RustCoinFlipper
{
public:
    explicit RustCoinFlipper(sparrow::SparrowRng& rng) noexcept
        : m_Rng(rng)
    {
    }

    bool RandomRatioOneOver(std::size_t denominator) noexcept
    {
        const std::size_t bits = std::numeric_limits<std::size_t>::digits;
        std::uint32_t count = static_cast<std::uint32_t>(
            std::min<std::size_t>(bits - 1U - LeadingZeroes(denominator), 32U));
        if (!FlipCHeads(count))
        {
            return false;
        }

        std::size_t numerator = std::size_t{1U} << count;
        while (numerator < denominator)
        {
            const std::size_t numeratorZeros = LeadingZeroes(numerator);
            const std::size_t denominatorZeros = LeadingZeroes(denominator);
            const std::size_t difference = numeratorZeros
                    > denominatorZeros + 1U
                ? numeratorZeros - (denominatorZeros + 1U)
                : 0U;
            count = static_cast<std::uint32_t>(
                std::clamp<std::size_t>(difference, 1U, 32U));
            if (FlipCHeads(count))
            {
                const std::size_t multiplier = std::size_t{1U} << count;
                numerator = numerator
                        > std::numeric_limits<std::size_t>::max() / multiplier
                    ? std::numeric_limits<std::size_t>::max()
                    : numerator * multiplier;
            }
            else if (count == 1U)
            {
                const std::size_t nextNumerator =
                    numerator + numerator - denominator;
                if (nextNumerator == 0U || nextNumerator > numerator)
                {
                    return false;
                }
                numerator = nextNumerator;
            }
            else
            {
                return false;
            }
        }
        return true;
    }

private:
    bool FlipCHeads(std::uint32_t count) noexcept
    {
        while (true)
        {
            const std::uint32_t zeroCount = LeadingZeroes(m_Chunk);
            if (zeroCount < count)
            {
                const std::uint32_t consumed = zeroCount + 1U;
                m_Chunk <<= consumed;
                m_ChunkRemaining = consumed >= m_ChunkRemaining
                    ? 0U
                    : m_ChunkRemaining - consumed;
                return false;
            }
            if (m_ChunkRemaining >= count)
            {
                m_ChunkRemaining -= count;
                m_Chunk <<= count;
                return true;
            }
            count -= m_ChunkRemaining;
            m_Chunk = m_Rng.NextU32();
            m_ChunkRemaining = 32U;
        }
    }

    sparrow::SparrowRng& m_Rng;
    std::uint32_t m_Chunk = 0U;
    std::uint32_t m_ChunkRemaining = 0U;
};

std::optional<std::size_t> ChooseFromRustFilteredRange(
    sparrow::SparrowRng& rng,
    const std::vector<std::size_t>& candidates)
{
    RustCoinFlipper coinFlipper(rng);
    std::optional<std::size_t> selected;
    std::size_t consumed = 0U;
    for (const std::size_t candidate : candidates)
    {
        ++consumed;
        if (coinFlipper.RandomRatioOneOver(consumed))
        {
            selected = candidate;
        }
    }
    return selected;
}

double DoubleFromBits(std::uint64_t bits) noexcept
{
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

std::size_t CountConsumedU64Words(
    sparrow::SparrowRng before,
    const sparrow::SparrowRng& after) noexcept
{
    for (std::size_t words = 0U; words <= 64U; ++words)
    {
        if (before.State() == after.State())
        {
            return words;
        }
        before.NextU64();
    }
    return 65U;
}

std::array<std::uint64_t, 4U> FollowingU64(
    sparrow::SparrowRng rng) noexcept
{
    std::array<std::uint64_t, 4U> values{};
    for (std::uint64_t& value : values)
    {
        value = rng.NextU64();
    }
    return values;
}

// rand_distr 0.6 samples StandardNormal with ZIGNOR in f64, then casts to f32.
// Keep this local to Exploration so other SparrowRng consumers retain their sampler.
struct NormalZigguratTables
{
    std::array<double, 257U> x{};
    std::array<double, 257U> f{};
};

const NormalZigguratTables& GetNormalZigguratTables() noexcept
{
    static const NormalZigguratTables tables = [] {
        constexpr double tail = 3.654152885361008796;
        constexpr double area = 0.00492867323399;
        NormalZigguratTables result;
        result.x[1U] = tail;
        for (std::size_t index = 2U; index < result.x.size(); ++index)
        {
            const double previous = result.x[index - 1U];
            result.x[index] = std::sqrt(
                -2.0 * std::log(
                    area / previous
                    + std::exp(-previous * previous / 2.0)));
        }
        result.x[0U] = area / std::exp(-tail * tail / 2.0);
        // rand_distr 0.6 stores the terminal Ziggurat edge as exact zero.
        // Reapplying the recurrence can round its log argument above 1.0,
        // producing NaN and rejecting every candidate in the last layer.
        result.x.back() = 0.0;
        for (std::size_t index = 0U; index < result.x.size(); ++index)
        {
            result.f[index] =
                std::exp(-result.x[index] * result.x[index] / 2.0);
        }
        return result;
    }();
    return tables;
}

double RustUniform01F64(sparrow::SparrowRng& rng) noexcept
{
    constexpr double scale = 1.0 / 9007199254740992.0;
    return static_cast<double>(rng.NextU64() >> 11U) * scale;
}

double RustOpen01F64(sparrow::SparrowRng& rng) noexcept
{
    constexpr std::uint64_t exponentOne = 0x3ff0000000000000ULL;
    const double oneToTwo = DoubleFromBits(
        exponentOne | (rng.NextU64() >> 12U));
    return oneToTwo
        - (1.0 - std::numeric_limits<double>::epsilon() / 2.0);
}

double RustStandardNormalF64(sparrow::SparrowRng& rng)
{
    constexpr std::uint64_t exponentTwo = 0x4000000000000000ULL;
    constexpr double tail = 3.654152885361008796;
    const NormalZigguratTables& tables = GetNormalZigguratTables();
    for (;;)
    {
        const std::uint64_t bits = rng.NextU64();
        const std::size_t index = static_cast<std::size_t>(bits & 0xffU);
        const double u = DoubleFromBits(exponentTwo | (bits >> 12U)) - 3.0;
        const double x = u * tables.x[index];
        if (std::abs(x) < tables.x[index + 1U])
        {
            return x;
        }
        if (index == 0U)
        {
            double tailX = 1.0;
            double tailY = 0.0;
            while (-2.0 * tailY < tailX * tailX)
            {
                tailX = std::log(RustOpen01F64(rng)) / tail;
                tailY = std::log(RustOpen01F64(rng));
            }
            return u < 0.0 ? tailX - tail : tail - tailX;
        }

        const double vertical = tables.f[index + 1U]
            + (tables.f[index] - tables.f[index + 1U])
                * RustUniform01F64(rng);
        if (vertical < std::exp(-x * x / 2.0))
        {
            return x;
        }
    }
}

float RustNormalF32(sparrow::SparrowRng& rng, float standardDeviation)
{
    return standardDeviation
        * static_cast<float>(RustStandardNormalF64(rng));
}

const jaguar::LayoutPlacementSnapshot* FindPlacement(
    const jaguar::LayoutSnapshot& snapshot,
    jaguar::PlacedItemHandle handle) noexcept
{
    for (const auto& placement : snapshot.placements)
    {
        if (placement.handle == handle)
        {
            return &placement;
        }
    }
    return nullptr;
}

struct InfeasibleCandidate
{
    InfeasibleCandidate(jaguar::SPSolution value, float candidateLoss)
        : solution(std::move(value)),
          loss(candidateLoss)
    {
    }

    jaguar::SPSolution solution;
    float loss = 0.0F;
};

sparrow::Result<std::vector<jaguar::PlacedItemHandle>>
CollectContainedHandles(
    const jaguar::SPSolution& solution,
    jaguar::PlacedItemHandle containerHandle)
{
    jaguar::Layout layout(solution.layoutSnapshot.container);
    const auto restored = layout.Restore(solution.layoutSnapshot);
    if (!restored.HasValue())
    {
        return sparrow::Result<std::vector<jaguar::PlacedItemHandle>>::FromError(
            restored.GetError());
    }
    const auto* containerPlacement = FindPlacement(
        solution.layoutSnapshot, containerHandle);
    if (containerPlacement == nullptr)
    {
        return sparrow::Result<std::vector<jaguar::PlacedItemHandle>>::FromError(
            {jaguar::ErrorCode::InvalidInput,
             "contained-item query handle is not in the layout"});
    }

    auto engine = solution.layoutSnapshot.container.CreateCollisionEngine();
    if (!engine || !engine->Restore(solution.layoutSnapshot.cdeSnapshot).HasValue())
    {
        return sparrow::Result<std::vector<jaguar::PlacedItemHandle>>::FromError(
            {jaguar::ErrorCode::CollisionEngineFailure,
             "contained-item collision engine is unavailable"});
    }
    jaguar::HazardFilter filter;
    // Collision-engine handles include static hazards; the entity stores the layout handle.
    filter.irrelevant = [containerHandle](
                            const jaguar::HazardEntity& entity) {
        return entity.kind == jaguar::HazardKind::PlacedItem
            && entity.placedItemHandle == containerHandle;
    };
    jaguar::HazardCollector collector;
    engine->CollectPolygonCollisionsUntil(
        containerPlacement->placedItem.CollisionShape(),
        collector,
        filter,
        [](const jaguar::HazardEntity&) { return false; });

    std::vector<std::pair<jaguar::HazardHandle, jaguar::PlacedItemHandle>> orderedHandles;
    for (const auto& entry : collector.Entries())
    {
        if (entry.second.kind != jaguar::HazardKind::PlacedItem)
        {
            continue;
        }
        const auto* other = FindPlacement(
            solution.layoutSnapshot, entry.second.placedItemHandle);
        if (other != nullptr
            && containerPlacement->placedItem.CollisionShape().ContainsRustF32(
                   other->placedItem.CollisionShape()
                       .PointOfInaccessibility().center))
        {
            orderedHandles.emplace_back(
                entry.first, entry.second.placedItemHandle);
        }
    }
    // Rust's BasicHazardCollector stores entries in a SecondaryMap and
    // iterates them by hazard-key slot. Match that order rather than keeping
    // the quadtree traversal order used to populate the C++ collector.
    std::sort(orderedHandles.begin(), orderedHandles.end(), [](const auto& left, const auto& right) {
        if (left.first.index != right.first.index)
        {
            return left.first.index < right.first.index;
        }
        return left.first.generation < right.first.generation;
    });
    std::vector<jaguar::PlacedItemHandle> result;
    result.reserve(orderedHandles.size());
    for (const auto& entry : orderedHandles)
    {
        result.push_back(entry.second);
    }
    return sparrow::Result<std::vector<jaguar::PlacedItemHandle>>::FromValue(
        std::move(result));
}

void TraceDisruptionMove(
    const sparrow::detail::ExplorationTraceCallback* trace,
    std::size_t failureId,
    sparrow::detail::ExplorationTraceEvent::DisruptionMoveKind kind,
    std::size_t itemIndex,
    std::size_t relatedItemIndex,
    const jaguar::Transform2d& from,
    const jaguar::Transform2d& to)
{
    if (trace == nullptr)
    {
        return;
    }
    sparrow::detail::ExplorationTraceEvent event;
    event.type = sparrow::detail::ExplorationTraceEvent::Type::DisruptionMove;
    event.failureId = failureId;
    event.disruptionMoveKind = kind;
    event.itemIndex = itemIndex;
    event.relatedItemIndex = relatedItemIndex;
    event.fromPose = {static_cast<float>(from.rotationRad),
                      static_cast<float>(from.translation.x),
                      static_cast<float>(from.translation.y)};
    event.toPose = {static_cast<float>(to.rotationRad),
                    static_cast<float>(to.translation.x),
                    static_cast<float>(to.translation.y)};
    (*trace)(event);
}

sparrow::Result<void> DisruptSolution(
    sparrow::Separator& separator,
    const sparrow::ExplorationConfig& config,
    std::size_t failureId,
    const sparrow::detail::ExplorationTraceCallback* trace)
{
    if (config.disruptionCount == 0U
        && config.containedRelocationCount == 0U)
    {
        return sparrow::Result<void>::FromValue();
    }

    const jaguar::SPSolution solution = separator.SaveSolution();
    if (solution.layoutSnapshot.placements.size() < 2U)
    {
        return sparrow::Result<void>::FromValue();
    }

    float totalConvexHullArea = 0.0F;
    std::vector<float> itemHullAreas;
    itemHullAreas.reserve(separator.Problem().Instance().ItemCount());
    for (std::size_t index = 0U;
         index < separator.Problem().Instance().ItemCount();
         ++index)
    {
        const jaguar::Surrogate* surrogate = separator.Problem().Instance()
            .GetItem(index).CollisionShape().GetSurrogate();
        const float hullArea = surrogate == nullptr
            ? 0.0F
            : static_cast<float>(surrogate->ConvexHullArea());
        itemHullAreas.push_back(hullArea);
        totalConvexHullArea += hullArea * static_cast<float>(
            separator.Problem().Instance().ItemDemand(index));
    }
    const float cutoff = totalConvexHullArea
        * static_cast<float>(config.largeItemChAreaCutoffPercentile);
    std::vector<std::pair<float, std::size_t>> ordered;
    for (std::size_t index = 0U; index < itemHullAreas.size(); ++index)
    {
        ordered.emplace_back(itemHullAreas[index], index);
    }
    std::sort(ordered.begin(), ordered.end(),
              [](const auto& left, const auto& right) {
                  if (left.first != right.first)
                  {
                      return left.first > right.first;
                  }
                  return left.second < right.second;
              });
    float cumulative = 0.0F;
    float cutoffArea = 0.0F;
    for (const auto& entry : ordered)
    {
        cumulative += entry.first * static_cast<float>(
            separator.Problem().Instance().ItemDemand(entry.second));
        if (cumulative > cutoff)
        {
            cutoffArea = entry.first;
            break;
        }
    }

    std::vector<std::size_t> large;
    std::vector<std::size_t> all;
    for (std::size_t index = 0U;
         index < solution.layoutSnapshot.placements.size();
         ++index)
    {
        all.push_back(index);
        const auto& placed = solution.layoutSnapshot.placements[index].placedItem;
        const jaguar::Surrogate* surrogate =
            placed.CollisionShape().GetSurrogate();
        if (surrogate != nullptr && surrogate->ConvexHullArea() >= cutoffArea)
        {
            large.push_back(index);
        }
    }
    if (large.empty())
    {
        return sparrow::Result<void>::FromError(
            {jaguar::ErrorCode::Infeasible,
             "[DSRP] failed to choose first item"});
    }

    const auto firstChoice = ChooseFromRustFilteredRange(
        separator.Random(), large);
    if (!firstChoice.has_value())
    {
        return sparrow::Result<void>::FromValue();
    }
    const std::size_t firstIndex = *firstChoice;
    std::vector<std::size_t> distinct;
    for (const std::size_t index : large)
    {
        if (index == firstIndex)
        {
            continue;
        }
        const auto& firstItem = solution.layoutSnapshot.placements[firstIndex]
            .placedItem;
        const auto& candidate = solution.layoutSnapshot.placements[index]
            .placedItem;
        if (sparrow::detail::IsRustF32Distinct(
                candidate.Area(),
                firstItem.Area(),
                candidate.CollisionShape().Diameter(),
                firstItem.CollisionShape().Diameter()))
        {
            distinct.push_back(index);
        }
    }
    const std::vector<std::size_t>& secondPool = distinct.empty() ? all : distinct;
    std::vector<std::size_t> filteredSecondPool;
    for (const std::size_t index : secondPool)
    {
        if (index != firstIndex)
        {
            filteredSecondPool.push_back(index);
        }
    }
    if (filteredSecondPool.empty())
    {
        return sparrow::Result<void>::FromValue();
    }
    const auto secondChoice = ChooseFromRustFilteredRange(
        separator.Random(), filteredSecondPool);
    if (!secondChoice.has_value())
    {
        return sparrow::Result<void>::FromValue();
    }
    const std::size_t secondIndex = *secondChoice;

    const auto& firstPlacement = solution.layoutSnapshot.placements[firstIndex];
    const auto& secondPlacement = solution.layoutSnapshot.placements[secondIndex];
    const jaguar::Transform2d firstOld = firstPlacement.placedItem.Transform();
    const jaguar::Transform2d secondOld = secondPlacement.placedItem.Transform();
    const auto firstNewResult = LegalizeTransform(
        secondOld, firstPlacement.placedItem.GetItem());
    if (!firstNewResult.HasValue())
    {
        return sparrow::Result<void>::FromError(firstNewResult.GetError());
    }
    const jaguar::Transform2d firstNew = firstNewResult.Value();
    const auto secondNewResult = LegalizeTransform(
        firstOld, secondPlacement.placedItem.GetItem());
    if (!secondNewResult.HasValue())
    {
        return sparrow::Result<void>::FromError(secondNewResult.GetError());
    }
    const jaguar::Transform2d secondNew = secondNewResult.Value();
    const std::size_t firstItemIndex =
        firstPlacement.placedItem.GetItemIndex();
    const std::size_t secondItemIndex =
        secondPlacement.placedItem.GetItemIndex();

    const auto firstMoved = separator.MoveItem(
        firstPlacement.handle, firstNew);
    if (!firstMoved.HasValue())
    {
        return sparrow::Result<void>::FromError(firstMoved.GetError());
    }
    TraceDisruptionMove(
        trace,
        failureId,
        sparrow::detail::ExplorationTraceEvent::DisruptionMoveKind::Swap,
        firstItemIndex,
        secondItemIndex,
        firstOld,
        firstNew);
    const auto secondMoved = separator.MoveItem(
        secondPlacement.handle, secondNew);
    if (!secondMoved.HasValue())
    {
        return sparrow::Result<void>::FromError(secondMoved.GetError());
    }
    TraceDisruptionMove(
        trace,
        failureId,
        sparrow::detail::ExplorationTraceEvent::DisruptionMoveKind::Swap,
        secondItemIndex,
        firstItemIndex,
        secondOld,
        secondNew);

    // jagua-rs: converting_transformation = dt_new.compose().inverse()
    //     .transform(&dt_old.compose()) — f32 矩阵路径；用 RustF32 位级对应。
    const jaguar::Transform2d firstConversion =
        firstNew.InverseRustF32().FollowedByRustF32(firstOld);
    const auto firstContained = CollectContainedHandles(
        separator.SaveSolution(), firstMoved.Value());
    if (!firstContained.HasValue())
    {
        return sparrow::Result<void>::FromError(firstContained.GetError());
    }
    for (const jaguar::PlacedItemHandle handle : firstContained.Value())
    {
        if (handle == secondMoved.Value())
        {
            continue;
        }
        const jaguar::SPSolution current = separator.SaveSolution();
        const auto* placement = FindPlacement(current.layoutSnapshot, handle);
        if (placement == nullptr)
        {
            continue;
        }
        const jaguar::Transform2d from = placement->placedItem.Transform();
        const auto toResult = LegalizeTransform(
            from.FollowedByRustF32(firstConversion),
            placement->placedItem.GetItem());
        if (!toResult.HasValue())
        {
            return sparrow::Result<void>::FromError(toResult.GetError());
        }
        const jaguar::Transform2d to = toResult.Value();
        const std::size_t itemIndex = placement->placedItem.GetItemIndex();
        const auto moved = separator.MoveItem(handle, to);
        if (!moved.HasValue())
        {
            return sparrow::Result<void>::FromError(moved.GetError());
        }
        TraceDisruptionMove(
            trace,
            failureId,
            sparrow::detail::ExplorationTraceEvent::DisruptionMoveKind::ContainedRelocation,
            itemIndex,
            firstItemIndex,
            from,
            to);
    }

    // jagua-rs: converting_transformation = dt2_new.compose().inverse()
    //     .transform(&dt2_old.compose()) — f32 矩阵路径；用 RustF32 位级对应。
    const jaguar::Transform2d secondConversion =
        secondNew.InverseRustF32().FollowedByRustF32(secondOld);
    const auto secondContained = CollectContainedHandles(
        separator.SaveSolution(), secondMoved.Value());
    if (!secondContained.HasValue())
    {
        return sparrow::Result<void>::FromError(secondContained.GetError());
    }
    for (const jaguar::PlacedItemHandle handle : secondContained.Value())
    {
        if (handle == firstMoved.Value())
        {
            continue;
        }
        const jaguar::SPSolution current = separator.SaveSolution();
        const auto* placement = FindPlacement(current.layoutSnapshot, handle);
        if (placement == nullptr)
        {
            continue;
        }
        const jaguar::Transform2d from = placement->placedItem.Transform();
        const auto toResult = LegalizeTransform(
            from.FollowedByRustF32(secondConversion),
            placement->placedItem.GetItem());
        if (!toResult.HasValue())
        {
            return sparrow::Result<void>::FromError(toResult.GetError());
        }
        const jaguar::Transform2d to = toResult.Value();
        const std::size_t itemIndex = placement->placedItem.GetItemIndex();
        const auto moved = separator.MoveItem(handle, to);
        if (!moved.HasValue())
        {
            return sparrow::Result<void>::FromError(moved.GetError());
        }
        TraceDisruptionMove(
            trace,
            failureId,
            sparrow::detail::ExplorationTraceEvent::DisruptionMoveKind::ContainedRelocation,
            itemIndex,
            secondItemIndex,
            from,
            to);
    }
    return sparrow::Result<void>::FromValue();
}

sparrow::OptimizationProgress MakeProgress(
    sparrow::OptimizationPhase phase,
    std::size_t iteration,
    const sparrow::LbfMetrics& metrics,
    float collisionLoss)
{
    return {phase,
            iteration,
            metrics.stripWidth,
            metrics.density,
            collisionLoss,
            metrics.feasible};
}
}

namespace sparrow::detail
{
float RustNormalF32(sparrow::SparrowRng& rng, float standardDeviation)
{
    return ::RustNormalF32(rng, standardDeviation);
}
}

namespace sparrow
{
Result<void> ExplorationConfig::Validate() const
{
    if (!std::isfinite(shrinkStep) || shrinkStep < 0.0
        || shrinkStep > 1.0
        || !std::isfinite(solutionPoolDistributionStddev)
        || solutionPoolDistributionStddev <= 0.0
        || !std::isfinite(largeItemChAreaCutoffPercentile)
        || largeItemChAreaCutoffPercentile < 0.0
        || largeItemChAreaCutoffPercentile > 1.0
        || timeLimit.count() < 0)
    {
        return Result<void>::FromError(
            {jaguar::ErrorCode::InvalidInput,
             "exploration limits, shrinkStep, or disruption settings are invalid"});
    }
    return separator.Validate();
}

namespace
{
Result<ExplorationResult> ExploreImpl(
    const jaguar::SPInstance& instance,
    const jaguar::SPSolution& initialSolution,
    const ExplorationConfig& config,
    std::uint64_t seed,
    OptimizationTerminator& terminator,
    OptimizationListener* listener,
    OptimizationObserver* observer,
    const detail::ExplorationTraceCallback* trace)
{
    const Result<void> configResult = config.Validate();
    if (!configResult.HasValue())
    {
        return Result<ExplorationResult>::FromError(configResult.GetError());
    }
    const auto initialMetrics = CalculateMetrics(instance, initialSolution, seed);
    if (!initialMetrics.HasValue())
    {
        return Result<ExplorationResult>::FromError(initialMetrics.GetError());
    }

    auto problemResult = jaguar::SPProblem::Create(instance);
    if (!problemResult.HasValue())
    {
        return Result<ExplorationResult>::FromError(problemResult.GetError());
    }
    jaguar::SPProblem problem = std::move(problemResult.Value());
    const auto restored = problem.Restore(initialSolution);
    if (!restored.HasValue())
    {
        return Result<ExplorationResult>::FromError(restored.GetError());
    }
    auto separatorResult = Separator::Create(std::move(problem), seed);
    if (!separatorResult.HasValue())
    {
        return Result<ExplorationResult>::FromError(
            separatorResult.GetError());
    }
    Separator separator = std::move(separatorResult.Value());

    jaguar::SPSolution bestSolution = initialSolution;
    LbfMetrics bestMetrics = initialMetrics.Value();
    const float minimumWidth = static_cast<float>(CalculateMinimumStripWidth(
        instance, initialSolution));
    float currentWidth = static_cast<float>(
        separator.Problem().StripWidth());
    float bestWidth = currentWidth;
    std::size_t totalSurrogateRejectCount = 0U;
    std::size_t totalExactCollisionCount = 0U;
    std::size_t failures = 0U;
    std::size_t iteration = 0U;
    std::size_t nextFailureId = 0U;
    std::vector<std::size_t> tracePoolOrder;
    std::vector<InfeasibleCandidate> infeasiblePool;
    OptimizationHistory history;
    PublishSolutionReport(
        history,
        listener,
        MakeSolutionReport(
            OptimizationReportType::OPTIMIZATION_REPORT_EXPLORATION_FEASIBLE,
            OptimizationPhase::OPTIMIZATION_PHASE_EXPLORATION,
            0U,
            bestSolution,
            0.0F,
            bestMetrics.feasible));
    if (observer != nullptr)
    {
        observer->OnProgress(MakeProgress(
            OptimizationPhase::OPTIMIZATION_PHASE_EXPLORATION,
            0U,
            bestMetrics,
            0.0F));
    }

    while (!terminator.ShouldTerminate(iteration)
           && (config.maxIterations == 0U
               || iteration < config.maxIterations))
    {
        const auto separated = separator.Run(
            config.separator,
            terminator,
            listener,
            &history,
            OptimizationPhase::OPTIMIZATION_PHASE_EXPLORATION);
        if (!separated.HasValue())
        {
            return Result<ExplorationResult>::FromError(
                separated.GetError());
        }
        const float totalLoss = separator.Tracker().TotalLoss();
        const auto metricsResult = CalculateMetrics(
            instance, separated.Value().solution, seed);
        if (!metricsResult.HasValue())
        {
            return Result<ExplorationResult>::FromError(
                metricsResult.GetError());
        }
        LbfMetrics metrics = metricsResult.Value();
        metrics.surrogateRejectCount = separated.Value().surrogateRejectCount;
        metrics.exactCollisionCount = separated.Value().exactCollisionCount;
        totalSurrogateRejectCount += metrics.surrogateRejectCount;
        totalExactCollisionCount += metrics.exactCollisionCount;
        if (totalLoss != 0.0F)
        {
            PublishSolutionReport(
                history,
                listener,
                MakeSolutionReport(
                    OptimizationReportType::OPTIMIZATION_REPORT_EXPLORATION_INFEASIBLE,
                    OptimizationPhase::OPTIMIZATION_PHASE_EXPLORATION,
                    iteration,
                    separated.Value().solution,
                    totalLoss,
                    false));
        }
        if (observer != nullptr)
        {
            observer->OnProgress(MakeProgress(
                OptimizationPhase::OPTIMIZATION_PHASE_EXPLORATION,
                iteration,
                metrics,
                totalLoss));
        }

        if (totalLoss == 0.0F)
        {
            if (currentWidth < bestWidth)
            {
                bestWidth = currentWidth;
                bestSolution = separated.Value().solution;
                bestMetrics = metrics;
                if (observer != nullptr)
                {
                    observer->OnImprovement(MakeProgress(
                        OptimizationPhase::OPTIMIZATION_PHASE_EXPLORATION,
                        iteration,
                        metrics,
                        totalLoss));
                }
                PublishSolutionReport(
                    history,
                    listener,
                    MakeSolutionReport(
                        OptimizationReportType::OPTIMIZATION_REPORT_EXPLORATION_FEASIBLE,
                        OptimizationPhase::OPTIMIZATION_PHASE_EXPLORATION,
                        iteration,
                        bestSolution,
                        totalLoss,
                        true));
            }
            const float nextWidth = currentWidth
                * (1.0F - static_cast<float>(config.shrinkStep));
            if (!std::isfinite(nextWidth) || nextWidth <= 0.0F
                || nextWidth >= currentWidth
                || (minimumWidth > 0.0F && nextWidth < minimumWidth))
            {
                break;
            }
            const auto changed = separator.ChangeStripWidth(nextWidth);
            if (!changed.HasValue())
            {
                return Result<ExplorationResult>::FromError(
                    changed.GetError());
            }
            currentWidth = nextWidth;
            infeasiblePool.clear();
            tracePoolOrder.clear();
            failures = 0U;
        }
        else
        {
            InfeasibleCandidate candidate(
                separated.Value().solution, totalLoss);
            const std::size_t insertionIndex = [&infeasiblePool,
                                                loss = candidate.loss]() {
                if (infeasiblePool.empty())
                {
                    return std::size_t{0U};
                }
                std::size_t left = 0U;
                std::size_t size = infeasiblePool.size();
                // Rust 1.98.1 slice::binary_search_by advances to the last
                // equal element before returning its matching index.
                while (size > 1U)
                {
                    const std::size_t half = size / 2U;
                    const std::size_t middle = left + half;
                    const float poolLoss = infeasiblePool[middle].loss;
                    if (std::isnan(poolLoss) || std::isnan(loss))
                    {
                        throw std::domain_error(
                            "Rust binary_search_by partial_cmp cannot compare NaN loss");
                    }
                    if (poolLoss > loss)
                    {
                        size = half;
                    }
                    else
                    {
                        left = middle;
                        size -= half;
                    }
                }
                const float candidateLoss = infeasiblePool[left].loss;
                if (std::isnan(candidateLoss) || std::isnan(loss))
                {
                    throw std::domain_error(
                        "Rust binary_search_by partial_cmp cannot compare NaN loss");
                }
                if (candidateLoss == loss)
                {
                    return left;
                }
                return candidateLoss < loss ? left + 1U : left;
            }();
            const auto insertion = infeasiblePool.begin() + insertionIndex;
            infeasiblePool.insert(insertion, std::move(candidate));
            const std::size_t failureId = nextFailureId++;
            if (trace != nullptr)
            {
                tracePoolOrder.insert(
                    tracePoolOrder.begin() + insertionIndex,
                    failureId);
            }
            if (config.infeasiblePoolSize > 0U
                && infeasiblePool.size() > config.infeasiblePoolSize)
            {
                infeasiblePool.pop_back();
                if (trace != nullptr)
                {
                    tracePoolOrder.pop_back();
                }
            }
            if (trace != nullptr)
            {
                detail::ExplorationTraceEvent event;
                event.type = detail::ExplorationTraceEvent::Type::PoolInsert;
                event.failureId = failureId;
                event.insertionIndex = insertionIndex;
                event.poolOrder = tracePoolOrder;
                (*trace)(event);
            }
            ++failures;
            if (config.maxConsecutiveFailures.has_value()
                && failures >= *config.maxConsecutiveFailures)
            {
                break;
            }

            std::optional<SparrowRng> rngBeforeSample;
            if (trace != nullptr)
            {
                rngBeforeSample = separator.Random();
            }
            const float rawSample = sparrow::detail::RustNormalF32(
                separator.Random(),
                static_cast<float>(config.solutionPoolDistributionStddev));
            const float sampled = std::min(std::abs(rawSample), 0.999F);
            const std::size_t selectedIndex = std::min(
                static_cast<std::size_t>(sampled
                    * static_cast<float>(infeasiblePool.size())),
                infeasiblePool.size() - 1U);
            if (trace != nullptr && rngBeforeSample.has_value())
            {
                sparrow::SparrowRng firstLayerProbe = *rngBeforeSample;
                detail::ExplorationTraceEvent event;
                event.type = detail::ExplorationTraceEvent::Type::PoolSample;
                event.failureId = failureId;
                event.rawSample = rawSample;
                event.mappedSample = sampled;
                event.selectedIndex = selectedIndex;
                event.consumedU64Words = CountConsumedU64Words(
                    *rngBeforeSample,
                    separator.Random());
                event.firstLayerIndex = static_cast<std::uint32_t>(
                    firstLayerProbe.NextU64() & 0xffU);
                event.followingU64 = FollowingU64(separator.Random());
                (*trace)(event);
            }
            const auto rolledBack = separator.Rollback(
                infeasiblePool[selectedIndex].solution);
            if (!rolledBack.HasValue())
            {
                return Result<ExplorationResult>::FromError(
                    rolledBack.GetError());
            }
            const auto disrupted = DisruptSolution(
                separator, config, failureId, trace);
            if (!disrupted.HasValue())
            {
                return Result<ExplorationResult>::FromError(
                    disrupted.GetError());
            }
        }
        ++iteration;
    }

    bestMetrics.seed = seed;
    bestMetrics.surrogateRejectCount = totalSurrogateRejectCount;
    bestMetrics.exactCollisionCount = totalExactCollisionCount;
    return Result<ExplorationResult>::FromValue(
        {bestSolution,
         separator.SaveSolution(),
         bestMetrics,
         std::move(history)});
}
}

Result<ExplorationResult> Explore(
    const jaguar::SPInstance& instance,
    const jaguar::SPSolution& initialSolution,
    const ExplorationConfig& config,
    std::uint64_t seed,
    OptimizationTerminator& terminator,
    OptimizationListener* listener,
    OptimizationObserver* observer)
{
    return ExploreImpl(instance,
                       initialSolution,
                       config,
                       seed,
                       terminator,
                       listener,
                       observer,
                       nullptr);
}

float detail::ExplorationMinimumStripWidthForTrace(
    const jaguar::SPInstance& instance,
    const jaguar::SPSolution& solution) noexcept
{
    return CalculateMinimumStripWidth(instance, solution);
}

Result<ExplorationResult> detail::ExploreWithTrace(
    const jaguar::SPInstance& instance,
    const jaguar::SPSolution& initialSolution,
    const ExplorationConfig& config,
    std::uint64_t seed,
    OptimizationTerminator& terminator,
    OptimizationListener* listener,
    const detail::ExplorationTraceCallback& callback,
    OptimizationObserver* observer)
{
    return ExploreImpl(instance,
                       initialSolution,
                       config,
                       seed,
                       terminator,
                       listener,
                       observer,
                       &callback);
}
}
