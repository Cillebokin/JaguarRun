#include "SparrowCore/random/sparrow_rng.h"
#include "SparrowCore/eval/lbf_evaluator.h"
#include "SparrowCore/optimizer/lbf_search.h"
#include "SparrowCore/optimizer/sample_eval.h"
#include "SparrowCore/sampling/coordinate_descent.h"
#include "SparrowCore/sampling/local_search_sampler.h"
#include "SparrowCore/sampling/rotation_distribution.h"
#include "SparrowCore/sampling/uniform_rect_sampler.h"

#include "JaguarCore/entities/item.h"
#include "JaguarCore/entities/container.h"
#include "JaguarCore/entities/layout.h"
#include "JaguarCore/collision/collision_engine.h"
#include "JaguarCore/collision/hazard_filter.h"
#include "JaguarCore/fail_fast/surrogate.h"
#include "JaguarCore/geometry/original_shape.h"
#include "JaguarCore/geometry/shape_modification.h"
#include "JaguarCore/geometry/simple_polygon.h"

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
bool expect(bool condition, std::ostream& output, const char* message)
{
    if (condition)
    {
        return true;
    }

    output << "SAMPLING TEST FAILURE: " << message << '\n';
    return false;
}

jaguar::Result<jaguar::Item> makeItem(jaguar::RotationRange rotation)
{
    const auto polygon = jaguar::SimplePolygon::create({
        {0.0, 0.0}, {2.0, 0.0}, {2.0, 1.0}, {0.0, 1.0}});
    if (!polygon.hasValue())
    {
        return jaguar::Result<jaguar::Item>::fromError(polygon.error());
    }

    jaguar::OriginalShape original{
        polygon.value(),
        jaguar::Transform2d::identity(),
        jaguar::ShapeModifyMode::Inflate,
        {}};
    jaguar::SurrogateConfig surrogateConfig{};
    surrogateConfig.poleLimits = {{{1, 0.0}, {0, 0.0}, {0, 0.0}}};
    return jaguar::Item::create(0,
                                1001,
                                std::move(original),
                                surrogateConfig,
                                std::nullopt,
                                std::move(rotation));
}

bool isOneOf(float value, const std::vector<float>& values) noexcept
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

bool testRotationModes(std::ostream& output)
{
    const auto noneItem = makeItem({jaguar::RotationMode::None, {}});
    if (!expect(noneItem.hasValue(), output, "None rotation item creation failed"))
    {
        return false;
    }
    const auto noneDistribution =
        sparrow::RotationDistribution::fromItem(noneItem.value());
    if (!expect(noneDistribution.hasValue(), output, "None rotation distribution failed"))
    {
        return false;
    }
    sparrow::SparrowRng noneRng(42U);
    for (std::size_t index = 0; index < 16U; ++index)
    {
        if (!expect(noneDistribution.value().sample(noneRng) == 0.0F,
                    output,
                    "None rotation must always sample zero"))
        {
            return false;
        }
    }

    const double pi = std::acos(-1.0);
    const auto discreteItem = makeItem({
        jaguar::RotationMode::Discrete,
        {0.0, 0.5 * pi, -0.25 * pi}});
    if (!expect(discreteItem.hasValue(),
                output,
                "Discrete rotation item creation failed"))
    {
        return false;
    }
    const auto discreteDistribution =
        sparrow::RotationDistribution::fromItem(discreteItem.value());
    if (!expect(discreteDistribution.hasValue(),
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
        const float sample = discreteDistribution.value().sample(discreteRng);
        if (!expect(isOneOf(sample, discreteAngles),
                    output,
                    "Discrete rotation returned a forbidden angle"))
        {
            return false;
        }
    }

    const auto continuousItem = makeItem({jaguar::RotationMode::Continuous, {}});
    if (!expect(continuousItem.hasValue(),
                output,
                "Continuous rotation item creation failed"))
    {
        return false;
    }
    const auto continuousDistribution =
        sparrow::RotationDistribution::fromItem(continuousItem.value());
    if (!expect(continuousDistribution.hasValue(),
                output,
                "Continuous rotation distribution failed"))
    {
        return false;
    }
    sparrow::SparrowRng continuousFirst(42U);
    sparrow::SparrowRng continuousSecond(42U);
    for (std::size_t index = 0; index < 128U; ++index)
    {
        const float first = continuousDistribution.value().sample(continuousFirst);
        const float second = continuousDistribution.value().sample(continuousSecond);
        if (!expect(std::isfinite(first) && first >= 0.0F
                        && first < static_cast<float>(2.0 * pi),
                    output,
                    "Continuous rotation sample is outside [0, 2pi)"))
        {
            return false;
        }
        if (!expect(first == second,
                    output,
                    "Continuous rotation sequence is not seed deterministic"))
        {
            return false;
        }
    }

    const auto emptyDiscreteItem = makeItem({jaguar::RotationMode::Discrete, {}});
    if (!expect(emptyDiscreteItem.hasValue(),
                output,
                "Empty discrete rotation item creation failed"))
    {
        return false;
    }
    const auto emptyDiscrete =
        sparrow::RotationDistribution::fromItem(emptyDiscreteItem.value());
    if (!expect(!emptyDiscrete.hasValue()
                    && emptyDiscrete.error().code == jaguar::ErrorCode::InvalidInput,
                output,
                "Empty discrete rotation must be invalid"))
    {
        return false;
    }

    const auto nonFiniteItem = makeItem({
        jaguar::RotationMode::Discrete,
        {std::numeric_limits<double>::quiet_NaN()}});
    if (!expect(nonFiniteItem.hasValue(),
                output,
                "Non-finite discrete rotation item creation failed"))
    {
        return false;
    }
    const auto nonFiniteDistribution =
        sparrow::RotationDistribution::fromItem(nonFiniteItem.value());
    return expect(!nonFiniteDistribution.hasValue()
                      && nonFiniteDistribution.error().code
                             == jaguar::ErrorCode::InvalidInput,
                  output,
                  "Non-finite discrete rotation must be invalid");
}

bool testUniformRectSampler(std::ostream& output)
{
    const auto item = makeItem({jaguar::RotationMode::None, {}});
    if (!expect(item.hasValue(), output, "Uniform sampler item creation failed"))
    {
        return false;
    }

    const jaguar::Rect2d sampleBounds{1.0, 2.0, 5.0, 7.0};
    const auto sampler = sparrow::UniformRectSampler::create(sampleBounds, item.value());
    if (!expect(sampler.hasValue(), output, "Uniform sampler creation failed"))
    {
        return false;
    }
    if (!expect(sampler.value().bbox().xMin == sampleBounds.xMin
                    && sampler.value().bbox().yMin == sampleBounds.yMin
                    && sampler.value().bbox().xMax == sampleBounds.xMax
                    && sampler.value().bbox().yMax == sampleBounds.yMax,
                output,
                "Uniform sampler bbox was not preserved"))
    {
        return false;
    }

    const auto invalidDegenerate =
        sparrow::UniformRectSampler::create({1.0, 2.0, 1.0, 7.0}, item.value());
    if (!expect(!invalidDegenerate.hasValue()
                    && invalidDegenerate.error().code == jaguar::ErrorCode::InvalidInput,
                output,
                "Degenerate uniform sampler bbox must be invalid"))
    {
        return false;
    }
    const auto invalidNonFinite = sparrow::UniformRectSampler::create(
        {0.0, 0.0, std::numeric_limits<double>::infinity(), 1.0}, item.value());
    if (!expect(!invalidNonFinite.hasValue()
                    && invalidNonFinite.error().code == jaguar::ErrorCode::InvalidInput,
                output,
                "Non-finite uniform sampler bbox must be invalid"))
    {
        return false;
    }

    sparrow::SparrowRng firstRng(42U);
    sparrow::SparrowRng secondRng(42U);
    for (std::size_t index = 0; index < 64U; ++index)
    {
        const jaguar::Transform2d first = sampler.value().sample(firstRng);
        const jaguar::Transform2d second = sampler.value().sample(secondRng);
        if (!expect(first.rotationRad == 0.0
                        && first.translation.x >= sampleBounds.xMin
                        && first.translation.x < sampleBounds.xMax
                        && first.translation.y >= sampleBounds.yMin
                        && first.translation.y < sampleBounds.yMax,
                    output,
                    "Uniform sampler produced an out-of-bounds candidate"))
        {
            return false;
        }
        if (!expect(first.rotationRad == second.rotationRad
                        && first.translation == second.translation,
                    output,
                    "Uniform sampler sequence is not seed deterministic"))
        {
            return false;
        }

        const jaguar::SimplePolygon transformed =
            item.value().collisionShape().transformed(first);
        const jaguar::Rect2d transformedBounds = transformed.boundingBox();
        if (!expect(std::isfinite(transformedBounds.xMin)
                        && std::isfinite(transformedBounds.yMin)
                        && std::isfinite(transformedBounds.xMax)
                        && std::isfinite(transformedBounds.yMax)
                        && std::abs(transformedBounds.xMin
                                    - first.translation.x)
                               < 1.0e-6
                        && std::abs(transformedBounds.yMin
                                    - first.translation.y)
                               < 1.0e-6
                        && std::abs(transformedBounds.width() - 2.0) < 1.0e-6
                        && std::abs(transformedBounds.height() - 1.0) < 1.0e-6,
                    output,
                    "Transformed shape bbox does not follow sampled translation"))
        {
            return false;
        }
    }

    const auto discreteItem = makeItem({
        jaguar::RotationMode::Discrete,
        {0.0, 0.5 * std::acos(-1.0)}});
    if (!expect(discreteItem.hasValue(),
                output,
                "Discrete uniform sampler item creation failed"))
    {
        return false;
    }
    const auto discreteSampler =
        sparrow::UniformRectSampler::create(sampleBounds, discreteItem.value());
    if (!expect(discreteSampler.hasValue(),
                output,
                "Discrete uniform sampler creation failed"))
    {
        return false;
    }
    const double discretePiHalf = static_cast<double>(static_cast<float>(0.5 * std::acos(-1.0)));
    sparrow::SparrowRng discreteRng(42U);
    for (std::size_t index = 0; index < 64U; ++index)
    {
        const double rotation = discreteSampler.value().sample(discreteRng).rotationRad;
        if (!expect(rotation == 0.0 || rotation == discretePiHalf,
                    output,
                    "Uniform sampler returned a forbidden discrete rotation"))
        {
            return false;
        }
    }

    return true;
}

bool sameTransform(const jaguar::Transform2d& left,
                   const jaguar::Transform2d& right) noexcept
{
    return left.rotationRad == right.rotationRad
        && left.translation == right.translation;
}

bool finiteTransform(const jaguar::Transform2d& transform) noexcept
{
    return std::isfinite(transform.rotationRad)
        && std::isfinite(transform.translation.x)
        && std::isfinite(transform.translation.y);
}

bool testLocalSearchSampler(std::ostream& output)
{
    const double pi = std::acos(-1.0);
    const auto continuousItem = makeItem({jaguar::RotationMode::Continuous, {}});
    if (!expect(continuousItem.hasValue(),
                output,
                "Local search continuous item creation failed"))
    {
        return false;
    }

    const jaguar::Transform2d reference =
        jaguar::Transform2d::fromRotationTranslation(0.25, {10.0, 20.0});
    const jaguar::Rect2d containerBounds{0.0, 0.0, 100.0, 50.0};
    sparrow::LbfConfig config;
    const auto created = sparrow::LocalSearchSampler::create(
        continuousItem.value(), reference, config, containerBounds);
    if (!expect(created.hasValue(), output, "Local search sampler creation failed"))
    {
        return false;
    }

    sparrow::LocalSearchSampler first = created.value();
    sparrow::LocalSearchSampler second = created.value();
    sparrow::SparrowRng firstRng(42U);
    sparrow::SparrowRng secondRng(42U);
    for (std::size_t index = 0; index < 32U; ++index)
    {
        const jaguar::Transform2d firstSample = first.sample(firstRng);
        const jaguar::Transform2d secondSample = second.sample(secondRng);
        if (!expect(finiteTransform(firstSample),
                    output,
                    "Local search sample must be finite"))
        {
            return false;
        }
        if (!expect(sameTransform(firstSample, secondSample),
                    output,
                    "Local search sequence must be seed deterministic"))
        {
            return false;
        }
    }

    auto decayZero = sparrow::LocalSearchSampler::create(
        continuousItem.value(), reference, config, containerBounds);
    auto fresh = sparrow::LocalSearchSampler::create(
        continuousItem.value(), reference, config, containerBounds);
    if (!expect(decayZero.hasValue() && fresh.hasValue(),
                output,
                "Local search decay comparison setup failed"))
    {
        return false;
    }
    decayZero.value().decayStddev(0.0F);
    sparrow::SparrowRng decayZeroRng(7U);
    sparrow::SparrowRng freshRng(7U);
    for (std::size_t index = 0; index < 8U; ++index)
    {
        if (!expect(sameTransform(decayZero.value().sample(decayZeroRng),
                                  fresh.value().sample(freshRng)),
                    output,
                    "progress=0 must preserve the initial standard deviations"))
        {
            return false;
        }
    }

    auto shifted = sparrow::LocalSearchSampler::create(
        continuousItem.value(), reference, config, containerBounds);
    const jaguar::Transform2d shiftedReference =
        jaguar::Transform2d::fromRotationTranslation(-0.5, {30.0, 40.0});
    auto directShift = sparrow::LocalSearchSampler::create(
        continuousItem.value(), shiftedReference, config, containerBounds);
    if (!expect(shifted.hasValue() && directShift.hasValue(),
                output,
                "Local search shift comparison setup failed"))
    {
        return false;
    }
    shifted.value().shiftMean(shiftedReference);
    shifted.value().decayStddev(1.0F);
    directShift.value().decayStddev(1.0F);
    sparrow::SparrowRng shiftedRng(9U);
    sparrow::SparrowRng directShiftRng(9U);
    for (std::size_t index = 0; index < 8U; ++index)
    {
        const auto shiftedSample = shifted.value().sample(shiftedRng);
        const auto directSample = directShift.value().sample(directShiftRng);
        if (!expect(sameTransform(shiftedSample, directSample),
                    output,
                    "shiftMean must update the local search distribution mean"))
        {
            return false;
        }
        if (!expect(finiteTransform(shiftedSample),
                    output,
                    "progress=1 local search sample must be finite"))
        {
            return false;
        }
    }

    const auto discreteItem = makeItem({
        jaguar::RotationMode::Discrete,
        {0.0, 0.5 * pi}});
    if (!expect(discreteItem.hasValue(),
                output,
                "Local search discrete item creation failed"))
    {
        return false;
    }
    const double discreteReference =
        static_cast<double>(static_cast<float>(0.5 * pi));
    auto discreteSampler = sparrow::LocalSearchSampler::create(
        discreteItem.value(),
        jaguar::Transform2d::fromRotationTranslation(discreteReference, {1.0, 2.0}),
        config,
        containerBounds);
    if (!expect(discreteSampler.hasValue(),
                output,
                "Local search discrete sampler creation failed"))
    {
        return false;
    }
    sparrow::SparrowRng discreteRng(42U);
    for (std::size_t index = 0; index < 16U; ++index)
    {
        if (!expect(discreteSampler.value().sample(discreteRng).rotationRad
                        == discreteReference,
                    output,
                    "Discrete local search must not wiggle rotation"))
        {
            return false;
        }
    }

    const auto noneItem = makeItem({jaguar::RotationMode::None, {}});
    if (!expect(noneItem.hasValue(), output, "Local search None item creation failed"))
    {
        return false;
    }
    auto noneSampler = sparrow::LocalSearchSampler::create(
        noneItem.value(), reference, config, containerBounds);
    if (!expect(noneSampler.hasValue(),
                output,
                "Local search None sampler creation failed"))
    {
        return false;
    }
    if (!expect(noneSampler.value().sample(discreteRng).rotationRad == 0.0,
                output,
                "None local search must keep zero rotation"))
    {
        return false;
    }

    const auto invalidBounds = sparrow::LocalSearchSampler::create(
        continuousItem.value(), reference, config, {0.0, 0.0, 0.0, 1.0});
    if (!expect(!invalidBounds.hasValue()
                    && invalidBounds.error().code == jaguar::ErrorCode::InvalidInput,
                output,
                "Degenerate local search bounds must be invalid"))
    {
        return false;
    }
    sparrow::LbfConfig invalidConfig = config;
    invalidConfig.samplesPerItem = 0U;
    const auto invalidConfigResult = sparrow::LocalSearchSampler::create(
        continuousItem.value(), reference, invalidConfig, containerBounds);
    return expect(!invalidConfigResult.hasValue()
                      && invalidConfigResult.error().code
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

    sparrow::SampleEval evaluate(
        const jaguar::Transform2d& transform,
        const std::optional<sparrow::SampleEval>&) override
    {
        m_Seen.push_back(transform);
        ++m_EvaluationCount;
        if (m_Mode == Mode::Equal)
        {
            return {sparrow::SampleKind::Clear, 0.0F};
        }
        if (m_Mode == Mode::Worse)
        {
            return {sparrow::SampleKind::Clear, 1.0F};
        }

        const float loss = static_cast<float>(
            std::abs(transform.translation.x)
            + std::abs(transform.translation.y)
            + std::abs(transform.rotationRad));
        return {sparrow::SampleKind::Clear, loss};
    }

    std::size_t evaluationCount() const noexcept override
    {
        return m_EvaluationCount;
    }

    const std::vector<jaguar::Transform2d>& seen() const noexcept
    {
        return m_Seen;
    }

private:
    Mode m_Mode;
    std::size_t m_EvaluationCount = 0;
    std::vector<jaguar::Transform2d> m_Seen;
};

bool testCoordinateDescentAxesAndBudget(std::ostream& output)
{
    struct AxisCase
    {
        std::uint64_t seed;
        int expectedAxis;
    };
    const std::array<AxisCase, 4> cases = {{{10U, 0}, {3U, 1}, {2U, 2}, {0U, 3}}};
    const jaguar::Transform2d initialTransform =
        jaguar::Transform2d::fromRotationTranslation(0.0, {10.0, 20.0});
    const sparrow::SampleEval initialEval{sparrow::SampleKind::Clear, 0.0F};

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
        const auto result = sparrow::refineCoordinateDescent(
            {initialTransform, initialEval}, evaluator, config, rng);
        if (!expect(!result.hasValue()
                        && result.error().code == jaguar::ErrorCode::TimeLimit,
                    output,
                    "Coordinate descent must stop at the evaluation budget"))
        {
            return false;
        }
        if (!expect(evaluator.seen().size() == 2U,
                    output,
                    "Coordinate descent must evaluate both axis candidates"))
        {
            return false;
        }

        const auto& first = evaluator.seen()[0];
        const auto& second = evaluator.seen()[1];
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
        if (!expect(axisMatches,
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
        const auto result = sparrow::refineCoordinateDescent(
            {initialTransform, initialEval}, evaluator, wiggleConfig, rng);
        if (!expect(!result.hasValue()
                        && result.error().code == jaguar::ErrorCode::TimeLimit,
                    output,
                    "Rotation wiggle must honor the evaluation budget"))
        {
            return false;
        }
        const auto& first = evaluator.seen()[0];
        const auto& second = evaluator.seen()[1];
        const bool expectedOrder = first.rotationRad == 1.0
            && second.rotationRad == -1.0;
        if (!expect(expectedOrder
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

bool testCoordinateDescentOutcomes(std::ostream& output)
{
    const jaguar::Transform2d initialTransform =
        jaguar::Transform2d::fromRotationTranslation(0.5, {2.0, -1.0});
    const sparrow::SampleEval initialEval{sparrow::SampleKind::Clear, 3.5F};

    sparrow::CoordinateDescentConfig objectiveConfig;
    objectiveConfig.translationStepInit = 0.5F;
    objectiveConfig.translationStepLimit = 0.02F;
    objectiveConfig.rotationStepInit = 0.2F;
    objectiveConfig.rotationStepLimit = 0.02F;
    objectiveConfig.rotationWiggle = true;
    objectiveConfig.maxEvaluations = 1000U;

    CoordinateTestEvaluator objective(CoordinateTestEvaluator::Mode::Objective);
    sparrow::SparrowRng objectiveRng(42U);
    const auto objectiveResult = sparrow::refineCoordinateDescent(
        {initialTransform, initialEval}, objective, objectiveConfig, objectiveRng);
    if (!expect(objectiveResult.hasValue(),
                output,
                "Coordinate descent objective fixture must finish"))
    {
        return false;
    }
    if (!expect(objectiveResult.value().second.kind == sparrow::SampleKind::Clear
                    && std::isfinite(objectiveResult.value().second.loss)
                    && objectiveResult.value().second.loss <= initialEval.loss,
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
    const auto equalResult = sparrow::refineCoordinateDescent(
        {initialTransform,
         {sparrow::SampleKind::Clear, 0.0F}},
        equalEvaluator,
        outcomeConfig,
        equalRng);
    if (!expect(equalResult.hasValue(),
                output,
                "Equal coordinate descent fixture must finish"))
    {
        return false;
    }
    if (!expect(equalResult.value().second.loss == 0.0F
                    && !sameTransform(equalResult.value().first, initialTransform),
                output,
                "Equal evaluation must update the coordinate descent position"))
    {
        return false;
    }

    CoordinateTestEvaluator worseEvaluator(CoordinateTestEvaluator::Mode::Worse);
    sparrow::SparrowRng worseRng(42U);
    const auto worseResult = sparrow::refineCoordinateDescent(
        {initialTransform,
         {sparrow::SampleKind::Clear, 0.0F}},
        worseEvaluator,
        outcomeConfig,
        worseRng);
    if (!expect(worseResult.hasValue(),
                output,
                "Worse coordinate descent fixture must finish"))
    {
        return false;
    }
    return expect(sameTransform(worseResult.value().first, initialTransform)
                      && worseResult.value().second.loss == 0.0F,
                  output,
                  "Worse evaluation must keep the current coordinate and loss");
}

jaguar::SimplePolygon makePolygon(const std::vector<jaguar::Point2d>& points)
{
    const auto polygon = jaguar::SimplePolygon::create(points);
    return polygon.hasValue() ? polygon.value() : jaguar::SimplePolygon::create({
        {0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}, {0.0, 1.0}}).value();
}

jaguar::Container makeEvaluatorContainer()
{
    const jaguar::SimplePolygon outer = makePolygon({
        {0.0, 0.0}, {20.0, 0.0}, {20.0, 20.0}, {0.0, 20.0}});
    const std::vector<jaguar::QualityZone> zones = {
        {0, {makePolygon({{8.0, 8.0}, {10.0, 8.0}, {10.0, 10.0}, {8.0, 10.0}})}},
        {1, {makePolygon({{0.0, 0.0}, {3.0, 0.0}, {3.0, 3.0}, {0.0, 3.0}})}},
        {2, {makePolygon({{15.0, 0.0}, {18.0, 0.0}, {18.0, 3.0}, {15.0, 3.0}})}},
    };
    jaguar::CDEConfig config{};
    config.quadtreeDepth = 4;
    config.cdThreshold = 8;
    const auto container = jaguar::Container::create(
        9100, {outer,
               jaguar::Transform2d::identity(),
               jaguar::ShapeModifyMode::Deflate,
               {}},
        zones,
        config);
    return container.value();
}

jaguar::Container makePlainEvaluatorContainer()
{
    const jaguar::SimplePolygon outer = makePolygon({
        {0.0, 0.0}, {30.0, 0.0}, {30.0, 30.0}, {0.0, 30.0}});
    jaguar::CDEConfig config{};
    config.quadtreeDepth = 4;
    config.cdThreshold = 8;
    const auto container = jaguar::Container::create(
        9102,
        {outer,
         jaguar::Transform2d::identity(),
         jaguar::ShapeModifyMode::Deflate,
         {}},
        {},
        config);
    return container.value();
}

jaguar::Item makeEvaluatorItem(
    std::vector<jaguar::Point2d> points,
    std::optional<std::size_t> minQuality = std::nullopt,
    jaguar::SurrogateConfig surrogateConfig = {})
{
    auto polygon = jaguar::SimplePolygon::create(std::move(points));
    surrogateConfig.poleLimits = {{{4, 0.0}, {2, 0.8}, {1, 0.9}}};
    const auto item = jaguar::Item::create(
        0,
        9101,
        {polygon.value(),
         jaguar::Transform2d::identity(),
         jaguar::ShapeModifyMode::Inflate,
         {}},
        surrogateConfig,
        minQuality);
    return item.value();
}

bool testLbfEvaluatorBasicCollisionAndLoss(std::ostream& output)
{
    const jaguar::Container container = makeEvaluatorContainer();
    jaguar::Layout emptyLayout(container);
    const jaguar::Item item = makeEvaluatorItem({
        {0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}, {0.0, 1.0}});
    const jaguar::NoFilter noFilter;
    sparrow::LbfEvaluator evaluator(emptyLayout, item, noFilter);

    const auto clear = evaluator.evaluate(
        jaguar::Transform2d::fromRotationTranslation(0.0, {4.0, 4.0}),
        std::nullopt);
    if (!expect(clear.kind == sparrow::SampleKind::Clear
                    && clear.loss == 93.5F,
                output,
                "LBF evaluator must return Sparrow LBF loss for a clear sample"))
    {
        return false;
    }

    const auto exterior = evaluator.evaluate(
        jaguar::Transform2d::fromRotationTranslation(0.0, {19.5, 1.0}),
        std::nullopt);
    if (!expect(exterior.kind == sparrow::SampleKind::Invalid,
                output,
                "LBF evaluator must reject a sample outside the exterior"))
    {
        return false;
    }

    const auto bounded = evaluator.evaluate(
        jaguar::Transform2d::fromRotationTranslation(0.0, {10.0, 10.0}),
        sparrow::SampleEval{sparrow::SampleKind::Clear, 0.0F});
    if (!expect(bounded.kind == sparrow::SampleKind::Invalid,
                output,
                "LBF evaluator must reject a candidate that cannot improve the upper bound"))
    {
        return false;
    }

    if (!expect(evaluator.evaluationCount() == 3U,
                output,
                "LBF evaluator must count every evaluation including upper-bound skips"))
    {
        return false;
    }

    jaguar::Layout blockedLayout(container);
    const jaguar::Item blocker = makeEvaluatorItem({
        {0.0, 0.0}, {2.0, 0.0}, {2.0, 2.0}, {0.0, 2.0}});
    if (!expect(blockedLayout.placeItem(
                         blocker,
                         jaguar::Transform2d::fromRotationTranslation(0.0, {4.0, 4.0}))
                    .hasValue(),
                output,
                "LBF evaluator blocker setup failed"))
    {
        return false;
    }
    sparrow::LbfEvaluator blockedEvaluator(blockedLayout, item, noFilter);
    const auto collision = blockedEvaluator.evaluate(
        jaguar::Transform2d::fromRotationTranslation(0.0, {4.5, 4.5}),
        std::nullopt);
    if (!expect(collision.kind == sparrow::SampleKind::Invalid,
                  output,
                  "LBF evaluator must reject exact placed-item collisions"))
    {
        return false;
    }

    jaguar::SurrogateConfig exactOnlyConfig{};
    exactOnlyConfig.failFastPoleAreaRatio = 0.0;
    exactOnlyConfig.failFastPiers = 0;
    const jaguar::Item exactOnlyItem = makeEvaluatorItem({
        {0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}, {0.0, 1.0}},
        std::nullopt,
        exactOnlyConfig);
    if (!expect(exactOnlyItem.collisionShape().surrogate() != nullptr
                    && exactOnlyItem.collisionShape().surrogate()->failFastPoleCount() == 0U,
                output,
                "exact-only evaluator item must have no fail-fast poles"))
    {
        return false;
    }
    sparrow::LbfEvaluator exactOnlyEvaluator(blockedLayout, exactOnlyItem, noFilter);
    const auto exactOnlyCollision = exactOnlyEvaluator.evaluate(
        jaguar::Transform2d::fromRotationTranslation(0.0, {4.5, 4.5}),
        std::nullopt);
    return expect(exactOnlyCollision.kind == sparrow::SampleKind::Invalid,
                  output,
                  "exact CDE must reject a collision after surrogate miss");
}

bool testLbfEvaluatorHoleAndQualityFilter(std::ostream& output)
{
    const jaguar::Container container = makeEvaluatorContainer();
    const jaguar::Item item = makeEvaluatorItem({
        {0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}, {0.0, 1.0}}, 2U);

    jaguar::HazardFilter qualityFilter{};
    qualityFilter.irrelevant = [](const jaguar::HazardEntity& entity) {
        return entity.kind == jaguar::HazardKind::InferiorQualityZone
            && entity.quality < 2U;
    };
    jaguar::Layout layout(container);
    sparrow::LbfEvaluator evaluator(layout, item, qualityFilter);

    const auto ignoredQuality = evaluator.evaluate(
        jaguar::Transform2d::fromRotationTranslation(0.0, {1.0, 1.0}),
        std::nullopt);
    if (!expect(ignoredQuality.kind == sparrow::SampleKind::Clear,
                output,
                "quality below minQuality must be ignored by the supplied filter"))
    {
        return false;
    }

    const auto requiredQuality = evaluator.evaluate(
        jaguar::Transform2d::fromRotationTranslation(0.0, {15.5, 0.5}),
        std::nullopt);
    if (!expect(requiredQuality.kind == sparrow::SampleKind::Invalid,
                output,
                "quality equal to minQuality must remain a collision"))
    {
        return false;
    }

    const auto hole = evaluator.evaluate(
        jaguar::Transform2d::fromRotationTranslation(0.0, {8.5, 8.5}),
        std::nullopt);
    return expect(hole.kind == sparrow::SampleKind::Invalid,
                  output,
                  "Hole must remain a collision even with a quality filter");
}

bool testLbfEvaluatorSurrogatePath(std::ostream& output)
{
    const jaguar::Container container = makePlainEvaluatorContainer();
    const jaguar::Item concaveItem = makeEvaluatorItem({
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
    const jaguar::Item blocker = makeEvaluatorItem({
        {0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}, {0.0, 1.0}});
    const jaguar::NoFilter noFilter;
    auto layoutOwner = std::make_unique<jaguar::Layout>(container);
    if (!expect(layoutOwner->placeItem(
                         blocker,
                         jaguar::Transform2d::fromRotationTranslation(0.0, {12.0, 12.0}))
                    .hasValue(),
                output,
                "Surrogate path blocker setup failed"))
    {
        return false;
    }
    const auto snapshot = layoutOwner->save();
    auto engine = snapshot.container.createCollisionEngine();
    if (!expect(engine != nullptr && engine->restore(snapshot.cdeSnapshot).hasValue(),
                output,
                "Surrogate path engine restore failed"))
    {
        return false;
    }

    const jaguar::Surrogate* surrogate = concaveItem.collisionShape().surrogate();
    if (!expect(surrogate != nullptr,
                output,
                "Surrogate path item has no cached surrogate"))
    {
        return false;
    }
    const jaguar::Transform2d colliding =
        jaguar::Transform2d::fromRotationTranslation(0.0, {12.0, 12.0});
    if (!expect(engine->detectSurrogateCollision(*surrogate, colliding, noFilter),
                output,
                "Surrogate path must detect the blocker conservatively"))
    {
        return false;
    }
    jaguar::HazardFilter equivalentFilter{};
    equivalentFilter.irrelevant = [](const jaguar::HazardEntity&) { return false; };
    if (!expect(engine->detectSurrogateCollision(*surrogate, colliding, equivalentFilter),
                output,
                "Equivalent wrapped filter must preserve surrogate collision"))
    {
        return false;
    }
    sparrow::LbfEvaluator evaluator(*layoutOwner, concaveItem, noFilter);
    const auto evaluated = evaluator.evaluate(colliding, std::nullopt);
    return expect(evaluated.kind == sparrow::SampleKind::Invalid,
                  output,
                  "surrogate collision path must fail fast as Invalid");
}

bool testLbfSearch(std::ostream& output)
{
    const jaguar::Container container = makePlainEvaluatorContainer();
    const jaguar::Item item = makeEvaluatorItem({
        {0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}, {0.0, 1.0}});
    const jaguar::NoFilter noFilter;

    sparrow::LbfConfig config;
    config.seed = 42U;
    config.samplesPerItem = 64U;
    config.localSearchFraction = 0.25;

    jaguar::Layout emptyLayout(container);
    sparrow::SparrowRng firstRng(config.seed);
    const auto first = sparrow::searchLbf(
        emptyLayout, item, config, firstRng, noFilter);
    if (!expect(first.hasValue() && first.value().has_value(),
                output,
                "LBF search must find a clear placement in an empty layout"))
    {
        return false;
    }
    if (!expect(first.value()->samples == config.samplesPerItem
                    && std::isfinite(first.value()->loss.cost())
                    && std::isfinite(first.value()->transform.rotationRad)
                    && std::isfinite(first.value()->transform.translation.x)
                    && std::isfinite(first.value()->transform.translation.y),
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
        jaguar::Transform2d transform = jaguar::Transform2d::identity();
        std::vector<sparrow::LbfLoss> history;
    } reference;

    std::ifstream referenceFile;
    for (const char* path : {
             "SparrowCore/tests/fixtures/lbf_search_reference.tsv",
             "../SparrowCore/tests/fixtures/lbf_search_reference.tsv",
             "..\\SparrowCore\\tests\\fixtures\\lbf_search_reference.tsv"})
    {
        referenceFile.open(path);
        if (referenceFile.is_open())
        {
            break;
        }
        referenceFile.clear();
    }
    if (!expect(referenceFile.is_open(),
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
            return expect(false, output, "malformed LBF search reference row");
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
                return expect(false, output, "malformed final LBF search reference row");
            }
            reference.samples = samples;
            reference.firstLoss = {firstX, firstY};
            reference.finalLoss = {finalX, finalY};
            reference.transform = jaguar::Transform2d::fromRotationTranslation(
                rotation, {translationX, translationY});
        }
        else if (kind == "history")
        {
            float xMax = 0.0F;
            float yMax = 0.0F;
            if (!(row >> xMax >> yMax))
            {
                return expect(false, output, "malformed history LBF search reference row");
            }
            if (index != reference.history.size())
            {
                return expect(false, output, "non-contiguous LBF search history fixture");
            }
            reference.history.push_back({xMax, yMax});
        }
        else
        {
            return expect(false, output, "unknown LBF search reference row kind");
        }
    }
    if (!expect(reference.samples == first.value()->samples
                    && reference.history.size()
                           == first.value()->tighteningHistory.size()
                    && !reference.history.empty(),
                output,
                "LBF search reference structure mismatch"))
    {
        return false;
    }
    if (!expect(std::abs(reference.firstLoss.xMax
                         - first.value()->tighteningHistory.front().xMax) < 1.0e-5F
                    && std::abs(reference.firstLoss.yMax
                                - first.value()->tighteningHistory.front().yMax)
                           < 1.0e-5F
                    && std::abs(reference.finalLoss.xMax - first.value()->loss.xMax)
                           < 1.0e-5F
                    && std::abs(reference.finalLoss.yMax - first.value()->loss.yMax)
                           < 1.0e-5F
                    && std::abs(reference.transform.rotationRad
                                - first.value()->transform.rotationRad)
                           < 1.0e-7
                    && std::abs(reference.transform.translation.x
                                - first.value()->transform.translation.x)
                           < 1.0e-7
                    && std::abs(reference.transform.translation.y
                                - first.value()->transform.translation.y)
                           < 1.0e-7,
                output,
                "LBF search final/first-best reference mismatch"))
    {
        return false;
    }
    for (std::size_t index = 0; index < reference.history.size(); ++index)
    {
        if (!expect(std::abs(reference.history[index].xMax
                             - first.value()->tighteningHistory[index].xMax)
                            < 1.0e-5F
                        && std::abs(reference.history[index].yMax
                                    - first.value()->tighteningHistory[index].yMax)
                               < 1.0e-5F,
                    output,
                    "LBF search tightening history mismatch"))
        {
            return false;
        }
    }

    sparrow::SparrowRng secondRng(config.seed);
    const auto second = sparrow::searchLbf(
        emptyLayout, item, config, secondRng, noFilter);
    if (!expect(second.hasValue() && second.value().has_value(),
                output,
                "Repeated LBF search must return a result"))
    {
        return false;
    }
    if (!expect(second.value()->samples == first.value()->samples
                    && second.value()->loss.xMax == first.value()->loss.xMax
                    && second.value()->loss.yMax == first.value()->loss.yMax
                    && second.value()->transform.rotationRad
                           == first.value()->transform.rotationRad
                    && second.value()->transform.translation
                           == first.value()->transform.translation
                    && second.value()->tighteningHistory.size()
                           == first.value()->tighteningHistory.size(),
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
    const auto shortSearch = sparrow::searchLbf(
        emptyLayout, item, shortConfig, shortRng, noFilter);
    const auto longSearch = sparrow::searchLbf(
        emptyLayout, item, longConfig, longRng, noFilter);
    if (!expect(shortSearch.hasValue() && shortSearch.value().has_value()
                    && longSearch.hasValue() && longSearch.value().has_value()
                    && longSearch.value()->loss.cost()
                           <= shortSearch.value()->loss.cost(),
                output,
                "More uniform samples must not lose the best known LBF result"))
    {
        return false;
    }

    const jaguar::Item blocker = makeEvaluatorItem({
        {0.0, 0.0}, {30.0, 0.0}, {30.0, 30.0}, {0.0, 30.0}});
    jaguar::Layout blockedLayout(container);
    if (!expect(blockedLayout.placeItem(blocker,
                                       jaguar::Transform2d::identity()).hasValue(),
                output,
                "LBF blocked-layout setup failed"))
    {
        return false;
    }
    sparrow::SparrowRng blockedRng(config.seed);
    const auto blocked = sparrow::searchLbf(
        blockedLayout, item, config, blockedRng, noFilter);
    if (!expect(blocked.hasValue() && !blocked.value().has_value(),
                output,
                "LBF search must return an empty optional for an infeasible layout"))
    {
        return false;
    }

    sparrow::LbfConfig invalidConfig = config;
    invalidConfig.samplesPerItem = 0U;
    sparrow::SparrowRng invalidRng(config.seed);
    const auto invalid = sparrow::searchLbf(
        emptyLayout, item, invalidConfig, invalidRng, noFilter);
    return expect(!invalid.hasValue()
                      && invalid.error().code == jaguar::ErrorCode::InvalidInput,
                  output,
                  "LBF search must reject an invalid configuration");
}
}

bool runSamplingTests(std::ostream& output)
{
    return testRotationModes(output)
        && testUniformRectSampler(output)
        && testLocalSearchSampler(output)
        && testCoordinateDescentAxesAndBudget(output)
        && testCoordinateDescentOutcomes(output)
        && testLbfEvaluatorBasicCollisionAndLoss(output)
        && testLbfEvaluatorHoleAndQualityFilter(output)
        && testLbfEvaluatorSurrogatePath(output)
        && testLbfSearch(output);
}
