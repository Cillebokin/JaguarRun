#include "SparrowCore/Eval/SeparationEvaluator.h"
#include "SparrowCore/Quantify/CollisionTracker.h"
#include "SparrowCore/Quantify/OverlapProxy.h"
#include "SparrowCore/Quantify/PairMatrix.h"

#include "JaguarCore/Entities/Item.h"
#include "JaguarCore/Problems/Spp.h"

#include <cmath>
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
    sparrow::PairMatrix matrix(3U);
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
    const auto shape = first.CollisionShape().Transformed(
        jaguar::Transform2d::FromRotationTranslation(0.0, {-1.0, 0.0}));
    if (!Expect(shape.HasValue(), output, "proxy shape transform failed"))
    {
        return false;
    }
    const auto bounds = jaguar::Rect2d::Create(0.0, 0.0, 4.0, 4.0);
    return Expect(std::isfinite(overlap) && overlap > separated
                      && std::isfinite(separated)
                      && sparrow::CalculateContainerPenalty(
                             shape.Value(), bounds.Value()) > 0.0F,
                  output,
                  "overlap proxy must be finite and detect separation/container overflow");
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
    const auto firstHandle = problem.PlaceItem({0U, {0.0, {0.0, 0.0}}});
    const auto secondHandle = problem.PlaceItem({1U, {0.0, {0.0, 0.0}}});
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
    return Expect(collision.kind == sparrow::SampleKind::SAMPLE_KIND_COLLISION
                      && std::isfinite(collision.loss) && collision.loss > 0.0F
                      && clear.kind == sparrow::SampleKind::SAMPLE_KIND_CLEAR,
                  output,
                  "separation evaluator must distinguish colliding and clear moves");
}
}

bool RunQuantifyTests(std::ostream& output)
{
    return TestPairMatrix(output)
        && TestOverlapProxy(output)
        && TestCollisionTrackerAndSeparationEvaluator(output);
}
