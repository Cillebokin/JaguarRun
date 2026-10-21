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
#include <string_view>
#include <utility>
#include <vector>

namespace
{
bool Expect(bool condition, std::ostream& output, const char* message);
float FloatFromBits(std::uint32_t bits) noexcept;
std::uint32_t FloatBits(float value) noexcept;

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

    const auto signedZeroSampler = sparrow::UniformRectSampler::Create(
        {0.0, 0.0, 0.0, 5.0}, item.Value(), {-0.0, 0.0, 10.0, 10.0});
    if (!Expect(signedZeroSampler.HasValue() && signedZeroSampler.Value().has_value(),
                output,
                "Signed-zero singleton sample bbox must be feasible"))
    {
        return false;
    }
    sparrow::SparrowRng signedZeroRng(42U);
    const jaguar::Transform2d signedZeroSample =
        signedZeroSampler.Value()->Sample(signedZeroRng);
    const float signedZeroX = static_cast<float>(signedZeroSample.translation.x);
    std::uint32_t signedZeroBits = 0U;
    std::memcpy(&signedZeroBits, &signedZeroX, sizeof(signedZeroX));
    if (signedZeroBits != 0x0000'0000U)
    {
        output << "signed-zero singleton X expected=0x00000000 actual=0x"
               << std::hex << signedZeroBits << std::dec << '\n';
        return Expect(false,
                      output,
                      "Signed-zero intersection must match Rust f32::max semantics");
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
    const auto overflowFloatBounds = sparrow::UniformRectSampler::Create(
        {0.0,
         0.0,
         static_cast<double>(std::numeric_limits<float>::max()) * 2.0,
         1.0},
        item.Value(),
        roomyContainerBounds);
    if (!Expect(overflowFloatBounds.HasValue()
                    && !overflowFloatBounds.Value().has_value(),
                output,
                "Bounds that become f32 infinity must match Rust no-fit semantics"))
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

    auto pinnedLocalSampler = sparrow::LocalSearchSampler::Create(
        noneItem.Value(),
        jaguar::Transform2d::FromRotationTranslation(
            0.0,
            {static_cast<double>(FloatFromBits(0x3f1500baU)),
             static_cast<double>(FloatFromBits(0x3f0f2398U))}),
        config,
        {0.0, 0.0, 30.0, 30.0});
    if (!Expect(pinnedLocalSampler.HasValue(),
                output,
                "Pinned Rust local normal fixture sampler creation failed"))
    {
        return false;
    }
    sparrow::SparrowRng pinnedLocalRng(42U);
    for (std::size_t draw = 0U; draw < 96U; ++draw)
    {
        pinnedLocalRng.NextU32();
    }
    const jaguar::Transform2d pinnedLocalSample =
        pinnedLocalSampler.Value().Sample(pinnedLocalRng);
    if (!Expect(FloatBits(static_cast<float>(pinnedLocalSample.translation.x))
                        == 0x3ccf2780U
                    && FloatBits(static_cast<float>(pinnedLocalSample.translation.y))
                        == 0x3f1d6982U,
                output,
                "LocalSearchSampler initial normal parameters must match pinned Rust f32 arithmetic"))
    {
        output << "  expected=(3ccf2780,3f1d6982) actual=("
               << std::hex
               << FloatBits(static_cast<float>(pinnedLocalSample.translation.x))
               << ','
               << FloatBits(static_cast<float>(pinnedLocalSample.translation.y))
               << std::dec << ")\n";
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
    const char* fixtureName,
    std::ostream& output)
{
    std::ifstream traceFile;
    for (const std::string& path : {
             std::string("SparrowCore/Tests/Fixtures/") + fixtureName,
             std::string("../SparrowCore/Tests/Fixtures/") + fixtureName,
             std::string("../JaguarRun/SparrowCore/Tests/Fixtures/") + fixtureName,
             std::string("..\\SparrowCore\\Tests\\Fixtures\\") + fixtureName})
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
            || FloatBits(actualEvaluation.loss) == FloatBits(expectedEvaluation.loss));
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

bool TestUniformSamplerNarrowIntervalRustReference(std::ostream& output)
{
    std::ifstream referenceFile;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/UniformSamplerNarrowIntervalRust.tsv",
             "../SparrowCore/Tests/Fixtures/UniformSamplerNarrowIntervalRust.tsv",
             "../JaguarRun/SparrowCore/Tests/Fixtures/UniformSamplerNarrowIntervalRust.tsv",
             "..\\SparrowCore\\Tests\\Fixtures\\UniformSamplerNarrowIntervalRust.tsv"})
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
                "Rust one-ULP sampler reference must be readable"))
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
                "Rust one-ULP sampler input must be readable"))
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
                "Rust one-ULP sampler input must import in C++"))
    {
        return false;
    }

    constexpr std::uint64_t SEED = 42U;
    const float low = FloatFromBits(0x3f800000U);
    const float high = FloatFromBits(0x3f800001U);
    const jaguar::Rect2d sampleBounds{
        static_cast<double>(low), 1.0, static_cast<double>(high), 2.0};
    const jaguar::Rect2d containerBounds{0.0, 0.0, 10.0, 10.0};
    const auto sampler = sparrow::UniformRectSampler::Create(
        sampleBounds, instance.Value().GetItem(0U), containerBounds);
    if (!Expect(sampler.HasValue() && sampler.Value().has_value(),
                output,
                "Rust one-ULP sampler must be constructible in C++"))
    {
        return false;
    }

    sparrow::SparrowRng rng(SEED);
    std::string line;
    std::size_t sampleCount = 0U;
    bool foundRngAfter = false;
    std::uint64_t expectedNextU64 = 0U;
    while (std::getline(referenceFile, line))
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
        if (record == "sample")
        {
            std::uint64_t seed = 0U;
            std::size_t ordinal = 0U;
            std::uint32_t expectedX = 0U;
            std::uint32_t expectedY = 0U;
            std::string extra;
            if (foundRngAfter
                || !(row >> seed >> ordinal >> std::hex >> expectedX >> expectedY)
                || (row >> extra)
                || seed != SEED || ordinal != sampleCount)
            {
                return Expect(false,
                              output,
                              "Rust one-ULP sample row must match its ordered schema");
            }
            const auto sample = sampler.Value()->Sample(rng);
            const float x = static_cast<float>(sample.translation.x);
            const float y = static_cast<float>(sample.translation.y);
            if (!Expect(FloatBits(x) == expectedX && FloatBits(y) == expectedY,
                        output,
                        "C++ one-ULP sample differs from Rust raw bits"))
            {
                output << "sample=" << sampleCount << " expected=(0x"
                       << std::hex << expectedX << ",0x" << expectedY
                       << ") actual=(0x" << FloatBits(x) << ",0x" << FloatBits(y)
                       << ")\n";
                return false;
            }
            ++sampleCount;
        }
        else if (record == "rng_after")
        {
            std::string extra;
            if (foundRngAfter || sampleCount != 16U
                || !(row >> std::hex >> expectedNextU64) || (row >> extra))
            {
                return Expect(false,
                              output,
                              "Rust one-ULP RNG-after row must be final and well formed");
            }
            foundRngAfter = true;
        }
        else
        {
            return Expect(false, output, "unknown Rust one-ULP sampler row");
        }
    }
    return Expect(sampleCount == 16U && foundRngAfter
                      && rng.NextU64() == expectedNextU64,
                  output,
                  "C++ one-ULP sampler samples or post-sample RNG state differ from Rust");
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
    std::array<std::uint64_t, 4> expectedFollowing{};
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
        if (!(row >> seed >> std::hex >> expectedRotationBits))
        {
            return Expect(false,
                          output,
                          "Rust continuous-grid row must match its schema");
        }
        for (std::uint64_t& expected : expectedFollowing)
        {
            if (!(row >> std::hex >> expected))
            {
                return Expect(false,
                              output,
                              "Rust continuous-grid row must contain four post-sample RNG words");
            }
        }
        if (row >> extra)
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
        for (std::size_t index = 0U; index < expectedFollowing.size(); ++index)
        {
            const std::uint64_t actual = rng.NextU64();
            if (actual != expectedFollowing[index])
            {
                output << "continuous sampler seed=" << seed << " following="
                       << index << " expected=0x" << std::hex
                       << expectedFollowing[index] << " actual=0x" << actual
                       << std::dec << '\n';
                return Expect(false,
                              output,
                              "continuous sampler post-sample RNG differs from Rust reference");
            }
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

bool TestRustLbfSearchTrace(
    std::ostream& output,
    const char* inputFixtureName,
    const char* traceFixtureName,
    double stripWidth,
    std::size_t sampleBudget,
    std::size_t coordinateDescentCapacity)
{
    std::vector<sparrow::LbfSearchTraceEvent> expected;
    std::uint64_t expectedNextU64AfterSearch = 0U;
    if (!ReadLbfSearchRustTrace(expected,
                                expectedNextU64AfterSearch,
                                traceFixtureName,
                                output))
    {
        return false;
    }

    std::ifstream instanceFile;
    for (const std::string& path : {
             std::string("SparrowCore/Tests/Fixtures/") + inputFixtureName,
             std::string("../SparrowCore/Tests/Fixtures/") + inputFixtureName,
             std::string("../JaguarRun/SparrowCore/Tests/Fixtures/") + inputFixtureName,
             std::string("..\\SparrowCore\\Tests\\Fixtures\\") + inputFixtureName})
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
    if (!Expect(strip.SetWidth(stripWidth).HasValue(),
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
    config.samplesPerItem = sampleBudget;
    config.nCoordinateDescents = coordinateDescentCapacity;
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

bool TestCoordinateDescentEvaluationBudgetBoundaries(std::ostream& output)
{
    const jaguar::Transform2d initialTransform =
        jaguar::Transform2d::FromRotationTranslation(0.0, {10.0, 20.0});
    const sparrow::SampleEval initialEval{
        sparrow::SampleKind::SAMPLE_KIND_CLEAR, 0.0F};
    sparrow::CoordinateDescentConfig config;
    config.translationStepInit = 1.0F;
    config.translationStepLimit = 0.5F;
    config.rotationStepInit = 0.0F;
    config.rotationStepLimit = 0.0F;
    config.rotationWiggle = false;

    config.maxEvaluations = 0U;
    CoordinateTestEvaluator zeroBudgetEvaluator(CoordinateTestEvaluator::Mode::Worse);
    sparrow::SparrowRng zeroBudgetRng(42U);
    sparrow::SparrowRng zeroBudgetControlRng(42U);
    const auto zeroBudgetResult = sparrow::RefineCoordinateDescent(
        {initialTransform, initialEval},
        zeroBudgetEvaluator,
        config,
        zeroBudgetRng);
    if (!Expect(!zeroBudgetResult.HasValue()
                    && zeroBudgetResult.GetError().code == jaguar::ErrorCode::TimeLimit,
                output,
                "Zero coordinate descent budget must return TimeLimit"))
    {
        return false;
    }
    if (!Expect(zeroBudgetEvaluator.EvaluationCount() == 0U
                    && zeroBudgetRng.State() == zeroBudgetControlRng.State(),
                output,
                "Zero coordinate descent budget must not evaluate or consume RNG"))
    {
        return false;
    }

    config.maxEvaluations = 1U;
    CoordinateTestEvaluator oneBudgetEvaluator(CoordinateTestEvaluator::Mode::Worse);
    sparrow::SparrowRng oneBudgetRng(42U);
    sparrow::SparrowRng oneBudgetControlRng(42U);
    static_cast<void>(oneBudgetControlRng.UniformIndex(4U));
    const auto oneBudgetResult = sparrow::RefineCoordinateDescent(
        {initialTransform, initialEval},
        oneBudgetEvaluator,
        config,
        oneBudgetRng);
    if (!Expect(!oneBudgetResult.HasValue()
                    && oneBudgetResult.GetError().code == jaguar::ErrorCode::TimeLimit,
                output,
                "One coordinate descent evaluation budget must return TimeLimit"))
    {
        return false;
    }
    return Expect(oneBudgetEvaluator.EvaluationCount() == 1U
                      && oneBudgetRng.State() == oneBudgetControlRng.State(),
                  output,
                  "One evaluation budget must stop between candidates without extra RNG use");
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

bool TestCoordinateDescentTieMatchesRustReference(
    std::ostream& output,
    const char* fixtureName,
    std::uint32_t initialLossBits,
    const char* tieDescription)
{
    std::ifstream referenceFile;
    const std::array<std::string, 4> paths = {
        std::string("SparrowCore/Tests/Fixtures/") + fixtureName,
        std::string("../SparrowCore/Tests/Fixtures/") + fixtureName,
        std::string("../JaguarRun/SparrowCore/Tests/Fixtures/") + fixtureName,
        std::string("..\\SparrowCore\\Tests\\Fixtures\\") + fixtureName};
    for (const std::string& path : paths)
    {
        referenceFile.open(path.c_str());
        if (referenceFile.is_open())
        {
            break;
        }
        referenceFile.clear();
    }
    if (!Expect(referenceFile.is_open(), output,
                "Rust coordinate-descent tie trace must be readable"))
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
        sparrow::SampleKind::SAMPLE_KIND_CLEAR, FloatFromBits(initialLossBits)};
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
    const std::string message =
        std::string("SparrowCore coordinate descent ") + tieDescription
        + " tie trace must match Rust";
    return Expect(actualRows == expectedRows, output, message.c_str());
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

jaguar::Container MakeLbfContainerWithBounds(const jaguar::Rect2d& bounds)
{
    const jaguar::SimplePolygon outer = MakePolygon({
        {bounds.xMin, bounds.yMin},
        {bounds.xMax, bounds.yMin},
        {bounds.xMax, bounds.yMax},
        {bounds.xMin, bounds.yMax}});
    jaguar::CDEConfig config{};
    config.quadtreeDepth = 4;
    config.cdThreshold = 8;
    const auto container = jaguar::Container::Create(
        9103,
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
    jaguar::SurrogateConfig surrogateConfig = {},
    jaguar::RotationRange allowedRotation = {})
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
        minQuality,
        std::move(allowedRotation));
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

bool TestLbfRuntimeBudgetSplit(std::ostream& output)
{
    const jaguar::Container container = MakePlainEvaluatorContainer();
    const jaguar::Item item = MakeEvaluatorItem({
        {0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}, {0.0, 1.0}});
    const jaguar::NoFilter noFilter;
    const jaguar::Layout layout(container);

    sparrow::LbfConfig config;
    config.seed = 42U;
    config.samplesPerItem = 64U;
    config.localSearchFraction = 0.25;

    std::size_t uniformEvaluations = 0U;
    std::size_t localEvaluations = 0U;
    std::size_t resultEvaluationCount = 0U;
    const jaguar::Rect2d containerBounds =
        layout.GetContainer().CollisionShape().BoundingBox();
    const jaguar::Rect2d itemBounds = item.CollisionShape().BoundingBox();
    const float sampleXMin = static_cast<float>(containerBounds.xMin);
    const float sampleYMin = static_cast<float>(containerBounds.yMin);
    const float sampleYMax = static_cast<float>(containerBounds.yMax);
    float activeSampleXMax = static_cast<float>(containerBounds.xMax);
    bool uniformSamplesRespectTightenedBounds = true;
    bool sawTightenedUniformBounds = false;
    bool sawUniformSampleOutsideFitTranslations = false;
    sparrow::SparrowRng rng(config.seed);
    const auto result = sparrow::SearchLbfWithTrace(
        layout,
        item,
        config,
        rng,
        noFilter,
        [&](const sparrow::LbfSearchTraceEvent& event) {
            if (event.type == sparrow::LbfSearchTraceEvent::Type::Evaluation)
            {
                if (event.phase == sparrow::LbfSearchTracePhase::ContainerSample)
                {
                    ++uniformEvaluations;
                    const float x = static_cast<float>(event.transform.translation.x);
                    const float y = static_cast<float>(event.transform.translation.y);
                    uniformSamplesRespectTightenedBounds =
                        uniformSamplesRespectTightenedBounds
                        && x >= sampleXMin && x <= activeSampleXMax
                        && y >= sampleYMin && y <= sampleYMax;
                    sawUniformSampleOutsideFitTranslations =
                        sawUniformSampleOutsideFitTranslations
                        || x > static_cast<float>(containerBounds.xMax - itemBounds.Width())
                        || y > static_cast<float>(containerBounds.yMax - itemBounds.Height());
                }
                else if (event.phase == sparrow::LbfSearchTracePhase::LocalSearch)
                {
                    ++localEvaluations;
                }
            }
            else if (event.type == sparrow::LbfSearchTraceEvent::Type::Report
                     && event.phase
                            == sparrow::LbfSearchTracePhase::ContainerSample
                     && event.accepted)
            {
                const auto transformed = item.CollisionShape().TransformedRustF32(
                    event.transform);
                if (transformed.HasValue())
                {
                    const sparrow::LbfLoss loss =
                        sparrow::LbfLoss::FromBoundingBox(
                            transformed.Value().BoundingBox());
                    const jaguar::Rect2d tightened = loss.TightenSampleBBox(
                        {sampleXMin,
                         sampleYMin,
                         static_cast<double>(activeSampleXMax),
                         static_cast<double>(sampleYMax)});
                    const float tightenedXMax = static_cast<float>(tightened.xMax);
                    sawTightenedUniformBounds =
                        sawTightenedUniformBounds
                        || tightenedXMax < activeSampleXMax;
                    activeSampleXMax = tightenedXMax;
                }
            }
            else if (event.type == sparrow::LbfSearchTraceEvent::Type::Result)
            {
                resultEvaluationCount = event.evaluationCount;
            }
        });

    if (!Expect(result.HasValue() && result.Value().has_value(),
                output,
                "fixed-seed LBF runtime budget search must find a placement"))
    {
        return false;
    }

    constexpr std::size_t expectedUniformEvaluations = 48U;
    constexpr std::size_t expectedLocalEvaluations = 16U;
    constexpr std::size_t expectedTotalEvaluations = 64U;
    const bool budgetMatches = uniformEvaluations == expectedUniformEvaluations
        && localEvaluations == expectedLocalEvaluations
        && resultEvaluationCount == expectedTotalEvaluations
        && result.Value()->samples == expectedTotalEvaluations
        && uniformSamplesRespectTightenedBounds
        && sawTightenedUniformBounds
        && sawUniformSampleOutsideFitTranslations;
    if (!budgetMatches)
    {
        output << "LBF fixed-seed runtime budget: seed=" << config.seed
               << " samplesPerItem=" << config.samplesPerItem
               << " localSearchFraction=" << config.localSearchFraction
               << " uniform=" << uniformEvaluations
               << " local=" << localEvaluations
               << " result=" << resultEvaluationCount
               << " samples=" << result.Value()->samples
               << " sampleBounds=" << uniformSamplesRespectTightenedBounds
               << " tightened=" << sawTightenedUniformBounds
               << " outsideFit=" << sawUniformSampleOutsideFitTranslations << '\n';
    }
    return Expect(budgetMatches,
                  output,
                  "fixed-seed LBF runtime must apply Rust's 48/16 sample split");
}

bool TestLbfSppSearchUsesCdeBounds(std::ostream& output)
{
    std::ifstream instanceFile;
    for (const char* path : {
             "JaguarCore/Tests/Fixtures/RustReference/optimizer_spp_instance.json",
             "../JaguarCore/Tests/Fixtures/RustReference/optimizer_spp_instance.json",
             "../JaguarRun/JaguarCore/Tests/Fixtures/RustReference/optimizer_spp_instance.json",
             "..\\JaguarCore\\Tests\\Fixtures\\RustReference\\optimizer_spp_instance.json"})
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
                "fixed-seed SPP CDE-bounds fixture must be readable"))
    {
        return false;
    }

    const std::string instanceJson{
        std::istreambuf_iterator<char>(instanceFile),
        std::istreambuf_iterator<char>()};
    jaguar::ImporterConfig importerConfig;
    importerConfig.cdeConfig.quadtreeDepth = 4U;
    importerConfig.cdeConfig.cdThreshold = 32U;
    importerConfig.cdeConfig.itemSurrogateConfig.poleLimits =
        {{{10, 0.0}, {0, 0.0}, {0, 0.0}}};
    importerConfig.cdeConfig.itemSurrogateConfig.failFastPoleAreaRatio = 0.5;
    importerConfig.cdeConfig.itemSurrogateConfig.failFastPiers = 0U;
    importerConfig.shapeModifyConfig.simplifyTolerance.reset();
    importerConfig.shapeModifyConfig.narrowConcavityCutoff.reset();
    importerConfig.shapeModifyConfig.offset.reset();
    const jaguar::Importer importer(importerConfig);
    const auto instance = importer.ImportSPInstanceJson(instanceJson);
    if (!Expect(instance.HasValue(),
                output,
                "fixed-seed SPP CDE-bounds fixture must import"))
    {
        return false;
    }

    const auto container = instance.Value().BaseStrip().CreateContainer();
    if (!Expect(container.HasValue(),
                output,
                "fixed-seed SPP base strip must create a container"))
    {
        return false;
    }
    const jaguar::Rect2d& bounds = container.Value().Bounds();
    const auto engine = container.Value().CreateCollisionEngine();
    if (!Expect(engine != nullptr
                    && FloatBits(static_cast<float>(bounds.xMin)) == 0xbfeaaaabU
                    && FloatBits(static_cast<float>(bounds.yMin)) == 0x00000000U
                    && FloatBits(static_cast<float>(bounds.xMax)) == 0x40855555U
                    && FloatBits(static_cast<float>(bounds.yMax)) == 0x40c00000U
                    && FloatBits(static_cast<float>(engine->Bounds().xMin))
                        == FloatBits(static_cast<float>(bounds.xMin))
                    && FloatBits(static_cast<float>(engine->Bounds().yMin))
                        == FloatBits(static_cast<float>(bounds.yMin))
                    && FloatBits(static_cast<float>(engine->Bounds().xMax))
                        == FloatBits(static_cast<float>(bounds.xMax))
                    && FloatBits(static_cast<float>(engine->Bounds().yMax))
                        == FloatBits(static_cast<float>(bounds.yMax)),
                output,
                "container and collision-engine bounds must match the pinned Rust CDE bbox"))
    {
        return false;
    }

    std::size_t firstItemIndex = 0U;
    for (std::size_t index = 1U; index < instance.Value().ItemCount(); ++index)
    {
        if (instance.Value().GetItem(index).CollisionShape().Diameter()
            > instance.Value().GetItem(firstItemIndex).CollisionShape().Diameter())
        {
            firstItemIndex = index;
        }
    }

    jaguar::Layout layout(container.Value());
    const jaguar::NoFilter noFilter;
    sparrow::LbfConfig config;
    config.seed = 42U;
    config.samplesPerItem = 64U;
    config.localSearchFraction = 0.25;
    config.useSparrowConstructorOrder = false;
    sparrow::SparrowRng rng(config.seed);
    std::optional<jaguar::Transform2d> firstUniformSample;
    std::size_t uniformEvaluations = 0U;
    const auto search = sparrow::SearchLbfWithTrace(
        layout,
        instance.Value().GetItem(firstItemIndex),
        config,
        rng,
        noFilter,
        [&](const sparrow::LbfSearchTraceEvent& event) {
            if (event.type == sparrow::LbfSearchTraceEvent::Type::Evaluation
                && event.phase == sparrow::LbfSearchTracePhase::ContainerSample)
            {
                ++uniformEvaluations;
                if (!firstUniformSample.has_value())
                {
                    firstUniformSample = event.transform;
                }
            }
        });
    if (!Expect(search.HasValue(),
                output,
                "fixed-seed SPP first LBF search must run"))
    {
        return false;
    }

    const bool firstSampleMatches = firstUniformSample.has_value()
        && uniformEvaluations == 48U
        && FloatBits(static_cast<float>(firstUniformSample->rotationRad))
            == 0x40490fdbU
        && FloatBits(static_cast<float>(firstUniformSample->translation.x))
            == 0x3da30150U
        && FloatBits(static_cast<float>(firstUniformSample->translation.y))
            == 0x40bce85dU;
    if (!firstSampleMatches && firstUniformSample.has_value())
    {
        output << "SPP first uniform sample bits: rotation=0x" << std::hex
               << FloatBits(static_cast<float>(firstUniformSample->rotationRad))
               << " x=0x"
               << FloatBits(static_cast<float>(firstUniformSample->translation.x))
               << " y=0x"
               << FloatBits(static_cast<float>(firstUniformSample->translation.y))
               << std::dec << " uniform_count=" << uniformEvaluations << '\n';
    }
    return Expect(firstSampleMatches,
                  output,
                  "fixed-seed SPP first uniform sample must match pinned CDE-bbox trace");
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

    sparrow::LbfConfig zeroLocalBudgetConfig = config;
    zeroLocalBudgetConfig.samplesPerItem = 1U;
    zeroLocalBudgetConfig.localSearchFraction = 0.25;
    zeroLocalBudgetConfig.nCoordinateDescents = 0U;
    sparrow::SparrowRng zeroLocalBudgetRng(zeroLocalBudgetConfig.seed);
    const auto zeroLocalBudgetResult = sparrow::SearchLbf(
        emptyLayout, item, zeroLocalBudgetConfig, zeroLocalBudgetRng, noFilter);
    if (!Expect(zeroLocalBudgetResult.HasValue()
                    && zeroLocalBudgetResult.Value().has_value()
                    && zeroLocalBudgetResult.Value()->samples == 1U,
                output,
                "a one-sample LBF budget must run one uniform sample and no local samples"))
    {
        return false;
    }

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
    if (!Expect(!first.Value()->tighteningHistory.empty()
                    && first.Value()->tighteningHistory.back().xMax
                           == first.Value()->loss.xMax
                    && first.Value()->tighteningHistory.back().yMax
                           == first.Value()->loss.yMax,
                output,
                "LBF search history must end at the selected bounding-box loss"))
    {
        return false;
    }
    for (std::size_t index = 1U;
         index < first.Value()->tighteningHistory.size();
         ++index)
    {
        if (!Expect(first.Value()->tighteningHistory[index].Cost()
                        < first.Value()->tighteningHistory[index - 1U].Cost(),
                    output,
                    "LBF search history must contain strictly improving bbox losses"))
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

std::ifstream OpenLbfSamplerFixture(const char* filename)
{
    std::ifstream input;
    for (const std::string& path : {
             std::string("SparrowCore/Tests/Fixtures/") + filename,
             std::string("../SparrowCore/Tests/Fixtures/") + filename,
             std::string("..\\SparrowCore\\Tests\\Fixtures\\") + filename})
    {
        input.open(path);
        if (input.is_open())
        {
            return input;
        }
        input.clear();
    }
    return input;
}

bool TestLbfQualityZoneFilterMatchesRustReference(std::ostream& output)
{
    std::ifstream input = OpenLbfSamplerFixture("LbfQualityZoneFilterRustTrace.tsv");
    if (!Expect(input.is_open(),
                output,
                "pinned Rust LBF quality-zone fixture must be readable"))
    {
        return false;
    }

    struct Case
    {
        const char* name;
        double x;
        double y;
    };
    constexpr std::array<Case, 3> cases{{
        {"hole", 8.5, 8.5},
        {"below-min-quality", 1.0, 1.0},
        {"equal-to-min-quality", 15.5, 0.5},
    }};

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

    std::size_t caseIndex = 0U;
    std::string line;
    while (std::getline(input, line))
    {
        if (line.empty() || line.front() == '#')
        {
            continue;
        }

        std::istringstream row(line);
        std::string caseName;
        unsigned int surrogateCollision = 0U;
        unsigned int polygonCollision = 0U;
        unsigned int rustInvalid = 0U;
        if (!Expect(static_cast<bool>(row >> caseName >> surrogateCollision
                                      >> polygonCollision >> rustInvalid),
                    output,
                    "pinned Rust LBF quality-zone row must have four fields"))
        {
            return false;
        }
        if (!Expect(caseIndex < cases.size(),
                    output,
                    "pinned Rust LBF quality-zone fixture has unexpected extra rows"))
        {
            return false;
        }
        const Case& testCase = cases[caseIndex];
        if (!Expect(caseName == testCase.name,
                    output,
                    "pinned Rust LBF quality-zone cases must remain in fixture order"))
        {
            return false;
        }
        if (!Expect(rustInvalid == (surrogateCollision || polygonCollision),
                    output,
                    "pinned Rust LBF invalid classification must match its collision phases"))
        {
            return false;
        }

        const auto actual = evaluator.Evaluate(
            jaguar::Transform2d::FromRotationTranslation(0.0, {testCase.x, testCase.y}),
            std::nullopt);
        const bool actualInvalid = actual.kind == sparrow::SampleKind::SAMPLE_KIND_INVALID;
        if (!Expect(actualInvalid == (rustInvalid != 0U),
                    output,
                    "C++ LBF quality-zone filtering must match the pinned Rust fixture"))
        {
            return false;
        }
        ++caseIndex;
    }

    return Expect(caseIndex == cases.size(),
                  output,
                  "pinned Rust LBF quality-zone fixture must contain all three cases");
}

std::string LbfSamplerFixtureField(
    const std::string& contents,
    std::string_view key)
{
    const std::size_t start = contents.find(key);
    if (start == std::string::npos)
    {
        return {};
    }
    const std::size_t valueStart = start + key.size();
    const std::size_t end = contents.find_first_of(" \t\r\n", valueStart);
    return contents.substr(valueStart,
                           end == std::string::npos
                               ? std::string::npos
                               : end - valueStart);
}

std::string LbfSamplerBitsText(float value)
{
    std::ostringstream text;
    text << std::hex << std::nouppercase << std::setfill('0') << std::setw(8)
         << FloatBits(value);
    return text.str();
}

std::vector<std::string> LbfSamplerTsvFields(const std::string& line)
{
    std::vector<std::string> fields;
    std::istringstream row(line);
    std::string field;
    while (std::getline(row, field, '\t'))
    {
        fields.push_back(field);
    }
    return fields;
}

bool TestLbfUniformSamplerNarrowBoundsRustReference(std::ostream& output)
{
    std::ifstream input = OpenLbfSamplerFixture(
        "LbfSamplerBoundaryAdjacentXRustTrace.tsv");
    if (!Expect(input.is_open(),
                output,
                "pinned Rust narrow LBF sampler fixture cannot be opened"))
    {
        return false;
    }
    std::ostringstream fixtureText;
    fixtureText << input.rdbuf();
    const std::string contents = fixtureText.str();
    const std::string expectedNext =
        LbfSamplerFixtureField(contents, "next_rng_u64=");
    const std::string expectedXMin =
        LbfSamplerFixtureField(contents, "x_min_bits=");
    const std::string expectedXMax =
        LbfSamplerFixtureField(contents, "x_max_bits=");
    if (!Expect(expectedNext == "1d794276a8ce5c1f"
                    && expectedXMin == "4e800000"
                    && expectedXMax == "4e800010",
                output,
                "pinned Rust narrow LBF sampler fixture must lock its bounds and RNG tail"))
    {
        return false;
    }

    std::vector<std::vector<std::string>> expectedRows;
    std::istringstream lines(contents);
    std::string line;
    while (std::getline(lines, line))
    {
        if (line.empty() || line.front() == '#')
        {
            continue;
        }
        expectedRows.push_back(LbfSamplerTsvFields(line));
    }
    if (!Expect(expectedRows.size() == 48U,
                output,
                "pinned Rust narrow LBF sampler fixture must contain 48 draws"))
    {
        return false;
    }

    const float xMin = FloatFromBits(0x4e800000U);
    const float xMax = FloatFromBits(0x4e800010U);
    const jaguar::Rect2d bounds{
        static_cast<double>(xMin), 0.0, static_cast<double>(xMax), 512.0};
    const jaguar::Item item = MakeEvaluatorItem({
        {0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}, {0.0, 1.0}});
    sparrow::SparrowRng rng(42U);
    const auto samples = sparrow::SampleLbfUniformForTrace(
        bounds, item, rng, expectedRows.size());
    if (!Expect(samples.HasValue()
                    && samples.Value().size() == expectedRows.size(),
                output,
                "private C++ LBF sampler must construct and emit all narrow-range draws"))
    {
        return false;
    }

    bool sawUpperEndpoint = false;
    for (std::size_t index = 0U; index < expectedRows.size(); ++index)
    {
        const auto& fields = expectedRows[index];
        if (!Expect(fields.size() == 4U
                        && fields[0] == std::to_string(index),
                    output,
                    "pinned Rust narrow sampler rows must have ordinal and three f32 values"))
        {
            return false;
        }
        const jaguar::Transform2d& sample = samples.Value()[index];
        const std::array<std::string, 3U> actual{
            LbfSamplerBitsText(static_cast<float>(sample.rotationRad)),
            LbfSamplerBitsText(static_cast<float>(sample.translation.x)),
            LbfSamplerBitsText(static_cast<float>(sample.translation.y))};
        if (!Expect(actual[0] == fields[1]
                        && actual[1] == fields[2]
                        && actual[2] == fields[3],
                    output,
                    "private C++ LBF sampler draw must match pinned Rust raw f32 bits"))
        {
            output << "  ordinal=" << index << " Rust=" << fields[1] << ','
                   << fields[2] << ',' << fields[3] << " C++=" << actual[0]
                   << ',' << actual[1] << ',' << actual[2] << '\n';
            return false;
        }
        sawUpperEndpoint |= FloatBits(static_cast<float>(sample.translation.x))
            == 0x4e800010U;
    }

    std::ostringstream next;
    next << std::hex << std::nouppercase << std::setfill('0') << std::setw(16)
         << rng.NextU64();
    return Expect(sawUpperEndpoint && next.str() == expectedNext,
                  output,
                  "narrow LBF sampling must preserve the rounded upper endpoint and Rust RNG continuation");
}

bool TestLbfUniformSamplerInvalidBoundsMapping(std::ostream& output)
{
    std::ifstream input = OpenLbfSamplerFixture(
        "LbfSamplerBoundaryInvalidBoundsRustTrace.tsv");
    if (!Expect(input.is_open(),
                output,
                "pinned Rust invalid LBF sampler bounds fixture cannot be opened"))
    {
        return false;
    }
    std::ostringstream fixtureText;
    fixtureText << input.rdbuf();
    const std::string contents = fixtureText.str();
    std::vector<std::vector<std::string>> expectedRows;
    std::istringstream lines(contents);
    std::string line;
    while (std::getline(lines, line))
    {
        if (line.empty() || line.front() == '#')
        {
            continue;
        }
        expectedRows.push_back(LbfSamplerTsvFields(line));
    }
    if (!Expect(expectedRows.size() == 4U,
                output,
                "pinned Rust invalid bounds fixture must contain four constructor exits"))
    {
        return false;
    }

    const double infinity = std::numeric_limits<double>::infinity();
    const std::array<std::pair<const char*, jaguar::Rect2d>, 4U> cases{{
        {"x-singleton", {1.0, 0.0, 1.0, 2.0}},
        {"y-singleton", {1.0, 0.0, 2.0, 0.0}},
        {"x-reversed", {2.0, 0.0, 1.0, 2.0}},
        {"x-nonfinite", {0.0, 0.0, infinity, 2.0}},
    }};
    const jaguar::Item item = MakeEvaluatorItem({
        {0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}, {0.0, 1.0}});
    for (std::size_t index = 0U; index < cases.size(); ++index)
    {
        const auto& expected = expectedRows[index];
        if (!Expect(expected.size() == 3U
                        && expected[0] == cases[index].first
                        && expected[1] == "panic",
                    output,
                    "pinned Rust invalid bound row must record its panic channel"))
        {
            return false;
        }
        sparrow::SparrowRng rng(42U);
        const auto before = rng.State();
        const auto result = sparrow::SampleLbfUniformForTrace(
            cases[index].second, item, rng, 1U);
        if (!Expect(!result.HasValue()
                        && result.GetError().code == jaguar::ErrorCode::InvalidInput,
                    output,
                    "C++ LBF sampler must map invalid constructor bounds to InvalidInput"))
        {
            output << "  case=" << cases[index].first << '\n';
            return false;
        }
        if (!Expect(expected[2] == (index == 3U ? "NonFinite" : "EmptyRange")
                        && rng.State() == before,
                    output,
                    "invalid LBF bounds must preserve Rust panic class and consume no RNG in C++"))
        {
            output << "  case=" << cases[index].first
                   << " Rust=" << expected[2] << '\n';
            return false;
        }
    }
    return true;
}

bool TestLbfSearchTighteningErrorAgainstRustReference(std::ostream& output)
{
    std::ifstream input = OpenLbfSamplerFixture(
        "LbfSamplerBoundaryTightenInvalidRustTrace.tsv");
    if (!Expect(input.is_open(),
                output,
                "pinned Rust LBF tightening-error fixture cannot be opened"))
    {
        return false;
    }
    std::ostringstream fixtureText;
    fixtureText << input.rdbuf();
    const std::string contents = fixtureText.str();
    const auto fieldEquals = [&contents](
                                 std::string_view key,
                                 std::string_view expected) {
        return LbfSamplerFixtureField(contents, key) == expected;
    };
    if (!Expect(fieldEquals("direct_status=", "panic")
                    && fieldEquals("traced_status=", "panic")
                    && fieldEquals("panic_kind=", "EmptyRange")
                    && fieldEquals("first_accepted=", "1"),
                output,
                "pinned Rust LBF reference must lock accepted-candidate panic semantics"))
    {
        return false;
    }

    const jaguar::Rect2d bounds{100.0, -1000.0, 200.0, -900.0};
    const jaguar::Container container = MakeLbfContainerWithBounds(bounds);
    const jaguar::Item item = MakeEvaluatorItem({
        {0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}, {0.0, 1.0}});
    const jaguar::NoFilter noFilter;
    const jaguar::Layout layout(container);
    sparrow::LbfConfig config;
    config.seed = 42U;
    config.samplesPerItem = 64U;
    config.localSearchFraction = 0.25;
    sparrow::SparrowRng rng(config.seed);
    std::vector<sparrow::LbfSearchTraceEvent> events;
    const auto result = sparrow::SearchLbfWithTrace(
        layout,
        item,
        config,
        rng,
        noFilter,
        [&events](const sparrow::LbfSearchTraceEvent& event) {
            events.push_back(event);
        });
    if (!Expect(!result.HasValue()
                    && result.GetError().code == jaguar::ErrorCode::InvalidInput
                    && result.GetError().message
                        == "LBF uniform sampler bounds must be finite and non-degenerate",
                output,
                "C++ LBF search must report structured InvalidInput after invalid tightening"))
    {
        return false;
    }
    if (!Expect(events.size() == 2U
                    && events[0].type
                        == sparrow::LbfSearchTraceEvent::Type::Evaluation
                    && events[1].type
                        == sparrow::LbfSearchTraceEvent::Type::Report
                    && events[1].accepted,
                output,
                "invalid tightening must occur after the first accepted uniform candidate"))
    {
        return false;
    }
    const auto& first = events[0].transform;
    if (!Expect(LbfSamplerBitsText(static_cast<float>(first.rotationRad))
                        == LbfSamplerFixtureField(contents, "first_rotation_bits=")
                    && LbfSamplerBitsText(static_cast<float>(first.translation.x))
                        == LbfSamplerFixtureField(contents, "first_x_bits=")
                    && LbfSamplerBitsText(static_cast<float>(first.translation.y))
                        == LbfSamplerFixtureField(contents, "first_y_bits="),
                output,
                "first accepted LBF candidate must match the Rust panic trace"))
    {
        output << "  Rust="
               << LbfSamplerFixtureField(contents, "first_rotation_bits=") << ','
               << LbfSamplerFixtureField(contents, "first_x_bits=") << ','
               << LbfSamplerFixtureField(contents, "first_y_bits=")
               << " C++="
               << LbfSamplerBitsText(static_cast<float>(first.rotationRad)) << ','
               << LbfSamplerBitsText(static_cast<float>(first.translation.x)) << ','
               << LbfSamplerBitsText(static_cast<float>(first.translation.y)) << '\n';
        return false;
    }
    const auto transformed = item.CollisionShape().TransformedRustF32(first);
    if (!Expect(transformed.HasValue(),
                output,
                "first accepted LBF candidate must transform before tightening"))
    {
        return false;
    }
    // SearchLbf uses the transformed bbox loss; SampleEval.loss is a separate
    // POI-weighted heuristic and is not the search's tightening cost.
    const sparrow::LbfLoss candidateLoss = sparrow::LbfLoss::FromBoundingBox(
        transformed.Value().BoundingBox());
    if (!Expect(LbfSamplerBitsText(candidateLoss.Cost())
                    == LbfSamplerFixtureField(contents, "first_cost_bits="),
                output,
                "accepted LBF bbox loss must match the Rust panic trace"))
    {
        output << "  Rust="
               << LbfSamplerFixtureField(contents, "first_cost_bits=")
               << " C++=" << LbfSamplerBitsText(candidateLoss.Cost()) << '\n';
        return false;
    }
    const auto tightened = candidateLoss.TightenSampleBBox(bounds);
    if (!Expect(LbfSamplerBitsText(static_cast<float>(tightened.xMin))
                        == LbfSamplerFixtureField(contents, "tightened_x_min_bits=")
                    && LbfSamplerBitsText(static_cast<float>(tightened.xMax))
                        == LbfSamplerFixtureField(contents, "tightened_x_max_bits=")
                    && tightened.xMax < tightened.xMin,
                output,
                "LBF loss must tighten x below the current lower bound in the Rust fixture"))
    {
        return false;
    }
    std::ostringstream next;
    next << std::hex << std::nouppercase << std::setfill('0') << std::setw(16)
         << rng.NextU64();
    return Expect(next.str()
                      == LbfSamplerFixtureField(contents, "direct_next_rng_u64=")
                      && next.str()
                          == LbfSamplerFixtureField(contents, "post_one_sample_rng_u64="),
                  output,
                  "C++ structured failure must retain the RNG state after Rust's panicking candidate");
}

bool RunLbfSearchSkipsLocalFixture(
    std::ostream& output,
    const char* fixtureName,
    const jaguar::Rect2d& bounds,
    double itemWidth,
    double itemHeight,
    double blockerWidth,
    double blockerHeight,
    std::uint64_t seed,
    std::size_t samplesPerItem,
    double localSearchFraction,
    std::size_t expectedUniformSamples)
{
    std::ifstream input = OpenLbfSamplerFixture(fixtureName);
    if (!Expect(input.is_open(),
                output,
                "pinned Rust no-uniform-placement fixture cannot be opened"))
    {
        return false;
    }

    std::ostringstream fixtureText;
    fixtureText << input.rdbuf();
    const std::string contents = fixtureText.str();
    std::vector<std::vector<std::string>> expectedRows;
    std::istringstream fixtureLines(contents);
    std::string line;
    while (std::getline(fixtureLines, line))
    {
        if (line.empty() || line.front() == '#')
        {
            continue;
        }
        expectedRows.push_back(LbfSamplerTsvFields(line));
    }
    std::ostringstream fractionText;
    fractionText << localSearchFraction;
    if (!Expect(LbfSamplerFixtureField(contents, "result_status=") == "none"
                    && LbfSamplerFixtureField(contents, "sample_counter=")
                        == std::to_string(expectedUniformSamples)
                    && LbfSamplerFixtureField(contents, "uniform_rows=")
                        == std::to_string(expectedUniformSamples)
                    && LbfSamplerFixtureField(contents, "local_rows=") == "0"
                    && LbfSamplerFixtureField(contents, "seed=")
                        == std::to_string(seed)
                    && LbfSamplerFixtureField(contents, "n_samples=")
                        == std::to_string(samplesPerItem)
                    && LbfSamplerFixtureField(contents, "ls_frac=")
                        == fractionText.str()
                    && expectedRows.size() == expectedUniformSamples,
                output,
                "pinned Rust must spend only the uniform budget when no candidate is valid"))
    {
        return false;
    }

    const jaguar::Container container = MakeLbfContainerWithBounds(bounds);
    const jaguar::Item item = MakeEvaluatorItem({
        {0.0, 0.0}, {itemWidth, 0.0},
        {itemWidth, itemHeight}, {0.0, itemHeight}});
    const jaguar::Item blocker = MakeEvaluatorItem({
        {0.0, 0.0}, {blockerWidth, 0.0},
        {blockerWidth, blockerHeight}, {0.0, blockerHeight}});
    jaguar::Layout blockedLayout(container);
    if (!Expect(blockedLayout.PlaceItem(
                        blocker, jaguar::Transform2d::Identity()).HasValue(),
                output,
                "blocked LBF fixture setup failed"))
    {
        return false;
    }

    sparrow::LbfConfig config;
    config.seed = seed;
    config.samplesPerItem = samplesPerItem;
    config.localSearchFraction = localSearchFraction;
    const jaguar::NoFilter noFilter;
    sparrow::SparrowRng rng(config.seed);
    std::vector<sparrow::LbfSearchTraceEvent> events;
    const auto result = sparrow::SearchLbfWithTrace(
        blockedLayout,
        item,
        config,
        rng,
        noFilter,
        [&events](const sparrow::LbfSearchTraceEvent& event) {
            events.push_back(event);
        });
    if (!Expect(result.HasValue() && !result.Value().has_value(),
                output,
                "blocked fixed-seed LBF search must return no placement"))
    {
        return false;
    }

    std::size_t uniformEvaluations = 0U;
    std::size_t localEvaluations = 0U;
    std::size_t rejectedUniformReports = 0U;
    for (const auto& event : events)
    {
        if (event.type == sparrow::LbfSearchTraceEvent::Type::Evaluation)
        {
            if (event.phase == sparrow::LbfSearchTracePhase::LocalSearch)
            {
                ++localEvaluations;
                continue;
            }
            if (event.phase != sparrow::LbfSearchTracePhase::ContainerSample
                || uniformEvaluations >= expectedRows.size())
            {
                output << "unexpected LBF evaluation phase or count\n";
                return false;
            }
            const auto& expected = expectedRows[uniformEvaluations];
            if (!Expect(expected.size() == 18U
                            && expected[0] == "uniform"
                            && expected[1] == std::to_string(uniformEvaluations)
                            && (expected[5] == "1"
                                || (expected[7] == "1" && expected[8] == "1"))
                            && expected[9] == "0"
                            && event.evaluation.kind
                                != sparrow::SampleKind::SAMPLE_KIND_CLEAR
                            && LbfSamplerBitsText(static_cast<float>(
                                   event.transform.rotationRad)) == expected[2]
                            && LbfSamplerBitsText(static_cast<float>(
                                   event.transform.translation.x)) == expected[3]
                            && LbfSamplerBitsText(static_cast<float>(
                                   event.transform.translation.y)) == expected[4],
                        output,
                        "blocked LBF uniform candidate must match the pinned Rust trace"))
            {
                output << "  uniform ordinal=" << uniformEvaluations << '\n';
                return false;
            }
            ++uniformEvaluations;
        }
        else if (event.type == sparrow::LbfSearchTraceEvent::Type::Report)
        {
            if (event.phase != sparrow::LbfSearchTracePhase::ContainerSample
                || event.accepted)
            {
                output << "blocked LBF trace reported an accepted or non-uniform candidate\n";
                return false;
            }
            ++rejectedUniformReports;
        }
        else
        {
            output << "blocked LBF trace unexpectedly emitted a result event\n";
            return false;
        }
    }

    std::ostringstream next;
    next << std::hex << std::nouppercase << std::setfill('0') << std::setw(16)
         << rng.NextU64();
    return Expect(uniformEvaluations == expectedUniformSamples
                      && localEvaluations == 0U
                      && rejectedUniformReports == expectedUniformSamples
                      && next.str()
                          == LbfSamplerFixtureField(contents, "next_rng_u64="),
                  output,
                  "blocked LBF search must skip local RNG draws and match Rust's post-search state");
}

bool TestLbfSearchSkipsLocalWithoutUniformPlacementRustReference(
    std::ostream& output)
{
    return RunLbfSearchSkipsLocalFixture(
               output,
               "LbfSearchNoUniformPlacementRustTrace.tsv",
               {0.0, 0.0, 30.0, 30.0},
               1.0,
               1.0,
               31.0,
               31.0,
               42U,
               64U,
               0.25,
               48U)
        && RunLbfSearchSkipsLocalFixture(
               output,
               "LbfSearchRectNoUniformPlacementRustTrace.tsv",
               {0.0, 0.0, 40.0, 24.0},
               3.0,
               2.0,
               43.0,
               26.0,
               17U,
               80U,
               0.30,
               56U);
}

bool TestConcaveNotchSearchPairFixturesMatch(std::ostream& output)
{
    auto leftInput = OpenLbfSamplerFixture(
        "LbfSearchRectObstacleFeasible-Continuous-ConcaveNotchRot45SearchPair-Seed42-Samples96RustTrace.tsv");
    auto rightInput = OpenLbfSamplerFixture(
        "LbfSearchRectObstacleFeasible-Continuous-ConcaveNotchRot225SearchPair-Seed42-Samples96RustTrace.tsv");
    if (!Expect(leftInput.is_open() && rightInput.is_open(),
                output,
                "notched 45/225 Rust trace fixtures must be available for pair comparison"))
    {
        return false;
    }

    std::ostringstream leftText;
    std::ostringstream rightText;
    leftText << leftInput.rdbuf();
    rightText << rightInput.rdbuf();
    const std::string leftContents = leftText.str();
    const std::string rightContents = rightText.str();
    const auto dataRows = [](const std::string& contents) {
        std::vector<std::string> rows;
        std::istringstream lines(contents);
        std::string line;
        while (std::getline(lines, line))
        {
            if (!line.empty() && line.front() != '#')
                rows.push_back(line);
        }
        return rows;
    };
    const std::array<std::string_view, 6> resultFields = {
        "result_samples=",
        "next_rng_u64=",
        "result_rotation=",
        "result_x=",
        "result_y=",
        "result_cost=",
    };
    const bool sameResult = std::all_of(
        resultFields.begin(),
        resultFields.end(),
        [&](std::string_view field) {
            return LbfSamplerFixtureField(leftContents, field)
                == LbfSamplerFixtureField(rightContents, field);
        });
    const auto leftRows = dataRows(leftContents);
    const auto rightRows = dataRows(rightContents);
    bool onlyExpectedPrunedDifferences = leftRows.size() == rightRows.size();
    std::size_t prunedSurrogateDifferences = 0;
    for (std::size_t index = 0;
         onlyExpectedPrunedDifferences && index < leftRows.size();
         ++index)
    {
        const auto left = LbfSamplerTsvFields(leftRows[index]);
        const auto right = LbfSamplerTsvFields(rightRows[index]);
        if (left == right)
            continue;
        const std::string expectedLoss = left.size() > 1 && left[1] == "8"
            ? "42394dea"
            : "424f2709";
        const bool expectedOrdinal = left.size() == 18
            && (left[1] == "8" || left[1] == "33");
        const bool sameCandidateAndOutcome = left.size() == 18
            && right.size() == 18
            && left[0] == "uniform"
            && right[0] == "uniform"
            && left[0] == right[0]
            && left[1] == right[1]
            && std::equal(left.begin() + 2, left.begin() + 5, right.begin() + 2)
            && left[7] == right[7]
            && left[8] == right[8]
            && left[9] == right[9]
            && std::equal(left.begin() + 10, left.end(), right.begin() + 10);
        const bool expectedSurrogateDifference = expectedOrdinal
            && left[5] == "1"
            && left[6] == "-"
            && right[5] == "0"
            && right[6] == expectedLoss
            && left[7] == "-"
            && right[7] == "-"
            && left[8] == "-"
            && right[8] == "-"
            && left[9] == "0"
            && right[9] == "0";
        onlyExpectedPrunedDifferences = sameCandidateAndOutcome
            && expectedSurrogateDifference;
        if (onlyExpectedPrunedDifferences)
            ++prunedSurrogateDifferences;
    }

    const auto parseBits = [](const std::string& value)
        -> std::optional<std::uint32_t> {
        std::uint32_t bits = 0;
        std::istringstream input(value);
        input >> std::hex >> bits;
        return input ? std::optional<std::uint32_t>{bits} : std::nullopt;
    };
    const jaguar::Container container = MakeLbfContainerWithBounds(
        {0.0, 0.0, 40.0, 24.0});
    jaguar::SurrogateConfig surrogateConfig{};
    surrogateConfig.failFastPoleAreaRatio = 0.5;
    const jaguar::Item item = MakeEvaluatorItem(
        {{0.0, 0.0}, {3.0, 0.0}, {3.0, 2.0}, {0.0, 2.0}},
        std::nullopt,
        surrogateConfig);
    const jaguar::Item blocker = MakeEvaluatorItem(
        {{0.0, 0.0}, {10.0, 0.0}, {10.0, 1.0}, {1.0, 1.0},
         {1.0, 9.0}, {10.0, 9.0}, {10.0, 10.0}, {0.0, 10.0}},
        std::nullopt,
        surrogateConfig);
    const auto classifyForcedProbe = [&](const std::string& contents,
                                         std::string_view probePrefix,
                                         double blockerRotation,
                                         jaguar::Point2d blockerTranslation)
        -> std::optional<std::pair<bool, std::optional<bool>>> {
        const std::string key(probePrefix);
        const auto rotationBits = parseBits(
            LbfSamplerFixtureField(contents, key + "rotation="));
        const auto xBits = parseBits(LbfSamplerFixtureField(contents, key + "x="));
        const auto yBits = parseBits(LbfSamplerFixtureField(contents, key + "y="));
        if (!rotationBits || !xBits || !yBits)
            return std::nullopt;

        jaguar::Layout layout(container);
        const auto blockerTransform = jaguar::Transform2d::FromRotationTranslation(
            blockerRotation, blockerTranslation);
        if (!layout.PlaceItem(blocker, blockerTransform).HasValue())
            return std::nullopt;
        auto engine = layout.GetContainer().CreateCollisionEngine();
        if (!engine || !engine->Restore(layout.Save().cdeSnapshot).HasValue())
            return std::nullopt;

        const auto transform = jaguar::Transform2d::FromRotationTranslation(
            static_cast<double>(FloatFromBits(*rotationBits)),
            {static_cast<double>(FloatFromBits(*xBits)),
             static_cast<double>(FloatFromBits(*yBits))});
        const auto surrogateCollision = engine->DetectSurrogateCollision(
            *item.CollisionShape().GetSurrogate(), transform, jaguar::NoFilter{});
        if (!surrogateCollision.HasValue())
            return std::nullopt;
        if (surrogateCollision.Value())
            return std::make_pair(true, std::optional<bool>{});

        const auto candidateShape =
            item.CollisionShape().TransformedRustF32(transform);
        if (!candidateShape.HasValue())
            return std::nullopt;
        return std::make_pair(
            false,
            std::optional<bool>{engine->DetectPolygonCollision(
                candidateShape.Value(), jaguar::NoFilter{})});
    };

    bool forcedProbeAgreement = true;
    struct ForcedProbe
    {
        std::string_view phase;
        std::string_view ordinal;
        std::string_view fixturePrefix;
    };
    const std::array<ForcedProbe, 8> forcedProbes = {{
        {"uniform", "7", "forced_probe_uniform_7_"},
        {"uniform", "8", "forced_probe_uniform_8_"},
        {"uniform", "10", "forced_probe_uniform_10_"},
        {"uniform", "33", "forced_probe_uniform_33_"},
        {"uniform", "39", "forced_probe_uniform_39_"},
        {"uniform", "50", "forced_probe_uniform_50_"},
        {"uniform", "67", "forced_probe_uniform_67_"},
        {"local", "26", "forced_probe_local_26_"},
    }};
    for (const ForcedProbe& probe : forcedProbes)
    {
        const std::string rowPrefix = std::string(probe.phase) + "\t"
            + std::string(probe.ordinal) + "\t";
        const auto leftFields = std::find_if(
            leftRows.begin(), leftRows.end(), [&](const std::string& row) {
                return row.compare(0, rowPrefix.size(), rowPrefix) == 0;
            });
        const auto rightFields = std::find_if(
            rightRows.begin(), rightRows.end(), [&](const std::string& row) {
                return row.compare(0, rowPrefix.size(), rowPrefix) == 0;
            });
        if (leftFields == leftRows.end() || rightFields == rightRows.end())
        {
            forcedProbeAgreement = false;
            break;
        }

        const auto candidateFields = LbfSamplerTsvFields(*leftFields);
        if (candidateFields.size() != 18)
        {
            forcedProbeAgreement = false;
            break;
        }
        const std::string key(probe.fixturePrefix);
        const bool sameProbeTransform =
            LbfSamplerFixtureField(leftContents, key + "rotation=")
                == candidateFields[2]
            && LbfSamplerFixtureField(leftContents, key + "x=")
                == candidateFields[3]
            && LbfSamplerFixtureField(leftContents, key + "y=")
                == candidateFields[4]
            && LbfSamplerFixtureField(rightContents, key + "rotation=")
                == candidateFields[2]
            && LbfSamplerFixtureField(rightContents, key + "x=")
                == candidateFields[3]
            && LbfSamplerFixtureField(rightContents, key + "y=")
                == candidateFields[4];
        const auto actual45 = classifyForcedProbe(
            leftContents,
            key,
            static_cast<double>(std::acos(-1.0f) / 4.0f),
            {static_cast<double>(7.071068f), static_cast<double>(4.0f)});
        const auto actual225 = classifyForcedProbe(
            rightContents,
            key,
            static_cast<double>(5.0f * std::acos(-1.0f) / 4.0f),
            {static_cast<double>(7.071068f),
             static_cast<double>(18.142136f)});
        const auto expected45Surrogate = LbfSamplerFixtureField(
            leftContents, key + "surrogate=");
        const auto expected45Polygon = LbfSamplerFixtureField(
            leftContents, key + "polygon=");
        const auto expected225Surrogate = LbfSamplerFixtureField(
            rightContents, key + "surrogate=");
        const auto expected225Polygon = LbfSamplerFixtureField(
            rightContents, key + "polygon=");
        const auto classificationText = [](
            const std::optional<std::pair<bool, std::optional<bool>>>& value) {
            if (!value)
                return std::string("error");
            const std::string surrogate = value->first ? "surrogate" : "clear";
            const std::string polygon = !value->second
                ? "-"
                : *value->second ? "collision" : "clear";
            return surrogate + "/" + polygon;
        };
        const std::string expected45Text = expected45Surrogate + "/"
            + expected45Polygon;
        const std::string expected225Text = expected225Surrogate + "/"
            + expected225Polygon;
        const auto matchesProbe = [](
            const std::optional<std::pair<bool, std::optional<bool>>>& actual,
            const std::string& expectedSurrogate,
            const std::string& expectedPolygon) {
            if (!actual || (expectedSurrogate != "0" && expectedSurrogate != "1"))
                return false;
            if (actual->first != (expectedSurrogate == "1"))
                return false;
            if (expectedPolygon == "-")
                return !actual->second.has_value();
            if (expectedPolygon == "0")
                return actual->second == std::optional<bool>{false};
            if (expectedPolygon == "1")
                return actual->second == std::optional<bool>{true};
            return false;
        };
        const bool expectedProbeMapping = matchesProbe(
                actual45, expected45Surrogate, expected45Polygon)
            && matchesProbe(
                actual225, expected225Surrogate, expected225Polygon);
        if (!expectedProbeMapping)
        {
            output << "forced probe " << probe.phase << '/' << probe.ordinal
                   << ": Rust45=" << expected45Text
                   << " C++45=" << classificationText(actual45)
                   << " Rust225=" << expected225Text
                   << " C++225=" << classificationText(actual225)
                   << " candidate_bits=" << candidateFields[2] << ','
                   << candidateFields[3] << ',' << candidateFields[4] << '\n';
        }
        forcedProbeAgreement = forcedProbeAgreement
            && sameProbeTransform
            && expectedProbeMapping;
    }
    return Expect(onlyExpectedPrunedDifferences
                      && prunedSurrogateDifferences == 2U
                      && sameResult
                      && forcedProbeAgreement,
                  output,
                  "C-shape 45/225 traces may differ only at pruned surrogate checks on uniform ordinals 8 and 33; all C++-invalid candidates with Rust-unobserved collision classification must match forced Rust/C++ surrogate and polygon probes");
}

struct LbfUnobservedSearchEvent
{
    std::string phase;
    std::size_t ordinal = 0U;
    sparrow::SampleKind kind = sparrow::SampleKind::SAMPLE_KIND_CLEAR;
};

bool TestLossPrunedSearchEventsMatchForcedProbes(
    std::ostream& output,
    const std::vector<LbfUnobservedSearchEvent>& plainNoObstacleSearchEvents,
    const std::vector<LbfUnobservedSearchEvent>& rectangularNoneSearchEvents,
    const std::vector<LbfUnobservedSearchEvent>& rectangularDiscreteSearchEvents,
    const std::vector<LbfUnobservedSearchEvent>& rectangularDiscreteThreeAngleSearchEvents,
    const std::vector<LbfUnobservedSearchEvent>& triangleDiscreteThreeAngleSearchEvents,
    const std::vector<LbfUnobservedSearchEvent>& rectangularSeed17SearchEvents,
    const std::vector<LbfUnobservedSearchEvent>& rectangularSeed42SearchEvents,
    const std::vector<LbfUnobservedSearchEvent>& rectangularSeed42Samples96SearchEvents,
    const std::vector<LbfUnobservedSearchEvent>& triangleSeed42Samples96SearchEvents,
    const std::vector<LbfUnobservedSearchEvent>& concaveOriginSeed42Samples96SearchEvents,
    const std::vector<LbfUnobservedSearchEvent>& concaveOffsetSeed42Samples96SearchEvents,
    const std::vector<LbfUnobservedSearchEvent>& concaveOffsetSeed17Samples96SearchEvents,
    const std::vector<LbfUnobservedSearchEvent>& concaveRot90Seed17Samples96SearchEvents,
    const std::vector<LbfUnobservedSearchEvent>& concaveRot90Seed42Samples96SearchEvents,
    const std::vector<LbfUnobservedSearchEvent>& concaveRot90OriginSeed42Samples96SearchEvents,
    const std::vector<LbfUnobservedSearchEvent>& concaveRot45Seed42Samples96SearchEvents,
    const std::vector<LbfUnobservedSearchEvent>& rotated45SearchEvents,
    const std::vector<LbfUnobservedSearchEvent>& rotated225SearchEvents,
    const std::vector<LbfUnobservedSearchEvent>& notchRotated45SearchEvents,
    const std::vector<LbfUnobservedSearchEvent>& notchRotated225SearchEvents)
{
    struct Fixture
    {
        std::string_view name;
        bool rectangular;
        bool triangular;
        bool originConcave;
        bool offsetConcave;
        bool notched;
        bool rotated45;
        std::size_t expectedUnobservedCount;
        const std::vector<LbfUnobservedSearchEvent>* searchEvents;
        bool rotated90 = false;
        bool rotated90Origin = false;
        bool rotated45Translated = false;
        bool hasBlocker = true;
        bool discreteThreeAngles = false;
    };
    const std::array<Fixture, 20> fixtures = {{
        {"LbfSearchRectFeasibleRustTrace.tsv",
         false,
         false,
         false,
         false,
         false,
         false,
         40U,
         &plainNoObstacleSearchEvents,
         false,
         false,
         false,
         false},
        {"LbfSearchRectObstacleFeasibleRustTrace.tsv",
         true,
         false,
         false,
         false,
         false,
         false,
         28U,
         &rectangularNoneSearchEvents},
        {"LbfSearchRectObstacleFeasible-DiscreteRustTrace.tsv",
         true,
         false,
         false,
         false,
         false,
         false,
         21U,
         &rectangularDiscreteSearchEvents},
        {"LbfSearchRectObstacleFeasible-Discrete-0-45-90RustTrace.tsv",
         true,
         false,
         false,
         false,
         false,
         false,
         23U,
         &rectangularDiscreteThreeAngleSearchEvents,
         false,
         false,
         false,
         true,
         true},
        {"LbfSearchRectObstacleFeasible-Discrete-0-45-90-TriangleRustTrace.tsv",
         false,
         true,
         false,
         false,
         false,
         false,
         26U,
         &triangleDiscreteThreeAngleSearchEvents,
         false,
         false,
         false,
         true,
         true},
        {"LbfSearchRectObstacleFeasible-ContinuousRustTrace.tsv",
         true,
         false,
         false,
         false,
         false,
         true,
         20U,
         &rectangularSeed17SearchEvents},
        {"LbfSearchRectObstacleFeasible-Continuous-Seed42RustTrace.tsv",
         true,
         false,
         false,
         false,
         false,
         true,
         21U,
         &rectangularSeed42SearchEvents},
        {"LbfSearchRectObstacleFeasible-Continuous-Seed42-Samples96RustTrace.tsv",
         true,
         false,
         false,
         false,
         false,
         true,
         28U,
         &rectangularSeed42Samples96SearchEvents},
        {"LbfSearchRectObstacleFeasible-Continuous-Triangle-Seed42-Samples96RustTrace.tsv",
         false,
         true,
         false,
         false,
         false,
         true,
         31U,
         &triangleSeed42Samples96SearchEvents},
        {"LbfSearchRectObstacleFeasible-Continuous-Concave-Seed42-Samples96RustTrace.tsv",
         false,
         false,
         true,
         false,
         false,
         false,
         32U,
         &concaveOriginSeed42Samples96SearchEvents},
        {"LbfSearchRectObstacleFeasible-Continuous-ConcaveOffset-Seed42-Samples96RustTrace.tsv",
         false,
         false,
         false,
         true,
         false,
         false,
         42U,
         &concaveOffsetSeed42Samples96SearchEvents},
        {"LbfSearchRectObstacleFeasible-Continuous-ConcaveOffset-Seed17-Samples96RustTrace.tsv",
         false,
         false,
         false,
         true,
         false,
         false,
         35U,
         &concaveOffsetSeed17Samples96SearchEvents},
        {"LbfSearchRectObstacleFeasible-Continuous-ConcaveRot90-Seed17-Samples96RustTrace.tsv",
         false,
         false,
         false,
         false,
         false,
         false,
         35U,
         &concaveRot90Seed17Samples96SearchEvents,
         true},
        {"LbfSearchRectObstacleFeasible-Continuous-ConcaveRot90-Seed42-Samples96RustTrace.tsv",
         false,
         false,
         false,
         false,
         false,
         false,
         42U,
         &concaveRot90Seed42Samples96SearchEvents,
         true},
        {"LbfSearchRectObstacleFeasible-Continuous-ConcaveRot90Origin-Seed42-Samples96RustTrace.tsv",
         false,
         false,
         false,
         false,
         false,
         false,
         38U,
         &concaveRot90OriginSeed42Samples96SearchEvents,
         false,
         true},
        {"LbfSearchRectObstacleFeasible-Continuous-ConcaveRot45-Seed42-Samples96RustTrace.tsv",
         false,
         false,
         false,
         false,
         false,
         false,
         42U,
         &concaveRot45Seed42Samples96SearchEvents,
         false,
         false,
         true},
        {"LbfSearchRectObstacleFeasible-Continuous-ConcaveRot45SearchPair-Seed42-Samples96RustTrace.tsv",
         false,
         false,
         false,
         false,
         false,
         true,
         36U,
         &rotated45SearchEvents},
        {"LbfSearchRectObstacleFeasible-Continuous-ConcaveRot225SearchPair-Seed42-Samples96RustTrace.tsv",
         false,
         false,
         false,
         false,
         false,
         false,
         31U,
         &rotated225SearchEvents},
        {"LbfSearchRectObstacleFeasible-Continuous-ConcaveNotchRot45SearchPair-Seed42-Samples96RustTrace.tsv",
         false,
         false,
         false,
         false,
         true,
         true,
         33U,
         &notchRotated45SearchEvents},
        {"LbfSearchRectObstacleFeasible-Continuous-ConcaveNotchRot225SearchPair-Seed42-Samples96RustTrace.tsv",
         false,
         false,
         false,
         false,
         true,
         false,
         35U,
         &notchRotated225SearchEvents},
    }};
    const auto parseBits = [](const std::string& value)
        -> std::optional<std::uint32_t> {
        std::uint32_t bits = 0;
        std::istringstream input(value);
        input >> std::hex >> bits;
        return input ? std::optional<std::uint32_t>{bits} : std::nullopt;
    };
    const jaguar::Container container = MakeLbfContainerWithBounds(
        {0.0, 0.0, 40.0, 24.0});
    jaguar::SurrogateConfig surrogateConfig{};
    surrogateConfig.failFastPoleAreaRatio = 0.5;
    const jaguar::Item item = MakeEvaluatorItem(
        {{0.0, 0.0}, {3.0, 0.0}, {3.0, 2.0}, {0.0, 2.0}},
        std::nullopt,
        surrogateConfig);
    const jaguar::Item lBlocker = MakeEvaluatorItem(
        {{0.0, 0.0}, {10.0, 0.0}, {10.0, 3.0}, {3.0, 3.0},
         {3.0, 10.0}, {0.0, 10.0}},
        std::nullopt,
        surrogateConfig);
    const jaguar::Item rectangularBlocker = MakeEvaluatorItem(
        {{0.0, 0.0}, {10.0, 0.0}, {10.0, 10.0}, {0.0, 10.0}},
        std::nullopt,
        surrogateConfig);
    const jaguar::Item triangularBlocker = MakeEvaluatorItem(
        {{0.0, 0.0}, {10.0, 0.0}, {0.0, 10.0}},
        std::nullopt,
        surrogateConfig);
    const jaguar::Item notchBlocker = MakeEvaluatorItem(
        {{0.0, 0.0}, {10.0, 0.0}, {10.0, 1.0}, {1.0, 1.0},
         {1.0, 9.0}, {10.0, 9.0}, {10.0, 10.0}, {0.0, 10.0}},
        std::nullopt,
        surrogateConfig);

    for (const Fixture& fixture : fixtures)
    {
        std::ifstream input = OpenLbfSamplerFixture(fixture.name.data());
        if (!Expect(input.is_open(),
                    output,
                    "loss-pruned forced-probe Rust fixture must be available"))
        {
            return false;
        }
        std::ostringstream text;
        text << input.rdbuf();
        const std::string contents = text.str();
        if (!Expect(!fixture.discreteThreeAngles
                        || LbfSamplerFixtureField(contents, "rotation_angles=")
                            == "0,pi/4,pi/2",
                    output,
                    "discrete three-angle Rust fixture must identify its allowed angle set"))
        {
            return false;
        }
        const auto blockerTransform = fixture.rectangular || fixture.triangular
                || fixture.originConcave
            ? jaguar::Transform2d::FromRotationTranslation(0.0, {0.0, 0.0})
            : fixture.offsetConcave
            ? jaguar::Transform2d::FromRotationTranslation(0.0, {12.0, 8.0})
            : fixture.rotated90Origin
                ? jaguar::Transform2d::FromRotationTranslation(
                      static_cast<double>(std::acos(-1.0f) / 2.0f),
                      {10.0, 0.0})
            : fixture.rotated90
                ? jaguar::Transform2d::FromRotationTranslation(
                      static_cast<double>(std::acos(-1.0f) / 2.0f),
                      {22.0, 8.0})
            : fixture.rotated45Translated
                ? jaguar::Transform2d::FromRotationTranslation(
                      static_cast<double>(std::acos(-1.0f) / 4.0f),
                      {19.1, 8.0})
            : fixture.notched
            ? (fixture.rotated45
                   ? jaguar::Transform2d::FromRotationTranslation(
                         static_cast<double>(std::acos(-1.0f) / 4.0f),
                         {7.071068f, 4.0f})
                   : jaguar::Transform2d::FromRotationTranslation(
                         static_cast<double>(5.0f * std::acos(-1.0f) / 4.0f),
                         {7.071068f, 18.142136f}))
            : (fixture.rotated45
                   ? jaguar::Transform2d::FromRotationTranslation(
                         static_cast<double>(std::acos(-1.0f) / 4.0f),
                         {7.0710678, 4.0})
                   : jaguar::Transform2d::FromRotationTranslation(
                         static_cast<double>(5.0f * std::acos(-1.0f) / 4.0f),
                         {7.0710678, 13.192388}));
        jaguar::Layout layout(container);
        const jaguar::Item& blocker = fixture.rectangular
            ? rectangularBlocker
            : (fixture.triangular
                   ? triangularBlocker
                   : (fixture.notched ? notchBlocker : lBlocker));
        if (fixture.hasBlocker
            && !Expect(layout.PlaceItem(blocker, blockerTransform).HasValue(),
                       output,
                       "forced-probe blocker setup must succeed"))
        {
            return false;
        }
        auto engine = layout.GetContainer().CreateCollisionEngine();
        if (!Expect(engine && engine->Restore(layout.Save().cdeSnapshot).HasValue(),
                    output,
                    "forced-probe CDE restore must succeed"))
        {
            return false;
        }

        std::vector<std::string> rows;
        std::istringstream lines(contents);
        std::string line;
        while (std::getline(lines, line))
        {
            if (!line.empty() && line.front() != '#')
                rows.push_back(line);
        }
        std::size_t unobservedCount = 0U;
        for (const std::string& row : rows)
        {
            const auto fields = LbfSamplerTsvFields(row);
            const bool rustCollisionUnobserved = fields.size() == 18
                && fields[5] == "0"
                && fields[7] == "-"
                && fields[8] == "-"
                && fields[9] == "0";
            if (!rustCollisionUnobserved)
                continue;

            ++unobservedCount;
            std::size_t ordinalValue = 0U;
            std::istringstream ordinalInput(fields[1]);
            ordinalInput >> ordinalValue;
            if (!Expect(fields[9] == "0" && ordinalInput,
                        output,
                        "loss-pruned candidate must be rejected and have a numeric ordinal"))
            {
                return false;
            }

            const auto searchEvent = std::find_if(
                fixture.searchEvents->begin(),
                fixture.searchEvents->end(),
                [&](const LbfUnobservedSearchEvent& event) {
                    return event.phase == fields[0]
                        && event.ordinal == ordinalValue;
                });
            if (!Expect(searchEvent != fixture.searchEvents->end(),
                        output,
                        "every Rust-unobserved candidate must have a corresponding C++ search event"))
            {
                return false;
            }

            const std::string ordinal = std::to_string(ordinalValue);
            const std::string key = "forced_probe_" + fields[0]
                + "_" + ordinal + "_";
            const auto rotationBits = parseBits(fields[2]);
            const auto xBits = parseBits(fields[3]);
            const auto yBits = parseBits(fields[4]);
            const auto probeRotationBits = parseBits(
                LbfSamplerFixtureField(contents, key + "rotation="));
            const auto probeXBits = parseBits(
                LbfSamplerFixtureField(contents, key + "x="));
            const auto probeYBits = parseBits(
                LbfSamplerFixtureField(contents, key + "y="));
            if (!Expect(rotationBits && xBits && yBits
                            && probeRotationBits == rotationBits
                            && probeXBits == xBits
                            && probeYBits == yBits,
                        output,
                        "forced-probe transform must match the actual search candidate bits"))
            {
                return false;
            }

            const auto transform = jaguar::Transform2d::FromRotationTranslation(
                static_cast<double>(FloatFromBits(*rotationBits)),
                {static_cast<double>(FloatFromBits(*xBits)),
                 static_cast<double>(FloatFromBits(*yBits))});
            const auto surrogateCollision = engine->DetectSurrogateCollision(
                *item.CollisionShape().GetSurrogate(),
                transform,
                jaguar::NoFilter{});
            if (!Expect(surrogateCollision.HasValue(),
                        output,
                        "forced C++ surrogate query must return a classification"))
            {
                return false;
            }
            std::optional<bool> polygonCollision;
            if (!surrogateCollision.Value())
            {
                const auto candidateShape =
                    item.CollisionShape().TransformedRustF32(transform);
                if (!Expect(candidateShape.HasValue(),
                            output,
                            "forced C++ polygon candidate must transform successfully"))
                {
                    return false;
                }
                polygonCollision = engine->DetectPolygonCollision(
                    candidateShape.Value(), jaguar::NoFilter{});
            }
            const auto collisionText = [](bool surrogate, std::optional<bool> polygon) {
                return std::pair<std::string, std::string>{
                    surrogate ? "1" : "0",
                    polygon ? (*polygon ? "1" : "0") : "-"};
            };
            const auto actual = collisionText(
                surrogateCollision.Value(), polygonCollision);
            const auto expected = std::pair<std::string, std::string>{
                LbfSamplerFixtureField(contents, key + "surrogate="),
                LbfSamplerFixtureField(contents, key + "polygon=")};
            if (!Expect(actual == expected
                            && ((surrogateCollision.Value()
                                 || polygonCollision == std::optional<bool>{true})
                                == (searchEvent->kind
                                    == sparrow::SampleKind::SAMPLE_KIND_INVALID))
                            && (searchEvent->kind
                                    == sparrow::SampleKind::SAMPLE_KIND_INVALID
                                || searchEvent->kind
                                    == sparrow::SampleKind::SAMPLE_KIND_CLEAR),
                        output,
                        "forced Rust/C++ classification must match the actual clear/invalid C++ search event"))
            {
                output << "  fixture=" << fixture.name
                       << " phase=" << fields[0] << " ordinal=" << ordinal
                       << " Rust=" << expected.first << '/' << expected.second
                       << " C++=" << actual.first << '/' << actual.second << '\n';
                return false;
            }
        }
        if (!Expect(unobservedCount == fixture.expectedUnobservedCount
                        && fixture.searchEvents->size() == fixture.expectedUnobservedCount,
                    output,
                    "forced-probe matrix must cover every Rust-unobserved search event and retain the reviewed size"))
        {
            output << "  fixture=" << fixture.name
                   << " fixture_unobserved=" << unobservedCount
                   << " C++_unobserved=" << fixture.searchEvents->size() << '\n';
            return false;
        }
    }
    return true;
}

bool RunLbfSearchFeasibleRectLocalFixture(
    std::ostream& output,
    const char* fixtureName,
    double blockerWidth,
    double blockerHeight,
    jaguar::RotationMode rotationMode,
    std::uint64_t seed,
    std::size_t sampleCount,
    std::size_t uniformSampleCount,
    std::string_view obstacleShape,
    std::vector<LbfUnobservedSearchEvent>* unobservedSearchEvents = nullptr,
    bool include45DiscreteAngle = false)
{
    std::ifstream input = OpenLbfSamplerFixture(fixtureName);
    if (!Expect(input.is_open(),
                output,
                "pinned Rust feasible-rectangle LBF fixture cannot be opened"))
    {
        return false;
    }

    std::ostringstream fixtureText;
    fixtureText << input.rdbuf();
    const std::string contents = fixtureText.str();
    std::vector<std::vector<std::string>> expectedRows;
    std::istringstream fixtureLines(contents);
    std::string line;
    while (std::getline(fixtureLines, line))
    {
        if (!line.empty() && line.front() != '#')
        {
            expectedRows.push_back(LbfSamplerTsvFields(line));
        }
    }
    if (!Expect(LbfSamplerFixtureField(contents, "result_status=") == "some"
                    && LbfSamplerFixtureField(contents, "seed=")
                        == std::to_string(seed)
                    && LbfSamplerFixtureField(contents, "n_samples=")
                        == std::to_string(sampleCount)
                    && LbfSamplerFixtureField(contents, "ls_frac=") == "0.3"
                    && LbfSamplerFixtureField(contents, "uniform_rows=")
                        == std::to_string(uniformSampleCount)
                    && LbfSamplerFixtureField(contents, "local_rows=")
                        == std::to_string(sampleCount - uniformSampleCount)
                    && LbfSamplerFixtureField(contents, "result_samples=")
                        == std::to_string(sampleCount)
                    && LbfSamplerFixtureField(contents, "rotation=")
                        == (rotationMode == jaguar::RotationMode::Continuous
                                ? "continuous"
                                : rotationMode == jaguar::RotationMode::Discrete
                                    ? "discrete"
                                    : "none")
                    && (!include45DiscreteAngle
                        || LbfSamplerFixtureField(contents, "rotation_angles=")
                            == "0,pi/4,pi/2")
                    && ((obstacleShape != "concave"
                         && obstacleShape != "concave-offset"
                         && obstacleShape != "concave-rot90"
                         && obstacleShape != "concave-rot90-origin"
                         && obstacleShape != "concave-rot45"
                         && obstacleShape != "concave-rot45-searchpair"
                         && obstacleShape != "concave-rot225-searchpair"
                         && obstacleShape != "concave-notch-rot45-searchpair"
                         && obstacleShape != "concave-notch-rot225-searchpair")
                        || LbfSamplerFixtureField(contents, "obstacle=")
                            == std::string(obstacleShape))
                    && (obstacleShape != "concave-rot90"
                        || LbfSamplerFixtureField(contents, "orientation_probe=")
                            == "unrotated-left-collides-rotated-left-clear")
                    && (obstacleShape != "concave-rot45"
                        || LbfSamplerFixtureField(contents, "orientation_probe=")
                            == "unrotated-left-collides-rotated45-clear")
                    && (obstacleShape != "concave-rot90-origin"
                        || LbfSamplerFixtureField(contents, "search_candidate_probe=")
                            == "uniform-ordinal-4-same-bbox-rotation-sensitive")
                    && ((obstacleShape != "concave-rot45-searchpair"
                         && obstacleShape != "concave-rot225-searchpair"
                         && obstacleShape != "concave-notch-rot45-searchpair"
                         && obstacleShape != "concave-notch-rot225-searchpair")
                        || LbfSamplerFixtureField(contents, "search_candidate_probe=")
                            == (obstacleShape == "concave-notch-rot45-searchpair"
                                    || obstacleShape
                                        == "concave-notch-rot225-searchpair"
                                    ? "same-aabb-45-vs-225-notched-topology"
                                    : "same-aabb-45-vs-225"))
                    && expectedRows.size() == sampleCount,
                output,
                "pinned Rust fixture must contain the configured uniform and local samples"))
    {
        return false;
    }

    const jaguar::Container container = MakeLbfContainerWithBounds(
        {0.0, 0.0, 40.0, 24.0});
    jaguar::RotationRange allowedRotation{rotationMode, {}};
    if (rotationMode == jaguar::RotationMode::Discrete)
    {
        allowedRotation.radians = include45DiscreteAngle
            ? std::vector<double>{
                  0.0,
                  static_cast<double>(std::acos(-1.0F) / 4.0F),
                  static_cast<double>(std::acos(-1.0F) / 2.0F)}
            : std::vector<double>{0.0, std::acos(-1.0) / 2.0};
    }
    const jaguar::Item item = MakeEvaluatorItem(
        {{0.0, 0.0}, {3.0, 0.0}, {3.0, 2.0}, {0.0, 2.0}},
        std::nullopt,
        {},
        allowedRotation);
    jaguar::Layout layout(container);
    if (blockerWidth > 0.0 && blockerHeight > 0.0)
    {
        const jaguar::Item blocker = obstacleShape == "triangle"
            ? MakeEvaluatorItem({
                  {0.0, 0.0}, {blockerWidth, 0.0}, {0.0, blockerHeight}})
            : ((obstacleShape == "concave-notch-rot45-searchpair"
                || obstacleShape == "concave-notch-rot225-searchpair")
                ? MakeEvaluatorItem({
                      {0.0, 0.0}, {blockerWidth, 0.0},
                      {blockerWidth, 1.0}, {1.0, 1.0},
                      {1.0, 9.0}, {blockerWidth, 9.0},
                      {blockerWidth, blockerHeight}, {0.0, blockerHeight}})
            : (obstacleShape == "concave"
                || obstacleShape == "concave-offset"
                || obstacleShape == "concave-rot90"
                || obstacleShape == "concave-rot90-origin"
                || obstacleShape == "concave-rot45"
                || obstacleShape == "concave-rot45-searchpair"
                || obstacleShape == "concave-rot225-searchpair")
                ? MakeEvaluatorItem({
                      {0.0, 0.0}, {blockerWidth, 0.0},
                      {blockerWidth, 3.0}, {3.0, 3.0},
                      {3.0, blockerHeight}, {0.0, blockerHeight}})
                : MakeEvaluatorItem({
                      {0.0, 0.0}, {blockerWidth, 0.0},
                      {blockerWidth, blockerHeight}, {0.0, blockerHeight}}));
        const auto blockerTransform = obstacleShape == "concave-offset"
            ? jaguar::Transform2d::FromRotationTranslation(0.0, {12.0, 8.0})
            : obstacleShape == "concave-rot90"
                ? jaguar::Transform2d::FromRotationTranslation(
                      static_cast<double>(std::acos(-1.0f) / 2.0f),
                      {22.0, 8.0})
                : obstacleShape == "concave-rot90-origin"
                    ? jaguar::Transform2d::FromRotationTranslation(
                          static_cast<double>(std::acos(-1.0f) / 2.0f),
                          {10.0, 0.0})
                : obstacleShape == "concave-rot45"
                    ? jaguar::Transform2d::FromRotationTranslation(
                          static_cast<double>(std::acos(-1.0f) / 4.0f),
                          {19.1, 8.0})
                    : obstacleShape == "concave-rot45-searchpair"
                        ? jaguar::Transform2d::FromRotationTranslation(
                              static_cast<double>(std::acos(-1.0f) / 4.0f),
                              {7.0710678, 4.0})
                        : obstacleShape == "concave-rot225-searchpair"
                            ? jaguar::Transform2d::FromRotationTranslation(
                                  static_cast<double>(5.0f * std::acos(-1.0f) / 4.0f),
                                  {7.0710678, 13.192388})
                            : obstacleShape == "concave-notch-rot45-searchpair"
                                ? jaguar::Transform2d::FromRotationTranslation(
                                      static_cast<double>(std::acos(-1.0f) / 4.0f),
                                      {7.071068f, 4.0f})
                                : obstacleShape == "concave-notch-rot225-searchpair"
                                    ? jaguar::Transform2d::FromRotationTranslation(
                                          static_cast<double>(5.0f * std::acos(-1.0f) / 4.0f),
                                          {7.071068f, 18.142136f})
                : jaguar::Transform2d::Identity();
        if (!Expect(layout.PlaceItem(blocker, blockerTransform).HasValue(),
                    output,
                    "feasible obstacle fixture setup failed"))
        {
            return false;
        }
        if (obstacleShape == "concave-rot45-searchpair"
            || obstacleShape == "concave-rot225-searchpair")
        {
            const auto probeTransform = jaguar::Transform2d::FromRotationTranslation(
                FloatFromBits(0x4088b931U),
                {static_cast<double>(FloatFromBits(0x3fd3d359U)),
                 static_cast<double>(FloatFromBits(0x4101f784U))});
            const auto rotated45Transform =
                jaguar::Transform2d::FromRotationTranslation(
                    static_cast<double>(std::acos(-1.0f) / 4.0f),
                    {7.0710678, 4.0});
            const auto rotated225Transform =
                jaguar::Transform2d::FromRotationTranslation(
                    static_cast<double>(5.0f * std::acos(-1.0f) / 4.0f),
                    {7.0710678, 13.192388});
            const auto rotated45Shape =
                blocker.CollisionShape().TransformedRustF32(rotated45Transform);
            const auto rotated225Shape =
                blocker.CollisionShape().TransformedRustF32(rotated225Transform);
            const auto boundsDifferByAtMost = [](const jaguar::Rect2d& left,
                                                  const jaguar::Rect2d& right,
                                                  double tolerance) {
                return std::abs(left.xMin - right.xMin) <= tolerance
                    && std::abs(left.yMin - right.yMin) <= tolerance
                    && std::abs(left.xMax - right.xMax) <= tolerance
                    && std::abs(left.yMax - right.yMax) <= tolerance;
            };
            const auto classify = [&](const jaguar::Transform2d& obstacleTransform)
                -> std::optional<std::pair<bool, std::optional<bool>>> {
                jaguar::Layout obstacleLayout(
                    MakeLbfContainerWithBounds({0.0, 0.0, 40.0, 24.0}));
                if (!obstacleLayout.PlaceItem(blocker, obstacleTransform).HasValue())
                    return std::nullopt;
                auto engine = obstacleLayout.GetContainer().CreateCollisionEngine();
                if (!engine
                    || !engine->Restore(obstacleLayout.Save().cdeSnapshot).HasValue())
                {
                    return std::nullopt;
                }
                const auto surrogateCollision = engine->DetectSurrogateCollision(
                    *item.CollisionShape().GetSurrogate(),
                    probeTransform,
                    jaguar::NoFilter{});
                if (!surrogateCollision.HasValue())
                    return std::nullopt;
                if (surrogateCollision.Value())
                    return std::make_pair(true, std::optional<bool>{});
                const auto candidateShape =
                    item.CollisionShape().TransformedRustF32(probeTransform);
                if (!candidateShape.HasValue())
                    return std::nullopt;
                return std::make_pair(
                    false,
                    std::optional<bool>{engine->DetectPolygonCollision(
                        candidateShape.Value(), jaguar::NoFilter{})});
            };
            const auto classes45 = classify(rotated45Transform);
            const auto classes225 = classify(rotated225Transform);
            if (!Expect(rotated45Shape.HasValue()
                            && rotated225Shape.HasValue()
                            && boundsDifferByAtMost(
                                rotated45Shape.Value().BoundingBox(),
                                rotated225Shape.Value().BoundingBox(),
                                2.0e-5),
                        output,
                        "45/225-degree blocker shapes must have the same actual AABB")
                || !Expect(classes45.has_value()
                               && classes225.has_value()
                               && *classes45
                                   == std::make_pair(false,
                                                     std::optional<bool>{false})
                               && *classes225
                                   == std::make_pair(false,
                                                     std::optional<bool>{true}),
                           output,
                           "LBF collision query must clear ordinal 4 at 45 degrees and reach polygon collision at 225 degrees"))
            {
                return false;
            }
        }
        if (obstacleShape == "concave-notch-rot45-searchpair"
            || obstacleShape == "concave-notch-rot225-searchpair")
        {
            const auto rotated45Transform =
                jaguar::Transform2d::FromRotationTranslation(
                    static_cast<double>(std::acos(-1.0f) / 4.0f),
                    {7.071068f, 4.0f});
            const auto rotated225Transform =
                jaguar::Transform2d::FromRotationTranslation(
                    static_cast<double>(5.0f * std::acos(-1.0f) / 4.0f),
                    {7.071068f, 18.142136f});
            const auto rotated45Shape =
                blocker.CollisionShape().TransformedRustF32(rotated45Transform);
            const auto rotated225Shape =
                blocker.CollisionShape().TransformedRustF32(rotated225Transform);
            const auto boundsDifferByAtMost = [](const jaguar::Rect2d& left,
                                                  const jaguar::Rect2d& right,
                                                  double tolerance) {
                return std::abs(left.xMin - right.xMin) <= tolerance
                    && std::abs(left.yMin - right.yMin) <= tolerance
                    && std::abs(left.xMax - right.xMax) <= tolerance
                    && std::abs(left.yMax - right.yMax) <= tolerance;
            };
            if (!Expect(rotated45Shape.HasValue()
                            && rotated225Shape.HasValue()
                            && boundsDifferByAtMost(
                                rotated45Shape.Value().BoundingBox(),
                                rotated225Shape.Value().BoundingBox(),
                                2.0e-5),
                        output,
                        "notched 45/225-degree blocker shapes must share the same actual AABB"))
            {
                return false;
            }
        }
        if (obstacleShape == "concave-rot90"
            || obstacleShape == "concave-rot45")
        {
            const auto probeTransform = obstacleShape == "concave-rot90"
                ? jaguar::Transform2d::FromRotationTranslation(0.0, {13.0, 13.0})
                : jaguar::Transform2d::FromRotationTranslation(0.0, {14.5, 17.1});
            jaguar::Layout unrotatedProbe(MakeLbfContainerWithBounds(
                {0.0, 0.0, 40.0, 24.0}));
            jaguar::Layout rotatedProbe(MakeLbfContainerWithBounds(
                {0.0, 0.0, 40.0, 24.0}));
            const bool probePlacementsSucceeded =
                unrotatedProbe.PlaceItem(
                    blocker,
                    jaguar::Transform2d::FromRotationTranslation(
                        0.0, {12.0, 8.0})).HasValue()
                && unrotatedProbe.PlaceItem(item, probeTransform).HasValue()
                && rotatedProbe.PlaceItem(blocker, blockerTransform).HasValue()
                && rotatedProbe.PlaceItem(item, probeTransform).HasValue();
            if (!Expect(probePlacementsSucceeded,
                        output,
                        "orientation-sensitive collision probe setup failed")
                || !Expect(!unrotatedProbe.IsFeasible()
                               && rotatedProbe.IsFeasible(),
                           output,
                           obstacleShape == "concave-rot90"
                               ? "90-degree blocker rotation must clear the left-arm probe"
                               : "45-degree blocker rotation must clear the left-arm probe"))
            {
                return false;
            }
        }
    }
    const jaguar::NoFilter noFilter;
    sparrow::LbfConfig config;
    config.seed = seed;
    config.samplesPerItem = sampleCount;
    config.localSearchFraction = 0.30;
    sparrow::SparrowRng rng(config.seed);
    std::vector<sparrow::LbfSearchTraceEvent> events;
    const auto result = sparrow::SearchLbfWithTrace(
        layout,
        item,
        config,
        rng,
        noFilter,
        [&events](const sparrow::LbfSearchTraceEvent& event) {
            events.push_back(event);
        });
    if (!Expect(result.HasValue() && result.Value().has_value(),
                output,
                "feasible rectangular LBF search must return a placement"))
    {
        return false;
    }

    std::size_t rowIndex = 0U;
    std::size_t uniformEvaluations = 0U;
    std::size_t localEvaluations = 0U;
    std::size_t uniformCollisions = 0U;
    std::size_t localCollisions = 0U;
    std::size_t uniformAccepted = 0U;
    std::size_t localAccepted = 0U;
    std::size_t uniformRotated = 0U;
    std::size_t localRotated = 0U;
    std::size_t rustCollisionUnobserved = 0U;
    std::size_t cppInvalidWithUnobservedCollision = 0U;
    std::size_t bestRowIndex = std::numeric_limits<std::size_t>::max();
    bool directionSensitiveCandidateObserved = false;
    bool awaitingReport = false;
    std::size_t resultEvents = 0U;
    const auto matchesTransform = [](const jaguar::Transform2d& transform,
                                     const std::vector<std::string>& row) {
        return row.size() == 18U
            && LbfSamplerBitsText(static_cast<float>(transform.rotationRad))
                == row[2]
            && LbfSamplerBitsText(static_cast<float>(transform.translation.x))
                == row[3]
            && LbfSamplerBitsText(static_cast<float>(transform.translation.y))
                == row[4];
    };
    for (const auto& event : events)
    {
        if (event.type == sparrow::LbfSearchTraceEvent::Type::Evaluation)
        {
            if (awaitingReport || rowIndex >= expectedRows.size())
            {
                output << "unexpected feasible LBF evaluation event ordering\n";
                return false;
            }
            const auto& expected = expectedRows[rowIndex];
            if (expected.size() != 18U)
            {
                output << "malformed pinned Rust feasible LBF row " << rowIndex
                       << '\n';
                return false;
            }
            const bool local = expected[0] == "local";
            const std::size_t ordinal = local
                ? localEvaluations++
                : uniformEvaluations++;
            const auto expectedPhase = local
                ? sparrow::LbfSearchTracePhase::LocalSearch
                : sparrow::LbfSearchTracePhase::ContainerSample;
            const bool expectedCollision = expected[5] == "1"
                || (expected[7] == "1" && expected[8] == "1");
            const bool collisionClassificationKnown = expected[5] == "1"
                || expected[7] != "-";
            if (expectedCollision)
            {
                local ? ++localCollisions : ++uniformCollisions;
            }
            if (!collisionClassificationKnown)
            {
                ++rustCollisionUnobserved;
                if (unobservedSearchEvents != nullptr)
                {
                    unobservedSearchEvents->push_back(
                        {expected[0], ordinal, event.evaluation.kind});
                }
                if (event.evaluation.kind
                    == sparrow::SampleKind::SAMPLE_KIND_INVALID)
                {
                    ++cppInvalidWithUnobservedCollision;
                    if (obstacleShape == "concave-rot45-searchpair"
                        || obstacleShape == "concave-rot225-searchpair"
                        || obstacleShape == "concave-notch-rot45-searchpair"
                        || obstacleShape == "concave-notch-rot225-searchpair")
                    {
                        const bool isRot45 = obstacleShape
                                == "concave-rot45-searchpair"
                            || obstacleShape
                                == "concave-notch-rot45-searchpair";
                        const bool isNotched = obstacleShape
                                == "concave-notch-rot45-searchpair"
                            || obstacleShape
                                == "concave-notch-rot225-searchpair";
                        const bool expectedInvalidCandidate = isNotched
                            ? local
                                ? ordinal == 26U
                                : ordinal == 7U || ordinal == 10U
                                    || ordinal == 50U || ordinal == 67U
                                    || (isRot45 && ordinal == 39U)
                            : local
                                ? ordinal == 26U
                                : isRot45
                                    ? ordinal == 10U || ordinal == 39U
                                        || ordinal == 50U
                                    : ordinal == 33U || ordinal == 37U
                                        || ordinal == 50U;
                        if (!Expect(expectedInvalidCandidate,
                                    output,
                                    "concave search-pair loss-pruned invalid candidates must remain in the reviewed fixed ordinal set"))
                        {
                            output << "  phase=" << expected[0]
                                   << " ordinal=" << ordinal << '\n';
                            return false;
                        }
                    }
                }
            }
            if (rotationMode == jaguar::RotationMode::Continuous
                && expected[2] != "00000000")
            {
                local ? ++localRotated : ++uniformRotated;
            }
            const auto expectedKind = expectedCollision
                ? sparrow::SampleKind::SAMPLE_KIND_INVALID
                : sparrow::SampleKind::SAMPLE_KIND_CLEAR;
            if ((obstacleShape == "concave"
                 || obstacleShape == "concave-rot90-origin"
                 || obstacleShape == "concave-rot45-searchpair"
                 || obstacleShape == "concave-rot225-searchpair")
                && !local && ordinal == 4U)
            {
                const bool baselineCollision = obstacleShape == "concave"
                    || obstacleShape == "concave-rot225-searchpair";
                if (!Expect(expected[2] == "4088b931"
                                && expected[3] == "3fd3d359"
                                && expected[4] == "4101f784"
                                && expected[5] == "0"
                                && expected[7] == "1"
                                && expected[8] == (baselineCollision ? "1" : "0")
                                && expected[9] == (baselineCollision ? "0" : "1"),
                            output,
                            "fixed uniform ordinal 4 must have the direction-sensitive collision and acceptance recorded in the fixture"))
                {
                    return false;
                }
                directionSensitiveCandidateObserved = true;
            }
            const bool expectedKindMatches = !collisionClassificationKnown
                || event.evaluation.kind == expectedKind;
            if (!Expect(expected[1] == std::to_string(ordinal)
                            && event.phase == expectedPhase
                            && expectedKindMatches
                            && matchesTransform(event.transform, expected),
                        output,
                        "C++ feasible LBF candidate must match the pinned Rust phase and transform"))
            {
                output << "  row=" << rowIndex << " phase=" << expected[0]
                       << " ordinal=" << expected[1]
                       << " expected_bits=" << expected[2] << ',' << expected[3]
                       << ',' << expected[4] << " actual_bits="
                       << LbfSamplerBitsText(static_cast<float>(
                              event.transform.rotationRad))
                       << ',' << LbfSamplerBitsText(static_cast<float>(
                              event.transform.translation.x))
                       << ',' << LbfSamplerBitsText(static_cast<float>(
                              event.transform.translation.y))
                       << " expected_kind="
                       << (!collisionClassificationKnown
                               ? "unobserved"
                               : expectedCollision ? "invalid" : "clear")
                       << " actual_kind="
                       << static_cast<int>(event.evaluation.kind) << '\n';
                return false;
            }
            awaitingReport = true;
        }
        else if (event.type == sparrow::LbfSearchTraceEvent::Type::Report)
        {
            if (!awaitingReport || rowIndex >= expectedRows.size())
            {
                output << "unexpected feasible LBF report event ordering\n";
                return false;
            }
            const auto& expected = expectedRows[rowIndex];
            const bool accepted = expected[9] == "1";
            if (event.phase
                    != (expected[0] == "local"
                            ? sparrow::LbfSearchTracePhase::LocalSearch
                            : sparrow::LbfSearchTracePhase::ContainerSample)
                || event.accepted != accepted
                || !matchesTransform(event.transform, expected))
            {
                output << "Rust/C++ feasible LBF report differs at row "
                       << rowIndex << '\n';
                return false;
            }
            if (accepted)
            {
                bestRowIndex = rowIndex;
                expected[0] == "local" ? ++localAccepted : ++uniformAccepted;
            }
            const bool retainedMatches =
                bestRowIndex == std::numeric_limits<std::size_t>::max()
                    ? event.retained.empty()
                    : event.retained.size() == 1U
                        && matchesTransform(event.retained[0].first,
                                            expectedRows[bestRowIndex]);
            if (!retainedMatches)
            {
                output << "Rust/C++ feasible LBF retained sample differs at row "
                       << rowIndex << '\n';
                return false;
            }
            ++rowIndex;
            awaitingReport = false;
        }
        else
        {
            ++resultEvents;
            if (!Expect(resultEvents == 1U && !awaitingReport
                            && rowIndex == expectedRows.size()
                            && event.evaluationCount == expectedRows.size()
                            && result.Value()->samples == expectedRows.size()
                            && LbfSamplerBitsText(static_cast<float>(
                                   event.transform.rotationRad))
                                == LbfSamplerFixtureField(
                                    contents, "result_rotation=")
                            && LbfSamplerBitsText(static_cast<float>(
                                   event.transform.translation.x))
                                == LbfSamplerFixtureField(contents, "result_x=")
                            && LbfSamplerBitsText(static_cast<float>(
                                   event.transform.translation.y))
                                == LbfSamplerFixtureField(contents, "result_y="),
                        output,
                        "Rust/C++ feasible LBF result transform and sample count must match"))
            {
                return false;
            }
        }
    }

    std::ostringstream next;
    next << std::hex << std::nouppercase << std::setfill('0') << std::setw(16)
         << rng.NextU64();
    const bool expectedSearchPairInvalidCount =
        (obstacleShape != "concave-rot45-searchpair"
         && obstacleShape != "concave-rot225-searchpair"
         && obstacleShape != "concave-notch-rot45-searchpair"
         && obstacleShape != "concave-notch-rot225-searchpair")
        || ((obstacleShape == "concave-rot45-searchpair"
             || obstacleShape == "concave-rot225-searchpair")
            && cppInvalidWithUnobservedCollision == 4U)
        || (obstacleShape == "concave-notch-rot45-searchpair"
            && cppInvalidWithUnobservedCollision == 6U)
        || (obstacleShape == "concave-notch-rot225-searchpair"
            && cppInvalidWithUnobservedCollision == 5U);
    const bool matches = Expect(rowIndex == expectedRows.size()
                                    && uniformEvaluations == uniformSampleCount
                                    && localEvaluations
                                        == sampleCount - uniformSampleCount
                                    && expectedSearchPairInvalidCount
                                    && uniformCollisions > 0U
                                    && (rotationMode
                                                == jaguar::RotationMode::Continuous
                                            ? uniformRotated == uniformEvaluations
                                                && localRotated == localEvaluations
                                            : localCollisions > 0U)
                                    && uniformAccepted > 0U
                                    && localAccepted > 0U
                                    && resultEvents == 1U
                                    && (((obstacleShape == "concave"
                                          || obstacleShape == "concave-rot90-origin"
                                          || obstacleShape == "concave-rot45-searchpair"
                                          || obstacleShape == "concave-rot225-searchpair")
                                             && directionSensitiveCandidateObserved)
                                        || (obstacleShape != "concave"
                                            && obstacleShape != "concave-rot90-origin"
                                            && obstacleShape != "concave-rot45-searchpair"
                                            && obstacleShape != "concave-rot225-searchpair"))
                                    && next.str()
                                        == LbfSamplerFixtureField(
                                            contents, "next_rng_u64=")
                                    && LbfSamplerBitsText(
                                           result.Value()->loss.Cost())
                                        == LbfSamplerFixtureField(
                                            contents, "result_cost="),
                                output,
                                "Rust/C++ feasible LBF local trace, final loss and RNG state must match");
    if (matches && rotationMode == jaguar::RotationMode::Continuous)
    {
        output << "LBF continuous obstacle trace: rotated uniform/local candidates="
               << uniformRotated << '/' << localRotated
               << ", Rust collision classifications unobserved after loss pruning="
               << rustCollisionUnobserved
               << ", C++ reported invalid among them="
               << cppInvalidWithUnobservedCollision << '\n';
    }
    return matches;
}

bool TestLbfSearchFeasibleRectLocalRustReference(std::ostream& output)
{
    std::vector<LbfUnobservedSearchEvent> plainNoObstacleUnobservedEvents;
    std::vector<LbfUnobservedSearchEvent> rectangularNoneUnobservedEvents;
    std::vector<LbfUnobservedSearchEvent> rectangularDiscreteUnobservedEvents;
    std::vector<LbfUnobservedSearchEvent> rectangularDiscreteThreeAngleUnobservedEvents;
    std::vector<LbfUnobservedSearchEvent> triangleDiscreteThreeAngleUnobservedEvents;
    std::vector<LbfUnobservedSearchEvent> rectangularSeed17UnobservedEvents;
    std::vector<LbfUnobservedSearchEvent> rectangularSeed42UnobservedEvents;
    std::vector<LbfUnobservedSearchEvent> rectangularSeed42Samples96UnobservedEvents;
    std::vector<LbfUnobservedSearchEvent> triangleSeed42Samples96UnobservedEvents;
    std::vector<LbfUnobservedSearchEvent> concaveOriginSeed42Samples96UnobservedEvents;
    std::vector<LbfUnobservedSearchEvent> concaveOffsetSeed42Samples96UnobservedEvents;
    std::vector<LbfUnobservedSearchEvent> concaveOffsetSeed17Samples96UnobservedEvents;
    std::vector<LbfUnobservedSearchEvent> concaveRot90Seed17Samples96UnobservedEvents;
    std::vector<LbfUnobservedSearchEvent> concaveRot90Seed42Samples96UnobservedEvents;
    std::vector<LbfUnobservedSearchEvent> concaveRot90OriginSeed42Samples96UnobservedEvents;
    std::vector<LbfUnobservedSearchEvent> concaveRot45Seed42Samples96UnobservedEvents;
    std::vector<LbfUnobservedSearchEvent> rotated45UnobservedEvents;
    std::vector<LbfUnobservedSearchEvent> rotated225UnobservedEvents;
    std::vector<LbfUnobservedSearchEvent> notchRotated45UnobservedEvents;
    std::vector<LbfUnobservedSearchEvent> notchRotated225UnobservedEvents;
    return RunLbfSearchFeasibleRectLocalFixture(
               output,
               "LbfSearchRectFeasibleRustTrace.tsv",
               0.0,
               0.0,
               jaguar::RotationMode::None,
               17U,
               80U,
               56U,
               "rect",
               &plainNoObstacleUnobservedEvents)
        && RunLbfSearchFeasibleRectLocalFixture(
               output,
               "LbfSearchRectObstacleFeasibleRustTrace.tsv",
               10.0,
               10.0,
               jaguar::RotationMode::None,
               17U,
               80U,
               56U,
               "rect",
               &rectangularNoneUnobservedEvents)
        && RunLbfSearchFeasibleRectLocalFixture(
               output,
               "LbfSearchRectObstacleFeasible-DiscreteRustTrace.tsv",
               10.0,
               10.0,
               jaguar::RotationMode::Discrete,
               17U,
               80U,
               56U,
               "rect",
               &rectangularDiscreteUnobservedEvents)
        && RunLbfSearchFeasibleRectLocalFixture(
               output,
               "LbfSearchRectObstacleFeasible-Discrete-0-45-90RustTrace.tsv",
               10.0,
               10.0,
               jaguar::RotationMode::Discrete,
               17U,
               80U,
               56U,
               "rect",
               &rectangularDiscreteThreeAngleUnobservedEvents,
               true)
        && RunLbfSearchFeasibleRectLocalFixture(
               output,
               "LbfSearchRectObstacleFeasible-Discrete-0-45-90-TriangleRustTrace.tsv",
               10.0,
               10.0,
               jaguar::RotationMode::Discrete,
               17U,
               80U,
               56U,
               "triangle",
               &triangleDiscreteThreeAngleUnobservedEvents,
               true)
        && RunLbfSearchFeasibleRectLocalFixture(
               output,
               "LbfSearchRectObstacleFeasible-ContinuousRustTrace.tsv",
               10.0,
               10.0,
               jaguar::RotationMode::Continuous,
               17U,
               80U,
               56U,
               "rect",
               &rectangularSeed17UnobservedEvents)
        && RunLbfSearchFeasibleRectLocalFixture(
               output,
               "LbfSearchRectObstacleFeasible-Continuous-Seed42RustTrace.tsv",
               10.0,
               10.0,
               jaguar::RotationMode::Continuous,
               42U,
               80U,
               56U,
               "rect",
               &rectangularSeed42UnobservedEvents)
        && RunLbfSearchFeasibleRectLocalFixture(
               output,
               "LbfSearchRectObstacleFeasible-Continuous-Seed42-Samples96RustTrace.tsv",
               10.0,
               10.0,
               jaguar::RotationMode::Continuous,
               42U,
               96U,
               68U,
               "rect",
               &rectangularSeed42Samples96UnobservedEvents)
        && RunLbfSearchFeasibleRectLocalFixture(
               output,
               "LbfSearchRectObstacleFeasible-Continuous-Triangle-Seed42-Samples96RustTrace.tsv",
               10.0,
               10.0,
               jaguar::RotationMode::Continuous,
               42U,
               96U,
               68U,
               "triangle",
               &triangleSeed42Samples96UnobservedEvents)
        && RunLbfSearchFeasibleRectLocalFixture(
               output,
               "LbfSearchRectObstacleFeasible-Continuous-Concave-Seed42-Samples96RustTrace.tsv",
               10.0,
               10.0,
               jaguar::RotationMode::Continuous,
               42U,
               96U,
               68U,
               "concave",
               &concaveOriginSeed42Samples96UnobservedEvents)
        && RunLbfSearchFeasibleRectLocalFixture(
               output,
               "LbfSearchRectObstacleFeasible-Continuous-ConcaveOffset-Seed42-Samples96RustTrace.tsv",
               10.0,
               10.0,
               jaguar::RotationMode::Continuous,
               42U,
               96U,
               68U,
               "concave-offset",
               &concaveOffsetSeed42Samples96UnobservedEvents)
        && RunLbfSearchFeasibleRectLocalFixture(
               output,
               "LbfSearchRectObstacleFeasible-Continuous-ConcaveOffset-Seed17-Samples96RustTrace.tsv",
               10.0,
               10.0,
               jaguar::RotationMode::Continuous,
               17U,
               96U,
               68U,
                "concave-offset",
                &concaveOffsetSeed17Samples96UnobservedEvents)
        && RunLbfSearchFeasibleRectLocalFixture(
               output,
               "LbfSearchRectObstacleFeasible-Continuous-ConcaveRot90-Seed42-Samples96RustTrace.tsv",
               10.0,
               10.0,
               jaguar::RotationMode::Continuous,
               42U,
               96U,
               68U,
               "concave-rot90",
               &concaveRot90Seed42Samples96UnobservedEvents)
        && RunLbfSearchFeasibleRectLocalFixture(
               output,
               "LbfSearchRectObstacleFeasible-Continuous-ConcaveRot90-Seed17-Samples96RustTrace.tsv",
               10.0,
               10.0,
               jaguar::RotationMode::Continuous,
               17U,
               96U,
               68U,
                "concave-rot90",
                &concaveRot90Seed17Samples96UnobservedEvents)
        && RunLbfSearchFeasibleRectLocalFixture(
               output,
               "LbfSearchRectObstacleFeasible-Continuous-ConcaveRot90Origin-Seed42-Samples96RustTrace.tsv",
               10.0,
               10.0,
               jaguar::RotationMode::Continuous,
               42U,
               96U,
               68U,
               "concave-rot90-origin",
               &concaveRot90OriginSeed42Samples96UnobservedEvents)
        && RunLbfSearchFeasibleRectLocalFixture(
               output,
               "LbfSearchRectObstacleFeasible-Continuous-ConcaveRot45-Seed42-Samples96RustTrace.tsv",
               10.0,
               10.0,
               jaguar::RotationMode::Continuous,
               42U,
               96U,
               68U,
               "concave-rot45",
               &concaveRot45Seed42Samples96UnobservedEvents)
        && RunLbfSearchFeasibleRectLocalFixture(
               output,
               "LbfSearchRectObstacleFeasible-Continuous-ConcaveRot45SearchPair-Seed42-Samples96RustTrace.tsv",
               10.0,
               10.0,
               jaguar::RotationMode::Continuous,
               42U,
               96U,
               68U,
                "concave-rot45-searchpair",
                &rotated45UnobservedEvents)
        && RunLbfSearchFeasibleRectLocalFixture(
               output,
               "LbfSearchRectObstacleFeasible-Continuous-ConcaveRot225SearchPair-Seed42-Samples96RustTrace.tsv",
               10.0,
               10.0,
               jaguar::RotationMode::Continuous,
               42U,
               96U,
               68U,
                "concave-rot225-searchpair",
                &rotated225UnobservedEvents)
        && RunLbfSearchFeasibleRectLocalFixture(
               output,
               "LbfSearchRectObstacleFeasible-Continuous-ConcaveNotchRot45SearchPair-Seed42-Samples96RustTrace.tsv",
               10.0,
               10.0,
               jaguar::RotationMode::Continuous,
               42U,
               96U,
               68U,
               "concave-notch-rot45-searchpair",
               &notchRotated45UnobservedEvents)
        && RunLbfSearchFeasibleRectLocalFixture(
               output,
               "LbfSearchRectObstacleFeasible-Continuous-ConcaveNotchRot225SearchPair-Seed42-Samples96RustTrace.tsv",
               10.0,
               10.0,
               jaguar::RotationMode::Continuous,
               42U,
               96U,
               68U,
               "concave-notch-rot225-searchpair",
               &notchRotated225UnobservedEvents)
        && TestConcaveNotchSearchPairFixturesMatch(output)
         && TestLossPrunedSearchEventsMatchForcedProbes(
             output,
             plainNoObstacleUnobservedEvents,
             rectangularNoneUnobservedEvents,
             rectangularDiscreteUnobservedEvents,
             rectangularDiscreteThreeAngleUnobservedEvents,
             triangleDiscreteThreeAngleUnobservedEvents,
             rectangularSeed17UnobservedEvents,
             rectangularSeed42UnobservedEvents,
             rectangularSeed42Samples96UnobservedEvents,
             triangleSeed42Samples96UnobservedEvents,
             concaveOriginSeed42Samples96UnobservedEvents,
             concaveOffsetSeed42Samples96UnobservedEvents,
              concaveOffsetSeed17Samples96UnobservedEvents,
              concaveRot90Seed17Samples96UnobservedEvents,
              concaveRot90Seed42Samples96UnobservedEvents,
              concaveRot90OriginSeed42Samples96UnobservedEvents,
              concaveRot45Seed42Samples96UnobservedEvents,
             rotated45UnobservedEvents,
             rotated225UnobservedEvents,
             notchRotated45UnobservedEvents,
             notchRotated225UnobservedEvents);
}
}

bool RunSamplingTests(std::ostream& output)
{
    return TestLbfSppSearchUsesCdeBounds(output)
        && TestCoordinateDescentRadiansMatchRustReference(output)
        && TestRotationModes(output)
        && TestBestSamplesRotationBoundary(output)
        && TestUniformRectSampler(output)
        && TestUniformSamplerUpperBoundaryRustReference(output)
        && TestUniformSamplerNarrowIntervalRustReference(output)
        && TestUniformSamplerContinuousGridRustReference(output)
        && TestUniformSamplerSingletonAxisRustReference(output)
        && TestUniformSamplerDegenerateBboxRustReference(output)
        && TestLbfUniformSamplerNarrowBoundsRustReference(output)
        && TestLbfUniformSamplerInvalidBoundsMapping(output)
        && TestLbfSearchTighteningErrorAgainstRustReference(output)
        && TestLbfSearchSkipsLocalWithoutUniformPlacementRustReference(output)
        && TestLbfSearchFeasibleRectLocalRustReference(output)
        && TestRustSamplingCoordinateDescentTrace(output)
        && TestLocalSearchSampler(output)
        && TestCoordinateDescentAxesAndBudget(output)
        && TestCoordinateDescentEvaluationBudgetBoundaries(output)
        && TestCoordinateDescentDefaultBudgetMatchesRust(output)
        && TestCoordinateDescentOutcomes(output)
        && TestCoordinateDescentTieMatchesRustReference(
            output,
            "SampleEvalCoordinateDescentRustReference.tsv",
            0x3f800004U,
            "approximate")
        && TestCoordinateDescentTieMatchesRustReference(
            output,
            "SampleEvalCoordinateDescentExactTieRustReference.tsv",
            0x3f800000U,
            "exact")
        && TestLbfEvaluatorBasicCollisionAndLoss(output)
        && TestLbfEvaluatorUsesPointOfInaccessibility(output)
        && TestLbfEvaluatorHoleAndQualityFilter(output)
        && TestLbfQualityZoneFilterMatchesRustReference(output)
        && TestLbfEvaluatorSurrogatePath(output)
        && TestLbfRuntimeBudgetSplit(output)
        && TestLbfSearch(output);
}

bool RunLbfBudgetSplitRuntimeTest(std::ostream& output)
{
    const bool passed = TestLbfRuntimeBudgetSplit(output);
    if (passed)
    {
        output << "LBF fixed-seed runtime budget passed: seed=42 uniform=48 "
                  "local=16 total=64\n";
    }
    return passed;
}

namespace
{
bool RunLbfPinnedSearchTraceProbeWithRotation(
    jaguar::RotationRange allowedRotation,
    const char* rotationName,
    std::ostream& output)
{
    const jaguar::Container container = MakePlainEvaluatorContainer();
    const jaguar::Item item = MakeEvaluatorItem({
        {0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}, {0.0, 1.0}},
        std::nullopt,
        {},
        std::move(allowedRotation));
    const jaguar::NoFilter noFilter;
    const jaguar::Layout layout(container);

    sparrow::LbfConfig config;
    config.seed = 42U;
    config.samplesPerItem = 64U;
    config.localSearchFraction = 0.25;
    sparrow::SparrowRng rng(config.seed);
    std::vector<sparrow::LbfSearchTraceEvent> events;
    const auto result = sparrow::SearchLbfWithTrace(
        layout,
        item,
        config,
        rng,
        noFilter,
        [&events](const sparrow::LbfSearchTraceEvent& event) {
            events.push_back(event);
        });
    if (!Expect(result.HasValue() && result.Value().has_value(),
                output,
                "fixed-seed LBF trace probe must find a placement"))
    {
        return false;
    }

    const auto floatBits = [](float value) {
        std::uint32_t raw = 0U;
        std::memcpy(&raw, &value, sizeof(raw));
        std::ostringstream text;
        text << std::hex << std::setfill('0') << std::setw(8) << raw;
        return text.str();
    };
    const auto phaseName = [](sparrow::LbfSearchTracePhase phase) {
        return phase == sparrow::LbfSearchTracePhase::ContainerSample
            ? "uniform"
            : "local";
    };
    const auto kindName = [](sparrow::SampleKind kind) {
        switch (kind)
        {
        case sparrow::SampleKind::SAMPLE_KIND_CLEAR:
            return "clear";
        case sparrow::SampleKind::SAMPLE_KIND_COLLISION:
            return "collision";
        case sparrow::SampleKind::SAMPLE_KIND_INVALID:
            return "invalid";
        }
        return "invalid";
    };

    output << "# generator=Sparrow SearchLbfWithTrace seed=42 n_samples=64 "
              "ls_frac=0.25 fixture=plain-square-30x30 rotation="
           << rotationName << '\n'
           << "# phase ordinal rotation_bits x_bits y_bits kind "
              "bbox_cost_bits accepted before_x_min_bits before_y_min_bits "
              "before_x_max_bits before_y_max_bits after_x_min_bits "
              "after_y_min_bits after_x_max_bits after_y_max_bits\n";

    jaguar::Rect2d activeBounds =
        layout.GetContainer().CollisionShape().BoundingBox();
    std::size_t uniformOrdinal = 0U;
    std::size_t localOrdinal = 0U;
    std::size_t candidateCount = 0U;
    std::optional<sparrow::LbfSearchTraceEvent> pendingEvaluation;
    for (const sparrow::LbfSearchTraceEvent& event : events)
    {
        if (event.type == sparrow::LbfSearchTraceEvent::Type::Evaluation)
        {
            pendingEvaluation = event;
            continue;
        }
        if (event.type != sparrow::LbfSearchTraceEvent::Type::Report
            || !pendingEvaluation.has_value())
        {
            continue;
        }

        const sparrow::LbfSearchTraceEvent& evaluation = *pendingEvaluation;
        const jaguar::Rect2d before = activeBounds;
        const float rotation =
            static_cast<float>(evaluation.transform.rotationRad);
        const float x =
            static_cast<float>(evaluation.transform.translation.x);
        const float y =
            static_cast<float>(evaluation.transform.translation.y);
        const auto transformed =
            item.CollisionShape().TransformedRustF32(evaluation.transform);
        std::optional<sparrow::LbfLoss> candidateLoss;
        if (transformed.HasValue())
        {
            candidateLoss = sparrow::LbfLoss::FromBoundingBox(
                transformed.Value().BoundingBox());
        }

        const bool uniform =
            evaluation.phase == sparrow::LbfSearchTracePhase::ContainerSample;
        const std::size_t ordinal = uniform ? uniformOrdinal++ : localOrdinal++;
        if (uniform && event.accepted && candidateLoss.has_value())
        {
            activeBounds = candidateLoss->TightenSampleBBox(activeBounds);
        }

        output << phaseName(evaluation.phase) << '\t' << ordinal << '\t'
               << floatBits(rotation) << '\t' << floatBits(x) << '\t'
               << floatBits(y) << '\t' << kindName(evaluation.evaluation.kind)
               << '\t'
               << (candidateLoss.has_value()
                       ? floatBits(candidateLoss->Cost())
                       : std::string("-"))
               << '\t' << (event.accepted ? '1' : '0') << '\t'
               << floatBits(static_cast<float>(before.xMin)) << '\t'
               << floatBits(static_cast<float>(before.yMin)) << '\t'
               << floatBits(static_cast<float>(before.xMax)) << '\t'
               << floatBits(static_cast<float>(before.yMax)) << '\t'
               << floatBits(static_cast<float>(activeBounds.xMin)) << '\t'
               << floatBits(static_cast<float>(activeBounds.yMin)) << '\t'
               << floatBits(static_cast<float>(activeBounds.xMax)) << '\t'
               << floatBits(static_cast<float>(activeBounds.yMax)) << '\n';
        ++candidateCount;
        pendingEvaluation.reset();
    }

    output << "# result_samples=" << result.Value()->samples
           << " next_rng_u64=" << std::hex << std::setfill('0')
           << std::setw(16) << rng.NextU64() << std::dec
           << " result_rotation="
           << floatBits(static_cast<float>(result.Value()->transform.rotationRad))
           << " result_x="
           << floatBits(static_cast<float>(result.Value()->transform.translation.x))
           << " result_y="
           << floatBits(static_cast<float>(result.Value()->transform.translation.y))
           << " result_cost=" << floatBits(result.Value()->loss.Cost()) << '\n';

    return Expect(candidateCount == 64U
                      && uniformOrdinal == 48U
                      && localOrdinal == 16U,
                  output,
                  "fixed-seed LBF trace probe must emit 48 uniform and 16 local rows");
}
}

bool RunLbfPinnedSearchTraceProbe(std::ostream& output)
{
    return RunLbfPinnedSearchTraceProbeWithRotation(
        {jaguar::RotationMode::None, {}}, "none", output);
}

bool RunLbfPinnedContinuousSearchTraceProbe(std::ostream& output)
{
    return RunLbfPinnedSearchTraceProbeWithRotation(
        {jaguar::RotationMode::Continuous, {}}, "continuous", output);
}

bool RunLbfPinnedDiscreteSearchTraceProbe(std::ostream& output)
{
    const double halfPi = std::acos(-1.0) / 2.0;
    return RunLbfPinnedSearchTraceProbeWithRotation(
        {jaguar::RotationMode::Discrete, {0.0, halfPi}}, "discrete", output);
}

bool RunLbfPinnedNormalComponentProbe(std::ostream& output)
{
    constexpr std::uint64_t seed = 42U;
    constexpr std::size_t uniformCandidates = 48U;
    constexpr std::size_t rawU32PerUniformCandidate = 2U;
    constexpr std::size_t componentsToTrace = 4U;
    constexpr std::uint32_t meanXBits = 0x3f1500baU;
    constexpr std::uint32_t meanYBits = 0x3f0f2398U;
    constexpr float translationStddev = 0.01F * 30.0F;

    std::ifstream input;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/LbfPinnedNormalRustTrace.tsv",
             "../SparrowCore/Tests/Fixtures/LbfPinnedNormalRustTrace.tsv",
             "../JaguarRun/SparrowCore/Tests/Fixtures/LbfPinnedNormalRustTrace.tsv",
             "..\\SparrowCore\\Tests\\Fixtures\\LbfPinnedNormalRustTrace.tsv"})
    {
        input.open(path);
        if (input.is_open())
        {
            break;
        }
        input.clear();
    }
    if (!Expect(input.is_open(),
                output,
                "pinned Rust local normal component fixture cannot be opened"))
    {
        return false;
    }

    std::string expectedPreLocalNext;
    std::vector<std::vector<std::string>> expectedRows;
    std::string line;
    while (std::getline(input, line))
    {
        constexpr std::string_view preLocalPrefix = "# pre_local_next_u64=";
        if (line.compare(0U, preLocalPrefix.size(), preLocalPrefix) == 0)
        {
            expectedPreLocalNext = line.substr(preLocalPrefix.size());
            continue;
        }
        if (line.empty() || line.front() == '#')
        {
            continue;
        }
        std::vector<std::string> fields;
        std::istringstream row(line);
        std::string field;
        while (std::getline(row, field, '\t'))
        {
            fields.push_back(field);
        }
        expectedRows.push_back(std::move(fields));
    }
    if (!Expect(!expectedPreLocalNext.empty() && expectedRows.size() == componentsToTrace,
                output,
                "pinned Rust normal fixture must contain its local-start state and four components"))
    {
        return false;
    }

    sparrow::SparrowRng rng(seed);
    for (std::size_t draw = 0U;
         draw < uniformCandidates * rawU32PerUniformCandidate;
         ++draw)
    {
        rng.NextU32();
    }

    const auto floatBits = [](float value) {
        std::uint32_t raw = 0U;
        std::memcpy(&raw, &value, sizeof(raw));
        std::ostringstream text;
        text << std::hex << std::setfill('0') << std::setw(8) << raw;
        return text.str();
    };
    const auto consumedU64Words = [](sparrow::SparrowRng before,
                                    const sparrow::SparrowRng& after) {
        for (std::size_t words = 0U; words <= 64U; ++words)
        {
            if (before.State() == after.State())
            {
                return words;
            }
            before.NextU64();
        }
        return std::size_t{65U};
    };
    const auto followingU64 = [](sparrow::SparrowRng following) {
        std::array<std::uint64_t, 4U> values{};
        for (std::uint64_t& value : values)
        {
            value = following.NextU64();
        }
        return values;
    };

    sparrow::SparrowRng preLocal = rng;
    std::ostringstream preLocalNext;
    preLocalNext << std::hex << std::setfill('0') << std::setw(16)
                 << preLocal.NextU64();
    if (!Expect(preLocalNext.str() == expectedPreLocalNext,
                output,
                "C++ LBF local-start raw RNG state must match pinned Rust"))
    {
        output << "  Rust=" << expectedPreLocalNext
               << " C++=" << preLocalNext.str() << '\n';
        return false;
    }

    output << "# generator=SparrowRng::Normal01F32 seed=" << seed
           << " after_uniform_u32_calls="
           << uniformCandidates * rawU32PerUniformCandidate << '\n'
           << "# columns: ordinal axis normal_z_bits consumed_u64_words "
              "candidate_value_bits next_u64_after_0..3\n"
           << "# pre_local_next_u64=" << preLocalNext.str() << '\n';

    for (std::size_t ordinal = 0U; ordinal < componentsToTrace; ++ordinal)
    {
        const sparrow::SparrowRng before = rng;
        const float normal = rng.Normal01F32();
        const std::size_t words = consumedU64Words(before, rng);
        const std::array<std::uint64_t, 4U> next = followingU64(rng);
        const char axis = (ordinal % 2U) == 0U ? 'x' : 'y';
        std::string candidateValue = "-";
        if (ordinal < 2U)
        {
            const float mean = FloatFromBits(axis == 'x' ? meanXBits : meanYBits);
            candidateValue = floatBits(mean + translationStddev * normal);
        }

        output << ordinal / 2U << '\t' << axis << '\t'
               << floatBits(normal) << '\t' << words << '\t'
               << candidateValue << '\t' << std::hex << std::setfill('0')
               << std::setw(16) << next[0] << '\t'
               << std::setw(16) << next[1] << '\t'
               << std::setw(16) << next[2] << '\t'
               << std::setw(16) << next[3] << std::dec << '\n';

        const std::vector<std::string>& expected = expectedRows[ordinal];
        const auto sameFollowing = [&]() {
            if (expected.size() != 9U)
            {
                return false;
            }
            for (std::size_t word = 0U; word < next.size(); ++word)
            {
                std::ostringstream actualWord;
                actualWord << std::hex << std::setfill('0') << std::setw(16)
                           << next[word];
                if (actualWord.str() != expected[5U + word])
                {
                    return false;
                }
            }
            return true;
        };
        const bool sampleValueMatches = ordinal >= 2U
            || candidateValue == expected[4U];
        if (!Expect(expected.size() == 9U
                        && expected[0U] == std::to_string(ordinal / 2U)
                        && expected[1U] == std::string(1U, axis)
                        && expected[2U] == floatBits(normal)
                        && expected[3U] == std::to_string(words)
                        && sampleValueMatches
                        && sameFollowing(),
                    output,
                    "LBF local normal components and raw RNG consumption must match pinned Rust"))
        {
            output << "  component=" << ordinal / 2U << ':' << axis
                   << " Rust_z="
                   << (expected.size() > 2U ? expected[2U] : "<missing>")
                   << " C++_z=" << floatBits(normal)
                   << " Rust_words="
                   << (expected.size() > 3U ? expected[3U] : "<missing>")
                   << " C++_words=" << words << '\n';
            return false;
        }
    }
    output << "pinned LBF normal components matched Rust: local start, "
              "4 ZIGNOR samples, per-sample raw u64 counts and following stream\n";
    return true;
}
