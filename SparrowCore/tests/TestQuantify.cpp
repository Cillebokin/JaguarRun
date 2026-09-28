#include "SparrowCore/Eval/SeparationEvaluator.h"
#include "SparrowCore/Quantify/CollisionTracker.h"
#include "SparrowCore/Quantify/OverlapProxy.h"
#include "SparrowCore/Quantify/PairMatrix.h"

#include "JaguarCore/Entities/Item.h"
#include "JaguarCore/Problems/Spp.h"

#include <cmath>
#include <limits>
#include <ostream>
#include <optional>
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

bool TestPairMatrixRejectsOverflow(std::ostream& output)
{
    const auto result = sparrow::PairMatrix::Create(
        std::numeric_limits<std::size_t>::max());
    return Expect(!result.HasValue()
                      && result.GetError().code == jaguar::ErrorCode::InvalidInput,
                  output,
                  "pair matrix must reject overflowing storage sizes");
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
        jaguar::Transform2d::Identity());
    const float separated = sparrow::CalculateOverlapProxy(
        *firstSurrogate,
        jaguar::Transform2d::Identity(),
        *secondSurrogate,
        jaguar::Transform2d::FromRotationTranslation(0.0, {20.0, 0.0}));
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
    if (!Expect(!tracker.Restore(invalidSnapshot, layout).HasValue(),
                output,
                "collision tracker must reject non-finite snapshot loss"))
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
}

bool RunQuantifyTests(std::ostream& output)
{
    return TestPairMatrix(output)
        && TestPairMatrixRejectsOverflow(output)
        && TestOverlapProxy(output)
        && TestCollisionTrackerAndSeparationEvaluator(output)
        && TestSeparationEvaluatorIgnoresItsCurrentHazard(output);
}
