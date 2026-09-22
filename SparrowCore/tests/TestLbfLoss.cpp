#include "SparrowCore/Optimizer/LbfLoss.h"
#include "SparrowCore/Optimizer/SampleEval.h"
#include "SparrowCore/Sampling/BestSamples.h"

#include "JaguarCore/Geometry/Rect.h"

#include <cmath>
#include <limits>
#include <ostream>

namespace
{
bool Expect(bool condition, std::ostream& output, const char* message)
{
    if (condition)
    {
        return true;
    }

    output << "LBF LOSS TEST FAILURE: " << message << '\n';
    return false;
}

bool TestLbfLossValues(std::ostream& output)
{
    const jaguar::Rect2d bbox{1.25, 2.5, 8.75, 6.5};
    const sparrow::LbfLoss loss = sparrow::LbfLoss::FromBoundingBox(bbox);
    if (!Expect(loss.xMax == static_cast<float>(8.75)
                    && loss.yMax == static_cast<float>(6.5),
                output,
                "LBF loss must preserve float bbox maxima"))
    {
        return false;
    }
    if (!Expect(loss.Cost() == 94.0F,
                output,
                "LBF loss cost must be 10*x_max+y_max"))
    {
        return false;
    }

    const jaguar::Rect2d wideSample{0.0, 0.0, 20.0, 10.0};
    const jaguar::Rect2d tightened = loss.TightenSampleBBox(wideSample);
    const double expectedTightenedXMax = static_cast<double>(static_cast<float>(9.4));
    if (!Expect(tightened.xMin == 0.0 && tightened.yMin == 0.0
                    && tightened.xMax == expectedTightenedXMax
                    && tightened.yMax == 10.0,
                output,
                "LBF loss must tighten sample x_max using the weighted cost"))
    {
        return false;
    }

    const jaguar::Rect2d alreadyTight{0.0, 0.0, 5.0, 10.0};
    const jaguar::Rect2d unchanged = loss.TightenSampleBBox(alreadyTight);
    if (!Expect(unchanged.xMax == 5.0 && unchanged.yMax == 10.0,
                output,
                "LBF loss must not widen an existing sample bbox"))
    {
        return false;
    }

    const sparrow::LbfLoss floatLoss =
        sparrow::LbfLoss::FromBoundingBox({0.0, 0.0, 0.123456789, 0.987654321});
    if (!Expect(floatLoss.xMax == static_cast<float>(0.123456789)
                    && floatLoss.yMax == static_cast<float>(0.987654321),
                output,
                "LBF loss must use float decision precision"))
    {
        return false;
    }

    const sparrow::LbfLoss equalLeft{1.0F, 2.0F};
    const sparrow::LbfLoss equalRight{0.0F, 12.0F};
    if (!Expect(!(equalLeft < equalRight) && !(equalRight < equalLeft),
                output,
                "equal LBF costs must compare equal"))
    {
        return false;
    }

    const sparrow::LbfLoss nanLoss{
        std::numeric_limits<float>::quiet_NaN(), 1.0F};
    const sparrow::LbfLoss finiteLoss{1.0F, 1.0F};
    if (!Expect(!(nanLoss < finiteLoss) && finiteLoss < nanLoss,
                output,
                "NaN LBF loss must never be preferred over finite loss"))
    {
        return false;
    }
    return Expect(std::isinf(nanLoss.Cost()),
                  output,
                  "NaN LBF loss cost must be treated as non-finite");
}

bool TestSampleEvalOrdering(std::ostream& output)
{
    const sparrow::SampleEval clearLow{sparrow::SampleKind::SAMPLE_KIND_CLEAR, 1.0F};
    const sparrow::SampleEval clearHigh{sparrow::SampleKind::SAMPLE_KIND_CLEAR, 2.0F};
    const sparrow::SampleEval collisionLow{sparrow::SampleKind::SAMPLE_KIND_COLLISION, 0.0F};
    const sparrow::SampleEval collisionHigh{sparrow::SampleKind::SAMPLE_KIND_COLLISION, 3.0F};
    const sparrow::SampleEval invalid{sparrow::SampleKind::SAMPLE_KIND_INVALID, 0.0F};

    if (!Expect(clearLow < clearHigh && collisionLow < collisionHigh,
                output,
                "same-kind SampleEval values must sort by loss"))
    {
        return false;
    }
    if (!Expect(clearLow < collisionLow && collisionLow < invalid,
                output,
                "SampleEval order must be Clear < Collision < Invalid"))
    {
        return false;
    }
    if (!Expect(sparrow::IsBetterSample(clearLow, collisionHigh)
                    && !sparrow::IsBetterSample(collisionHigh, clearLow),
                output,
                "betterSample must use the three-state ordering"))
    {
        return false;
    }

    const sparrow::SampleEval nanEval{
        sparrow::SampleKind::SAMPLE_KIND_CLEAR,
        std::numeric_limits<float>::quiet_NaN()};
    if (!Expect(!sparrow::IsBetterSample(nanEval, clearLow)
                    && sparrow::IsBetterSample(clearLow, nanEval),
                output,
                "NaN SampleEval must be rejected by ordering"))
    {
        return false;
    }
    return Expect(nanEval < invalid,
                  output,
                  "A non-invalid NaN sample must remain better than Invalid");
}

bool TestBestSamples(std::ostream& output)
{
    sparrow::BestSamples empty(0U, 0.5F);
    if (!Expect(!empty.Report(jaguar::Transform2d::Identity(),
                             {sparrow::SampleKind::SAMPLE_KIND_CLEAR, 1.0F})
                    && empty.Size() == 0U
                    && !empty.Best().has_value()
                    && empty.UpperBound().kind == sparrow::SampleKind::SAMPLE_KIND_INVALID,
                output,
                "zero-capacity BestSamples must remain empty"))
    {
        return false;
    }

    sparrow::BestSamples samples(2U, 0.5F);
    const auto firstTransform =
        jaguar::Transform2d::FromRotationTranslation(0.0, {0.0, 0.0});
    const auto secondTransform =
        jaguar::Transform2d::FromRotationTranslation(0.0, {10.0, 0.0});
    const auto thirdTransform =
        jaguar::Transform2d::FromRotationTranslation(0.0, {20.0, 0.0});
    if (!Expect(samples.Report(firstTransform, {sparrow::SampleKind::SAMPLE_KIND_CLEAR, 5.0F})
                    && samples.Report(secondTransform,
                                      {sparrow::SampleKind::SAMPLE_KIND_CLEAR, 3.0F})
                    && samples.Size() == 2U,
                output,
                "BestSamples must accept samples until capacity"))
    {
        return false;
    }
    if (!Expect(samples.UpperBound().loss == 5.0F,
                output,
                "BestSamples upper bound must be the current worst evaluation"))
    {
        return false;
    }
    if (!Expect(samples.Report(thirdTransform, {sparrow::SampleKind::SAMPLE_KIND_CLEAR, 4.0F})
                    && samples.Size() == 2U
                    && samples.UpperBound().loss == 4.0F,
                output,
                "A sample below the full-set upper bound must be accepted"))
    {
        return false;
    }
    if (!Expect(samples.Report(thirdTransform, {sparrow::SampleKind::SAMPLE_KIND_CLEAR, 2.0F})
                    && samples.Size() == 2U,
                output,
                "A better sample must evict the worst sample"))
    {
        return false;
    }
    if (!Expect(samples.Best().has_value()
                    && samples.Best()->second.loss == 2.0F
                    && samples.UpperBound().loss == 3.0F,
                output,
                "BestSamples must stay sorted and expose best/upper bound"))
    {
        return false;
    }

    const auto similarTransform =
        jaguar::Transform2d::FromRotationTranslation(0.01,
                                                     {20.1, 0.1});
    if (!Expect(!samples.Report(similarTransform,
                                {sparrow::SampleKind::SAMPLE_KIND_CLEAR, 2.5F}),
                output,
                "A similar sample with a worse loss must be rejected"))
    {
        return false;
    }
    if (!Expect(samples.Report(similarTransform,
                               {sparrow::SampleKind::SAMPLE_KIND_CLEAR, 1.5F})
                    && samples.Size() == 2U,
                output,
                "A better similar sample must replace the old sample"))
    {
        return false;
    }

    const sparrow::SampleEval nanEval{
        sparrow::SampleKind::SAMPLE_KIND_CLEAR,
        std::numeric_limits<float>::quiet_NaN()};
    if (!Expect(!samples.Report(
                    jaguar::Transform2d::FromRotationTranslation(0.0, {100.0, 100.0}),
                    nanEval),
                output,
                "BestSamples must reject NaN losses"))
    {
        return false;
    }
    return Expect(!samples.Report(
                       jaguar::Transform2d::FromRotationTranslation(0.0, {100.0, 100.0}),
                       {sparrow::SampleKind::SAMPLE_KIND_INVALID, 0.0F}),
                  output,
                  "BestSamples must reject Invalid evaluations");
}
}

bool RunLbfLossTests(std::ostream& output)
{
    return TestLbfLossValues(output)
        && TestSampleEvalOrdering(output)
        && TestBestSamples(output);
}
