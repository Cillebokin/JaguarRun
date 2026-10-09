#include "SparrowCore/Random/SparrowRng.h"
#include "SparrowCore/Eval/LbfEvaluator.h"
#include "SparrowCore/Optimizer/LbfSearch.h"
#include "../Source/Optimizer/LbfSearchTrace.h"
#include "SparrowCore/Optimizer/SampleEval.h"
#include "SparrowCore/Sampling/CoordinateDescent.h"
#include "SparrowCore/Sampling/BestSamples.h"
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
#include "JaguarCore/IO/Importer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
bool Expect(bool condition, std::ostream& output, const char* message);

bool TestCoordinateDescentRadiansMatchRustReference(std::ostream& output)
{
    constexpr float degrees[]{5.0F, 1.0F, 0.5F, 0.05F};
    constexpr std::uint32_t expectedBits[]{
        0x3db2b8c2U, 0x3c8efa35U, 0x3c0efa35U, 0x3a64c388U};
    for (std::size_t index = 0U; index < 4U; ++index)
    {
        const float actual = sparrow::RustF32DegreesToRadians(degrees[index]);
        std::uint32_t actualBits = 0U;
        std::memcpy(&actualBits, &actual, sizeof(actualBits));
        if (!Expect(actualBits == expectedBits[index], output,
                    "coordinate descent degree conversion must match Rust f32 to_radians bits"))
        {
            output << "  index=" << index << " actual=0x" << std::hex
                   << actualBits << " expected=0x" << expectedBits[index]
                   << std::dec << '\n';
            return false;
        }
    }
    return true;
}

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
        const float step = static_cast<float>(2.0 * pi / 16.0);
        const float nearest = std::round(first / step) * step;
        if (!Expect(std::abs(first - nearest) < 1.0e-6F,
                    output,
                    "Continuous uniform rotation must use the reference 16-angle grid"))
        {
            return false;
        }
    }

    // jagua-rs 的 1..=65536 角数量规则在 Importer 层执行（Item::new 不校验）；
    // 此处按 Importer 的 JSON 入口验证拒绝行为，与 Rust import_rotation 一致。
    const jaguar::Importer importer;
    const auto rejectedEmptyDiscrete = importer.ImportItemJson(R"json({
        "id": 9101,
        "orientation": {"rotation": {"mode": "discrete", "angles": []}},
        "shape": {"type": "rectangle", "data": {"x_min": 0, "y_min": 0, "width": 1, "height": 1}}
    })json", 0);
    if (!Expect(!rejectedEmptyDiscrete.HasValue()
                    && rejectedEmptyDiscrete.GetError().code
                           == jaguar::ErrorCode::InvalidInput,
                output,
                "Empty discrete rotation must be rejected at the import boundary"))
    {
        return false;
    }

    // jagua-rs Item::new 接受非有限角（NaN 在 Transformation 构造时才
    // NotNan panic）；JaguarCore 对应行为是接受并存储，由求值层产生
    // "无合法变换"（LegalizeTransform 对空/NaN 角集返回 nullopt）。
    const auto nonFiniteItem = MakeItem({
        jaguar::RotationMode::Discrete,
        {std::numeric_limits<double>::quiet_NaN()}});
    return Expect(nonFiniteItem.HasValue(),
                  output,
                  "Non-finite discrete rotation is deferred to evaluation "
                  "like jagua-rs Item::new");
}

bool TestBestSamplesRotationBoundary(std::ostream& output)
{
    sparrow::BestSamples samples(2U, 1.0F);
    const double twoPi = 2.0 * std::acos(-1.0);
    const auto first = jaguar::Transform2d::FromRotationTranslation(
        0.0, {1.0, 1.0});
    const auto nearWrap = jaguar::Transform2d::FromRotationTranslation(
        twoPi - 0.005, {1.0, 1.0});
    if (!Expect(samples.Report(
                    first,
                    {sparrow::SampleKind::SAMPLE_KIND_CLEAR, 1.0F}),
                output,
                "BestSamples must accept the first valid sample"))
    {
        return false;
    }
    if (!Expect(samples.Report(
                    nearWrap,
                    {sparrow::SampleKind::SAMPLE_KIND_CLEAR, 2.0F}),
                output,
                "Rust rotation modulo must not treat 0 and 2pi as similar"))
    {
        return false;
    }
    return Expect(samples.Size() == 2U,
                  output,
                  "rotation-boundary samples must remain distinct");
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
    if (!Expect(sampler.HasValue() && sampler.Value().has_value(),
                output,
                "Uniform sampler creation failed"))
    {
        return false;
    }
    if (!Expect(sampler.Value()->BoundingBox().xMin == sampleBounds.xMin
                    && sampler.Value()->BoundingBox().yMin == sampleBounds.yMin
                    && sampler.Value()->BoundingBox().xMax == sampleBounds.xMax
                    && sampler.Value()->BoundingBox().yMax == sampleBounds.yMax,
                output,
                "Uniform sampler bbox was not preserved"))
    {
        return false;
    }

    const jaguar::Rect2d degenerateSampleBounds{2.0, 0.0, 2.0, 5.0};
    const jaguar::Rect2d roomyContainerBounds{0.0, 0.0, 10.0, 10.0};
    const auto degenerateSampler = sparrow::UniformRectSampler::Create(
        degenerateSampleBounds, item.Value(), roomyContainerBounds);
    if (!Expect(degenerateSampler.HasValue() && degenerateSampler.Value().has_value(),
                output,
                "Singleton sample bbox axis must be accepted when feasible"))
    {
        return false;
    }
    sparrow::SparrowRng degenerateRng(42U);
    for (std::size_t index = 0U; index < 8U; ++index)
    {
        const jaguar::Transform2d sample = degenerateSampler.Value()->Sample(degenerateRng);
        if (!Expect(sample.translation.x == 2.0
                        && sample.translation.y >= 0.0
                        && sample.translation.y < 5.0,
                    output,
                    "Singleton sample bbox must keep its fixed coordinate"))
        {
            return false;
        }
    }

    const auto noFitSampler = sparrow::UniformRectSampler::Create(
        {20.0, 0.0, 25.0, 5.0}, item.Value(), roomyContainerBounds);
    if (!Expect(noFitSampler.HasValue() && !noFitSampler.Value().has_value(),
                output,
                "No feasible rotation must be represented as no-fit, not an error"))
    {
        return false;
    }

    const auto reversedBounds = sparrow::UniformRectSampler::Create(
        {5.0, 0.0, 0.0, 5.0}, item.Value(), roomyContainerBounds);
    if (!Expect(reversedBounds.HasValue() && !reversedBounds.Value().has_value(),
                output,
                "Reversed Rust sample range must map to no-fit"))
    {
        return false;
    }

    const auto invalidNonFinite = sparrow::UniformRectSampler::Create(
        {0.0, 0.0, std::numeric_limits<double>::infinity(), 1.0},
        item.Value(),
        roomyContainerBounds);
    if (!Expect(invalidNonFinite.HasValue() && !invalidNonFinite.Value().has_value(),
                output,
                "Non-finite Rust sample range must map to no-fit"))
    {
        return false;
    }
    const auto nonFiniteRotationItem = MakeItem({
        jaguar::RotationMode::Discrete,
        {std::numeric_limits<double>::quiet_NaN()}});
    if (!Expect(nonFiniteRotationItem.HasValue(),
                output,
                "Non-finite-rotation sampler test item creation failed"))
    {
        return false;
    }
    const auto invalidRotationSampler = sparrow::UniformRectSampler::Create(
        sampleBounds, nonFiniteRotationItem.Value(), roomyContainerBounds);
    if (!Expect(!invalidRotationSampler.HasValue()
                    && invalidRotationSampler.GetError().code
                           == jaguar::ErrorCode::InvalidInput,
                output,
                "Invalid rotation must remain an error distinct from no-fit"))
    {
        return false;
    }

    const jaguar::Rect2d focusedBounds{4.0, 4.0, 5.0, 5.0};
    const jaguar::Rect2d containerBounds{0.0, 0.0, 10.0, 10.0};
    const auto focusedSampler = sparrow::UniformRectSampler::Create(
        focusedBounds, item.Value(), containerBounds);
    if (!Expect(focusedSampler.HasValue() && focusedSampler.Value().has_value(),
                output,
                "Focused uniform sampler creation failed"))
    {
        return false;
    }
    sparrow::SparrowRng focusedRng(42U);
    for (std::size_t index = 0U; index < 64U; ++index)
    {
        const jaguar::Transform2d sample = focusedSampler.Value()->Sample(focusedRng);
        if (!Expect(sample.translation.x >= focusedBounds.xMin
                        && sample.translation.x < focusedBounds.xMax
                        && sample.translation.y >= focusedBounds.yMin
                        && sample.translation.y < focusedBounds.yMax,
                    output,
                    "Focused sampler must draw translations from the focused bbox"))
        {
            return false;
        }
        const auto transformed = item.Value().CollisionShape().Transformed(sample);
        if (!Expect(transformed.HasValue()
                        && transformed.Value().BoundingBox().xMin >= containerBounds.xMin
                        && transformed.Value().BoundingBox().yMin >= containerBounds.yMin
                        && transformed.Value().BoundingBox().xMax <= containerBounds.xMax
                        && transformed.Value().BoundingBox().yMax <= containerBounds.yMax,
                    output,
                    "Focused sampler must keep the rotated item inside the container"))
        {
            return false;
        }
    }

    sparrow::SparrowRng firstRng(42U);
    sparrow::SparrowRng secondRng(42U);
    for (std::size_t index = 0; index < 64U; ++index)
    {
        const jaguar::Transform2d first = sampler.Value()->Sample(firstRng);
        const jaguar::Transform2d second = sampler.Value()->Sample(secondRng);
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
    if (!Expect(discreteSampler.HasValue() && discreteSampler.Value().has_value(),
                output,
                "Discrete uniform sampler creation failed"))
    {
        return false;
    }
    const double discretePiHalf = static_cast<double>(static_cast<float>(0.5 * std::acos(-1.0)));
    sparrow::SparrowRng discreteRng(42U);
    for (std::size_t index = 0; index < 64U; ++index)
    {
        const double rotation =
            discreteSampler.Value()->Sample(discreteRng).rotationRad;
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
    return Expect(invalidConfigResult.HasValue(),
                  output,
                  "zero LBF search samples must not invalidate the sampler parameters");
}

class CoordinateTestEvaluator final : public sparrow::SampleEvaluator
{
public:
    enum class Mode
    {
        Objective,
        Equal,
        ApproximateTie,
        Worse,
        LongImprovement,
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
        if (m_Mode == Mode::ApproximateTie)
        {
            return {sparrow::SampleKind::SAMPLE_KIND_CLEAR, 1.0F};
        }
        if (m_Mode == Mode::Worse)
        {
            return {sparrow::SampleKind::SAMPLE_KIND_CLEAR, 1.0F};
        }
        if (m_Mode == Mode::LongImprovement)
        {
            const float loss = m_EvaluationCount <= 1100U
                ? 2000.0F - static_cast<float>(m_EvaluationCount)
                : 900.0F;
            return {sparrow::SampleKind::SAMPLE_KIND_CLEAR, loss};
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

struct RustTraceRow
{
    std::size_t ordinal = 0U;
    float rotation = 0.0F;
    float x = 0.0F;
    float y = 0.0F;
    std::string kind;
    float loss = 0.0F;
};

float TraceLoss(const jaguar::Transform2d& transform) noexcept
{
    const float x = static_cast<float>(transform.translation.x);
    const float y = static_cast<float>(transform.translation.y);
    const float rotation = static_cast<float>(transform.rotationRad);
    return std::abs(x - 3.0F)
        + 2.0F * std::abs(y - 4.0F)
        + 3.0F * std::abs(rotation - 0.2F);
}

float FloatFromBits(std::uint32_t bits) noexcept
{
    float value = 0.0F;
    static_assert(sizeof(value) == sizeof(bits), "float trace requires 32-bit floats");
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

std::uint32_t FloatBits(float value) noexcept
{
    std::uint32_t bits = 0U;
    static_assert(sizeof(value) == sizeof(bits), "float trace requires 32-bit floats");
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

bool ReadRustTrace(
    std::vector<RustTraceRow>& samples,
    std::vector<RustTraceRow>& coordinateCandidates,
    RustTraceRow& finalResult,
    std::size_t& evaluationCount,
    std::ostream& output)
{
    std::ifstream traceFile;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/SamplingCoordinateDescentRustReference.tsv",
             "../SparrowCore/Tests/Fixtures/SamplingCoordinateDescentRustReference.tsv",
             "../JaguarRun/SparrowCore/Tests/Fixtures/SamplingCoordinateDescentRustReference.tsv",
             "..\\SparrowCore\\Tests\\Fixtures\\SamplingCoordinateDescentRustReference.tsv"})
    {
        traceFile.open(path);
        if (traceFile.is_open())
        {
            break;
        }
        traceFile.clear();
    }
    if (!Expect(traceFile.is_open(),
                output,
                "Rust sampler and coordinate-descent trace must be readable"))
    {
        return false;
    }

    std::string line;
    while (std::getline(traceFile, line))
    {
        if (line.empty() || line.front() == '#')
        {
            continue;
        }
        std::istringstream row(line);
        std::string stage;
        std::uint64_t seed = 0U;
        RustTraceRow trace;
        std::uint32_t rotationBits = 0U;
        std::uint32_t xBits = 0U;
        std::uint32_t yBits = 0U;
        std::uint32_t lossBits = 0U;
        if (!(row >> stage >> seed >> trace.ordinal >> std::hex
                  >> rotationBits >> xBits >> yBits >> trace.kind >> lossBits)
            || seed != 42U)
        {
            return Expect(false,
                          output,
                          "Rust trace row must match its schema and fixed seed");
        }
        trace.rotation = FloatFromBits(rotationBits);
        trace.x = FloatFromBits(xBits);
        trace.y = FloatFromBits(yBits);
        trace.loss = FloatFromBits(lossBits);
        if (stage == "sample")
        {
            samples.push_back(trace);
        }
        else if (stage == "coordinate")
        {
            coordinateCandidates.push_back(trace);
        }
        else if (stage == "result")
        {
            finalResult = trace;
            evaluationCount = trace.ordinal;
        }
        else
        {
            return Expect(false, output, "Rust trace contains an unknown stage");
        }
    }
    return Expect(samples.size() == 8U
                      && !coordinateCandidates.empty()
                      && evaluationCount == coordinateCandidates.size(),
                  output,
                  "Rust trace must contain all sample and coordinate evaluations");
}

struct TraceEvaluation
{
    jaguar::Transform2d transform = jaguar::Transform2d::Identity();
    sparrow::SampleEval evaluation{};
};

class TraceEvaluator final : public sparrow::SampleEvaluator
{
public:
    sparrow::SampleEval Evaluate(
        const jaguar::Transform2d& transform,
        const std::optional<sparrow::SampleEval>&) override
    {
        const sparrow::SampleEval evaluation{
            sparrow::SampleKind::SAMPLE_KIND_CLEAR,
            TraceLoss(transform)};
        m_Evaluations.push_back({transform, evaluation});
        return evaluation;
    }

    std::size_t EvaluationCount() const noexcept override
    {
        return m_Evaluations.size();
    }

    const std::vector<TraceEvaluation>& Evaluations() const noexcept
    {
        return m_Evaluations;
    }

private:
    std::vector<TraceEvaluation> m_Evaluations;
};

bool TraceMatches(const jaguar::Transform2d& actual,
                  const sparrow::SampleEval& evaluation,
                  const RustTraceRow& expected) noexcept
{
    constexpr float TRANSFORM_TOLERANCE = 1.0e-6F;
    constexpr float LOSS_TOLERANCE = 1.0e-5F;
    return expected.kind == "clear"
        && std::abs(static_cast<float>(actual.rotationRad) - expected.rotation)
               <= TRANSFORM_TOLERANCE
        && std::abs(static_cast<float>(actual.translation.x) - expected.x)
               <= TRANSFORM_TOLERANCE
        && std::abs(static_cast<float>(actual.translation.y) - expected.y)
               <= TRANSFORM_TOLERANCE
        && evaluation.kind == sparrow::SampleKind::SAMPLE_KIND_CLEAR
        && std::abs(evaluation.loss - expected.loss) <= LOSS_TOLERANCE;
}

bool TestRustSamplingCoordinateDescentTrace(std::ostream& output)
{
    std::vector<RustTraceRow> expectedSamples;
    std::vector<RustTraceRow> expectedCoordinateCandidates;
    RustTraceRow expectedFinal;
    std::size_t expectedEvaluationCount = 0U;
    if (!ReadRustTrace(expectedSamples,
                       expectedCoordinateCandidates,
                       expectedFinal,
                       expectedEvaluationCount,
                       output))
    {
        return false;
    }

    std::ifstream instanceFile;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/RustSppSingleItem.json",
             "../SparrowCore/Tests/Fixtures/RustSppSingleItem.json",
             "../JaguarRun/SparrowCore/Tests/Fixtures/RustSppSingleItem.json",
             "..\\SparrowCore\\Tests\\Fixtures\\RustSppSingleItem.json"})
    {
        instanceFile.open(path);
        if (instanceFile.is_open())
        {
            break;
        }
        instanceFile.clear();
    }
    if (!Expect(instanceFile.is_open(),
                output,
                "shared Rust/C++ sampling input must be readable"))
    {
        return false;
    }
    const std::string instanceJson{
        std::istreambuf_iterator<char>(instanceFile),
        std::istreambuf_iterator<char>()};
    const jaguar::Importer importer;
    const auto instance = importer.ImportSPInstanceJson(instanceJson);
    if (!Expect(instance.HasValue(),
                output,
                "shared sampling input must import successfully"))
    {
        return false;
    }

    const jaguar::Rect2d bounds{0.0, 0.0, 10.0, 10.0};
    const auto sampler = sparrow::UniformRectSampler::Create(
        bounds, instance.Value().GetItem(0U), bounds);
    if (!Expect(sampler.HasValue() && sampler.Value().has_value(),
                output,
                "Rust trace uniform sampler must be constructible in C++"))
    {
        return false;
    }
    sparrow::SparrowRng samplingRng(42U);
    for (std::size_t index = 0U; index < expectedSamples.size(); ++index)
    {
        const jaguar::Transform2d actual = sampler.Value()->Sample(samplingRng);
        const sparrow::SampleEval evaluation{
            sparrow::SampleKind::SAMPLE_KIND_CLEAR,
            TraceLoss(actual)};
        if (!Expect(TraceMatches(actual, evaluation, expectedSamples[index]),
                    output,
                    "uniform sampler candidate differs from Rust reference trace"))
        {
            output << "sample index=" << index << " expected=("
                   << expectedSamples[index].rotation << ','
                   << expectedSamples[index].x << ','
                   << expectedSamples[index].y << ") actual=("
                   << actual.rotationRad << ',' << actual.translation.x << ','
                   << actual.translation.y << ")\n";
            return false;
        }
    }

    const jaguar::Transform2d initial =
        jaguar::Transform2d::FromRotationTranslation(0.5, {5.0, 6.0});
    const sparrow::SampleEval initialEvaluation{
        sparrow::SampleKind::SAMPLE_KIND_CLEAR,
        TraceLoss(initial)};
    sparrow::CoordinateDescentConfig config;
    config.translationStepInit = 0.5F;
    config.translationStepLimit = 0.125F;
    config.rotationStepInit = 0.25F;
    config.rotationStepLimit = 0.0625F;
    config.rotationWiggle = true;
    TraceEvaluator evaluator;
    sparrow::SparrowRng descentRng(42U);
    const auto result = sparrow::RefineCoordinateDescent(
        {initial, initialEvaluation}, evaluator, config, descentRng);
    if (!Expect(result.HasValue(),
                output,
                "C++ coordinate descent must complete the Rust trace fixture"))
    {
        return false;
    }
    if (!Expect(evaluator.Evaluations().size() == expectedEvaluationCount,
                output,
                "coordinate descent evaluation count differs from Rust"))
    {
        output << "coordinate evaluation counts: Rust="
               << expectedEvaluationCount << " C++="
               << evaluator.Evaluations().size() << '\n';
        if (!evaluator.Evaluations().empty()
            && !expectedCoordinateCandidates.empty())
        {
            const TraceEvaluation& first = evaluator.Evaluations().front();
            output << "first coordinate candidate: Rust=("
                   << expectedCoordinateCandidates.front().rotation << ','
                   << expectedCoordinateCandidates.front().x << ','
                   << expectedCoordinateCandidates.front().y << ") C++=("
                   << first.transform.rotationRad << ','
                   << first.transform.translation.x << ','
                   << first.transform.translation.y << ")\n";
        }
        return false;
    }
    for (std::size_t index = 0U;
         index < expectedCoordinateCandidates.size();
         ++index)
    {
        const TraceEvaluation& actual = evaluator.Evaluations()[index];
        if (!Expect(TraceMatches(actual.transform,
                                 actual.evaluation,
                                 expectedCoordinateCandidates[index]),
                    output,
                    "coordinate descent candidate/loss differs from Rust trace"))
        {
            output << "coordinate index=" << index << " expected=("
                   << expectedCoordinateCandidates[index].rotation << ','
                   << expectedCoordinateCandidates[index].x << ','
                   << expectedCoordinateCandidates[index].y << ';'
                   << expectedCoordinateCandidates[index].loss << ") actual=("
                   << actual.transform.rotationRad << ','
                   << actual.transform.translation.x << ','
                   << actual.transform.translation.y << ';'
                   << actual.evaluation.loss << ")\n";
            return false;
        }
    }
    return Expect(TraceMatches(result.Value().first,
                               result.Value().second,
                               expectedFinal),
                  output,
                  "coordinate descent final state differs from Rust");
}

bool ParseSearchPhase(const std::string& text,
                     sparrow::LbfSearchTracePhase& phase) noexcept
{
    if (text == "container")
    {
        phase = sparrow::LbfSearchTracePhase::ContainerSample;
        return true;
    }
    if (text == "prerefine")
    {
        phase = sparrow::LbfSearchTracePhase::PreRefine;
        return true;
    }
    if (text == "finalrefine")
    {
        phase = sparrow::LbfSearchTracePhase::FinalRefine;
        return true;
    }
    return false;
}

bool ParseSearchKind(const std::string& text,
                     sparrow::SampleKind& kind) noexcept
{
    if (text == "clear")
    {
        kind = sparrow::SampleKind::SAMPLE_KIND_CLEAR;
        return true;
    }
    if (text == "collision")
    {
        kind = sparrow::SampleKind::SAMPLE_KIND_COLLISION;
        return true;
    }
    if (text == "invalid")
    {
        kind = sparrow::SampleKind::SAMPLE_KIND_INVALID;
        return true;
    }
    return false;
}

bool ReadLbfSearchRustTrace(
    std::vector<sparrow::LbfSearchTraceEvent>& events,
    std::uint64_t& expectedNextU64AfterSearch,
    std::ostream& output)
{
    std::ifstream traceFile;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/LbfSearchRustTrace.tsv",
             "../SparrowCore/Tests/Fixtures/LbfSearchRustTrace.tsv",
             "../JaguarRun/SparrowCore/Tests/Fixtures/LbfSearchRustTrace.tsv",
             "..\\SparrowCore\\Tests\\Fixtures\\LbfSearchRustTrace.tsv"})
    {
        traceFile.open(path);
        if (traceFile.is_open())
        {
            break;
        }
        traceFile.clear();
    }
    if (!Expect(traceFile.is_open(),
                output,
                "Rust LBF search trace fixture must be readable"))
    {
        return false;
    }

    std::size_t lastReport = std::numeric_limits<std::size_t>::max();
    std::size_t expectedKeepCount = 0U;
    std::size_t lastReportOrdinal = 0U;
    std::array<std::size_t, 3> evaluationOrdinals{};
    std::array<std::size_t, 3> reportOrdinals{};
    bool foundResult = false;
    bool foundRngAfter = false;
    std::string line;
    while (std::getline(traceFile, line))
    {
        if (line.empty() || line.front() == '#')
        {
            continue;
        }
        std::istringstream row(line);
        std::string record;
        if (!(row >> record))
        {
            continue;
        }
        if (record != "keep"
            && lastReport != std::numeric_limits<std::size_t>::max())
        {
            if (!Expect(events[lastReport].retained.size() == expectedKeepCount,
                        output,
                        "Rust LBF retained count must match its report row"))
            {
                return false;
            }
            lastReport = std::numeric_limits<std::size_t>::max();
        }
        if (record == "rng_after")
        {
            std::string unexpected;
            if (!foundResult || foundRngAfter
                || !(row >> std::hex >> expectedNextU64AfterSearch)
                || (row >> unexpected))
            {
                return Expect(false,
                              output,
                              "malformed Rust LBF post-search RNG row");
            }
            foundRngAfter = true;
            continue;
        }
        if (foundRngAfter)
        {
            return Expect(false,
                          output,
                          "Rust LBF post-search RNG row must be final");
        }
        sparrow::LbfSearchTraceEvent event;
        std::string kindText;
        std::uint32_t rotationBits = 0U;
        std::uint32_t xBits = 0U;
        std::uint32_t yBits = 0U;
        std::uint32_t lossBits = 0U;
        if (record == "eval")
        {
            std::string phaseText;
            std::size_t ordinal = 0U;
            if (!(row >> phaseText >> ordinal >> std::hex >> rotationBits
                      >> xBits >> yBits >> kindText >> lossBits)
                || !ParseSearchPhase(phaseText, event.phase)
                || !ParseSearchKind(kindText, event.evaluation.kind)
                || ordinal != evaluationOrdinals[
                       static_cast<std::size_t>(event.phase)])
            {
                return Expect(false, output, "malformed Rust LBF evaluation row");
            }
            ++evaluationOrdinals[static_cast<std::size_t>(event.phase)];
            event.type = sparrow::LbfSearchTraceEvent::Type::Evaluation;
            event.transform = jaguar::Transform2d::FromRotationTranslation(
                FloatFromBits(rotationBits),
                {FloatFromBits(xBits), FloatFromBits(yBits)});
            event.evaluation.loss = FloatFromBits(lossBits);
            events.push_back(std::move(event));
        }
        else if (record == "report")
        {
            std::string phaseText;
            std::size_t ordinal = 0U;
            unsigned accepted = 0U;
            std::size_t retainedCount = 0U;
            if (!(row >> phaseText >> ordinal >> accepted >> std::hex
                      >> rotationBits >> xBits >> yBits >> kindText >> lossBits
                      >> std::dec >> retainedCount)
                || !ParseSearchPhase(phaseText, event.phase)
                || !ParseSearchKind(kindText, event.evaluation.kind)
                || accepted > 1U
                || ordinal != reportOrdinals[
                       static_cast<std::size_t>(event.phase)])
            {
                return Expect(false, output, "malformed Rust LBF report row");
            }
            ++reportOrdinals[static_cast<std::size_t>(event.phase)];
            event.type = sparrow::LbfSearchTraceEvent::Type::Report;
            event.accepted = accepted != 0U;
            event.transform = jaguar::Transform2d::FromRotationTranslation(
                FloatFromBits(rotationBits),
                {FloatFromBits(xBits), FloatFromBits(yBits)});
            event.evaluation.loss = FloatFromBits(lossBits);
            event.retained.reserve(retainedCount);
            events.push_back(std::move(event));
            lastReport = events.size() - 1U;
            expectedKeepCount = retainedCount;
            lastReportOrdinal = ordinal;
        }
        else if (record == "keep")
        {
            std::string phaseText;
            std::size_t ordinal = 0U;
            std::size_t rank = 0U;
            if (!(row >> phaseText >> ordinal >> rank >> std::hex
                      >> rotationBits >> xBits >> yBits >> kindText >> lossBits)
                || !ParseSearchPhase(phaseText, event.phase)
                || !ParseSearchKind(kindText, event.evaluation.kind)
                || lastReport == std::numeric_limits<std::size_t>::max()
                || ordinal != lastReportOrdinal)
            {
                return Expect(false, output, "malformed Rust LBF retained-sample row");
            }
            sparrow::LbfSearchTraceEvent& report = events[lastReport];
            if (report.type != sparrow::LbfSearchTraceEvent::Type::Report
                || report.phase != event.phase || rank != report.retained.size())
            {
                return Expect(false, output, "Rust LBF retained order is malformed");
            }
            const jaguar::Transform2d transform =
                jaguar::Transform2d::FromRotationTranslation(
                    FloatFromBits(rotationBits),
                    {FloatFromBits(xBits), FloatFromBits(yBits)});
            report.retained.emplace_back(
                transform,
                sparrow::SampleEval{event.evaluation.kind,
                                    FloatFromBits(lossBits)});
        }
        else if (record == "result")
        {
            unsigned present = 0U;
            if (foundResult || !(row >> event.evaluationCount >> present)
                || present > 1U)
            {
                return Expect(false, output, "malformed Rust LBF result row");
            }
            foundResult = true;
            event.type = sparrow::LbfSearchTraceEvent::Type::Result;
            event.phase = sparrow::LbfSearchTracePhase::FinalRefine;
            if (present != 0U)
            {
                if (!(row >> std::hex >> rotationBits >> xBits >> yBits
                          >> kindText >> lossBits)
                    || !ParseSearchKind(kindText, event.evaluation.kind))
                {
                    return Expect(false, output, "malformed Rust LBF result sample");
                }
                event.transform = jaguar::Transform2d::FromRotationTranslation(
                    FloatFromBits(rotationBits),
                    {FloatFromBits(xBits), FloatFromBits(yBits)});
                event.evaluation.loss = FloatFromBits(lossBits);
            }
            events.push_back(std::move(event));
            lastReport = std::numeric_limits<std::size_t>::max();
        }
        else
        {
            return Expect(false, output, "unknown record in Rust LBF search trace");
        }
    }
    if (lastReport != std::numeric_limits<std::size_t>::max()
        && !Expect(events[lastReport].retained.size() == expectedKeepCount,
                   output,
                   "final Rust LBF retained count must match its report row"))
    {
        return false;
    }
    return Expect(foundResult && foundRngAfter && !events.empty()
                      && events.front().type
                             == sparrow::LbfSearchTraceEvent::Type::Evaluation
                      && events.back().type
                             == sparrow::LbfSearchTraceEvent::Type::Result,
                  output,
                  "Rust LBF search trace must include evaluations and final result");
}

bool SameTraceSample(const jaguar::Transform2d& actualTransform,
                     const sparrow::SampleEval& actualEvaluation,
                     const jaguar::Transform2d& expectedTransform,
                     const sparrow::SampleEval& expectedEvaluation) noexcept
{
    constexpr float TRANSFORM_TOLERANCE = 2.0e-5F;
    constexpr float LOSS_TOLERANCE = 2.0e-3F;
    return actualEvaluation.kind == expectedEvaluation.kind
        && std::abs(static_cast<float>(actualTransform.rotationRad)
                    - static_cast<float>(expectedTransform.rotationRad))
               <= TRANSFORM_TOLERANCE
        && std::abs(static_cast<float>(actualTransform.translation.x)
                    - static_cast<float>(expectedTransform.translation.x))
               <= TRANSFORM_TOLERANCE
        && std::abs(static_cast<float>(actualTransform.translation.y)
                    - static_cast<float>(expectedTransform.translation.y))
               <= TRANSFORM_TOLERANCE
        && (actualEvaluation.kind == sparrow::SampleKind::SAMPLE_KIND_INVALID
            || std::abs(actualEvaluation.loss - expectedEvaluation.loss)
                   <= LOSS_TOLERANCE);
}

bool TestUniformSamplerUpperBoundaryRustReference(std::ostream& output)
{
    std::ifstream referenceFile;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/UniformSamplerUpperBoundaryRust.tsv",
             "../SparrowCore/Tests/Fixtures/UniformSamplerUpperBoundaryRust.tsv",
             "../JaguarRun/SparrowCore/Tests/Fixtures/UniformSamplerUpperBoundaryRust.tsv",
             "..\\SparrowCore\\Tests\\Fixtures\\UniformSamplerUpperBoundaryRust.tsv"})
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
                "Rust sampler upper-boundary reference must be readable"))
    {
        return false;
    }

    std::uint64_t seed = 0U;
    std::uint32_t lowBits = 0U;
    std::uint32_t highBits = 0U;
    std::uint32_t expectedXBits = 0U;
    std::string line;
    bool foundRow = false;
    while (std::getline(referenceFile, line))
    {
        if (line.empty() || line.front() == '#')
        {
            continue;
        }
        std::istringstream row(line);
        if (!(row >> seed >> std::hex >> lowBits >> highBits >> expectedXBits))
        {
            return Expect(false,
                          output,
                          "Rust sampler upper-boundary row must match its schema");
        }
        foundRow = true;
        break;
    }
    if (!Expect(foundRow, output, "Rust sampler upper-boundary row must exist"))
    {
        return false;
    }

    const auto item = MakeItem({jaguar::RotationMode::None, {}});
    if (!Expect(item.HasValue(), output, "Boundary sampler item creation failed"))
    {
        return false;
    }
    const jaguar::Rect2d sampleBounds{
        static_cast<double>(FloatFromBits(lowBits)),
        1.0,
        static_cast<double>(FloatFromBits(highBits)),
        2.0};
    const jaguar::Rect2d containerBounds{0.0, 0.0, 10.0, 10.0};
    const auto sampler = sparrow::UniformRectSampler::Create(
        sampleBounds, item.Value(), containerBounds);
    if (!Expect(sampler.HasValue() && sampler.Value().has_value(),
                output,
                "Boundary sampler must be constructible in C++"))
    {
        return false;
    }

    sparrow::SparrowRng rng(seed);
    const jaguar::Transform2d sample = sampler.Value()->Sample(rng);
    const float x = static_cast<float>(sample.translation.x);
    std::uint32_t actualXBits = 0U;
    std::memcpy(&actualXBits, &x, sizeof(x));
    if (actualXBits != expectedXBits)
    {
        output << "boundary sample seed=" << seed << " expected=0x"
               << std::hex << expectedXBits << " actual=0x" << actualXBits
               << std::dec << '\n';
    }
    return Expect(actualXBits == expectedXBits,
                  output,
                  "uniform sampler upper-endpoint rounding differs from Rust");
}

bool TestUniformSamplerContinuousGridRustReference(std::ostream& output)
{
    std::ifstream referenceFile;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/UniformSamplerContinuousGridRust.tsv",
             "../SparrowCore/Tests/Fixtures/UniformSamplerContinuousGridRust.tsv",
             "../JaguarRun/SparrowCore/Tests/Fixtures/UniformSamplerContinuousGridRust.tsv",
             "..\\SparrowCore\\Tests\\Fixtures\\UniformSamplerContinuousGridRust.tsv"})
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
                "Rust continuous-grid reference must be readable"))
    {
        return false;
    }

    const auto item = MakeItem({jaguar::RotationMode::Continuous, {}});
    if (!Expect(item.HasValue(), output, "Continuous sampler item creation failed"))
    {
        return false;
    }
    const jaguar::Rect2d bounds{-100.0, -100.0, 100.0, 100.0};
    const auto sampler = sparrow::UniformRectSampler::Create(
        bounds, item.Value(), bounds);
    if (!Expect(sampler.HasValue() && sampler.Value().has_value(),
                output,
                "Continuous uniform sampler must be constructible"))
    {
        return false;
    }
    const auto rotationDistribution =
        sparrow::RotationDistribution::FromItem(item.Value());
    if (!Expect(rotationDistribution.HasValue(),
                output,
                "Continuous rotation distribution must be constructible"))
    {
        return false;
    }

    std::uint64_t seed = 0U;
    std::uint32_t expectedRotationBits = 0U;
    std::string line;
    std::size_t rows = 0U;
    while (std::getline(referenceFile, line))
    {
        if (line.empty() || line.front() == '#')
        {
            continue;
        }

        std::istringstream row(line);
        std::string extra;
        if (!(row >> seed >> std::hex >> expectedRotationBits)
            || (row >> extra))
        {
            return Expect(false,
                          output,
                          "Rust continuous-grid row must match its schema");
        }

        sparrow::SparrowRng rng(seed);
        const jaguar::Transform2d sample = sampler.Value()->Sample(rng);
        const float rotation = static_cast<float>(sample.rotationRad);
        std::uint32_t actualRotationBits = 0U;
        std::memcpy(&actualRotationBits, &rotation, sizeof(rotation));
        if (actualRotationBits != expectedRotationBits)
        {
            output << "continuous sampler seed=" << seed << " expected=0x"
                   << std::hex << expectedRotationBits << " actual=0x"
                   << actualRotationBits << std::dec << '\n';
            return Expect(false,
                          output,
                          "continuous sampler rotation differs from Rust reference");
        }

        sparrow::SparrowRng distributionRng(seed);
        const float distributionRotation =
            rotationDistribution.Value().Sample(distributionRng);
        std::uint32_t distributionRotationBits = 0U;
        std::memcpy(&distributionRotationBits,
                    &distributionRotation,
                    sizeof(distributionRotation));
        if (distributionRotationBits != expectedRotationBits)
        {
            output << "continuous rotation distribution seed=" << seed
                   << " expected=0x" << std::hex << expectedRotationBits
                   << " actual=0x" << distributionRotationBits << std::dec << '\n';
            return Expect(false,
                          output,
                          "continuous rotation distribution differs from Rust reference");
        }
        ++rows;
    }

    return Expect(rows == 16U,
                  output,
                  "Rust continuous-grid fixture must cover all 16 rotations");
}

bool TestUniformSamplerSingletonAxisRustReference(std::ostream& output)
{
    std::ifstream referenceFile;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/UniformSamplerSingletonAxisRust.tsv",
             "../SparrowCore/Tests/Fixtures/UniformSamplerSingletonAxisRust.tsv",
             "../JaguarRun/SparrowCore/Tests/Fixtures/UniformSamplerSingletonAxisRust.tsv",
             "..\\SparrowCore\\Tests\\Fixtures\\UniformSamplerSingletonAxisRust.tsv"})
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
                "Rust singleton-axis reference must be readable"))
    {
        return false;
    }

    std::ifstream instanceFile;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/RustSppSingleItem.json",
             "../SparrowCore/Tests/Fixtures/RustSppSingleItem.json",
             "../JaguarRun/SparrowCore/Tests/Fixtures/RustSppSingleItem.json",
             "..\\SparrowCore\\Tests\\Fixtures\\RustSppSingleItem.json"})
    {
        instanceFile.open(path);
        if (instanceFile.is_open())
        {
            break;
        }
        instanceFile.clear();
    }
    if (!Expect(instanceFile.is_open(),
                output,
                "Rust singleton-axis input must be readable"))
    {
        return false;
    }
    const std::string instanceJson{
        std::istreambuf_iterator<char>(instanceFile),
        std::istreambuf_iterator<char>()};
    const jaguar::Importer importer;
    const auto instance = importer.ImportSPInstanceJson(instanceJson);
    if (!Expect(instance.HasValue(),
                output,
                "Rust singleton-axis input must import in C++"))
    {
        return false;
    }
    const jaguar::Rect2d sampleBounds{0.0, 0.0, 2.0, 5.0};
    const jaguar::Rect2d containerBounds{0.0, 0.0, 2.0, 10.0};
    const auto sampler = sparrow::UniformRectSampler::Create(
        sampleBounds, instance.Value().GetItem(0U), containerBounds);
    if (!Expect(sampler.HasValue() && sampler.Value().has_value(),
                output,
                "Singleton-axis uniform sampler must be constructible"))
    {
        return false;
    }

    constexpr std::uint64_t SEED = 42U;
    sparrow::SparrowRng rng(SEED);
    std::uint64_t seed = 0U;
    std::size_t sampleIndex = 0U;
    std::uint32_t expectedXBits = 0U;
    std::uint32_t expectedYBits = 0U;
    std::string line;
    std::size_t rows = 0U;
    while (std::getline(referenceFile, line))
    {
        if (line.empty() || line.front() == '#')
        {
            continue;
        }

        std::istringstream row(line);
        std::string extra;
        if (!(row >> seed >> sampleIndex >> std::hex >> expectedXBits >> expectedYBits)
            || (row >> extra))
        {
            return Expect(false,
                          output,
                          "Rust singleton-axis row must match its schema");
        }
        if (!Expect(seed == SEED && sampleIndex == rows,
                    output,
                    "Rust singleton-axis fixture must be one ordered seed trace"))
        {
            return false;
        }

        const jaguar::Transform2d sample = sampler.Value()->Sample(rng);
        const float x = static_cast<float>(sample.translation.x);
        const float y = static_cast<float>(sample.translation.y);
        std::uint32_t actualXBits = 0U;
        std::uint32_t actualYBits = 0U;
        std::memcpy(&actualXBits, &x, sizeof(x));
        std::memcpy(&actualYBits, &y, sizeof(y));
        if (actualXBits != expectedXBits || actualYBits != expectedYBits)
        {
            output << "singleton-axis sample=" << sampleIndex << " expected=(0x"
                   << std::hex << expectedXBits << ",0x" << expectedYBits
                   << ") actual=(0x" << actualXBits << ",0x" << actualYBits
                   << ")" << std::dec << '\n';
            return Expect(false,
                          output,
                          "singleton axis shifted or changed the Rust sampling stream");
        }
        ++rows;
    }

    return Expect(rows == 8U,
                  output,
                  "Rust singleton-axis fixture must cover eight consecutive samples");
}

bool TestUniformSamplerDegenerateBboxRustReference(std::ostream& output)
{
    std::ifstream referenceFile;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/UniformSamplerDegenerateBboxRust.tsv",
             "../SparrowCore/Tests/Fixtures/UniformSamplerDegenerateBboxRust.tsv",
             "../JaguarRun/SparrowCore/Tests/Fixtures/UniformSamplerDegenerateBboxRust.tsv",
             "..\\SparrowCore\\Tests\\Fixtures\\UniformSamplerDegenerateBboxRust.tsv"})
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
                "Rust degenerate-bbox reference must be readable"))
    {
        return false;
    }

    std::ifstream instanceFile;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/RustSppSingleItem.json",
             "../SparrowCore/Tests/Fixtures/RustSppSingleItem.json",
             "../JaguarRun/SparrowCore/Tests/Fixtures/RustSppSingleItem.json",
             "..\\SparrowCore\\Tests\\Fixtures\\RustSppSingleItem.json"})
    {
        instanceFile.open(path);
        if (instanceFile.is_open())
        {
            break;
        }
        instanceFile.clear();
    }
    if (!Expect(instanceFile.is_open(),
                output,
                "Rust degenerate-bbox input must be readable"))
    {
        return false;
    }
    const std::string instanceJson{
        std::istreambuf_iterator<char>(instanceFile),
        std::istreambuf_iterator<char>()};
    const jaguar::Importer importer;
    const auto instance = importer.ImportSPInstanceJson(instanceJson);
    if (!Expect(instance.HasValue(),
                output,
                "Rust degenerate-bbox input must import in C++"))
    {
        return false;
    }

    struct Case
    {
        const char* name;
        jaguar::Rect2d sampleBounds;
    };
    constexpr Case cases[]{
        {"x", {2.0, 0.0, 2.0, 5.0}},
        {"y", {0.0, 2.0, 5.0, 2.0}},
        {"xy", {2.0, 2.0, 2.0, 2.0}},
    };
    const jaguar::Rect2d containerBounds{0.0, 0.0, 10.0, 10.0};
    std::string line;
    std::size_t totalRows = 0U;
    for (const Case& testCase : cases)
    {
        const auto sampler = sparrow::UniformRectSampler::Create(
            testCase.sampleBounds, instance.Value().GetItem(0U), containerBounds);
        if (!Expect(sampler.HasValue() && sampler.Value().has_value(),
                    output,
                    "Rust-accepted singleton input bbox must be constructible in C++"))
        {
            return false;
        }

        constexpr std::uint64_t SEED = 42U;
        sparrow::SparrowRng rng(SEED);
        for (std::size_t sampleIndex = 0U; sampleIndex < 8U; ++sampleIndex)
        {
            do
            {
                if (!std::getline(referenceFile, line))
                {
                    return Expect(false,
                                  output,
                                  "Rust degenerate-bbox fixture ended before all samples");
                }
            } while (line.empty() || line.front() == '#');

            std::istringstream row(line);
            std::string caseName;
            std::string extra;
            std::uint64_t seed = 0U;
            std::size_t expectedIndex = 0U;
            std::uint32_t expectedXBits = 0U;
            std::uint32_t expectedYBits = 0U;
            if (!(row >> caseName >> seed >> expectedIndex >> std::hex
                  >> expectedXBits >> expectedYBits)
                || (row >> extra))
            {
                return Expect(false,
                              output,
                              "Rust degenerate-bbox row must match its schema");
            }
            if (!Expect(caseName == testCase.name && seed == SEED
                            && expectedIndex == sampleIndex,
                        output,
                        "Rust degenerate-bbox fixture case/seed/order mismatch"))
            {
                return false;
            }

            const jaguar::Transform2d sample = sampler.Value()->Sample(rng);
            const float x = static_cast<float>(sample.translation.x);
            const float y = static_cast<float>(sample.translation.y);
            std::uint32_t actualXBits = 0U;
            std::uint32_t actualYBits = 0U;
            std::memcpy(&actualXBits, &x, sizeof(x));
            std::memcpy(&actualYBits, &y, sizeof(y));
            if (actualXBits != expectedXBits || actualYBits != expectedYBits)
            {
                output << "degenerate-bbox case=" << testCase.name
                       << " sample=" << sampleIndex << " expected=(0x"
                       << std::hex << expectedXBits << ",0x" << expectedYBits
                       << ") actual=(0x" << actualXBits << ",0x" << actualYBits
                       << ")" << std::dec << '\n';
                return Expect(false,
                              output,
                              "C++ singleton input bbox samples differ from Rust raw bits");
            }
            ++totalRows;
        }
    }

    while (std::getline(referenceFile, line))
    {
        if (!line.empty() && line.front() != '#')
        {
            return Expect(false,
                          output,
                          "Rust degenerate-bbox fixture has unexpected extra rows");
        }
    }
    return Expect(totalRows == 24U,
                  output,
                  "Rust degenerate-bbox fixture must cover 24 ordered samples");
}

bool TestRustLbfSearchTrace(std::ostream& output)
{
    std::vector<sparrow::LbfSearchTraceEvent> expected;
    std::uint64_t expectedNextU64AfterSearch = 0U;
    if (!ReadLbfSearchRustTrace(expected,
                                expectedNextU64AfterSearch,
                                output))
    {
        return false;
    }

    std::ifstream instanceFile;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/RustSppSingleItem.json",
             "../SparrowCore/Tests/Fixtures/RustSppSingleItem.json",
             "../JaguarRun/SparrowCore/Tests/Fixtures/RustSppSingleItem.json",
             "..\\SparrowCore\\Tests\\Fixtures\\RustSppSingleItem.json"})
    {
        instanceFile.open(path);
        if (instanceFile.is_open())
        {
            break;
        }
        instanceFile.clear();
    }
    if (!Expect(instanceFile.is_open(),
                output,
                "shared LBF search input fixture must be readable"))
    {
        return false;
    }
    const std::string instanceJson{
        std::istreambuf_iterator<char>(instanceFile),
        std::istreambuf_iterator<char>()};
    const jaguar::Importer importer;
    const auto instance = importer.ImportSPInstanceJson(instanceJson);
    if (!Expect(instance.HasValue(),
                output,
                "shared LBF search input must import successfully"))
    {
        return false;
    }
    jaguar::Strip strip = instance.Value().BaseStrip();
    if (!Expect(strip.SetWidth(10.0).HasValue(),
                output,
                "LBF search trace strip must resize successfully"))
    {
        return false;
    }
    const auto container = strip.CreateContainer();
    if (!Expect(container.HasValue(),
                output,
                "LBF search trace container must be valid"))
    {
        return false;
    }
    const jaguar::Layout layout(container.Value());
    const jaguar::NoFilter filter;
    sparrow::LbfConfig config;
    config.seed = 42U;
    config.samplesPerItem = 16U;
    config.nCoordinateDescents = 2U;
    sparrow::SparrowRng rng(config.seed);
    std::vector<sparrow::LbfSearchTraceEvent> actual;
    const auto result = sparrow::SearchLbfWithTrace(
        layout,
        instance.Value().GetItem(0U),
        config,
        rng,
        filter,
        [&actual](const sparrow::LbfSearchTraceEvent& event) {
            actual.push_back(event);
        });
    if (!Expect(result.HasValue() && result.Value().has_value(),
                output,
                "C++ LBF search trace must return a placement"))
    {
        return false;
    }
    const std::uint64_t actualNextU64AfterSearch = rng.NextU64();
    if (!Expect(actualNextU64AfterSearch == expectedNextU64AfterSearch,
                output,
                "C++ LBF search post-state RNG output differs from Rust"))
    {
        output << "post-search next_u64: Rust=0x" << std::hex
               << expectedNextU64AfterSearch << " C++=0x"
               << actualNextU64AfterSearch << std::dec << '\n';
        return false;
    }
    const std::size_t sharedEventCount = std::min(actual.size(), expected.size());
    for (std::size_t index = 0U; index < sharedEventCount; ++index)
    {
        const auto& rustEvent = expected[index];
        const auto& cppEvent = actual[index];
        bool matches = rustEvent.type == cppEvent.type
            && rustEvent.phase == cppEvent.phase;
        if (matches
            && rustEvent.type == sparrow::LbfSearchTraceEvent::Type::Evaluation)
        {
            matches = SameTraceSample(cppEvent.transform,
                                      cppEvent.evaluation,
                                      rustEvent.transform,
                                      rustEvent.evaluation);
        }
        else if (matches
                 && rustEvent.type == sparrow::LbfSearchTraceEvent::Type::Report)
        {
            matches = rustEvent.accepted == cppEvent.accepted
                && SameTraceSample(cppEvent.transform,
                                   cppEvent.evaluation,
                                   rustEvent.transform,
                                   rustEvent.evaluation)
                && rustEvent.retained.size() == cppEvent.retained.size();
            for (std::size_t rank = 0U;
                 matches && rank < rustEvent.retained.size();
                 ++rank)
            {
                matches = SameTraceSample(
                    cppEvent.retained[rank].first,
                    cppEvent.retained[rank].second,
                    rustEvent.retained[rank].first,
                    rustEvent.retained[rank].second);
            }
        }
        else if (matches
                 && rustEvent.type == sparrow::LbfSearchTraceEvent::Type::Result)
        {
            matches = cppEvent.evaluationCount == rustEvent.evaluationCount
                && result.Value()->samples == rustEvent.evaluationCount
                && std::abs(static_cast<float>(result.Value()->transform.rotationRad)
                            - static_cast<float>(rustEvent.transform.rotationRad))
                       <= 2.0e-5F
                && std::abs(static_cast<float>(result.Value()->transform.translation.x)
                            - static_cast<float>(rustEvent.transform.translation.x))
                       <= 2.0e-5F
                && std::abs(static_cast<float>(result.Value()->transform.translation.y)
                            - static_cast<float>(rustEvent.transform.translation.y))
                       <= 2.0e-5F
                && SameTraceSample(cppEvent.transform,
                                   cppEvent.evaluation,
                                   rustEvent.transform,
                                   rustEvent.evaluation);
        }
        if (!Expect(matches,
                    output,
                    "Rust/C++ LBF search trace differs at an event"))
        {
            output << std::setprecision(10)
                   << "first trace mismatch at event " << index
                   << " type=" << static_cast<int>(rustEvent.type)
                   << " phase=" << static_cast<int>(rustEvent.phase)
                   << " Rust candidate=(" << rustEvent.transform.rotationRad
                   << ',' << rustEvent.transform.translation.x << ','
                   << rustEvent.transform.translation.y << ';'
                   << static_cast<int>(rustEvent.evaluation.kind) << ','
                   << rustEvent.evaluation.loss << ") C++ candidate=("
                   << cppEvent.transform.rotationRad << ','
                   << cppEvent.transform.translation.x << ','
                   << cppEvent.transform.translation.y << ';'
                   << static_cast<int>(cppEvent.evaluation.kind) << ','
                   << cppEvent.evaluation.loss << ") Rust retained="
                   << rustEvent.retained.size() << " C++ retained="
                   << cppEvent.retained.size() << '\n';
            const auto transformed = instance.Value().GetItem(0U)
                .CollisionShape().TransformedRustF32(cppEvent.transform);
            if (transformed.HasValue())
            {
                const auto& bbox = transformed.Value().BoundingBox();
                const auto& poi = transformed.Value().PointOfInaccessibility();
                output << "C++ geometry bbox=(" << bbox.xMin << ',' << bbox.yMin
                       << ',' << bbox.xMax << ',' << bbox.yMax << ") poi=("
                       << poi.center.x << ',' << poi.center.y << ")\n";
            }
            return false;
        }
    }
    return Expect(actual.size() == expected.size(),
                  output,
                  "Rust/C++ LBF search trace event counts must match");
}

bool TestCoordinateDescentAxesAndBudget(std::ostream& output)
{
    struct AxisCase
    {
        std::uint64_t seed;
        int expectedAxis;
    };
    const std::array<AxisCase, 4> cases = {{{10U, 0}, {3U, 0}, {2U, 3}, {0U, 1}}};
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
    const std::array<AxisCase, 1> wiggleCases = {{{2U, 4}}};
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

bool TestCoordinateDescentApproximateTieMatchesRustReference(std::ostream& output)
{
    std::ifstream referenceFile;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/SampleEvalCoordinateDescentRustReference.tsv",
             "../SparrowCore/Tests/Fixtures/SampleEvalCoordinateDescentRustReference.tsv",
             "../JaguarRun/SparrowCore/Tests/Fixtures/SampleEvalCoordinateDescentRustReference.tsv",
             "..\\SparrowCore\\Tests\\Fixtures\\SampleEvalCoordinateDescentRustReference.tsv"})
    {
        referenceFile.open(path);
        if (referenceFile.is_open())
        {
            break;
        }
        referenceFile.clear();
    }
    if (!Expect(referenceFile.is_open(), output,
                "Rust coordinate-descent comparator boundary trace must be readable"))
    {
        return false;
    }
    std::vector<std::string> expectedRows;
    std::string line;
    while (std::getline(referenceFile, line))
    {
        if (!line.empty() && line.front() != '#')
        {
            expectedRows.push_back(line);
        }
    }

    const jaguar::Transform2d initialTransform =
        jaguar::Transform2d::FromRotationTranslation(0.5, {2.0, -1.0});
    const sparrow::SampleEval initialEvaluation{
        sparrow::SampleKind::SAMPLE_KIND_CLEAR,
        FloatFromBits(0x3f800004U)};
    sparrow::CoordinateDescentConfig config;
    config.translationStepInit = 1.0F;
    config.translationStepLimit = 0.6F;
    config.rotationStepInit = 0.0F;
    config.rotationStepLimit = 0.0F;
    config.rotationWiggle = false;
    CoordinateTestEvaluator evaluator(CoordinateTestEvaluator::Mode::ApproximateTie);
    sparrow::SparrowRng rng(42U);
    const auto result = sparrow::RefineCoordinateDescent(
        {initialTransform, initialEvaluation}, evaluator, config, rng);
    if (!Expect(result.HasValue(),
                output,
                "Rust approximate-tie coordinate-descent case must finish"))
    {
        return false;
    }

    std::vector<std::string> actualRows;
    const auto appendTransform = [&actualRows](
                                    const char* stage,
                                    const jaguar::Transform2d& transform) {
        std::ostringstream row;
        row << stage << '\t' << std::hex << std::nouppercase << std::setfill('0')
            << std::setw(8) << FloatBits(static_cast<float>(transform.rotationRad))
            << '\t' << std::setw(8)
            << FloatBits(static_cast<float>(transform.translation.x))
            << '\t' << std::setw(8)
            << FloatBits(static_cast<float>(transform.translation.y));
        actualRows.push_back(row.str());
    };
    for (const auto& candidate : evaluator.Seen())
    {
        appendTransform("candidate", candidate);
    }
    const auto& finalTransform = result.Value().first;
    std::ostringstream finalRow;
    finalRow << "result\t" << std::hex << std::nouppercase << std::setfill('0')
             << std::setw(8) << FloatBits(static_cast<float>(finalTransform.rotationRad))
             << '\t' << std::setw(8)
             << FloatBits(static_cast<float>(finalTransform.translation.x))
             << '\t' << std::setw(8)
             << FloatBits(static_cast<float>(finalTransform.translation.y))
             << '\t' << std::setw(8) << FloatBits(result.Value().second.loss)
             << '\t' << std::dec << evaluator.EvaluationCount();
    actualRows.push_back(finalRow.str());

    if (actualRows != expectedRows)
    {
        output << "Rust coordinate-descent rows=" << expectedRows.size()
               << " C++ rows=" << actualRows.size() << '\n';
        const std::size_t count = std::min(expectedRows.size(), actualRows.size());
        for (std::size_t index = 0U; index < count; ++index)
        {
            if (expectedRows[index] != actualRows[index])
            {
                output << index << " expected: " << expectedRows[index]
                       << " actual: " << actualRows[index] << '\n';
            }
        }
    }
    return Expect(actualRows == expectedRows,
                  output,
                  "SparrowCore coordinate descent comparator-boundary trace must match Rust");
}

bool TestCoordinateDescentDefaultBudgetMatchesRust(std::ostream& output)
{
    sparrow::CoordinateDescentConfig config;
    config.translationStepInit = 1.0e-30F;
    config.translationStepLimit = 1.0e-31F;
    config.rotationStepInit = 0.0F;
    config.rotationStepLimit = 0.0F;
    config.rotationWiggle = false;

    CoordinateTestEvaluator evaluator(
        CoordinateTestEvaluator::Mode::LongImprovement);
    sparrow::SparrowRng rng(42U);
    const auto result = sparrow::RefineCoordinateDescent(
        {jaguar::Transform2d::Identity(),
         {sparrow::SampleKind::SAMPLE_KIND_CLEAR, 2000.0F}},
        evaluator,
        config,
        rng);
    return Expect(result.HasValue()
                      && evaluator.EvaluationCount() > 1000U,
                  output,
                  "default coordinate descent must not truncate Rust's uncapped refinement at 1000 evaluations");
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
                    && clear.loss == 104.5F,
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
        jaguar::Transform2d::FromRotationTranslation(0.0, {12.0, 12.0}),
        sparrow::SampleEval{sparrow::SampleKind::SAMPLE_KIND_CLEAR, 0.0F});
    if (!Expect(bounded.kind == sparrow::SampleKind::SAMPLE_KIND_CLEAR
                    && bounded.loss == 280.5F,
                output,
                "LBF evaluator must evaluate worse candidates instead of pruning by upper bound"))
    {
        return false;
    }

    if (!Expect(evaluator.EvaluationCount() == 3U,
                output,
                "LBF evaluator must count every evaluated candidate"))
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

bool TestLbfEvaluatorUsesPointOfInaccessibility(std::ostream& output)
{
    const jaguar::Container container = MakePlainEvaluatorContainer();
    jaguar::Layout layout(container);
    const jaguar::Item item = MakeEvaluatorItem({
        {0.0, 0.0}, {2.0, 0.0}, {0.0, 2.0}});
    const jaguar::NoFilter noFilter;
    sparrow::LbfEvaluator evaluator(layout, item, noFilter);
    const jaguar::Transform2d transform =
        jaguar::Transform2d::FromRotationTranslation(0.0, {4.0, 4.0});
    const auto transformed = item.CollisionShape().TransformedRustF32(transform);
    if (!Expect(transformed.HasValue(),
                output,
                "POI loss fixture transform failed"))
    {
        return false;
    }

    const jaguar::SimplePolygon& shape = transformed.Value();
    const jaguar::Point2d poi = shape.PointOfInaccessibility().center;
    const jaguar::Point2d centroid = shape.Centroid();
    const jaguar::Rect2d& bounds = shape.BoundingBox();
    const float expectedPoiLoss =
        10.0F * (static_cast<float>(poi.x) + static_cast<float>(bounds.xMax))
        + static_cast<float>(poi.y + bounds.yMax);
    const float centroidLoss =
        10.0F * (static_cast<float>(centroid.x)
                 + static_cast<float>(bounds.xMax))
        + static_cast<float>(centroid.y + bounds.yMax);
    const sparrow::SampleEval actual = evaluator.Evaluate(transform, std::nullopt);
    return Expect(actual.kind == sparrow::SampleKind::SAMPLE_KIND_CLEAR
                      && std::abs(expectedPoiLoss - centroidLoss) > 0.01F
                      && std::abs(actual.loss - expectedPoiLoss) < 1.0e-4F,
                  output,
                  "LBF placement loss must rank by the pole of inaccessibility, not the polygon centroid");
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

    sparrow::LbfConfig zeroSampleConfig = config;
    zeroSampleConfig.samplesPerItem = 0U;
    sparrow::SparrowRng zeroSampleRng(zeroSampleConfig.seed);
    const auto zeroSampleResult = sparrow::SearchLbf(
        emptyLayout, item, zeroSampleConfig, zeroSampleRng, noFilter);
    if (!Expect(zeroSampleResult.HasValue()
                    && !zeroSampleResult.Value().has_value(),
                output,
                "zero container samples must return no candidate like the Rust search loop"))
    {
        return false;
    }
    if (!Expect(zeroSampleRng.NextU64() == 15021278609987233951ULL,
                output,
                "zero-budget empty-pool LBF search must preserve the seed-42 RNG stream"))
    {
        return false;
    }

    const auto nonFiniteRotationItem = MakeItem({
        jaguar::RotationMode::Discrete,
        {std::numeric_limits<double>::quiet_NaN()}});
    if (!Expect(nonFiniteRotationItem.HasValue(),
                output,
                "LBF rotation-error item creation failed"))
    {
        return false;
    }
    sparrow::SparrowRng rotationErrorRng(config.seed);
    const auto rotationError = sparrow::SearchLbf(
        emptyLayout,
        nonFiniteRotationItem.Value(),
        config,
        rotationErrorRng,
        noFilter);
    if (!Expect(!rotationError.HasValue()
                    && rotationError.GetError().code == jaguar::ErrorCode::InvalidInput,
                output,
                "LBF must propagate sampler construction errors instead of mapping them to no-fit"))
    {
        return false;
    }
    if (!Expect(rotationErrorRng.NextU64() == 15021278609987233951ULL,
                output,
                "non-finite-rotation search failure must leave the seed-42 RNG stream untouched"))
    {
        return false;
    }

    sparrow::LbfConfig zeroCapacityConfig = config;
    zeroCapacityConfig.samplesPerItem = 1U;
    zeroCapacityConfig.nCoordinateDescents = 0U;
    sparrow::SparrowRng zeroCapacityRng(zeroCapacityConfig.seed);
#if !defined(NDEBUG)
    bool zeroCapacityPanicked = false;
    try
    {
        static_cast<void>(sparrow::SearchLbf(
            emptyLayout, item, zeroCapacityConfig, zeroCapacityRng, noFilter));
    }
    catch (const std::underflow_error&)
    {
        zeroCapacityPanicked = true;
    }
    if (!Expect(zeroCapacityPanicked,
                output,
                "zero coordinate-descent capacity must preserve Rust Debug underflow"))
    {
        return false;
    }
#else
    const auto zeroCapacityResult = sparrow::SearchLbf(
        emptyLayout, item, zeroCapacityConfig, zeroCapacityRng, noFilter);
    if (!Expect(zeroCapacityResult.HasValue()
                    && zeroCapacityResult.Value().has_value(),
                output,
                "zero coordinate-descent capacity must preserve Rust Release behavior"))
    {
        return false;
    }
#endif

    const auto first = sparrow::SearchLbf(
        emptyLayout, item, config, firstRng, noFilter);
    if (!Expect(first.HasValue() && first.Value().has_value(),
                output,
                "LBF search must find a clear placement in an empty layout"))
    {
        return false;
    }
    if (!Expect(first.Value()->samples >= config.samplesPerItem
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
        output << "LBF search structure: reference samples="
               << reference.samples << " actual samples="
               << first.Value()->samples << " reference history="
               << reference.history.size() << " actual history="
               << first.Value()->tighteningHistory.size() << '\n'
               << std::setprecision(10)
               << "final 42 0 " << first.Value()->samples << ' '
               << first.Value()->tighteningHistory.front().xMax << ' '
               << first.Value()->tighteningHistory.front().yMax << ' '
               << first.Value()->loss.xMax << ' ' << first.Value()->loss.yMax << ' '
               << first.Value()->transform.rotationRad << ' '
               << first.Value()->transform.translation.x << ' '
               << first.Value()->transform.translation.y << '\n';
        for (std::size_t index = 0;
             index < first.Value()->tighteningHistory.size();
             ++index)
        {
            output << "history 42 " << index << ' '
                   << first.Value()->tighteningHistory[index].xMax << ' '
                   << first.Value()->tighteningHistory[index].yMax << '\n';
        }
        return false;
    }
    const bool finalMatches = std::abs(reference.firstLoss.xMax
                                        - first.Value()->tighteningHistory.front().xMax)
            < 1.0e-5F
        && std::abs(reference.firstLoss.yMax
                    - first.Value()->tighteningHistory.front().yMax) < 1.0e-5F
        && std::abs(reference.finalLoss.xMax - first.Value()->loss.xMax) < 1.0e-5F
        && std::abs(reference.finalLoss.yMax - first.Value()->loss.yMax) < 1.0e-5F
        && std::abs(reference.transform.rotationRad
                    - first.Value()->transform.rotationRad) < 1.0e-7
        && std::abs(reference.transform.translation.x
                    - first.Value()->transform.translation.x) < 1.0e-7
        && std::abs(reference.transform.translation.y
                    - first.Value()->transform.translation.y) < 1.0e-7;
    if (!finalMatches)
    {
        output << std::setprecision(10)
               << "LBF search actual fixture rows:\n"
               << "final 42 0 " << first.Value()->samples << ' '
               << first.Value()->tighteningHistory.front().xMax << ' '
               << first.Value()->tighteningHistory.front().yMax << ' '
               << first.Value()->loss.xMax << ' ' << first.Value()->loss.yMax << ' '
               << first.Value()->transform.rotationRad << ' '
               << first.Value()->transform.translation.x << ' '
               << first.Value()->transform.translation.y << '\n';
        for (std::size_t index = 0;
             index < first.Value()->tighteningHistory.size();
             ++index)
        {
            output << "history 42 " << index << ' '
                   << first.Value()->tighteningHistory[index].xMax << ' '
                   << first.Value()->tighteningHistory[index].yMax << '\n';
        }
    }
    if (!Expect(finalMatches,
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
                    && std::isfinite(shortSearch.Value()->loss.Cost())
                    && std::isfinite(longSearch.Value()->loss.Cost()),
                output,
                "Different LBF sample budgets must return finite valid results"))
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
    return Expect(invalid.HasValue() && !invalid.Value().has_value(),
                  output,
                  "LBF search with zero samples must return no candidate without evaluation");
}
}

bool RunSamplingTests(std::ostream& output)
{
    return TestCoordinateDescentRadiansMatchRustReference(output)
        && TestRotationModes(output)
        && TestBestSamplesRotationBoundary(output)
        && TestUniformRectSampler(output)
        && TestUniformSamplerUpperBoundaryRustReference(output)
        && TestUniformSamplerContinuousGridRustReference(output)
        && TestUniformSamplerSingletonAxisRustReference(output)
        && TestUniformSamplerDegenerateBboxRustReference(output)
        && TestRustSamplingCoordinateDescentTrace(output)
        && TestLocalSearchSampler(output)
        && TestCoordinateDescentAxesAndBudget(output)
        && TestCoordinateDescentDefaultBudgetMatchesRust(output)
        && TestCoordinateDescentOutcomes(output)
        && TestCoordinateDescentApproximateTieMatchesRustReference(output)
        && TestLbfEvaluatorBasicCollisionAndLoss(output)
        && TestLbfEvaluatorUsesPointOfInaccessibility(output)
        && TestLbfEvaluatorHoleAndQualityFilter(output)
        && TestLbfEvaluatorSurrogatePath(output)
        && TestRustLbfSearchTrace(output)
        && TestLbfSearch(output);
}
