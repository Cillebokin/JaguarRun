#include "SparrowCore/optimizer/lbf_loss.h"
#include "SparrowCore/optimizer/sample_eval.h"
#include "SparrowCore/sampling/best_samples.h"

#include "JaguarCore/geometry/rect.h"

#include <cmath>
#include <limits>
#include <ostream>

namespace
{
bool expect(bool condition, std::ostream& output, const char* message)
{
    if (condition)
    {
        return true;
    }

    output << "LBF LOSS TEST FAILURE: " << message << '\n';
    return false;
}

bool testLbfLossValues(std::ostream& output)
{
    const jaguar::Rect2d bbox{1.25, 2.5, 8.75, 6.5};
    const sparrow::LbfLoss loss = sparrow::LbfLoss::fromBoundingBox(bbox);
    if (!expect(loss.xMax == static_cast<float>(8.75)
                    && loss.yMax == static_cast<float>(6.5),
                output,
                "LBF loss must preserve float bbox maxima"))
    {
        return false;
    }
    if (!expect(loss.cost() == 94.0F,
                output,
                "LBF loss cost must be 10*x_max+y_max"))
    {
        return false;
    }

    const jaguar::Rect2d wideSample{0.0, 0.0, 20.0, 10.0};
    const jaguar::Rect2d tightened = loss.tightenSampleBBox(wideSample);
    const double expectedTightenedXMax = static_cast<double>(static_cast<float>(9.4));
    if (!expect(tightened.xMin == 0.0 && tightened.yMin == 0.0
                    && tightened.xMax == expectedTightenedXMax
                    && tightened.yMax == 10.0,
                output,
                "LBF loss must tighten sample x_max using the weighted cost"))
    {
        return false;
    }

    const jaguar::Rect2d alreadyTight{0.0, 0.0, 5.0, 10.0};
    const jaguar::Rect2d unchanged = loss.tightenSampleBBox(alreadyTight);
    if (!expect(unchanged.xMax == 5.0 && unchanged.yMax == 10.0,
                output,
                "LBF loss must not widen an existing sample bbox"))
    {
        return false;
    }

    const sparrow::LbfLoss floatLoss =
        sparrow::LbfLoss::fromBoundingBox({0.0, 0.0, 0.123456789, 0.987654321});
    if (!expect(floatLoss.xMax == static_cast<float>(0.123456789)
                    && floatLoss.yMax == static_cast<float>(0.987654321),
                output,
                "LBF loss must use float decision precision"))
    {
        return false;
    }

    const sparrow::LbfLoss equalLeft{1.0F, 2.0F};
    const sparrow::LbfLoss equalRight{0.0F, 12.0F};
    if (!expect(!(equalLeft < equalRight) && !(equalRight < equalLeft),
                output,
                "equal LBF costs must compare equal"))
    {
        return false;
    }

    const sparrow::LbfLoss nanLoss{
        std::numeric_limits<float>::quiet_NaN(), 1.0F};
    const sparrow::LbfLoss finiteLoss{1.0F, 1.0F};
    if (!expect(!(nanLoss < finiteLoss) && finiteLoss < nanLoss,
                output,
                "NaN LBF loss must never be preferred over finite loss"))
    {
        return false;
    }
    return expect(std::isinf(nanLoss.cost()),
                  output,
                  "NaN LBF loss cost must be treated as non-finite");
}

bool testSampleEvalOrdering(std::ostream& output)
{
    const sparrow::SampleEval clearLow{sparrow::SampleKind::Clear, 1.0F};
    const sparrow::SampleEval clearHigh{sparrow::SampleKind::Clear, 2.0F};
    const sparrow::SampleEval collisionLow{sparrow::SampleKind::Collision, 0.0F};
    const sparrow::SampleEval collisionHigh{sparrow::SampleKind::Collision, 3.0F};
    const sparrow::SampleEval invalid{sparrow::SampleKind::Invalid, 0.0F};

    if (!expect(clearLow < clearHigh && collisionLow < collisionHigh,
                output,
                "same-kind SampleEval values must sort by loss"))
    {
        return false;
    }
    if (!expect(clearLow < collisionLow && collisionLow < invalid,
                output,
                "SampleEval order must be Clear < Collision < Invalid"))
    {
        return false;
    }
    if (!expect(sparrow::betterSample(clearLow, collisionHigh)
                    && !sparrow::betterSample(collisionHigh, clearLow),
                output,
                "betterSample must use the three-state ordering"))
    {
        return false;
    }

    const sparrow::SampleEval nanEval{
        sparrow::SampleKind::Clear,
        std::numeric_limits<float>::quiet_NaN()};
    if (!expect(!sparrow::betterSample(nanEval, clearLow)
                    && sparrow::betterSample(clearLow, nanEval),
                output,
                "NaN SampleEval must be rejected by ordering"))
    {
        return false;
    }
    return expect(nanEval < invalid,
                  output,
                  "A non-invalid NaN sample must remain better than Invalid");
}

bool testBestSamples(std::ostream& output)
{
    sparrow::BestSamples empty(0U, 0.5F);
    if (!expect(!empty.report(jaguar::Transform2d::identity(),
                             {sparrow::SampleKind::Clear, 1.0F})
                    && empty.size() == 0U
                    && !empty.best().has_value()
                    && empty.upperBound().kind == sparrow::SampleKind::Invalid,
                output,
                "zero-capacity BestSamples must remain empty"))
    {
        return false;
    }

    sparrow::BestSamples samples(2U, 0.5F);
    const auto firstTransform =
        jaguar::Transform2d::fromRotationTranslation(0.0, {0.0, 0.0});
    const auto secondTransform =
        jaguar::Transform2d::fromRotationTranslation(0.0, {10.0, 0.0});
    const auto thirdTransform =
        jaguar::Transform2d::fromRotationTranslation(0.0, {20.0, 0.0});
    if (!expect(samples.report(firstTransform, {sparrow::SampleKind::Clear, 5.0F})
                    && samples.report(secondTransform,
                                      {sparrow::SampleKind::Clear, 3.0F})
                    && samples.size() == 2U,
                output,
                "BestSamples must accept samples until capacity"))
    {
        return false;
    }
    if (!expect(samples.upperBound().loss == 5.0F,
                output,
                "BestSamples upper bound must be the current worst evaluation"))
    {
        return false;
    }
    if (!expect(samples.report(thirdTransform, {sparrow::SampleKind::Clear, 4.0F})
                    && samples.size() == 2U
                    && samples.upperBound().loss == 4.0F,
                output,
                "A sample below the full-set upper bound must be accepted"))
    {
        return false;
    }
    if (!expect(samples.report(thirdTransform, {sparrow::SampleKind::Clear, 2.0F})
                    && samples.size() == 2U,
                output,
                "A better sample must evict the worst sample"))
    {
        return false;
    }
    if (!expect(samples.best().has_value()
                    && samples.best()->second.loss == 2.0F
                    && samples.upperBound().loss == 3.0F,
                output,
                "BestSamples must stay sorted and expose best/upper bound"))
    {
        return false;
    }

    const auto similarTransform =
        jaguar::Transform2d::fromRotationTranslation(0.01,
                                                     {20.1, 0.1});
    if (!expect(!samples.report(similarTransform,
                                {sparrow::SampleKind::Clear, 2.5F}),
                output,
                "A similar sample with a worse loss must be rejected"))
    {
        return false;
    }
    if (!expect(samples.report(similarTransform,
                               {sparrow::SampleKind::Clear, 1.5F})
                    && samples.size() == 2U,
                output,
                "A better similar sample must replace the old sample"))
    {
        return false;
    }

    const sparrow::SampleEval nanEval{
        sparrow::SampleKind::Clear,
        std::numeric_limits<float>::quiet_NaN()};
    if (!expect(!samples.report(
                    jaguar::Transform2d::fromRotationTranslation(0.0, {100.0, 100.0}),
                    nanEval),
                output,
                "BestSamples must reject NaN losses"))
    {
        return false;
    }
    return expect(!samples.report(
                       jaguar::Transform2d::fromRotationTranslation(0.0, {100.0, 100.0}),
                       {sparrow::SampleKind::Invalid, 0.0F}),
                  output,
                  "BestSamples must reject Invalid evaluations");
}
}

bool runLbfLossTests(std::ostream& output)
{
    return testLbfLossValues(output)
        && testSampleEvalOrdering(output)
        && testBestSamples(output);
}
