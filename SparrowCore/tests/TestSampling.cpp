#include "SparrowCore/Random/SparrowRng.h"
#include "SparrowCore/Eval/LbfEvaluator.h"
#include "SparrowCore/Optimizer/LbfSearch.h"
#include "SparrowCore/Optimizer/SampleEval.h"
#include "SparrowCore/Sampling/CoordinateDescent.h"
#include "SparrowCore/Sampling/LocalSearchSampler.h"
#include "SparrowCore/Sampling/RotationDistribution.h"
#include "SparrowCore/Sampling/UniformRectSampler.h"

#include "JaguarCore/Entities/Item.h"
#include "JaguarCore/Entities/Container.h"
#include "JaguarCore/Entities/Layout.h"
#include "JaguarCore/Collision/CollisionEngine.h"
#include "JaguarCore/Collision/HazardFilter.h"
#include "JaguarCore/FailFast/Surrogate.h"
#include "JaguarCore/Geometry/OriginalShape.h"
#include "JaguarCore/Geometry/ShapeModification.h"
#include "JaguarCore/Geometry/SimplePolygon.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <ostream>
#include <sstream>
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

    output << "SAMPLING TEST FAILURE: " << message << '\n';
    return false;
}

jaguar::Result<jaguar::Item> MakeItem(jaguar::RotationRange rotation)
{
    const auto polygon = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {2.0, 0.0}, {2.0, 1.0}, {0.0, 1.0}});
    if (!polygon.HasValue())
    {
        return jaguar::Result<jaguar::Item>::FromError(polygon.GetError());
    }

    jaguar::OriginalShape original{
        polygon.Value(),
        jaguar::Transform2d::Identity(),
        jaguar::ShapeModifyMode::Inflate,
        {}};
    jaguar::SurrogateConfig surrogateConfig{};
    surrogateConfig.poleLimits = {{{1, 0.0}, {0, 0.0}, {0, 0.0}}};
    return jaguar::Item::Create(0,
                                1001,
                                std::move(original),
                                surrogateConfig,
                                std::nullopt,
                                std::move(rotation));
}

bool IsOneOf(float value, const std::vector<float>& values) noexcept
{
    for (const float expected : values)
    {
        if (value == expected)
        {
            return true;
        }
    }
    return false;
}

bool TestRotationModes(std::ostream& output)
{
    const auto noneItem = MakeItem({jaguar::RotationMode::None, {}});
    if (!Expect(noneItem.HasValue(), output, "None rotation item creation failed"))
    {
        return false;
    }
    const auto noneDistribution =
        sparrow::RotationDistribution::FromItem(noneItem.Value());
    if (!Expect(noneDistribution.HasValue(), output, "None rotation distribution failed"))
    {
        return false;
    }
    sparrow::SparrowRng noneRng(42U);
    for (std::size_t index = 0; index < 16U; ++index)
    {
        if (!Expect(noneDistribution.Value().Sample(noneRng) == 0.0F,
                    output,
                    "None rotation must always sample zero"))
        {
            return false;
        }
    }

    const double pi = std::acos(-1.0);
    const auto discreteItem = MakeItem({
        jaguar::RotationMode::Discrete,
        {0.0, 0.5 * pi, -0.25 * pi}});
    if (!Expect(discreteItem.HasValue(),
                output,
                "Discrete rotation item creation failed"))
    {
        return false;
    }
    const auto discreteDistribution =
        sparrow::RotationDistribution::FromItem(discreteItem.Value());
    if (!Expect(discreteDistribution.HasValue(),
                output,
                "Discrete rotation distribution failed"))
    {
        return false;
    }
    const std::vector<float> discreteAngles = {
        0.0F,
        static_cast<float>(0.5 * pi),
        static_cast<float>(-0.25 * pi),
    };
    sparrow::SparrowRng discreteRng(42U);
    for (std::size_t index = 0; index < 128U; ++index)
    {
        const float sample = discreteDistribution.Value().Sample(discreteRng);
        if (!Expect(IsOneOf(sample, discreteAngles),
                    output,
                    "Discrete rotation returned a forbidden angle"))
        {
            return false;
        }
    }

    const auto continuousItem = MakeItem({jaguar::RotationMode::Continuous, {}});
    if (!Expect(continuousItem.HasValue(),
                output,
                "Continuous rotation item creation failed"))
    {
        return false;
    }
    const auto continuousDistribution =
        sparrow::RotationDistribution::FromItem(continuousItem.Value());
    if (!Expect(continuousDistribution.HasValue(),
                output,
                "Continuous rotation distribution failed"))
    {
        return false;
    }
    sparrow::SparrowRng continuousFirst(42U);
    sparrow::SparrowRng continuousSecond(42U);
    for (std::size_t index = 0; index < 128U; ++index)
    {
        const float first = continuousDistribution.Value().Sample(continuousFirst);
        const float second = continuousDistribution.Value().Sample(continuousSecond);
        if (!Expect(std::isfinite(first) && first >= 0.0F
                        && first < static_cast<float>(2.0 * pi),
                    output,
                    "Continuous rotation sample is outside [0, 2pi)"))
        {
            return false;
        }
        if (!Expect(first == second,
                    output,
                    "Continuous rotation sequence is not seed deterministic"))
        {
            return false;
        }
    }

    const auto emptyDiscreteItem = MakeItem({jaguar::RotationMode::Discrete, {}});
    if (!Expect(!emptyDiscreteItem.HasValue()
                    && emptyDiscreteItem.GetError().code == jaguar::ErrorCode::InvalidInput,
                output,
                "Empty discrete rotation must be rejected by JaguarCore"))
    {
        return false;
    }

    const auto nonFiniteItem = MakeItem({
        jaguar::RotationMode::Discrete,
        {std::numeric_limits<double>::quiet_NaN()}});
    return Expect(!nonFiniteItem.HasValue()
                      && nonFiniteItem.GetError().code == jaguar::ErrorCode::InvalidInput,
                output,
                "Non-finite discrete rotation must be rejected by JaguarCore");
}

bool TestUniformRectSampler(std::ostream& output)
{
    const auto item = MakeItem({jaguar::RotationMode::None, {}});
    if (!Expect(item.HasValue(), output, "Uniform sampler item creation failed"))
    {
        return false;
    }

    const jaguar::Rect2d sampleBounds{1.0, 2.0, 5.0, 7.0};
    const auto sampler = sparrow::UniformRectSampler::Create(sampleBounds, item.Value());
    if (!Expect(sampler.HasValue(), output, "Uniform sampler creation failed"))
    {
        return false;
    }
    if (!Expect(sampler.Value().BoundingBox().xMin == sampleBounds.xMin
                    && sampler.Value().BoundingBox().yMin == sampleBounds.yMin
                    && sampler.Value().BoundingBox().xMax == sampleBounds.xMax
                    && sampler.Value().BoundingBox().yMax == sampleBounds.yMax,
                output,
                "Uniform sampler bbox was not preserved"))
    {
        return false;
    }

    const auto invalidDegenerate =
        sparrow::UniformRectSampler::Create({1.0, 2.0, 1.0, 7.0}, item.Value());
    if (!Expect(!invalidDegenerate.HasValue()
                    && invalidDegenerate.GetError().code == jaguar::ErrorCode::InvalidInput,
                output,
                "Degenerate uniform sampler bbox must be invalid"))
    {
        return false;
    }
    const auto invalidNonFinite = sparrow::UniformRectSampler::Create(
        {0.0, 0.0, std::numeric_limits<double>::infinity(), 1.0}, item.Value());
    if (!Expect(!invalidNonFinite.HasValue()
                    && invalidNonFinite.GetError().code == jaguar::ErrorCode::InvalidInput,
                output,
                "Non-finite uniform sampler bbox must be invalid"))
    {
        return false;
    }

    sparrow::SparrowRng firstRng(42U);
    sparrow::SparrowRng secondRng(42U);
    for (std::size_t index = 0; index < 64U; ++index)
    {
        const jaguar::Transform2d first = sampler.Value().Sample(firstRng);
        const jaguar::Transform2d second = sampler.Value().Sample(secondRng);
        if (!Expect(first.rotationRad == 0.0
                        && first.translation.x >= sampleBounds.xMin
                        && first.translation.x < sampleBounds.xMax
                        && first.translation.y >= sampleBounds.yMin
                        && first.translation.y < sampleBounds.yMax,
                    output,
                    "Uniform sampler produced an out-of-bounds candidate"))
        {
            return false;
        }
        if (!Expect(first.rotationRad == second.rotationRad
                        && first.translation == second.translation,
                    output,
                    "Uniform sampler sequence is not seed deterministic"))
        {
            return false;
        }

        const auto transformedResult = item.Value().CollisionShape().Transformed(first);
        if (!Expect(transformedResult.HasValue(),
                    output,
                    "Transformed sampled shape must be valid"))
        {
            return false;
        }
        const jaguar::SimplePolygon transformed = transformedResult.Value();
        const jaguar::Rect2d transformedBounds = transformed.BoundingBox();
        if (!Expect(std::isfinite(transformedBounds.xMin)
                        && std::isfinite(transformedBounds.yMin)
                        && std::isfinite(transformedBounds.xMax)
                        && std::isfinite(transformedBounds.yMax)
                        && std::abs(transformedBounds.xMin
                                    - first.translation.x)
                               < 1.0e-6
                        && std::abs(transformedBounds.yMin
                                    - first.translation.y)
                               < 1.0e-6
                        && std::abs(transformedBounds.Width() - 2.0) < 1.0e-6
                        && std::abs(transformedBounds.Height() - 1.0) < 1.0e-6,
                    output,
                    "Transformed shape bbox does not follow sampled translation"))
        {
            return false;
        }
    }

    const auto discreteItem = MakeItem({
        jaguar::RotationMode::Discrete,
        {0.0, 0.5 * std::acos(-1.0)}});
    if (!Expect(discreteItem.HasValue(),
                output,
                "Discrete uniform sampler item creation failed"))
    {
        return false;
    }
    const auto discreteSampler =
        sparrow::UniformRectSampler::Create(sampleBounds, discreteItem.Value());
    if (!Expect(discreteSampler.HasValue(),
                output,
                "Discrete uniform sampler creation failed"))
    {
        return false;
    }
    const double discretePiHalf = static_cast<double>(static_cast<float>(0.5 * std::acos(-1.0)));
    sparrow::SparrowRng discreteRng(42U);
    for (std::size_t index = 0; index < 64U; ++index)
    {
        const double rotation = discreteSampler.Value().Sample(discreteRng).rotationRad;
        if (!Expect(rotation == 0.0 || rotation == discretePiHalf,
                    output,
                    "Uniform sampler returned a forbidden discrete rotation"))
        {
            return false;
        }
    }

    return true;
}

bool SameTransform(const jaguar::Transform2d& left,
                   const jaguar::Transform2d& right) noexcept
{
    return left.rotationRad == right.rotationRad
        && left.translation == right.translation;
}

bool IsFiniteTransform(const jaguar::Transform2d& transform) noexcept
{
    return std::isfinite(transform.rotationRad)
        && std::isfinite(transform.translation.x)
        && std::isfinite(transform.translation.y);
}

bool TestLocalSearchSampler(std::ostream& output)
{
    const double pi = std::acos(-1.0);
    const auto continuousItem = MakeItem({jaguar::RotationMode::Continuous, {}});
    if (!Expect(continuousItem.HasValue(),
                output,
                "Local search continuous item creation failed"))
    {
        return false;
    }

    const jaguar::Transform2d reference =
        jaguar::Transform2d::FromRotationTranslation(0.25, {10.0, 20.0});
    const jaguar::Rect2d containerBounds{0.0, 0.0, 100.0, 50.0};
    sparrow::LbfConfig config;
    const auto created = sparrow::LocalSearchSampler::Create(
        continuousItem.Value(), reference, config, containerBounds);
    if (!Expect(created.HasValue(), output, "Local search sampler creation failed"))
    {
        return false;
    }

    sparrow::LocalSearchSampler first = created.Value();
    sparrow::LocalSearchSampler second = created.Value();
    sparrow::SparrowRng firstRng(42U);
    sparrow::SparrowRng secondRng(42U);
    for (std::size_t index = 0; index < 32U; ++index)
    {
        const jaguar::Transform2d firstSample = first.Sample(firstRng);
        const jaguar::Transform2d secondSample = second.Sample(secondRng);
        if (!Expect(IsFiniteTransform(firstSample),
                    output,
                    "Local search sample must be finite"))
        {
            return false;
        }
        if (!Expect(SameTransform(firstSample, secondSample),
                    output,
                    "Local search sequence must be seed deterministic"))
        {
            return false;
        }
    }

    auto decayZero = sparrow::LocalSearchSampler::Create(
        continuousItem.Value(), reference, config, containerBounds);
    auto fresh = sparrow::LocalSearchSampler::Create(
        continuousItem.Value(), reference, config, containerBounds);
    if (!Expect(decayZero.HasValue() && fresh.HasValue(),
                output,
                "Local search decay comparison setup failed"))
    {
        return false;
    }
    decayZero.Value().DecayStddev(0.0F);
    sparrow::SparrowRng decayZeroRng(7U);
    sparrow::SparrowRng freshRng(7U);
    for (std::size_t index = 0; index < 8U; ++index)
    {
        if (!Expect(SameTransform(decayZero.Value().Sample(decayZeroRng),
                                  fresh.Value().Sample(freshRng)),
                    output,
                    "progress=0 must preserve the initial standard deviations"))
        {
            return false;
        }
    }

    auto shifted = sparrow::LocalSearchSampler::Create(
        continuousItem.Value(), reference, config, containerBounds);
    const jaguar::Transform2d shiftedReference =
        jaguar::Transform2d::FromRotationTranslation(-0.5, {30.0, 40.0});
    auto directShift = sparrow::LocalSearchSampler::Create(
        continuousItem.Value(), shiftedReference, config, containerBounds);
    if (!Expect(shifted.HasValue() && directShift.HasValue(),
                output,
                "Local search shift comparison setup failed"))
    {
        return false;
    }
    shifted.Value().ShiftMean(shiftedReference);
    shifted.Value().DecayStddev(1.0F);
    directShift.Value().DecayStddev(1.0F);
    sparrow::SparrowRng shiftedRng(9U);
    sparrow::SparrowRng directShiftRng(9U);
    for (std::size_t index = 0; index < 8U; ++index)
    {
        const auto shiftedSample = shifted.Value().Sample(shiftedRng);
        const auto directSample = directShift.Value().Sample(directShiftRng);
        if (!Expect(SameTransform(shiftedSample, directSample),
                    output,
                    "shiftMean must update the local search distribution mean"))
        {
            return false;
        }
        if (!Expect(IsFiniteTransform(shiftedSample),
                    output,
                    "progress=1 local search sample must be finite"))
        {
            return false;
        }
    }

    const auto discreteItem = MakeItem({
        jaguar::RotationMode::Discrete,
        {0.0, 0.5 * pi}});
    if (!Expect(discreteItem.HasValue(),
                output,
                "Local search discrete item creation failed"))
    {
        return false;
    }
    const double discreteReference =
        static_cast<double>(static_cast<float>(0.5 * pi));
    auto discreteSampler = sparrow::LocalSearchSampler::Create(
        discreteItem.Value(),
        jaguar::Transform2d::FromRotationTranslation(discreteReference, {1.0, 2.0}),
        config,
        containerBounds);
    if (!Expect(discreteSampler.HasValue(),
                output,
                "Local search discrete sampler creation failed"))
    {
        return false;
    }
    sparrow::SparrowRng discreteRng(42U);
    for (std::size_t index = 0; index < 16U; ++index)
    {
        if (!Expect(discreteSampler.Value().Sample(discreteRng).rotationRad
                        == discreteReference,
                    output,
                    "Discrete local search must not wiggle rotation"))
        {
            return false;
        }
    }

    const auto noneItem = MakeItem({jaguar::RotationMode::None, {}});
    if (!Expect(noneItem.HasValue(), output, "Local search None item creation failed"))
    {
        return false;
    }
    auto noneSampler = sparrow::LocalSearchSampler::Create(
        noneItem.Value(), reference, config, containerBounds);
    if (!Expect(noneSampler.HasValue(),
                output,
                "Local search None sampler creation failed"))
    {
        return false;
    }
    if (!Expect(noneSampler.Value().Sample(discreteRng).rotationRad == 0.0,
                output,
                "None local search must keep zero rotation"))
    {
        return false;
    }

    const auto invalidBounds = sparrow::LocalSearchSampler::Create(
        continuousItem.Value(), reference, config, {0.0, 0.0, 0.0, 1.0});
    if (!Expect(!invalidBounds.HasValue()
                    && invalidBounds.GetError().code == jaguar::ErrorCode::InvalidInput,
                output,
                "Degenerate local search bounds must be invalid"))
    {
        return false;
    }
    sparrow::LbfConfig invalidConfig = config;
    invalidConfig.samplesPerItem = 0U;
    const auto invalidConfigResult = sparrow::LocalSearchSampler::Create(
        continuousItem.Value(), reference, invalidConfig, containerBounds);
    return Expect(!invalidConfigResult.HasValue()
                      && invalidConfigResult.GetError().code
                             == jaguar::ErrorCode::InvalidInput,
                  output,
                  "Invalid LBF config must reject local search sampler creation");
}

class CoordinateTestEvaluator final : public sparrow::SampleEvaluator
{
public:
    enum class Mode
    {
        Objective,
        Equal,
        Worse,
    };

    explicit CoordinateTestEvaluator(Mode mode) : m_Mode(mode) {}

    sparrow::SampleEval Evaluate(
        const jaguar::Transform2d& transform,
        const std::optional<sparrow::SampleEval>&) override
    {
        m_Seen.push_back(transform);
        ++m_EvaluationCount;
        if (m_Mode == Mode::Equal)
        {
            return {sparrow::SampleKind::SAMPLE_KIND_CLEAR, 0.0F};
        }
        if (m_Mode == Mode::Worse)
        {
            return {sparrow::SampleKind::SAMPLE_KIND_CLEAR, 1.0F};
        }

        const float loss = static_cast<float>(
            std::abs(transform.translation.x)
            + std::abs(transform.translation.y)
            + std::abs(transform.rotationRad));
        return {sparrow::SampleKind::SAMPLE_KIND_CLEAR, loss};
    }

    std::size_t EvaluationCount() const noexcept override
    {
        return m_EvaluationCount;
    }

    const std::vector<jaguar::Transform2d>& Seen() const noexcept
    {
        return m_Seen;
    }

private:
    Mode m_Mode;
    std::size_t m_EvaluationCount = 0;
    std::vector<jaguar::Transform2d> m_Seen;
};

bool TestCoordinateDescentAxesAndBudget(std::ostream& output)
{
    struct AxisCase
    {
        std::uint64_t seed;
        int expectedAxis;
    };
    const std::array<AxisCase, 4> cases = {{{10U, 0}, {3U, 1}, {2U, 2}, {0U, 3}}};
    const jaguar::Transform2d initialTransform =
        jaguar::Transform2d::FromRotationTranslation(0.0, {10.0, 20.0});
    const sparrow::SampleEval initialEval{sparrow::SampleKind::SAMPLE_KIND_CLEAR, 0.0F};

    sparrow::CoordinateDescentConfig config;
    config.translationStepInit = 1.0F;
    config.translationStepLimit = 0.25F;
    config.rotationStepInit = 0.0F;
    config.rotationStepLimit = 0.0F;
    config.rotationWiggle = false;
    config.maxEvaluations = 2U;

    for (const AxisCase& axisCase : cases)
    {
        CoordinateTestEvaluator evaluator(CoordinateTestEvaluator::Mode::Worse);
        sparrow::SparrowRng rng(axisCase.seed);
        const auto result = sparrow::RefineCoordinateDescent(
            {initialTransform, initialEval}, evaluator, config, rng);
        if (!Expect(!result.HasValue()
                        && result.GetError().code == jaguar::ErrorCode::TimeLimit,
                    output,
                    "Coordinate descent must stop at the evaluation budget"))
        {
            return false;
        }
        if (!Expect(evaluator.Seen().size() == 2U,
                    output,
                    "Coordinate descent must evaluate both axis candidates"))
        {
            return false;
        }

        const auto& first = evaluator.Seen()[0];
        const auto& second = evaluator.Seen()[1];
        bool axisMatches = false;
        switch (axisCase.expectedAxis)
        {
        case 0:
            axisMatches = first.translation.x == 11.0
                && second.translation.x == 9.0
                && first.translation.y == 20.0
                && second.translation.y == 20.0;
            break;
        case 1:
            axisMatches = first.translation.x == 10.0
                && second.translation.x == 10.0
                && first.translation.y == 21.0
                && second.translation.y == 19.0;
            break;
        case 2:
            axisMatches = first.translation.x == 11.0
                && first.translation.y == 21.0
                && second.translation.x == 9.0
                && second.translation.y == 19.0;
            break;
        case 3:
            axisMatches = first.translation.x == 9.0
                && first.translation.y == 21.0
                && second.translation.x == 11.0
                && second.translation.y == 19.0;
            break;
        }
        if (!Expect(axisMatches,
                    output,
                    "Coordinate descent generated the wrong translation axis"))
        {
            return false;
        }
    }

    sparrow::CoordinateDescentConfig wiggleConfig;
    wiggleConfig.translationStepInit = 0.0F;
    wiggleConfig.translationStepLimit = 0.0F;
    wiggleConfig.rotationStepInit = 1.0F;
    wiggleConfig.rotationStepLimit = 0.25F;
    wiggleConfig.rotationWiggle = true;
    wiggleConfig.maxEvaluations = 2U;
    const std::array<AxisCase, 2> wiggleCases = {{{2U, 4}, {0U, 5}}};
    for (const AxisCase& axisCase : wiggleCases)
    {
        CoordinateTestEvaluator evaluator(CoordinateTestEvaluator::Mode::Worse);
        sparrow::SparrowRng rng(axisCase.seed);
        const auto result = sparrow::RefineCoordinateDescent(
            {initialTransform, initialEval}, evaluator, wiggleConfig, rng);
        if (!Expect(!result.HasValue()
                        && result.GetError().code == jaguar::ErrorCode::TimeLimit,
                    output,
                    "Rotation wiggle must honor the evaluation budget"))
        {
            return false;
        }
        const auto& first = evaluator.Seen()[0];
        const auto& second = evaluator.Seen()[1];
        const bool expectedOrder = first.rotationRad == 1.0
            && second.rotationRad == -1.0;
        if (!Expect(expectedOrder
                        && first.translation == initialTransform.translation
                        && second.translation == initialTransform.translation,
                    output,
                    "Coordinate descent did not generate the rotation wiggle axis"))
        {
            return false;
        }
    }
    return true;
}

bool TestCoordinateDescentOutcomes(std::ostream& output)
{
    const jaguar::Transform2d initialTransform =
        jaguar::Transform2d::FromRotationTranslation(0.5, {2.0, -1.0});
    const sparrow::SampleEval initialEval{sparrow::SampleKind::SAMPLE_KIND_CLEAR, 3.5F};

    sparrow::CoordinateDescentConfig objectiveConfig;
    objectiveConfig.translationStepInit = 0.5F;
    objectiveConfig.translationStepLimit = 0.02F;
    objectiveConfig.rotationStepInit = 0.2F;
    objectiveConfig.rotationStepLimit = 0.02F;
    objectiveConfig.rotationWiggle = true;
    objectiveConfig.maxEvaluations = 1000U;

    CoordinateTestEvaluator objective(CoordinateTestEvaluator::Mode::Objective);
    sparrow::SparrowRng objectiveRng(42U);
    const auto objectiveResult = sparrow::RefineCoordinateDescent(
        {initialTransform, initialEval}, objective, objectiveConfig, objectiveRng);
    if (!Expect(objectiveResult.HasValue(),
                output,
                "Coordinate descent objective fixture must finish"))
    {
        return false;
    }
    if (!Expect(objectiveResult.Value().second.kind == sparrow::SampleKind::SAMPLE_KIND_CLEAR
                    && std::isfinite(objectiveResult.Value().second.loss)
                    && objectiveResult.Value().second.loss <= initialEval.loss,
                output,
                "Coordinate descent final loss must not exceed initial loss"))
    {
        return false;
    }

    sparrow::CoordinateDescentConfig outcomeConfig;
    outcomeConfig.translationStepInit = 1.0F;
    outcomeConfig.translationStepLimit = 0.5F;
    outcomeConfig.rotationStepInit = 0.0F;
    outcomeConfig.rotationStepLimit = 0.0F;
    outcomeConfig.rotationWiggle = false;
    outcomeConfig.maxEvaluations = 1000U;

    CoordinateTestEvaluator equalEvaluator(CoordinateTestEvaluator::Mode::Equal);
    sparrow::SparrowRng equalRng(42U);
    const auto equalResult = sparrow::RefineCoordinateDescent(
        {initialTransform,
         {sparrow::SampleKind::SAMPLE_KIND_CLEAR, 0.0F}},
        equalEvaluator,
        outcomeConfig,
        equalRng);
    if (!Expect(equalResult.HasValue(),
                output,
                "Equal coordinate descent fixture must finish"))
    {
        return false;
    }
    if (!Expect(equalResult.Value().second.loss == 0.0F
                    && !SameTransform(equalResult.Value().first, initialTransform),
                output,
                "Equal evaluation must update the coordinate descent position"))
    {
        return false;
    }

    CoordinateTestEvaluator worseEvaluator(CoordinateTestEvaluator::Mode::Worse);
    sparrow::SparrowRng worseRng(42U);
    const auto worseResult = sparrow::RefineCoordinateDescent(
        {initialTransform,
         {sparrow::SampleKind::SAMPLE_KIND_CLEAR, 0.0F}},
        worseEvaluator,
        outcomeConfig,
        worseRng);
    if (!Expect(worseResult.HasValue(),
                output,
                "Worse coordinate descent fixture must finish"))
    {
        return false;
    }
    return Expect(SameTransform(worseResult.Value().first, initialTransform)
                      && worseResult.Value().second.loss == 0.0F,
                  output,
                  "Worse evaluation must keep the current coordinate and loss");
}

jaguar::SimplePolygon MakePolygon(const std::vector<jaguar::Point2d>& points)
{
    const auto polygon = jaguar::SimplePolygon::Create(points);
    return polygon.HasValue() ? polygon.Value() : jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}, {0.0, 1.0}}).Value();
}

jaguar::Container MakeEvaluatorContainer()
{
    const jaguar::SimplePolygon outer = MakePolygon({
        {0.0, 0.0}, {20.0, 0.0}, {20.0, 20.0}, {0.0, 20.0}});
    const std::vector<jaguar::QualityZone> zones = {
        {0, {MakePolygon({{8.0, 8.0}, {10.0, 8.0}, {10.0, 10.0}, {8.0, 10.0}})}},
        {1, {MakePolygon({{0.0, 0.0}, {3.0, 0.0}, {3.0, 3.0}, {0.0, 3.0}})}},
        {2, {MakePolygon({{15.0, 0.0}, {18.0, 0.0}, {18.0, 3.0}, {15.0, 3.0}})}},
    };
    jaguar::CDEConfig config{};
    config.quadtreeDepth = 4;
    config.cdThreshold = 8;
    const auto container = jaguar::Container::Create(
        9100, {outer,
               jaguar::Transform2d::Identity(),
               jaguar::ShapeModifyMode::Deflate,
               {}},
        zones,
        config);
    return container.Value();
}

jaguar::Container MakePlainEvaluatorContainer()
{
    const jaguar::SimplePolygon outer = MakePolygon({
        {0.0, 0.0}, {30.0, 0.0}, {30.0, 30.0}, {0.0, 30.0}});
    jaguar::CDEConfig config{};
    config.quadtreeDepth = 4;
    config.cdThreshold = 8;
    const auto container = jaguar::Container::Create(
        9102,
        {outer,
         jaguar::Transform2d::Identity(),
         jaguar::ShapeModifyMode::Deflate,
         {}},
        {},
        config);
    return container.Value();
}

jaguar::Item MakeEvaluatorItem(
    std::vector<jaguar::Point2d> points,
    std::optional<std::size_t> minQuality = std::nullopt,
    jaguar::SurrogateConfig surrogateConfig = {})
{
    auto polygon = jaguar::SimplePolygon::Create(std::move(points));
    surrogateConfig.poleLimits = {{{4, 0.0}, {2, 0.8}, {1, 0.9}}};
    const auto item = jaguar::Item::Create(
        0,
        9101,
        {polygon.Value(),
         jaguar::Transform2d::Identity(),
         jaguar::ShapeModifyMode::Inflate,
         {}},
        surrogateConfig,
        minQuality);
    return item.Value();
}

bool TestLbfEvaluatorBasicCollisionAndLoss(std::ostream& output)
{
    const jaguar::Container container = MakeEvaluatorContainer();
    jaguar::Layout emptyLayout(container);
    const jaguar::Item item = MakeEvaluatorItem({
        {0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}, {0.0, 1.0}});
    const jaguar::NoFilter noFilter;
    sparrow::LbfEvaluator evaluator(emptyLayout, item, noFilter);

    const auto clear = evaluator.Evaluate(
        jaguar::Transform2d::FromRotationTranslation(0.0, {4.0, 4.0}),
        std::nullopt);
    if (!Expect(clear.kind == sparrow::SampleKind::SAMPLE_KIND_CLEAR
                    && clear.loss == 93.5F,
                output,
                "LBF evaluator must return Sparrow LBF loss for a clear sample"))
    {
        return false;
    }

    const auto exterior = evaluator.Evaluate(
        jaguar::Transform2d::FromRotationTranslation(0.0, {19.5, 1.0}),
        std::nullopt);
    if (!Expect(exterior.kind == sparrow::SampleKind::SAMPLE_KIND_INVALID,
                output,
                "LBF evaluator must reject a sample outside the exterior"))
    {
        return false;
    }

    const auto bounded = evaluator.Evaluate(
        jaguar::Transform2d::FromRotationTranslation(0.0, {10.0, 10.0}),
        sparrow::SampleEval{sparrow::SampleKind::SAMPLE_KIND_CLEAR, 0.0F});
    if (!Expect(bounded.kind == sparrow::SampleKind::SAMPLE_KIND_INVALID,
                output,
                "LBF evaluator must reject a candidate that cannot improve the upper bound"))
    {
        return false;
    }

    if (!Expect(evaluator.EvaluationCount() == 3U,
                output,
                "LBF evaluator must count every evaluation including upper-bound skips"))
    {
        return false;
    }

    jaguar::Layout blockedLayout(container);
    const jaguar::Item blocker = MakeEvaluatorItem({
        {0.0, 0.0}, {2.0, 0.0}, {2.0, 2.0}, {0.0, 2.0}});
    if (!Expect(blockedLayout.PlaceItem(
                         blocker,
                         jaguar::Transform2d::FromRotationTranslation(0.0, {4.0, 4.0}))
                    .HasValue(),
                output,
                "LBF evaluator blocker setup failed"))
    {
        return false;
    }
    sparrow::LbfEvaluator blockedEvaluator(blockedLayout, item, noFilter);
    const auto collision = blockedEvaluator.Evaluate(
        jaguar::Transform2d::FromRotationTranslation(0.0, {4.5, 4.5}),
        std::nullopt);
    if (!Expect(collision.kind == sparrow::SampleKind::SAMPLE_KIND_INVALID,
                  output,
                  "LBF evaluator must reject exact placed-item collisions"))
    {
        return false;
    }

    jaguar::SurrogateConfig exactOnlyConfig{};
    exactOnlyConfig.failFastPoleAreaRatio = 0.0;
    exactOnlyConfig.failFastPiers = 0;
    const jaguar::Item exactOnlyItem = MakeEvaluatorItem({
        {0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}, {0.0, 1.0}},
        std::nullopt,
        exactOnlyConfig);
    if (!Expect(exactOnlyItem.CollisionShape().GetSurrogate() != nullptr
                    && exactOnlyItem.CollisionShape().GetSurrogate()->FailFastPoleCount() == 0U,
                output,
                "exact-only evaluator item must have no fail-fast poles"))
    {
        return false;
    }
    sparrow::LbfEvaluator exactOnlyEvaluator(blockedLayout, exactOnlyItem, noFilter);
    const auto exactOnlyCollision = exactOnlyEvaluator.Evaluate(
        jaguar::Transform2d::FromRotationTranslation(0.0, {4.5, 4.5}),
        std::nullopt);
    return Expect(exactOnlyCollision.kind == sparrow::SampleKind::SAMPLE_KIND_INVALID,
                  output,
                  "exact CDE must reject a collision after surrogate miss");
}

bool TestLbfEvaluatorHoleAndQualityFilter(std::ostream& output)
{
    const jaguar::Container container = MakeEvaluatorContainer();
    const jaguar::Item item = MakeEvaluatorItem({
        {0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}, {0.0, 1.0}}, 2U);

    jaguar::HazardFilter qualityFilter{};
    qualityFilter.irrelevant = [](const jaguar::HazardEntity& entity) {
        return entity.kind == jaguar::HazardKind::InferiorQualityZone
            && entity.quality < 2U;
    };
    jaguar::Layout layout(container);
    sparrow::LbfEvaluator evaluator(layout, item, qualityFilter);

    const auto ignoredQuality = evaluator.Evaluate(
        jaguar::Transform2d::FromRotationTranslation(0.0, {1.0, 1.0}),
        std::nullopt);
    if (!Expect(ignoredQuality.kind == sparrow::SampleKind::SAMPLE_KIND_CLEAR,
                output,
                "quality below minQuality must be ignored by the supplied filter"))
    {
        return false;
    }

    const auto requiredQuality = evaluator.Evaluate(
        jaguar::Transform2d::FromRotationTranslation(0.0, {15.5, 0.5}),
        std::nullopt);
    if (!Expect(requiredQuality.kind == sparrow::SampleKind::SAMPLE_KIND_INVALID,
                output,
                "quality equal to minQuality must remain a collision"))
    {
        return false;
    }

    const auto hole = evaluator.Evaluate(
        jaguar::Transform2d::FromRotationTranslation(0.0, {8.5, 8.5}),
        std::nullopt);
    return Expect(hole.kind == sparrow::SampleKind::SAMPLE_KIND_INVALID,
                  output,
                  "Hole must remain a collision even with a quality filter");
}

bool TestLbfEvaluatorSurrogatePath(std::ostream& output)
{
    const jaguar::Container container = MakePlainEvaluatorContainer();
    const jaguar::Item concaveItem = MakeEvaluatorItem({
        {0.0, 0.0}, {6.0, 0.0}, {6.0, 2.0}, {2.0, 2.0},
        {2.0, 6.0}, {0.0, 6.0}},
        std::nullopt,
        [] {
            jaguar::SurrogateConfig config{};
            config.poleLimits = {{{4, 0.0}, {2, 0.8}, {1, 0.9}}};
            config.failFastPoleAreaRatio = 1.0;
            config.failFastPiers = 4;
            return config;
        }());
    const jaguar::Item blocker = MakeEvaluatorItem({
        {0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}, {0.0, 1.0}});
    const jaguar::NoFilter noFilter;
    auto layoutOwner = std::make_unique<jaguar::Layout>(container);
    if (!Expect(layoutOwner->PlaceItem(
                         blocker,
                         jaguar::Transform2d::FromRotationTranslation(0.0, {12.0, 12.0}))
                    .HasValue(),
                output,
                "Surrogate path blocker setup failed"))
    {
        return false;
    }
    const auto snapshot = layoutOwner->Save();
    auto engine = snapshot.container.CreateCollisionEngine();
    if (!Expect(engine != nullptr && engine->Restore(snapshot.cdeSnapshot).HasValue(),
                output,
                "Surrogate path engine restore failed"))
    {
        return false;
    }

    const jaguar::Surrogate* surrogate = concaveItem.CollisionShape().GetSurrogate();
    if (!Expect(surrogate != nullptr,
                output,
                "Surrogate path item has no cached surrogate"))
    {
        return false;
    }
    const jaguar::Transform2d colliding =
        jaguar::Transform2d::FromRotationTranslation(0.0, {12.0, 12.0});
    const auto surrogateCollision =
        engine->DetectSurrogateCollision(*surrogate, colliding, noFilter);
    if (!Expect(surrogateCollision.HasValue() && surrogateCollision.Value(),
                output,
                "Surrogate path must detect the blocker conservatively"))
    {
        return false;
    }
    jaguar::HazardFilter equivalentFilter{};
    equivalentFilter.irrelevant = [](const jaguar::HazardEntity&) { return false; };
    const auto equivalentCollision =
        engine->DetectSurrogateCollision(*surrogate, colliding, equivalentFilter);
    if (!Expect(equivalentCollision.HasValue() && equivalentCollision.Value(),
                output,
                "Equivalent wrapped filter must preserve surrogate collision"))
    {
        return false;
    }
    sparrow::LbfEvaluator evaluator(*layoutOwner, concaveItem, noFilter);
    const auto evaluated = evaluator.Evaluate(colliding, std::nullopt);
    return Expect(evaluated.kind == sparrow::SampleKind::SAMPLE_KIND_INVALID,
                  output,
                  "surrogate collision path must fail fast as Invalid");
}

bool TestLbfSearch(std::ostream& output)
{
    const jaguar::Container container = MakePlainEvaluatorContainer();
    const jaguar::Item item = MakeEvaluatorItem({
        {0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}, {0.0, 1.0}});
    const jaguar::NoFilter noFilter;

    sparrow::LbfConfig config;
    config.seed = 42U;
    config.samplesPerItem = 64U;
    config.localSearchFraction = 0.25;

    jaguar::Layout emptyLayout(container);
    sparrow::SparrowRng firstRng(config.seed);
    const auto first = sparrow::SearchLbf(
        emptyLayout, item, config, firstRng, noFilter);
    if (!Expect(first.HasValue() && first.Value().has_value(),
                output,
                "LBF search must find a clear placement in an empty layout"))
    {
        return false;
    }
    if (!Expect(first.Value()->samples == config.samplesPerItem
                    && std::isfinite(first.Value()->loss.Cost())
                    && std::isfinite(first.Value()->transform.rotationRad)
                    && std::isfinite(first.Value()->transform.translation.x)
                    && std::isfinite(first.Value()->transform.translation.y),
                output,
                "LBF search result must contain finite loss, transform and full sample count"))
    {
        return false;
    }
    struct SearchReference
    {
        std::size_t samples = 0U;
        sparrow::LbfLoss firstLoss{};
        sparrow::LbfLoss finalLoss{};
        jaguar::Transform2d transform = jaguar::Transform2d::Identity();
        std::vector<sparrow::LbfLoss> history;
    } reference;

    std::ifstream referenceFile;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/LbfSearchReference.tsv",
             "../SparrowCore/Tests/Fixtures/LbfSearchReference.tsv",
             "../JaguarRun/SparrowCore/Tests/Fixtures/LbfSearchReference.tsv",
             "..\\SparrowCore\\Tests\\Fixtures\\LbfSearchReference.tsv"})
    {
        referenceFile.open(path);
        if (referenceFile.is_open())
        {
            break;
        }
        referenceFile.clear();
    }
    if (!Expect(referenceFile.is_open(),
                output,
                "LBF search reference fixture must be readable"))
    {
        return false;
    }

    std::string line;
    while (std::getline(referenceFile, line))
    {
        if (line.empty() || line[0] == '#')
        {
            continue;
        }
        std::istringstream row(line);
        std::string kind;
        std::uint64_t seed = 0U;
        std::size_t index = 0U;
        if (!(row >> kind >> seed >> index) || seed != 42U)
        {
            return Expect(false, output, "malformed LBF search reference row");
        }
        if (kind == "final")
        {
            std::size_t samples = 0U;
            float firstX = 0.0F;
            float firstY = 0.0F;
            float finalX = 0.0F;
            float finalY = 0.0F;
            double rotation = 0.0;
            double translationX = 0.0;
            double translationY = 0.0;
            if (!(row >> samples >> firstX >> firstY >> finalX >> finalY
                  >> rotation >> translationX >> translationY))
            {
                return Expect(false, output, "malformed final LBF search reference row");
            }
            reference.samples = samples;
            reference.firstLoss = {firstX, firstY};
            reference.finalLoss = {finalX, finalY};
            reference.transform = jaguar::Transform2d::FromRotationTranslation(
                rotation, {translationX, translationY});
        }
        else if (kind == "history")
        {
            float xMax = 0.0F;
            float yMax = 0.0F;
            if (!(row >> xMax >> yMax))
            {
                return Expect(false, output, "malformed history LBF search reference row");
            }
            if (index != reference.history.size())
            {
                return Expect(false, output, "non-contiguous LBF search history fixture");
            }
            reference.history.push_back({xMax, yMax});
        }
        else
        {
            return Expect(false, output, "unknown LBF search reference row kind");
        }
    }
    if (!Expect(reference.samples == first.Value()->samples
                    && reference.history.size()
                           == first.Value()->tighteningHistory.size()
                    && !reference.history.empty(),
                output,
                "LBF search reference structure mismatch"))
    {
        return false;
    }
    if (!Expect(std::abs(reference.firstLoss.xMax
                         - first.Value()->tighteningHistory.front().xMax) < 1.0e-5F
                    && std::abs(reference.firstLoss.yMax
                                - first.Value()->tighteningHistory.front().yMax)
                           < 1.0e-5F
                    && std::abs(reference.finalLoss.xMax - first.Value()->loss.xMax)
                           < 1.0e-5F
                    && std::abs(reference.finalLoss.yMax - first.Value()->loss.yMax)
                           < 1.0e-5F
                    && std::abs(reference.transform.rotationRad
                                - first.Value()->transform.rotationRad)
                           < 1.0e-7
                    && std::abs(reference.transform.translation.x
                                - first.Value()->transform.translation.x)
                           < 1.0e-7
                    && std::abs(reference.transform.translation.y
                                - first.Value()->transform.translation.y)
                           < 1.0e-7,
                output,
                "LBF search final/first-best reference mismatch"))
    {
        return false;
    }
    for (std::size_t index = 0; index < reference.history.size(); ++index)
    {
        if (!Expect(std::abs(reference.history[index].xMax
                             - first.Value()->tighteningHistory[index].xMax)
                            < 1.0e-5F
                        && std::abs(reference.history[index].yMax
                                    - first.Value()->tighteningHistory[index].yMax)
                               < 1.0e-5F,
                    output,
                    "LBF search tightening history mismatch"))
        {
            return false;
        }
    }

    sparrow::SparrowRng secondRng(config.seed);
    const auto second = sparrow::SearchLbf(
        emptyLayout, item, config, secondRng, noFilter);
    if (!Expect(second.HasValue() && second.Value().has_value(),
                output,
                "Repeated LBF search must return a result"))
    {
        return false;
    }
    if (!Expect(second.Value()->samples == first.Value()->samples
                    && second.Value()->loss.xMax == first.Value()->loss.xMax
                    && second.Value()->loss.yMax == first.Value()->loss.yMax
                    && second.Value()->transform.rotationRad
                           == first.Value()->transform.rotationRad
                    && second.Value()->transform.translation
                           == first.Value()->transform.translation
                    && second.Value()->tighteningHistory.size()
                           == first.Value()->tighteningHistory.size(),
                output,
                "Fixed seed LBF search must be deterministic"))
    {
        return false;
    }

    sparrow::LbfConfig shortConfig = config;
    shortConfig.samplesPerItem = 16U;
    shortConfig.localSearchFraction = 0.0;
    sparrow::LbfConfig longConfig = shortConfig;
    longConfig.samplesPerItem = 64U;
    sparrow::SparrowRng shortRng(config.seed);
    sparrow::SparrowRng longRng(config.seed);
    const auto shortSearch = sparrow::SearchLbf(
        emptyLayout, item, shortConfig, shortRng, noFilter);
    const auto longSearch = sparrow::SearchLbf(
        emptyLayout, item, longConfig, longRng, noFilter);
    if (!Expect(shortSearch.HasValue() && shortSearch.Value().has_value()
                    && longSearch.HasValue() && longSearch.Value().has_value()
                    && longSearch.Value()->loss.Cost()
                           <= shortSearch.Value()->loss.Cost(),
                output,
                "More uniform samples must not lose the best known LBF result"))
    {
        return false;
    }

    const jaguar::Item blocker = MakeEvaluatorItem({
        {0.0, 0.0}, {30.0, 0.0}, {30.0, 30.0}, {0.0, 30.0}});
    jaguar::Layout blockedLayout(container);
    if (!Expect(blockedLayout.PlaceItem(blocker,
                                       jaguar::Transform2d::Identity()).HasValue(),
                output,
                "LBF blocked-layout setup failed"))
    {
        return false;
    }
    sparrow::SparrowRng blockedRng(config.seed);
    const auto blocked = sparrow::SearchLbf(
        blockedLayout, item, config, blockedRng, noFilter);
    if (!Expect(blocked.HasValue() && !blocked.Value().has_value(),
                output,
                "LBF search must return an empty optional for an infeasible layout"))
    {
        return false;
    }

    sparrow::LbfConfig invalidConfig = config;
    invalidConfig.samplesPerItem = 0U;
    sparrow::SparrowRng invalidRng(config.seed);
    const auto invalid = sparrow::SearchLbf(
        emptyLayout, item, invalidConfig, invalidRng, noFilter);
    return Expect(!invalid.HasValue()
                      && invalid.GetError().code == jaguar::ErrorCode::InvalidInput,
                  output,
                  "LBF search must reject an invalid configuration");
}
}

bool RunSamplingTests(std::ostream& output)
{
    return TestRotationModes(output)
        && TestUniformRectSampler(output)
        && TestLocalSearchSampler(output)
        && TestCoordinateDescentAxesAndBudget(output)
        && TestCoordinateDescentOutcomes(output)
        && TestLbfEvaluatorBasicCollisionAndLoss(output)
        && TestLbfEvaluatorHoleAndQualityFilter(output)
        && TestLbfEvaluatorSurrogatePath(output)
        && TestLbfSearch(output);
}
