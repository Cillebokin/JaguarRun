#include "SparrowCore/Eval/SeparationEvaluator.h"
#include "SparrowCore/Quantify/CollisionTracker.h"
#include "SparrowCore/Quantify/OverlapProxy.h"
#include "SparrowCore/Quantify/PairMatrix.h"
#include "SparrowCore/Optimizer/SparrowConfig.h"

#include "JaguarCore/Entities/Item.h"
#include "JaguarCore/Entities/Container.h"
#include "JaguarCore/Entities/Layout.h"
#include "JaguarCore/IO/Importer.h"
#include "JaguarCore/Problems/Spp.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <ostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
bool Expect(bool condition, std::ostream& output, const char* message)
{
    if (condition)
    {
        return true;
    }
    output << "QUANTIFY TEST FAILURE: " << message << '\n';
    return false;
}

jaguar::Item MakeItem(jaguar::ItemIndex index,
                      double width = 1.0,
                      double height = 1.0)
{
    const auto polygon = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {width, 0.0}, {width, height}, {0.0, height}});
    jaguar::SurrogateConfig surrogateConfig{};
    surrogateConfig.poleLimits = {{{3U, 0.0}, {0U, 0.0}, {0U, 0.0}}};
    const auto item = jaguar::Item::Create(
        index,
        static_cast<jaguar::ItemId>(1000U + index),
        {polygon.Value(),
         jaguar::Transform2d::Identity(),
         jaguar::ShapeModifyMode::Inflate,
         {}},
        surrogateConfig);
    return item.Value();
}

jaguar::Result<jaguar::SPInstance> MakeInstance()
{
    return jaguar::SPInstance::Create(
        {MakeItem(0U), MakeItem(1U)},
        {1U, 1U},
        4.0,
        jaguar::CDEConfig{},
        jaguar::ShapeModifyConfig{});
}

bool TestPairMatrix(std::ostream& output)
{
    const auto matrixResult = sparrow::PairMatrix::Create(3U);
    if (!Expect(matrixResult.HasValue(),
                output,
                "pair matrix creation must accept a small size"))
    {
        return false;
    }
    sparrow::PairMatrix matrix = matrixResult.Value();
    matrix.At(0U, 2U).loss = 2.0F;
    matrix.At(0U, 2U).weight = 3.0F;
    if (!Expect(matrix.Contains(2U, 0U)
                    && matrix.At(2U, 0U).WeightedLoss() == 6.0F,
                output,
                "pair matrix must provide symmetric triangular access"))
    {
        return false;
    }
    return Expect(matrix.TotalLoss() == 2.0F
                      && matrix.TotalWeightedLoss() == 6.0F,
                  output,
                  "pair matrix totals must count each pair once");
}

bool TestPairMatrixRustLayoutAndLossAccumulation(std::ostream& output)
{
    static_assert(
        !noexcept(std::declval<sparrow::PairMatrix&>().At(0U, 0U)),
        "PairMatrix::At must not promise noexcept for invalid indices");

    const auto matrixResult = sparrow::PairMatrix::Create(4U);
    if (!Expect(matrixResult.HasValue(),
                output,
                "Rust-layout pair matrix creation failed"))
    {
        return false;
    }
    sparrow::PairMatrix matrix = matrixResult.Value();
    matrix.At(0U, 2U).loss = 1.0F;
    matrix.At(0U, 3U).loss = 1.0F;
    matrix.At(1U, 1U).loss = 16777216.0F;
    return Expect(matrix.TotalLoss() == 16777218.0F,
                  output,
                  "pair loss accumulation must follow Rust's upper-triangle row order");
}

bool TestPairMatrixBoundsBehavior(std::ostream& output)
{
    const auto matrixResult = sparrow::PairMatrix::Create(4U);
    if (!Expect(matrixResult.HasValue(),
                output,
                "pair matrix bounds fixture creation failed"))
    {
        return false;
    }
    sparrow::PairMatrix matrix = matrixResult.Value();
    matrix.At(1U, 1U).loss = 11.0F;
    bool backingOutOfRangeThrows = false;
#ifdef NDEBUG
    const bool logicalOutOfRangeAliasesRustSlot = matrix.At(4U, 0U).loss == 11.0F;
#else
    bool logicalOutOfRangeThrows = false;
    try
    {
        static_cast<void>(matrix.At(4U, 0U).loss);
    }
    catch (const std::out_of_range&)
    {
        logicalOutOfRangeThrows = true;
    }
#endif
    try
    {
        static_cast<void>(matrix.At(10U, 0U).loss);
    }
    catch (const std::out_of_range&)
    {
        backingOutOfRangeThrows = true;
    }
    return Expect(
#ifdef NDEBUG
                  logicalOutOfRangeAliasesRustSlot &&
#else
                  logicalOutOfRangeThrows &&
#endif
                      backingOutOfRangeThrows,
                  output,
                  "PairMatrix::At must follow Rust's profile-specific invalid-index behavior");
}

bool TestPairMatrixPreservesNonFiniteLossArithmetic(std::ostream& output)
{
    const auto matrixResult = sparrow::PairMatrix::Create(2U);
    if (!Expect(matrixResult.HasValue(),
                output,
                "non-finite pair matrix fixture creation failed"))
    {
        return false;
    }
    sparrow::PairMatrix matrix = matrixResult.Value();
    matrix.At(0U, 0U).loss = std::numeric_limits<float>::infinity();
    matrix.At(0U, 0U).weight = 2.0F;
    matrix.At(0U, 1U).loss = std::numeric_limits<float>::quiet_NaN();
    return Expect(std::isinf(matrix.At(0U, 0U).WeightedLoss())
                      && std::isnan(matrix.TotalLoss())
                      && std::isnan(matrix.TotalWeightedLoss()),
                  output,
                  "pair loss and weighted-loss accumulation must preserve IEEE values");
}

bool TestPairMatrixOverflowMatchesRustProfile(std::ostream& output)
{
#ifndef NDEBUG
    bool debugOverflowThrows = false;
    try
    {
        static_cast<void>(sparrow::PairMatrix::Create(
            std::numeric_limits<std::size_t>::max()));
    }
    catch (const std::overflow_error&)
    {
        debugOverflowThrows = true;
    }
    return Expect(debugOverflowThrows,
                  output,
                  "debug pair matrix length arithmetic must fail on overflow");
#else
    const auto result = sparrow::PairMatrix::Create(
        std::numeric_limits<std::size_t>::max());
    if (!Expect(result.HasValue()
                    && result.Value().Size()
                           == std::numeric_limits<std::size_t>::max(),
                output,
                "release pair matrix length arithmetic must wrap like Rust"))
    {
        return false;
    }
    bool emptyBackingStoreThrows = false;
    try
    {
        static_cast<void>(result.Value().At(0U, 0U));
    }
    catch (const std::out_of_range&)
    {
        emptyBackingStoreThrows = true;
    }
    return Expect(emptyBackingStoreThrows,
                  output,
                  "wrapped release pair matrix must still bounds-check its empty Vec");
#endif
}

bool TestOverlapProxy(std::ostream& output)
{
    const jaguar::Item first = MakeItem(0U, 2.0, 2.0);
    const jaguar::Item second = MakeItem(1U, 2.0, 2.0);
    const jaguar::Surrogate* firstSurrogate =
        first.CollisionShape().GetSurrogate();
    const jaguar::Surrogate* secondSurrogate =
        second.CollisionShape().GetSurrogate();
    if (!Expect(firstSurrogate != nullptr && secondSurrogate != nullptr,
                output,
                "test items must have surrogates"))
    {
        return false;
    }
    const float overlap = sparrow::CalculateOverlapProxy(
        *firstSurrogate,
        jaguar::Transform2d::Identity(),
        *secondSurrogate,
        jaguar::Transform2d::Identity(),
        0.01F);
    const float separated = sparrow::CalculateOverlapProxy(
        *firstSurrogate,
        jaguar::Transform2d::Identity(),
        *secondSurrogate,
        jaguar::Transform2d::FromRotationTranslation(0.0, {20.0, 0.0}),
        0.01F);
    const float epsilon = static_cast<float>(std::max(
        first.CollisionShape().Diameter(), second.CollisionShape().Diameter()))
        * 0.01F;
    const float referenceProxy = sparrow::CalculateOverlapProxy(
        *firstSurrogate,
        jaguar::Transform2d::Identity(),
        *secondSurrogate,
        jaguar::Transform2d::Identity(),
        epsilon);
    const double shapePenalty = std::sqrt(
        std::sqrt(firstSurrogate->ConvexHullArea())
        * std::sqrt(secondSurrogate->ConvexHullArea()));
    const float expectedPairLoss = static_cast<float>(
        std::sqrt(static_cast<double>(referenceProxy)
                  + static_cast<double>(epsilon) * epsilon)
        * shapePenalty);
    const float pairLoss = sparrow::CalculatePolygonCollisionLoss(
        first.CollisionShape(), second.CollisionShape());
    const auto shape = first.CollisionShape().Transformed(
        jaguar::Transform2d::FromRotationTranslation(0.0, {-1.0, 0.0}));
    if (!Expect(shape.HasValue(), output, "proxy shape transform failed"))
    {
        return false;
    }
    const auto bounds = jaguar::Rect2d::Create(0.0, 0.0, 4.0, 4.0);
    if (!Expect(std::isfinite(overlap) && overlap > separated
                    && std::isfinite(separated)
                    && std::isfinite(pairLoss)
                    && std::abs(pairLoss - expectedPairLoss) < 1.0e-5F,
                output,
                "overlap proxy must apply Sparrow epsilon and shape penalty"))
    {
        return false;
    }

    const jaguar::Rect2d shapeBounds = shape.Value().BoundingBox();
    const auto intersection = shapeBounds.Intersection(bounds.Value());
    const double shapeArea = shapeBounds.Area();
    const double overlapArea = (shapeArea - intersection->Area())
        + 0.0001 * shapeArea;
    const double expectedContainerLoss = 2.0 * std::sqrt(overlapArea)
        * std::sqrt(shape.Value().GetSurrogate()->ConvexHullArea());
    const float containerLoss = sparrow::CalculateContainerPenalty(
        shape.Value(), bounds.Value());
    return Expect(std::isfinite(containerLoss)
                      && std::abs(containerLoss
                                   - static_cast<float>(expectedContainerLoss))
                             < 1.0e-5F,
                  output,
                  "container loss must match Sparrow's bounding-box formula");
}

bool TestCollisionTrackerAndSeparationEvaluator(std::ostream& output)
{
    const auto instanceResult = MakeInstance();
    if (!Expect(instanceResult.HasValue(), output, "tracker instance creation failed"))
    {
        return false;
    }
    auto problemResult = jaguar::SPProblem::Create(instanceResult.Value());
    if (!Expect(problemResult.HasValue(), output, "tracker problem creation failed"))
    {
        return false;
    }
    jaguar::SPProblem problem = std::move(problemResult.Value());
    if (!Expect(problem.ChangeStripWidth(8.0).HasValue(),
                output,
                "tracker strip resize failed"))
    {
        return false;
    }
    const auto firstHandle = problem.PlaceItem({0U, {0.0, {1.0, 1.0}}});
    const auto secondHandle = problem.PlaceItem({1U, {0.0, {1.0, 1.0}}});
    if (!Expect(firstHandle.HasValue() && secondHandle.HasValue(),
                output,
                "tracker overlapping placement setup failed"))
    {
        return false;
    }
    const jaguar::SPSolution solution = problem.Save();
    jaguar::Layout layout(solution.layoutSnapshot.container);
    if (!Expect(layout.Restore(solution.layoutSnapshot).HasValue(),
                output,
                "tracker layout restore failed"))
    {
        return false;
    }
    auto trackerResult = sparrow::CollisionTracker::Create(layout);
    if (!Expect(trackerResult.HasValue(), output, "collision tracker creation failed"))
    {
        return false;
    }
    sparrow::CollisionTracker tracker = std::move(trackerResult.Value());
    if (!Expect(tracker.ItemCount() == 2U
                    && tracker.TotalLoss() > 0.0F
                    && tracker.PairLoss(0U, 1U) == tracker.PairLoss(1U, 0U),
                output,
                "collision tracker must quantify the overlapping pair"))
    {
        return false;
    }
    const auto rejectsOutOfRange = [](const auto& getter) {
        try
        {
            (void)getter();
        }
        catch (const std::out_of_range&)
        {
            return true;
        }
        return false;
    };
    if (!Expect(rejectsOutOfRange([&] { return tracker.PairLoss(9U, 0U); })
                    && rejectsOutOfRange([&] { return tracker.PairWeight(9U, 0U); })
                    && rejectsOutOfRange([&] { return tracker.ContainerLoss(2U); })
                    && rejectsOutOfRange([&] { return tracker.ContainerWeight(2U); })
                    && rejectsOutOfRange([&] { return tracker.ItemLoss(2U); })
                    && rejectsOutOfRange([&] { return tracker.WeightedItemLoss(2U); }),
                output,
                "tracker numeric getters must reject invalid indices instead of returning zero"))
    {
        return false;
    }
    const sparrow::CollisionTrackerSnapshot snapshot = tracker.Save();
    tracker.UpdateWeights();
    if (!Expect(tracker.Restore(snapshot, layout).HasValue(),
                output,
                "collision tracker snapshot restore failed"))
    {
        return false;
    }

    sparrow::CollisionTrackerSnapshot invalidSnapshot = snapshot;
    invalidSnapshot.isSeparated = !tracker.IsSeparated();
    if (!Expect(!tracker.Restore(invalidSnapshot, layout).HasValue(),
                output,
                "collision tracker must reject a stale feasibility flag"))
    {
        return false;
    }

    invalidSnapshot = snapshot;
    invalidSnapshot.pairMatrix.At(0U, 1U).loss =
        std::numeric_limits<float>::quiet_NaN();
    sparrow::CollisionTracker nonFiniteTracker = tracker;
    if (!Expect(nonFiniteTracker.Restore(invalidSnapshot, layout).HasValue(),
                output,
                "collision tracker restore must preserve Rust's non-finite loss state"))
    {
        return false;
    }
    if (!Expect(std::isnan(nonFiniteTracker.PairLoss(0U, 1U))
                    && std::isnan(nonFiniteTracker.ItemLoss(0U))
                    && std::isnan(nonFiniteTracker.WeightedItemLoss(0U))
                    && std::isnan(nonFiniteTracker.TotalLoss())
                    && std::isnan(nonFiniteTracker.TotalWeightedLoss()),
                output,
                "tracker loss accessors must propagate non-finite values like Rust"))
    {
        return false;
    }
    nonFiniteTracker.UpdateWeights();
    if (!Expect(nonFiniteTracker.PairWeight(0U, 1U) == 1.0F,
                output,
                "Rust f32 max must floor NaN and infinite GLS updates at one"))
    {
        return false;
    }

    sparrow::CollisionTracker positiveInfiniteTracker = tracker;
    sparrow::CollisionTrackerSnapshot positiveInfiniteSnapshot = snapshot;
    positiveInfiniteSnapshot.pairMatrix.At(0U, 1U).loss =
        std::numeric_limits<float>::infinity();
    if (!Expect(positiveInfiniteTracker.Restore(positiveInfiniteSnapshot, layout).HasValue()
                    && positiveInfiniteTracker.TotalLoss()
                           == std::numeric_limits<float>::infinity()
                    && positiveInfiniteTracker.TotalWeightedLoss()
                           == std::numeric_limits<float>::infinity(),
                output,
                "tracker totals must preserve positive infinite collision loss"))
    {
        return false;
    }
    positiveInfiniteTracker.UpdateWeights();
    if (!Expect(positiveInfiniteTracker.PairWeight(0U, 1U) == 1.0F,
                output,
                "Rust f32 max must floor an infinite GLS update at one"))
    {
        return false;
    }

    sparrow::CollisionTracker negativeInfiniteTracker = tracker;
    sparrow::CollisionTrackerSnapshot negativeInfiniteSnapshot = snapshot;
    negativeInfiniteSnapshot.pairMatrix.At(0U, 1U).loss =
        -std::numeric_limits<float>::infinity();
    if (!Expect(negativeInfiniteTracker.Restore(negativeInfiniteSnapshot, layout).HasValue()
                    && negativeInfiniteTracker.TotalLoss()
                           == -std::numeric_limits<float>::infinity()
                    && negativeInfiniteTracker.TotalWeightedLoss()
                           == -std::numeric_limits<float>::infinity(),
                output,
                "tracker totals must preserve negative infinite collision loss"))
    {
        return false;
    }
    negativeInfiniteTracker.UpdateWeights();
    if (!Expect(negativeInfiniteTracker.PairWeight(0U, 1U) == 1.0F,
                output,
                "Rust f32 max must floor a negative infinite GLS update at one"))
    {
        return false;
    }

    sparrow::SeparationEvaluator evaluator(
        layout,
        instanceResult.Value().GetItem(1U),
        secondHandle.Value(),
        tracker);
    const sparrow::SampleEval collision = evaluator.Evaluate(
        jaguar::Transform2d::Identity(), std::nullopt);
    const sparrow::SampleEval clear = evaluator.Evaluate(
        jaguar::Transform2d::FromRotationTranslation(0.0, {4.0, 1.0}),
        std::nullopt);
    if (!Expect(collision.kind == sparrow::SampleKind::SAMPLE_KIND_COLLISION
                    && std::isfinite(collision.loss) && collision.loss > 0.0F
                    && clear.kind == sparrow::SampleKind::SAMPLE_KIND_CLEAR
                    && evaluator.SurrogateRejectCount()
                           <= evaluator.EvaluationCount()
                    && evaluator.ExactCollisionCount() > 0U,
                output,
                "separation evaluator must distinguish colliding and clear moves and count checks"))
    {
        return false;
    }

    const auto removed = problem.RemoveItem(secondHandle.Value());
    if (!Expect(removed.HasValue(), output, "tracker incremental remove failed"))
    {
        return false;
    }
    const auto moved = problem.PlaceItem({
        removed.Value().itemIndex,
        jaguar::Transform2d::FromRotationTranslation(0.0, {4.0, 1.0})});
    if (!Expect(moved.HasValue(), output, "tracker incremental placement failed"))
    {
        return false;
    }
    const jaguar::SPSolution movedSolution = problem.Save();
    jaguar::Layout movedLayout(movedSolution.layoutSnapshot.container);
    if (!Expect(movedLayout.Restore(movedSolution.layoutSnapshot).HasValue(),
                output,
                "tracker incremental layout restore failed"))
    {
        return false;
    }
    if (!Expect(tracker.UpdateMovedItem(
                    movedLayout, secondHandle.Value(), moved.Value())
                    .HasValue(),
                output,
                "tracker must update one moved item without a full rebuild"))
    {
        return false;
    }
    const bool oldHandleTracked =
        tracker.IndexForHandle(secondHandle.Value()).has_value();
    const bool newHandleTracked = tracker.IndexForHandle(moved.Value()).has_value();
    return Expect(!oldHandleTracked && newHandleTracked && tracker.IsSeparated()
                      && tracker.PairLoss(0U, 1U) == 0.0F
                      && tracker.TotalLoss() == 0.0F,
                  output,
                  "incremental tracker update must replace the moved handle and clear separated losses");
}

bool TestCollisionTrackerMoveRecomputesOnlyOneRow(std::ostream& output)
{
    std::vector<jaguar::Item> items;
    for (jaguar::ItemIndex index = 0U; index < 3U; ++index)
    {
        items.push_back(MakeItem(index));
    }
    const auto instanceResult = jaguar::SPInstance::Create(
        std::move(items),
        {1U, 1U, 1U},
        4.0,
        jaguar::CDEConfig{},
        jaguar::ShapeModifyConfig{});
    if (!Expect(instanceResult.HasValue(),
                output,
                "tracker move instance creation failed"))
    {
        return false;
    }
    auto problemResult = jaguar::SPProblem::Create(instanceResult.Value());
    if (!Expect(problemResult.HasValue(), output, "tracker move problem creation failed"))
    {
        return false;
    }
    jaguar::SPProblem problem = std::move(problemResult.Value());
    if (!Expect(problem.ChangeStripWidth(8.0).HasValue(),
                output,
                "tracker move strip resize failed"))
    {
        return false;
    }

    std::vector<jaguar::PlacedItemHandle> handles;
    for (jaguar::ItemIndex index = 0U; index < 3U; ++index)
    {
        const auto placed = problem.PlaceItem({index, {0.0, {1.0, 1.0}}});
        if (!Expect(placed.HasValue(), output, "tracker move placement failed"))
        {
            return false;
        }
        handles.push_back(placed.Value());
    }
    const jaguar::SPSolution initialSolution = problem.Save();
    jaguar::Layout initialLayout(initialSolution.layoutSnapshot.container);
    if (!Expect(initialLayout.Restore(initialSolution.layoutSnapshot).HasValue(),
                output,
                "tracker move initial layout restore failed"))
    {
        return false;
    }
    auto trackerResult = sparrow::CollisionTracker::Create(initialLayout);
    if (!Expect(trackerResult.HasValue(), output, "tracker move tracker creation failed"))
    {
        return false;
    }
    sparrow::CollisionTracker tracker = std::move(trackerResult.Value());
    sparrow::CollisionTrackerSnapshot weightedSnapshot = tracker.Save();
    std::size_t pairOrdinal = 0U;
    for (std::size_t first = 0U; first < 3U; ++first)
    {
        for (std::size_t second = first; second < 3U; ++second)
        {
            weightedSnapshot.pairMatrix.At(first, second).weight =
                1.0F + static_cast<float>(pairOrdinal++) * 0.25F;
        }
        weightedSnapshot.containerEntries[first].weight =
            1.5F + static_cast<float>(first) * 0.5F;
    }
    if (!Expect(tracker.Restore(weightedSnapshot, initialLayout).HasValue(),
                output,
                "tracker move unique weights restore failed"))
    {
        return false;
    }
    const sparrow::CollisionTrackerSnapshot before = tracker.Save();

    const auto removed = problem.RemoveItem(handles[2]);
    if (!Expect(removed.HasValue(), output, "tracker move removal failed"))
    {
        return false;
    }
    const auto moved = problem.PlaceItem({
        removed.Value().itemIndex,
        jaguar::Transform2d::FromRotationTranslation(0.0, {4.0, 1.0})});
    if (!Expect(moved.HasValue(), output, "tracker move replacement failed"))
    {
        return false;
    }
    const jaguar::SPSolution movedSolution = problem.Save();
    jaguar::Layout movedLayout(movedSolution.layoutSnapshot.container);
    if (!Expect(movedLayout.Restore(movedSolution.layoutSnapshot).HasValue(),
                output,
                "tracker move final layout restore failed"))
    {
        return false;
    }
    if (!Expect(tracker.UpdateMovedItem(movedLayout, handles[2], moved.Value()).HasValue(),
                output,
                "tracker move incremental update failed"))
    {
        return false;
    }
    const sparrow::CollisionTrackerSnapshot after = tracker.Save();
    for (std::size_t first = 0U; first < 3U; ++first)
    {
        for (std::size_t second = first; second < 3U; ++second)
        {
            const sparrow::CollisionEntry& oldEntry = before.pairMatrix.At(first, second);
            const sparrow::CollisionEntry& newEntry = after.pairMatrix.At(first, second);
            const bool isMovedRow = first == 2U || second == 2U;
            if (!Expect((isMovedRow ? newEntry.loss == 0.0F
                                    : newEntry.loss == oldEntry.loss)
                            && newEntry.weight == oldEntry.weight,
                        output,
                        "tracker move must update only the moved pair row and preserve all weights"))
            {
                return false;
            }
        }
        const bool containerLossMatches = first == 2U
            ? after.containerEntries[first].loss == 0.0F
            : after.containerEntries[first].loss == before.containerEntries[first].loss;
        if (!Expect(containerLossMatches
                        && after.containerEntries[first].weight
                               == before.containerEntries[first].weight,
                    output,
                    "tracker move must update only the moved container entry"))
        {
            return false;
        }
    }
    return Expect(tracker.TotalLoss() == before.pairMatrix.At(0U, 1U).loss
                      && !tracker.IndexForHandle(handles[2]).has_value()
                      && tracker.IndexForHandle(moved.Value()).has_value(),
                  output,
                  "tracker move must preserve the untouched pair and replace the old handle");
}

bool TestExteriorHazardRawLossMatchesRust(std::ostream& output)
{
    std::ifstream fixture("SparrowCore/Tests/Fixtures/CollisionLossFloorRust.json");
    if (!fixture.is_open())
    {
        fixture.open("../SparrowCore/Tests/Fixtures/CollisionLossFloorRust.json");
    }
    if (!Expect(fixture.is_open(), output,
                "Exterior Rust reference fixture could not be opened"))
    {
        return false;
    }
    const std::string json{std::istreambuf_iterator<char>(fixture),
                           std::istreambuf_iterator<char>()};
    const jaguar::Importer importer(sparrow::MakeRustDefaultImporterConfig());
    const auto instance = importer.ImportSPInstanceJson(json);
    if (!Expect(instance.HasValue(), output,
                "Exterior Rust reference fixture import failed"))
    {
        return false;
    }
    std::vector<jaguar::Item> items;
    std::vector<std::size_t> demands;
    items.reserve(instance.Value().ItemCount());
    demands.reserve(instance.Value().ItemCount());
    for (std::size_t index = 0U; index < instance.Value().ItemCount(); ++index)
    {
        items.push_back(instance.Value().GetItem(index));
        demands.push_back(instance.Value().ItemDemand(index));
    }
    jaguar::Strip strip = instance.Value().BaseStrip();
    if (!Expect(strip.SetWidth(1.0).HasValue(), output,
                "Exterior reference strip width setup failed"))
    {
        return false;
    }
    const auto explicitWidthInstance = jaguar::SPInstance::Create(
        std::move(items), std::move(demands), strip);
    if (!Expect(explicitWidthInstance.HasValue(), output,
                "Exterior reference explicit-width instance creation failed"))
    {
        return false;
    }
    auto problemResult = jaguar::SPProblem::Create(explicitWidthInstance.Value());
    if (!Expect(problemResult.HasValue(), output,
                "Exterior reference problem creation failed"))
    {
        output << "  error: " << problemResult.GetError().message << '\n';
        return false;
    }
    jaguar::SPProblem problem = std::move(problemResult.Value());
    const auto firstHandle = problem.PlaceItem({0U, {0.0, {0.2, 0.5}}});
    const auto movingHandle = problem.PlaceItem({1U, {0.0, {0.5, 0.5}}});
    if (!Expect(firstHandle.HasValue() && movingHandle.HasValue(), output,
                "Exterior reference placements failed"))
    {
        return false;
    }
    const jaguar::SPSolution solution = problem.Save();
    jaguar::Layout layout(solution.layoutSnapshot.container);
    if (!Expect(layout.Restore(solution.layoutSnapshot).HasValue(), output,
                "Exterior reference layout restore failed"))
    {
        return false;
    }
    auto trackerResult = sparrow::CollisionTracker::Create(layout);
    if (!Expect(trackerResult.HasValue(), output,
                "Exterior reference tracker creation failed"))
    {
        return false;
    }
    sparrow::CollisionTracker tracker = std::move(trackerResult.Value());
    if (!Expect(tracker.PairLoss(0U, 1U) == 0.0F, output,
                "Exterior fixture must begin without a PlacedItem collision"))
    {
        return false;
    }

    sparrow::SeparationEvaluator evaluator(
        layout,
        instance.Value().GetItem(1U),
        movingHandle.Value(),
        tracker);
    const sparrow::SampleEval collision = evaluator.Evaluate(
        jaguar::Transform2d::FromRotationTranslation(0.0, {1.0002, 0.5}),
        std::nullopt);
    std::uint32_t lossBits = 0U;
    std::memcpy(&lossBits, &collision.loss, sizeof(lossBits));
    return Expect(collision.kind == sparrow::SampleKind::SAMPLE_KIND_COLLISION
                      && lossBits == 0x391152f2U,
                  output,
                  "Exterior hazard loss must match the Rust raw f32 bits");
}

bool TestSeparationEvaluatorPreservesTinyCollisionLoss(std::ostream& output)
{
    jaguar::SurrogateConfig surrogateConfig{};
    surrogateConfig.poleLimits = {{{64U, 0.0}, {16U, 0.8}, {8U, 0.9}}};
    surrogateConfig.failFastPoleAreaRatio = 0.5;
    const auto makeTinyItem = [&surrogateConfig](jaguar::ItemIndex index) {
        const std::vector<jaguar::Point2d> points = index == 0U
            ? std::vector<jaguar::Point2d>{{-0.00005, -0.00005},
                                           {0.00005, -0.00005},
                                           {0.00005, 0.00005},
                                           {-0.00005, 0.00005}}
            : std::vector<jaguar::Point2d>{{-0.00006, -0.00004},
                                           {0.00006, -0.00004},
                                           {0.00006, 0.00004},
                                           {-0.00006, 0.00004}};
        const auto polygon = jaguar::SimplePolygon::Create(points);
        jaguar::RotationRange rotationRange;
        rotationRange.mode = jaguar::RotationMode::Continuous;
        return jaguar::Item::Create(
            index,
            static_cast<jaguar::ItemId>(1000U + index),
            {polygon.Value(),
             jaguar::Transform2d::Identity(),
             jaguar::ShapeModifyMode::Inflate,
             {}},
            surrogateConfig,
            std::nullopt,
            rotationRange);
    };
    const auto firstItem = makeTinyItem(0U);
    const auto secondItem = makeTinyItem(1U);
    if (!Expect(firstItem.HasValue() && secondItem.HasValue(),
                output,
                "tiny-loss item creation failed"))
    {
        return false;
    }
    jaguar::CDEConfig cdeConfig{};
    cdeConfig.cdThreshold = 64U;
    cdeConfig.itemSurrogateConfig = surrogateConfig;
    const auto stripResult = jaguar::Strip::Create(
        1.0, cdeConfig, jaguar::ShapeModifyConfig{}, 1.0);
    if (!Expect(stripResult.HasValue(),
                output,
                "tiny-loss strip creation failed"))
    {
        return false;
    }
    const auto instanceResult = jaguar::SPInstance::Create(
        {firstItem.Value(), secondItem.Value()},
        {1U, 1U},
        stripResult.Value());
    if (!Expect(instanceResult.HasValue(),
                output,
                "tiny-loss instance creation failed"))
    {
        return false;
    }
    auto problemResult = jaguar::SPProblem::Create(instanceResult.Value());
    if (!Expect(problemResult.HasValue(),
                output,
                "tiny-loss problem creation failed"))
    {
        return false;
    }
    jaguar::SPProblem problem = std::move(problemResult.Value());
    const jaguar::Transform2d overlapTransform =
        jaguar::Transform2d::FromRotationTranslation(0.0, {0.5, 0.5});
    const auto first = problem.PlaceItem({0U, overlapTransform});
    const auto moving = problem.PlaceItem({1U, overlapTransform});
    if (!Expect(first.HasValue() && moving.HasValue(),
                output,
                "tiny-loss overlapping placements failed"))
    {
        return false;
    }

    const jaguar::SPSolution solution = problem.Save();
    jaguar::Layout layout(solution.layoutSnapshot.container);
    if (!Expect(layout.Restore(solution.layoutSnapshot).HasValue(),
                output,
                "tiny-loss layout restore failed"))
    {
        return false;
    }
    auto trackerResult = sparrow::CollisionTracker::Create(layout);
    if (!Expect(trackerResult.HasValue(),
                output,
                "tiny-loss tracker creation failed"))
    {
        return false;
    }
    sparrow::SeparationEvaluator evaluator(
        layout,
        instanceResult.Value().GetItem(1U),
        moving.Value(),
        trackerResult.Value());
    const sparrow::SampleEval collision =
        evaluator.Evaluate(overlapTransform, std::nullopt);
    std::uint32_t lossBits = 0U;
    std::memcpy(&lossBits, &collision.loss, sizeof(lossBits));
    if (lossBits != 0x3256579cU)
    {
        output << "QUANTIFY TEST FAILURE: tiny collision loss bits differ from Rust; got "
               << lossBits << '\n';
    }
    if (!Expect(collision.kind == sparrow::SampleKind::SAMPLE_KIND_COLLISION
                    && collision.loss > 0.0F && collision.loss < 1.0e-6F
                    && lossBits == 0x3256579cU,
                output,
                "separation evaluator must preserve the Rust sub-micro collision loss bits"))
    {
        return false;
    }

    const jaguar::Transform2d rotatedTransform =
        jaguar::Transform2d::FromRotationTranslation(0.137, {0.5, 0.5});
    const sparrow::SampleEval rotatedCollision =
        evaluator.Evaluate(rotatedTransform, std::nullopt);
    std::uint32_t rotatedLossBits = 0U;
    std::memcpy(&rotatedLossBits,
                &rotatedCollision.loss,
                sizeof(rotatedLossBits));
    if (rotatedLossBits != 0x32560315U)
    {
        output << "QUANTIFY TEST FAILURE: rotated tiny collision loss bits differ from Rust; got "
               << rotatedLossBits << '\n';
    }
    if (!Expect(rotatedCollision.kind
                        == sparrow::SampleKind::SAMPLE_KIND_COLLISION
                    && rotatedCollision.loss > 0.0F
                    && rotatedCollision.loss < 1.0e-6F
                      && rotatedLossBits == 0x32560315U,
                output,
                "separation evaluator must preserve Rust pole accumulation order"))
    {
        return false;
    }

    constexpr double rotations[]{
        0.013, 0.281, 0.5235988, 0.7853982, 1.234, 2.345, 3.1415927, 4.712389, 5.999};
    constexpr std::uint32_t expectedLossBits[]{
        0x32565e21U,
        0x325695a6U,
        0x32559559U,
        0x325433cdU,
        0x32568aabU,
        0x32543197U,
        0x325658d2U,
        0x325656fdU,
        0x325698bcU};
    for (std::size_t index = 0U;
         index < sizeof(rotations) / sizeof(rotations[0]);
         ++index)
    {
        const auto transform = jaguar::Transform2d::FromRotationTranslation(
            rotations[index], {0.5, 0.5});
        const sparrow::SampleEval evaluation =
            evaluator.Evaluate(transform, std::nullopt);
        std::uint32_t actualLossBits = 0U;
        std::memcpy(&actualLossBits, &evaluation.loss, sizeof(actualLossBits));
        if (actualLossBits != expectedLossBits[index])
        {
            output << "QUANTIFY TEST FAILURE: raw loss bits differ from Rust at rotation "
                   << rotations[index] << "; got " << actualLossBits << '\n';
        }
        if (!Expect(evaluation.kind == sparrow::SampleKind::SAMPLE_KIND_COLLISION
                        && evaluation.loss > 0.0F && evaluation.loss < 1.0e-6F
                        && actualLossBits == expectedLossBits[index],
                    output,
                    "separation evaluator must preserve Rust multi-angle raw loss bits"))
        {
            return false;
        }
    }
    return true;
}

bool TestRustMultiHazardLossAndEarlyStop(std::ostream& output)
{
    jaguar::SurrogateConfig surrogateConfig{};
    surrogateConfig.poleLimits = {{{64U, 0.0}, {16U, 0.8}, {8U, 0.9}}};
    surrogateConfig.failFastPoleAreaRatio = 0.5;
    constexpr double dimensions[][2] = {
        {0.0001, 0.0001},
        {0.00012, 0.00008},
        {0.0002, 0.0001},
        {0.00013, 0.00007},
    };
    std::vector<jaguar::Item> items;
    items.reserve(4U);
    for (jaguar::ItemIndex index = 0U; index < 4U; ++index)
    {
        const double width = dimensions[index][0];
        const double height = dimensions[index][1];
        const auto polygon = jaguar::SimplePolygon::Create({
            {-width / 2.0, -height / 2.0},
            {width / 2.0, -height / 2.0},
            {width / 2.0, height / 2.0},
            {-width / 2.0, height / 2.0},
        });
        if (!Expect(polygon.HasValue(),
                    output,
                    "multi-hazard polygon creation failed"))
        {
            return false;
        }
        jaguar::RotationRange rotationRange;
        rotationRange.mode = jaguar::RotationMode::Continuous;
        const auto item = jaguar::Item::Create(
            index,
            static_cast<jaguar::ItemId>(950U + index),
            {polygon.Value(),
             jaguar::Transform2d::Identity(),
             jaguar::ShapeModifyMode::Inflate,
             {}},
            surrogateConfig,
            std::nullopt,
            rotationRange);
        if (!Expect(item.HasValue(),
                    output,
                    "multi-hazard item creation failed"))
        {
            return false;
        }
        items.push_back(item.Value());
    }

    jaguar::CDEConfig cdeConfig{};
    cdeConfig.cdThreshold = 64U;
    cdeConfig.itemSurrogateConfig = surrogateConfig;
    const auto strip = jaguar::Strip::Create(
        1.0, cdeConfig, jaguar::ShapeModifyConfig{}, 1.0);
    if (!Expect(strip.HasValue(), output, "multi-hazard strip creation failed"))
    {
        return false;
    }
    const auto instance = jaguar::SPInstance::Create(
        std::move(items), {1U, 1U, 1U, 1U}, strip.Value());
    if (!Expect(instance.HasValue(), output, "multi-hazard instance creation failed"))
    {
        return false;
    }
    auto problemResult = jaguar::SPProblem::Create(instance.Value());
    if (!Expect(problemResult.HasValue(), output, "multi-hazard problem creation failed"))
    {
        return false;
    }
    jaguar::SPProblem problem = std::move(problemResult.Value());
    const jaguar::Transform2d transform =
        jaguar::Transform2d::FromRotationTranslation(0.0, {0.5, 0.5});
    std::vector<jaguar::PlacedItemHandle> handles;
    for (jaguar::ItemIndex index = 0U; index < 4U; ++index)
    {
        const auto placed = problem.PlaceItem({index, transform});
        if (!Expect(placed.HasValue(), output, "multi-hazard placement failed"))
        {
            return false;
        }
        handles.push_back(placed.Value());
    }

    const jaguar::SPSolution solution = problem.Save();
    jaguar::Layout layout(solution.layoutSnapshot.container);
    if (!Expect(layout.Restore(solution.layoutSnapshot).HasValue(),
                output,
                "multi-hazard layout restore failed"))
    {
        return false;
    }
    auto trackerResult = sparrow::CollisionTracker::Create(layout);
    if (!Expect(trackerResult.HasValue(),
                output,
                "multi-hazard tracker creation failed"))
    {
        return false;
    }
    sparrow::CollisionTracker tracker = std::move(trackerResult.Value());
    auto trackerSnapshot = tracker.Save();
    constexpr float weights[]{5.0e14F, 1.0e8F, 5.0e6F};
    for (std::size_t index = 0U; index < 3U; ++index)
    {
        trackerSnapshot.pairMatrix.At(3U, index).weight = weights[index];
    }
    if (!Expect(tracker.Restore(trackerSnapshot, layout).HasValue(),
                output,
                "multi-hazard reference weights must restore"))
    {
        return false;
    }

    sparrow::SeparationEvaluator evaluator(
        layout, instance.Value().GetItem(3U), handles[3], tracker);
    const sparrow::SampleEval collision = evaluator.Evaluate(transform, std::nullopt);
    std::uint32_t lossBits = 0U;
    std::memcpy(&lossBits, &collision.loss, sizeof(lossBits));
    if (lossBits != 0x4ab5c931U)
    {
        output << "QUANTIFY TEST FAILURE: multi-hazard raw loss differs from Rust; got "
               << lossBits << '\n';
    }
    if (!Expect(collision.kind == sparrow::SampleKind::SAMPLE_KIND_COLLISION
                    && lossBits == 0x4ab5c931U,
                output,
                "multi-hazard weighted loss must match Rust raw bits"))
    {
        return false;
    }

    const float belowLoss = std::nextafter(
        collision.loss, -std::numeric_limits<float>::infinity());
    const float aboveLoss = std::nextafter(
        collision.loss, std::numeric_limits<float>::infinity());
    const sparrow::SampleEval belowBound = evaluator.Evaluate(
        transform,
        sparrow::SampleEval{sparrow::SampleKind::SAMPLE_KIND_COLLISION, belowLoss});
    const sparrow::SampleEval equalBound = evaluator.Evaluate(
        transform,
        sparrow::SampleEval{sparrow::SampleKind::SAMPLE_KIND_COLLISION, collision.loss});
    const sparrow::SampleEval aboveBound = evaluator.Evaluate(
        transform,
        sparrow::SampleEval{sparrow::SampleKind::SAMPLE_KIND_COLLISION, aboveLoss});
    std::uint32_t aboveBoundBits = 0U;
    std::memcpy(&aboveBoundBits, &aboveBound.loss, sizeof(aboveBoundBits));
    if (!Expect(belowBound.kind == sparrow::SampleKind::SAMPLE_KIND_INVALID
                    && equalBound.kind == sparrow::SampleKind::SAMPLE_KIND_INVALID
                    && aboveBound.kind == sparrow::SampleKind::SAMPLE_KIND_COLLISION
                    && aboveBoundBits == 0x4ab5c931U,
                output,
                "adjacent collision-loss bounds must match Rust stop semantics"))
    {
        return false;
    }

    const std::size_t exactCountBefore = evaluator.ExactCollisionCount();
    const std::size_t surrogateCountBefore = evaluator.SurrogateRejectCount();
    const sparrow::SampleEval bounded = evaluator.Evaluate(
        transform,
        sparrow::SampleEval{sparrow::SampleKind::SAMPLE_KIND_COLLISION, 0.01F});
    if (!Expect(bounded.kind == sparrow::SampleKind::SAMPLE_KIND_INVALID
                    && evaluator.SurrogateRejectCount() == surrogateCountBefore + 1U
                    && evaluator.ExactCollisionCount() == exactCountBefore,
                output,
                "over-budget surrogate collision must stop before exact collection"))
    {
        return false;
    }

    sparrow::CollisionTrackerSnapshot zeroWeightSnapshot = tracker.Save();
    const float minimumPositiveWeight = std::numeric_limits<float>::denorm_min();
    for (std::size_t index = 0U; index < 3U; ++index)
    {
        zeroWeightSnapshot.pairMatrix.At(3U, index).weight = minimumPositiveWeight;
    }
    if (!Expect(tracker.Restore(zeroWeightSnapshot, layout).HasValue(),
                output,
                "underflow-weight reference state must restore"))
    {
        return false;
    }
    sparrow::SeparationEvaluator zeroLossEvaluator(
        layout, instance.Value().GetItem(3U), handles[3], tracker);
    const sparrow::SampleEval zeroLossCollision = zeroLossEvaluator.Evaluate(
        transform,
        sparrow::SampleEval{sparrow::SampleKind::SAMPLE_KIND_CLEAR, 0.0F});
    return Expect(zeroLossCollision.kind == sparrow::SampleKind::SAMPLE_KIND_COLLISION
                      && zeroLossCollision.loss == 0.0F,
                  output,
                  "Clear upper bound must preserve the classification of a zero-loss collision");
}

bool TestSeparationEvaluatorIgnoresItsCurrentHazard(std::ostream& output)
{
    const auto instanceResult = MakeInstance();
    if (!Expect(instanceResult.HasValue(),
                output,
                "self-hazard instance creation failed"))
    {
        return false;
    }
    auto problemResult = jaguar::SPProblem::Create(instanceResult.Value());
    if (!Expect(problemResult.HasValue(),
                output,
                "self-hazard problem creation failed"))
    {
        return false;
    }
    jaguar::SPProblem problem = std::move(problemResult.Value());
    if (!Expect(problem.ChangeStripWidth(8.0).HasValue(),
                output,
                "self-hazard strip resize failed"))
    {
        return false;
    }
    const auto first = problem.PlaceItem({0U, {0.0, {1.0, 1.0}}});
    const auto moving = problem.PlaceItem({1U, {0.0, {5.0, 1.0}}});
    if (!Expect(first.HasValue() && moving.HasValue(),
                output,
                "self-hazard placements failed"))
    {
        return false;
    }

    const jaguar::SPSolution solution = problem.Save();
    jaguar::Layout layout(solution.layoutSnapshot.container);
    if (!Expect(layout.Restore(solution.layoutSnapshot).HasValue(),
                output,
                "self-hazard layout restore failed"))
    {
        return false;
    }
    auto trackerResult = sparrow::CollisionTracker::Create(layout);
    if (!Expect(trackerResult.HasValue(),
                output,
                "self-hazard tracker creation failed"))
    {
        return false;
    }

    sparrow::SeparationEvaluator evaluator(
        layout,
        instanceResult.Value().GetItem(1U),
        moving.Value(),
        trackerResult.Value());
    const auto current = evaluator.Evaluate(
        jaguar::Transform2d::FromRotationTranslation(0.0, {5.0, 1.0}),
        std::nullopt);
    return Expect(current.kind == sparrow::SampleKind::SAMPLE_KIND_CLEAR
                      && current.loss == 0.0F
                      && evaluator.SurrogateRejectCount() == 0U
                      && evaluator.ExactCollisionCount() == 0U,
                  output,
                  "separation evaluation must ignore the moving item's actual CDE hazard handle");
}

bool TestUnsupportedContainerHazardsMatchRustPanic(std::ostream& output)
{
    const auto makePolygon = [](std::vector<jaguar::Point2d> points) {
        const auto polygon = jaguar::SimplePolygon::Create(std::move(points));
        return polygon.Value();
    };
    const jaguar::SimplePolygon outer = makePolygon({
        {0.0, 0.0}, {20.0, 0.0}, {20.0, 20.0}, {0.0, 20.0}});
    const jaguar::SimplePolygon hole = makePolygon({
        {8.0, 8.0}, {10.0, 8.0}, {10.0, 10.0}, {8.0, 10.0}});
    const jaguar::SimplePolygon inferiorZone = makePolygon({
        {0.0, 0.0}, {3.0, 0.0}, {3.0, 3.0}, {0.0, 3.0}});
    jaguar::CDEConfig cdeConfig{};
    cdeConfig.quadtreeDepth = 4U;
    cdeConfig.cdThreshold = 8U;
    const auto containerResult = jaguar::Container::Create(
        9600U,
        {outer, jaguar::Transform2d::Identity(), jaguar::ShapeModifyMode::Deflate, {}},
        {{0U, {hole}}, {1U, {inferiorZone}}},
        cdeConfig);
    if (!Expect(containerResult.HasValue(), output,
                "unsupported-hazard container creation failed"))
    {
        return false;
    }

    const jaguar::Item movingItem = MakeItem(0U);
    const auto trackerCreationThrows = [&](const jaguar::Transform2d& transform) {
        jaguar::Layout hazardLayout(containerResult.Value());
        if (!hazardLayout.PlaceItem(movingItem, transform).HasValue())
        {
            return false;
        }
        try
        {
            (void)sparrow::CollisionTracker::Create(hazardLayout);
        }
        catch (const std::logic_error& error)
        {
            return std::string(error.what()) == "unsupported hazard entity";
        }
        return false;
    };
    if (!Expect(trackerCreationThrows(
                    jaguar::Transform2d::FromRotationTranslation(0.0, {9.0, 9.0}))
                    && trackerCreationThrows(
                        jaguar::Transform2d::FromRotationTranslation(0.0, {1.5, 1.5})),
                output,
                "tracker creation must propagate Hole and InferiorQualityZone hazards"))
    {
        return false;
    }

    jaguar::Layout layout(containerResult.Value());
    const auto movingHandle = layout.PlaceItem(
        movingItem, jaguar::Transform2d::FromRotationTranslation(0.0, {12.0, 12.0}));
    if (!Expect(movingHandle.HasValue(), output,
                "unsupported-hazard moving item placement failed"))
    {
        return false;
    }
    const auto trackerResult = sparrow::CollisionTracker::Create(layout);
    if (!Expect(trackerResult.HasValue(), output,
                "unsupported-hazard collision tracker creation failed"))
    {
        return false;
    }

    sparrow::SeparationEvaluator evaluator(
        layout, movingItem, movingHandle.Value(), trackerResult.Value());
    const auto throwsUnsupportedHazard = [&evaluator](const jaguar::Transform2d& transform) {
        try
        {
            (void)evaluator.Evaluate(transform, std::nullopt);
        }
        catch (const std::logic_error& error)
        {
            return std::string(error.what()) == "unsupported hazard entity";
        }
        return false;
    };

    if (!Expect(throwsUnsupportedHazard(
                    jaguar::Transform2d::FromRotationTranslation(0.0, {8.5, 8.5})),
                output,
                "Hole hazard must propagate the Rust unsupported-hazard panic"))
    {
        return false;
    }
    if (!Expect(throwsUnsupportedHazard(
                    jaguar::Transform2d::FromRotationTranslation(0.0, {1.5, 1.5})),
                output,
                "InferiorQualityZone hazard must propagate the Rust unsupported-hazard panic"))
    {
        return false;
    }

    sparrow::CollisionTrackerSnapshot dirtySnapshot = trackerResult.Value().Save();
    dirtySnapshot.containerEntries[0U].loss = 7.0F;
    dirtySnapshot.isSeparated = false;
    sparrow::CollisionTracker movingTracker = trackerResult.Value();
    if (!Expect(movingTracker.Restore(dirtySnapshot, layout).HasValue(),
                output,
                "unsupported moved-hazard state setup failed"))
    {
        return false;
    }
    const auto removed = layout.RemoveItem(movingHandle.Value());
    if (!Expect(removed.HasValue(), output,
                "unsupported moved-hazard removal failed"))
    {
        return false;
    }
    const auto movedHandle = layout.PlaceItem(
        movingItem, jaguar::Transform2d::FromRotationTranslation(0.0, {9.0, 9.0}));
    if (!Expect(movedHandle.HasValue(), output,
                "unsupported moved-hazard replacement failed"))
    {
        return false;
    }
    bool updateThrows = false;
    try
    {
        (void)movingTracker.UpdateMovedItem(
            layout, movingHandle.Value(), movedHandle.Value());
    }
    catch (const std::logic_error& error)
    {
        updateThrows = std::string(error.what()) == "unsupported hazard entity";
    }
    return Expect(updateThrows
                      && !movingTracker.IndexForHandle(movingHandle.Value()).has_value()
                      && movingTracker.IndexForHandle(movedHandle.Value()).has_value()
                      && movingTracker.ContainerLoss(0U) == 0.0F
                      && movingTracker.TotalLoss() == 0.0F
                      && movingTracker.IsSeparated(),
                  output,
                  "failed moved-hazard recomputation must preserve the Rust-cleared tracker row state");
}
}

bool RunQuantifyTests(std::ostream& output)
{
    return TestPairMatrix(output)
        && TestPairMatrixRustLayoutAndLossAccumulation(output)
        && TestPairMatrixBoundsBehavior(output)
        && TestPairMatrixPreservesNonFiniteLossArithmetic(output)
        && TestPairMatrixOverflowMatchesRustProfile(output)
        && TestOverlapProxy(output)
        && TestCollisionTrackerAndSeparationEvaluator(output)
        && TestCollisionTrackerMoveRecomputesOnlyOneRow(output)
        && TestExteriorHazardRawLossMatchesRust(output)
        && TestSeparationEvaluatorPreservesTinyCollisionLoss(output)
        && TestRustMultiHazardLossAndEarlyStop(output)
        && TestSeparationEvaluatorIgnoresItsCurrentHazard(output)
        && TestUnsupportedContainerHazardsMatchRustPanic(output);
}
