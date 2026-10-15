#include "SparrowCore/Eval/SeparationEvaluator.h"
#include "SparrowCore/Quantify/CollisionTracker.h"
#include "SparrowCore/Quantify/OverlapProxy.h"
#include "SparrowCore/Quantify/PairMatrix.h"
#include "SparrowCore/Optimizer/SparrowConfig.h"

#include "JaguarCore/Entities/Item.h"
#include "JaguarCore/Entities/Container.h"
#include "JaguarCore/Entities/Layout.h"
#include "JaguarCore/Collision/CollisionEngine.h"
#include "JaguarCore/Geometry/ShapeModification.h"
#include "JaguarCore/IO/Importer.h"
#include "JaguarCore/Problems/Spp.h"

#include <cmath>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <ostream>
#include <optional>
#include <sstream>
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

std::uint32_t FloatBits(float value) noexcept
{
    std::uint32_t bits = 0U;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

float FloatFromBits(std::uint32_t bits) noexcept
{
    float value = 0.0F;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

std::uint64_t PolygonF32BitsHash(const jaguar::SimplePolygon& polygon)
{
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    for (const jaguar::Point2d& point : polygon.Vertices())
    {
        for (const float value : {static_cast<float>(point.x),
                                  static_cast<float>(point.y)})
        {
            std::uint32_t bits = 0U;
            std::memcpy(&bits, &value, sizeof(bits));
            for (unsigned int byteIndex = 0U; byteIndex < 4U; ++byteIndex)
            {
                hash ^= static_cast<std::uint8_t>(bits >> (byteIndex * 8U));
                hash *= 0x100000001b3ULL;
            }
        }
    }
    return hash;
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

bool TestExtremeEpsilonOverlapProxyBoundary(std::ostream& output)
{
    const jaguar::Item item = MakeItem(90U, 2.0, 2.0);
    const jaguar::Surrogate* surrogate = item.CollisionShape().GetSurrogate();
    if (!Expect(surrogate != nullptr,
                output,
                "extreme-epsilon proxy item must have a surrogate"))
    {
        return false;
    }
    const float epsilon = std::numeric_limits<float>::max();
#ifndef NDEBUG
    bool observedRustDebugBoundary = false;
    try
    {
        static_cast<void>(sparrow::CalculateOverlapProxy(
            *surrogate,
            jaguar::Transform2d::Identity(),
            *surrogate,
            jaguar::Transform2d::Identity(),
            epsilon));
    }
    catch (const std::domain_error& error)
    {
        observedRustDebugBoundary =
            std::string(error.what()).find("total_overlap.is_normal()")
            != std::string::npos;
    }
    return Expect(observedRustDebugBoundary,
                  output,
                  "Debug raw proxy must surface Rust's NaN assertion");
#else
    const float raw = sparrow::CalculateOverlapProxy(
        *surrogate,
        jaguar::Transform2d::Identity(),
        *surrogate,
        jaguar::Transform2d::Identity(),
        epsilon);
    return Expect(std::isnan(raw),
                  output,
                  "Release raw proxy must preserve Rust's NaN arithmetic");
#endif
}

bool TestSubnormalPolygonLossBranch(std::ostream& output)
{
    constexpr double half = 5.0e-21;
    const auto polygonResult = jaguar::SimplePolygon::Create({
        {-half, -half}, {half, -half}, {half, half}, {-half, half}});
    if (!Expect(polygonResult.HasValue(),
                output,
                "subnormal-area polygon must construct for the loss probe"))
    {
        return false;
    }

    jaguar::SimplePolygon polygon = polygonResult.Value();
    jaguar::SurrogateConfig config{};
    config.poleLimits = {{{2U, 0.0}, {2U, 0.8}, {2U, 0.9}}};
    if (!Expect(polygon.GenerateSurrogate(config).HasValue(),
                output,
                "subnormal-area surrogate must construct for the loss probe"))
    {
        return false;
    }

#ifndef NDEBUG
    bool observedRustDebugBoundary = false;
    try
    {
        static_cast<void>(sparrow::CalculatePolygonCollisionLoss(polygon, polygon));
    }
    catch (const std::domain_error& error)
    {
        observedRustDebugBoundary =
            std::string(error.what()).find("total_overlap.is_normal()")
            != std::string::npos;
    }
    return Expect(observedRustDebugBoundary,
                  output,
                  "Debug pair helper must surface Rust's subnormal proxy assertion");
#else
    const float diameter = static_cast<float>(polygon.Diameter());
    const float epsilon = diameter * 0.01F;
    const float rawProxy = sparrow::CalculateOverlapProxy(
        *polygon.GetSurrogate(),
        jaguar::Transform2d::Identity(),
        *polygon.GetSurrogate(),
        jaguar::Transform2d::Identity(),
        epsilon);
    const float loss = sparrow::CalculatePolygonCollisionLoss(polygon, polygon);
    output << "subnormal_overlap_proxy bits=0x" << std::hex << FloatBits(rawProxy)
           << " loss bits=0x" << FloatBits(loss) << std::dec << '\n';
    return Expect(FloatBits(rawProxy) == 0x0001c3bdU
                      && FloatBits(loss) == 0x000162e2U,
                  output,
                  "subnormal pair loss must match Rust Release raw bits");
#endif
}

bool TestFiniteTransformOverflowingContainerBboxAreaMatchesRust(
    std::ostream& output)
{
    constexpr double side = 1.2e19;
    constexpr double half = side * 0.5;
    const auto sourceResult = jaguar::SimplePolygon::Create({
        {-half, -half}, {half, -half}, {half, half}, {-half, half}});
    if (!Expect(sourceResult.HasValue(), output,
                "finite large source square must construct"))
    {
        return false;
    }

    jaguar::SimplePolygon source = sourceResult.Value();
    jaguar::SurrogateConfig surrogateConfig{};
    surrogateConfig.poleLimits = {{{2U, 0.0}, {2U, 0.8}, {2U, 0.9}}};
    if (!Expect(source.GenerateSurrogate(surrogateConfig).HasValue(),
                output,
                "finite large source square surrogate must construct"))
    {
        return false;
    }

    const float translation = std::ldexp(1.5F, 86);
    if (!Expect(FloatBits(translation) == 0x6ac00000U,
                output,
                "candidate translation must use the locked Rust f32 value"))
    {
        return false;
    }
    const jaguar::Transform2d candidateTransform =
        jaguar::Transform2d::FromRotationTranslation(
            0.0, {static_cast<double>(translation),
                  static_cast<double>(translation)});
    const auto candidate = source.TransformedRustF32(candidateTransform);
    if (!Expect(candidate.HasValue(), output,
                "finite candidate transform must preserve a valid bbox"))
    {
        return false;
    }

    const jaguar::Rect2d& candidateBounds = candidate.Value().BoundingBox();
    const float width = static_cast<float>(candidateBounds.xMax)
        - static_cast<float>(candidateBounds.xMin);
    const float height = static_cast<float>(candidateBounds.yMax)
        - static_cast<float>(candidateBounds.yMin);
    const float bboxArea = width * height;
    if (!Expect(source.Area() > 0.0 && std::isfinite(source.Area())
                    && source.Diameter() > 0.0
                    && std::isfinite(source.Diameter())
                    && FloatBits(static_cast<float>(source.Area())) == 0x7ed8aac7U
                    && FloatBits(static_cast<float>(source.Diameter())) == 0x5f6b838cU
                    && FloatBits(width) == 0x5f800000U
                    && FloatBits(height) == 0x5f800000U
                    && std::isinf(bboxArea),
                output,
                "finite source and transform must reach an infinite candidate bbox area"))
    {
        output << "  C++ bits: source_area=0x" << std::hex
               << FloatBits(static_cast<float>(source.Area()))
               << " diameter=0x" << FloatBits(static_cast<float>(source.Diameter()))
               << " width=0x" << FloatBits(width)
               << " height=0x" << FloatBits(height)
               << " area=0x" << FloatBits(bboxArea) << std::dec << '\n';
        return false;
    }

    const jaguar::Rect2d containerBounds{-1.0, -1.0, 1.0, 1.0};
#ifndef NDEBUG
    bool observedRustDebugBoundary = false;
    try
    {
        static_cast<void>(sparrow::CalculateContainerPenalty(
            candidate.Value(), containerBounds));
    }
    catch (const std::domain_error& error)
    {
        observedRustDebugBoundary =
            std::string(error.what()).find("overlap.is_normal()")
            != std::string::npos;
    }
    return Expect(observedRustDebugBoundary,
                  output,
                  "Debug must surface Rust's non-normal overlap assertion");
#else
    const float loss = sparrow::CalculateContainerPenalty(
        candidate.Value(), containerBounds);
    return Expect(loss == std::numeric_limits<float>::infinity()
                      && FloatBits(loss) == 0x7f800000U,
                  output,
                  "Release must preserve Rust's positive-infinity container loss");
#endif
}

bool TestSubnormalLossThroughSeparationEvaluator(std::ostream& output)
{
    constexpr double side = 1.0e-20;
    constexpr double fixedHeight = 2.0e-19;
    jaguar::SurrogateConfig surrogateConfig{};
    surrogateConfig.poleLimits = {{{2U, 0.0}, {2U, 0.8}, {2U, 0.9}}};

    const auto polygon0 = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {side, 0.0}, {side, side}, {0.0, side}});
    const auto polygon1 = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {side, 0.0}, {side, side}, {0.0, side}});
    if (!Expect(polygon0.HasValue() && polygon1.HasValue(),
                output,
                "tiny finite item polygons must construct"))
    {
        return false;
    }
    const auto item0 = jaguar::Item::Create(
        0U,
        1U,
        {polygon0.Value(),
         jaguar::Transform2d::Identity(),
         jaguar::ShapeModifyMode::Inflate,
         {}},
        surrogateConfig);
    const auto item1 = jaguar::Item::Create(
        1U,
        2U,
        {polygon1.Value(),
         jaguar::Transform2d::Identity(),
         jaguar::ShapeModifyMode::Inflate,
         {}},
        surrogateConfig);
    if (!Expect(item0.HasValue() && item1.HasValue(),
                output,
                "tiny finite items and their surrogates must construct"))
    {
        return false;
    }

    jaguar::CDEConfig cdeConfig{};
    cdeConfig.itemSurrogateConfig = surrogateConfig;
    const auto strip = jaguar::Strip::Create(
        fixedHeight, cdeConfig, {}, fixedHeight);
    if (!Expect(strip.HasValue(), output, "tiny strip must construct"))
    {
        return false;
    }
    const auto instance = jaguar::SPInstance::Create(
        {item0.Value(), item1.Value()}, {1U, 1U}, strip.Value());
    if (!Expect(instance.HasValue(), output, "tiny SPP instance must construct"))
    {
        return false;
    }
    auto problemResult = jaguar::SPProblem::Create(instance.Value());
    if (!Expect(problemResult.HasValue(), output, "tiny SPP problem must construct"))
    {
        return false;
    }
    jaguar::SPProblem problem = std::move(problemResult.Value());
    const auto first = problem.PlaceItem({
        0U,
        jaguar::Transform2d::FromRotationTranslation(
            0.0, {0.5e-19, 0.5e-19})});
    const auto second = problem.PlaceItem({
        1U,
        jaguar::Transform2d::FromRotationTranslation(
            0.0, {1.5e-19, 1.5e-19})});
    if (!Expect(first.HasValue() && second.HasValue(),
                output,
                "tiny separated initial placements must succeed"))
    {
        return false;
    }
    const jaguar::SPSolution solution = problem.Save();
    jaguar::Layout layout(solution.layoutSnapshot.container);
    if (!Expect(layout.Restore(solution.layoutSnapshot).HasValue(),
                output,
                "tiny layout restore must succeed"))
    {
        return false;
    }
    auto trackerResult = sparrow::CollisionTracker::Create(layout);
    if (!Expect(trackerResult.HasValue(),
                output,
                "non-overlapping tiny items must create a tracker"))
    {
        return false;
    }
    sparrow::CollisionTracker tracker = std::move(trackerResult.Value());
    sparrow::SeparationEvaluator evaluator(
        layout, problem.Instance().GetItem(0U), first.Value(), tracker);
    const jaguar::Transform2d candidateTransform =
        jaguar::Transform2d::FromRotationTranslation(
            0.0, {1.5e-19, 1.5e-19});

#ifndef NDEBUG
    bool observedRustDebugBoundary = false;
    try
    {
        static_cast<void>(evaluator.Evaluate(candidateTransform, std::nullopt));
    }
    catch (const std::domain_error& error)
    {
        observedRustDebugBoundary =
            std::string(error.what()).find("total_overlap.is_normal()")
            != std::string::npos;
    }
    return Expect(observedRustDebugBoundary,
                  output,
                  "Debug evaluator must surface Rust's subnormal proxy assertion");
#else
    const sparrow::SampleEval evaluation =
        evaluator.Evaluate(candidateTransform, std::nullopt);
    output << "actual_evaluator_subnormal_loss bits=0x" << std::hex
           << FloatBits(evaluation.loss) << std::dec << '\n';
    return Expect(evaluation.kind == sparrow::SampleKind::SAMPLE_KIND_COLLISION
                      && FloatBits(evaluation.loss) == 0x000162e2U,
                  output,
                  "Release evaluator must match Rust's subnormal collision loss");
#endif
}

bool TestPositiveShoelaceZeroBboxCandidateIsRejectedBeforeLoss(
    std::ostream& output)
{
    const std::vector<jaguar::Point2d> points{
        {static_cast<double>(FloatFromBits(0x000003caU)),
         static_cast<double>(FloatFromBits(0x3d7fffffU))},
        {static_cast<double>(FloatFromBits(0x00000aadU)),
         static_cast<double>(FloatFromBits(0x3d7fffffU))},
        {static_cast<double>(FloatFromBits(0x00000d11U)),
         static_cast<double>(FloatFromBits(0x3d7fffffU))},
        {static_cast<double>(FloatFromBits(0x000009d3U)),
         static_cast<double>(FloatFromBits(0x3d800000U))},
        {static_cast<double>(FloatFromBits(0x00000f67U)),
         static_cast<double>(FloatFromBits(0x3d800000U))},
        {static_cast<double>(FloatFromBits(0x00000528U)),
         static_cast<double>(FloatFromBits(0x3d800000U))},
        {static_cast<double>(FloatFromBits(0x000000a8U)),
         static_cast<double>(FloatFromBits(0x3d800000U))},
        {static_cast<double>(FloatFromBits(0x00000011U)),
         static_cast<double>(FloatFromBits(0x3d800000U))},
    };

    float xMin = std::numeric_limits<float>::infinity();
    float yMin = std::numeric_limits<float>::infinity();
    float xMax = -std::numeric_limits<float>::infinity();
    float yMax = -std::numeric_limits<float>::infinity();
    for (const jaguar::Point2d& point : points)
    {
        const float x = static_cast<float>(point.x);
        const float y = static_cast<float>(point.y);
        xMin = std::min(xMin, x);
        yMin = std::min(yMin, y);
        xMax = std::max(xMax, x);
        yMax = std::max(yMax, y);
    }
    const float width = xMax - xMin;
    const float height = yMax - yMin;
    float twiceArea = 0.0F;
    for (std::size_t i = 0; i < points.size(); ++i)
    {
        const std::size_t j = (i + 1U) % points.size();
        const float xI = static_cast<float>(points[i].x);
        const float yI = static_cast<float>(points[i].y);
        const float xJ = static_cast<float>(points[j].x);
        const float yJ = static_cast<float>(points[j].y);
        twiceArea += (yI + yJ) * (xI - xJ);
    }
    const float area = 0.5F * twiceArea;
    if (!Expect(width > 0.0F && height > 0.0F && width * height == 0.0F,
                output,
                "fixture must retain positive f32 bbox axes with zero area product"))
    {
        return false;
    }
    if (!Expect(FloatBits(area) == 0x00000001U,
                output,
                "fixture shoelace area must match Rust's positive minimum subnormal"))
    {
        return false;
    }

    const auto candidate = jaguar::SimplePolygon::Create(points);
    return Expect(!candidate.HasValue()
                      && candidate.GetError().code == jaguar::ErrorCode::InvalidPolygon,
                  output,
                  "C++ POI construction must reject the zero-clearance candidate before loss");
}

bool TestSubnormalContainerLossThroughEvaluator(std::ostream& output)
{
    constexpr double side = 1.0e-20;
    constexpr double containerSide = 2.0e-19;
    const auto polygon = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {side, 0.0}, {side, side}, {0.0, side}});
    if (!Expect(polygon.HasValue(), output, "tiny container-probe item must construct"))
    {
        return false;
    }
    jaguar::SurrogateConfig surrogateConfig{};
    surrogateConfig.poleLimits = {{{2U, 0.0}, {2U, 0.8}, {2U, 0.9}}};
    const auto item = jaguar::Item::Create(
        0U,
        3U,
        {polygon.Value(),
         jaguar::Transform2d::Identity(),
         jaguar::ShapeModifyMode::Inflate,
         {}},
        surrogateConfig);
    if (!Expect(item.HasValue(), output, "tiny container-probe item must have a surrogate"))
    {
        return false;
    }
    jaguar::CDEConfig cdeConfig{};
    cdeConfig.itemSurrogateConfig = surrogateConfig;
    const auto strip = jaguar::Strip::Create(
        containerSide, cdeConfig, {}, containerSide);
    if (!Expect(strip.HasValue(), output, "tiny container-probe strip must construct"))
    {
        return false;
    }
    const auto instance = jaguar::SPInstance::Create(
        {item.Value()}, {1U}, strip.Value());
    if (!Expect(instance.HasValue(), output, "tiny container-probe instance must construct"))
    {
        return false;
    }
    auto problemResult = jaguar::SPProblem::Create(instance.Value());
    if (!Expect(problemResult.HasValue(), output, "tiny container-probe problem must construct"))
    {
        return false;
    }
    jaguar::SPProblem problem = std::move(problemResult.Value());
    const auto handle = problem.PlaceItem({
        0U,
        jaguar::Transform2d::FromRotationTranslation(
            0.0, {1.0e-19, 1.0e-19})});
    if (!Expect(handle.HasValue(), output, "tiny interior placement must succeed"))
    {
        return false;
    }
    const jaguar::SPSolution solution = problem.Save();
    jaguar::Layout layout(solution.layoutSnapshot.container);
    if (!Expect(layout.Restore(solution.layoutSnapshot).HasValue(),
                output,
                "tiny container-probe layout restore must succeed"))
    {
        return false;
    }
    auto trackerResult = sparrow::CollisionTracker::Create(layout);
    if (!Expect(trackerResult.HasValue(),
                output,
                "tiny interior placement must create a tracker"))
    {
        return false;
    }
    sparrow::CollisionTracker tracker = std::move(trackerResult.Value());
    sparrow::SeparationEvaluator evaluator(
        layout, problem.Instance().GetItem(0U), handle.Value(), tracker);
    const jaguar::Transform2d outsideTransform =
        jaguar::Transform2d::FromRotationTranslation(
            0.0, {-2.0e-21, -2.0e-21});

#ifndef NDEBUG
    bool observedRustDebugBoundary = false;
    try
    {
        static_cast<void>(evaluator.Evaluate(outsideTransform, std::nullopt));
    }
    catch (const std::domain_error& error)
    {
        observedRustDebugBoundary =
            std::string(error.what()).find("overlap.is_normal()")
            != std::string::npos;
    }
    return Expect(observedRustDebugBoundary,
                  output,
                  "Debug evaluator must surface Rust's subnormal container assertion");
#else
    const sparrow::SampleEval evaluation =
        evaluator.Evaluate(outsideTransform, std::nullopt);
    output << "actual_evaluator_subnormal_container_loss bits=0x" << std::hex
           << FloatBits(evaluation.loss) << std::dec << '\n';
    return Expect(evaluation.kind == sparrow::SampleKind::SAMPLE_KIND_COLLISION
                      && FloatBits(evaluation.loss) == 0x00014e8eU,
                  output,
                  "Release evaluator must match Rust's subnormal container loss");
#endif
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

bool TestFiniteExteriorCentroidDistanceOverflowMatchesRust(std::ostream& output)
{
    std::ifstream fixture(
        "SparrowCore/Tests/Fixtures/CollisionLossFiniteDistanceOverflowRust.json");
    if (!fixture.is_open())
    {
        fixture.open(
            "../SparrowCore/Tests/Fixtures/CollisionLossFiniteDistanceOverflowRust.json");
    }
    if (!Expect(fixture.is_open(), output,
                "finite-distance-overflow Rust fixture could not be opened"))
    {
        return false;
    }

    const std::string json{std::istreambuf_iterator<char>(fixture),
                           std::istreambuf_iterator<char>()};
    const jaguar::Importer importer(sparrow::MakeRustDefaultImporterConfig());
    const auto instance = importer.ImportSPInstanceJson(json);
    if (!Expect(instance.HasValue(), output,
                "finite-distance-overflow Rust fixture import failed"))
    {
        output << "  error: " << instance.GetError().message << '\n';
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
    if (!Expect(strip.SetWidth(4.0e12).HasValue(), output,
                "finite-distance-overflow strip width setup failed"))
    {
        return false;
    }
    const auto explicitWidthInstance = jaguar::SPInstance::Create(
        std::move(items), std::move(demands), strip);
    if (!Expect(explicitWidthInstance.HasValue(), output,
                "finite-distance-overflow explicit-width instance creation failed"))
    {
        return false;
    }
    auto problemResult = jaguar::SPProblem::Create(explicitWidthInstance.Value());
    if (!Expect(problemResult.HasValue(), output,
                "finite-distance-overflow problem creation failed"))
    {
        output << "  error: " << problemResult.GetError().message << '\n';
        return false;
    }
    jaguar::SPProblem problem = std::move(problemResult.Value());
    const jaguar::Transform2d originalTransform =
        jaguar::Transform2d::FromRotationTranslation(0.0, {2.0e12, 1.0e12});
    const auto movingHandle = problem.PlaceItem({0U, originalTransform});
    if (!Expect(movingHandle.HasValue(), output,
                "finite-distance-overflow item placement failed"))
    {
        return false;
    }

    const jaguar::SPSolution solution = problem.Save();
    jaguar::Layout layout(solution.layoutSnapshot.container);
    if (!Expect(layout.Restore(solution.layoutSnapshot).HasValue(),
                output,
                "finite-distance-overflow layout restore failed"))
    {
        return false;
    }
    auto trackerResult = sparrow::CollisionTracker::Create(layout);
    if (!Expect(trackerResult.HasValue(), output,
                "finite-distance-overflow tracker creation failed"))
    {
        return false;
    }
    sparrow::CollisionTracker tracker = std::move(trackerResult.Value());
    const std::uint32_t originalLossBits = [&tracker]() {
        const float loss = tracker.TotalLoss();
        std::uint32_t bits = 0U;
        std::memcpy(&bits, &loss, sizeof(bits));
        return bits;
    }();

    sparrow::SeparationEvaluator evaluator(
        layout,
        instance.Value().GetItem(0U),
        movingHandle.Value(),
        tracker);
    const jaguar::Transform2d farButFinite =
        jaguar::Transform2d::FromRotationTranslation(0.0, {1.9e19, 1.0e12});
    bool debugThrew = false;
    sparrow::SampleEval farEvaluation{};
    try
    {
        farEvaluation = evaluator.Evaluate(farButFinite, std::nullopt);
    }
    catch (const std::domain_error& error)
    {
        debugThrew = true;
        if (!Expect(std::string(error.what()).find("overlap.is_normal")
                        != std::string::npos,
                    output,
                    "finite centroid overflow must preserve the Rust assertion message"))
        {
            return false;
        }
    }

#ifndef NDEBUG
    if (!Expect(debugThrew, output,
                "Debug must assert when finite centroid distance overflows"))
    {
        return false;
    }
#else
    if (!Expect(!debugThrew
                    && farEvaluation.kind == sparrow::SampleKind::SAMPLE_KIND_COLLISION
                    && std::isinf(farEvaluation.loss)
                    && farEvaluation.loss > 0.0F,
                output,
                "Release must preserve the Rust positive-infinity collision result"))
    {
        return false;
    }
#endif

    if (!Expect(evaluator.EvaluationCount() == 1U,
                output,
                "overflow evaluation must be counted exactly once"))
    {
        return false;
    }
    const sparrow::SampleEval recovered = evaluator.Evaluate(
        originalTransform, std::nullopt);
    std::uint32_t recoveredLossBits = 0U;
    std::memcpy(&recoveredLossBits, &recovered.loss, sizeof(recoveredLossBits));
    const float finalTotalLoss = tracker.TotalLoss();
    std::uint32_t finalLossBits = 0U;
    std::memcpy(&finalLossBits, &finalTotalLoss, sizeof(finalLossBits));
    return Expect(recovered.kind == sparrow::SampleKind::SAMPLE_KIND_CLEAR
                      && recoveredLossBits == 0U
                      && evaluator.EvaluationCount() == 2U
                      && finalLossBits == originalLossBits,
                  output,
                  "evaluator must recover with unchanged layout/tracker after overflow");
}

bool TestFinalMultiplyOverflowSquareMatchesRustConstruction(
    std::ostream& output)
{
    const float bboxArea = std::numeric_limits<float>::max() * 0.49999F;
    const float side = std::sqrt(bboxArea);
    const float halfSide = side * 0.5F;
    const float actualBoxArea = side * side;
    const float diameterSquared = actualBoxArea + actualBoxArea;
    if (!Expect(std::isfinite(actualBoxArea) && std::isfinite(diameterSquared),
                output,
                "overflow fixture must have finite area and diameter"))
    {
        return false;
    }

    const std::vector<jaguar::Point2d> points{
        {-halfSide, -halfSide},
        {halfSide, -halfSide},
        {halfSide, halfSide},
        {-halfSide, halfSide},
    };
    const auto polygon = jaguar::SimplePolygon::Create(points);
    if (!Expect(polygon.HasValue(), output,
                "Rust accepts a finite-vertex polygon with a non-finite lazy centroid"))
    {
        if (!polygon.HasValue())
            output << "  polygon error: " << polygon.GetError().message << '\n';
        return false;
    }

    const jaguar::Point2d centroid = polygon.Value().Centroid();
    if (!Expect(std::isfinite(polygon.Value().Area())
                    && std::isfinite(polygon.Value().BoundingBox().AreaRustF32())
                    && std::isfinite(polygon.Value().Diameter())
                    && !centroid.IsFinite(),
                  output,
                  "finite polygon construction must preserve Rust's lazy non-finite centroid result"))
    {
        return false;
    }

    jaguar::ExternalItem external{};
    external.id = 4U;
    external.rotation = jaguar::RotationPolicy::Continuous();
    external.shape = jaguar::ExternalShape::SimplePolygon(points);
    const jaguar::Importer importer(sparrow::MakeRustDefaultImporterConfig());
    const auto imported = importer.ImportItem(external, 0U);
    return Expect(!imported.HasValue(), output,
                  "a non-finite external centering transform must still stop import before evaluation");
}

bool TestStrictConvexInflateContourOrderMatchesRust(std::ostream& output)
{
    const auto checkOffset = [&output](
                                std::vector<jaguar::Point2d> vertices,
                                std::uint32_t areaBits,
                                std::uint32_t bboxBits,
                                std::uint64_t hash,
                                const char* label) {
        const auto shape = jaguar::SimplePolygon::Create(std::move(vertices));
        if (!Expect(shape.HasValue(), output,
                    "strict-convex offset reference fixture must construct"))
        {
            return false;
        }
        const auto offset = jaguar::OffsetShape(
            shape.Value(), jaguar::ShapeModifyMode::Inflate, 1.25);
        if (!Expect(offset.HasValue(), output,
                    "strict-convex offset reference fixture must inflate"))
        {
            return false;
        }

        const jaguar::SimplePolygon& polygon = offset.Value();
        const std::uint64_t actualHash = PolygonF32BitsHash(polygon);
        const std::uint32_t actualAreaBits =
            FloatBits(static_cast<float>(polygon.Area()));
        const std::uint32_t actualBboxBits = FloatBits(static_cast<float>(
            polygon.BoundingBox().AreaRustF32()));
        if (!Expect(polygon.VertexCount() == 66U &&
                        actualAreaBits == areaBits &&
                        actualBboxBits == bboxBits && actualHash == hash,
                    output,
                    "strict-convex offset vertex order and metrics must match Rust"))
        {
            output << "  " << label << " count: " << polygon.VertexCount()
                   << " hash: " << std::hex << actualHash
                   << " area bits: " << actualAreaBits
                   << " bbox bits: " << actualBboxBits << std::dec << '\n';
            return false;
        }
        return true;
    };

    if (!checkOffset({{-3.0, -2.0}, {8.0, -1.0}, {7.0, 6.0}, {-4.0, 5.0}},
                     0x43003100U, 0x431839f6U, 0x3eeb7f64793c4728ULL,
                     "skew quadrilateral") ||
        !checkOffset({{8.0, -1.0}, {7.0, 6.0}, {-4.0, 5.0}, {-3.0, -2.0}},
                     0x43003100U, 0x431839f6U, 0x3eeb7f64793c4728ULL,
                     "rotated vertex start") ||
        !checkOffset({{-2.0, -3.0}, {9.0, -1.0}, {6.0, 7.0}, {-5.0, 4.0}},
                     0x430f53e0U, 0x434e36c0U, 0xc89c2d67a1a13ff5ULL,
                     "independent skew quadrilateral"))
    {
        return false;
    }

    const auto largeSquare = jaguar::SimplePolygon::Create({
        {1000000.0, 1000000.0}, {2000000.0, 1000000.0},
        {2000000.0, 2000000.0}, {1000000.0, 2000000.0}});
    if (!Expect(largeSquare.HasValue(), output,
                "sub-ULP offset square must construct"))
    {
        return false;
    }
    const auto subUlpOffset = jaguar::OffsetShape(
        largeSquare.Value(), jaguar::ShapeModifyMode::Inflate, 0.01);
    if (!Expect(subUlpOffset.HasValue(), output,
                "sub-ULP convex offset must succeed"))
    {
        return false;
    }
    const std::vector<jaguar::Point2d> expectedSubUlpVertices = {
        {1000000.0, 2000000.0}, {1000000.0, 1000000.0},
        {2000000.0, 1000000.0}, {2000000.0, 2000000.0}};
    return Expect(subUlpOffset.Value().Vertices() == expectedSubUlpVertices &&
                      FloatBits(static_cast<float>(subUlpOffset.Value().Area()))
                          == 0x5368d4a5U &&
                      PolygonF32BitsHash(subUlpOffset.Value())
                          == 0x30b539c7ccd489c5ULL,
                  output,
                  "sub-ULP offset bits and contour start must match Rust");
}

bool TestConcaveInflateContourBitsMatchRust(std::ostream& output)
{
    const auto shape = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {6.0, 0.0}, {6.0, 2.0},
        {2.0, 2.0}, {2.0, 6.0}, {0.0, 6.0}});
    if (!Expect(shape.HasValue(), output,
                "concave offset reference fixture must construct"))
    {
        return false;
    }

    const auto offset = jaguar::OffsetShape(
        shape.Value(), jaguar::ShapeModifyMode::Inflate, 0.25);
    if (!Expect(offset.HasValue(), output,
                "concave offset reference fixture must inflate"))
    {
        return false;
    }

    const jaguar::SimplePolygon& polygon = offset.Value();
    const std::uint64_t actualHash = PolygonF32BitsHash(polygon);
    const std::uint32_t actualAreaBits =
        FloatBits(static_cast<float>(polygon.Area()));
    const std::uint32_t actualBboxBits = FloatBits(static_cast<float>(
        polygon.BoundingBox().AreaRustF32()));
    if (!Expect(polygon.VertexCount() == 86U &&
                    actualAreaBits == 0x41d175dbU &&
                    actualBboxBits == 0x42290000U &&
                    actualHash == 0xbf324096dceae641ULL,
                output,
                "concave Inflate contour bits must match Rust"))
    {
        output << "  concave L count: " << polygon.VertexCount()
               << " hash: " << std::hex << actualHash
               << " area bits: " << actualAreaBits
               << " bbox bits: " << actualBboxBits << std::dec << '\n';
        return false;
    }
    return true;
}

bool TestConvexDeflateContourBitsMatchRust(std::ostream& output)
{
    const auto shape = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {4.0, 0.0}, {5.0, 2.0},
        {2.0, 4.0}, {-1.0, 2.0}});
    if (!Expect(shape.HasValue(), output,
                "convex offset reference fixture must construct"))
    {
        return false;
    }

    const auto offset = jaguar::OffsetShape(
        shape.Value(), jaguar::ShapeModifyMode::Deflate, 0.3);
    if (!Expect(offset.HasValue(), output,
                "convex offset reference fixture must deflate"))
    {
        return false;
    }

    const jaguar::SimplePolygon& polygon = offset.Value();
    const std::uint64_t actualHash = PolygonF32BitsHash(polygon);
    const std::uint32_t actualAreaBits =
        FloatBits(static_cast<float>(polygon.Area()));
    const std::uint32_t actualBboxBits = FloatBits(static_cast<float>(
        polygon.BoundingBox().AreaRustF32()));
    if (!Expect(polygon.VertexCount() == 5U &&
                    actualAreaBits == 0x413a00b7U &&
                    actualBboxBits == 0x418ba0c6U &&
                    actualHash == 0x1710858bdb9fdb5fULL,
                output,
                "convex Deflate contour bits must match Rust"))
    {
        output << "  convex pentagon count: " << polygon.VertexCount()
               << " hash: " << std::hex << actualHash
               << " area bits: " << actualAreaBits
               << " bbox bits: " << actualBboxBits << std::dec << '\n';
        return false;
    }
    return true;
}

bool TestOffsetOrientationDegenerateScaleMatchesRust(std::ostream& output)
{
    const auto check = [&output](std::vector<jaguar::Point2d> vertices,
                                 jaguar::ShapeModifyMode mode,
                                 double distance,
                                 std::size_t expectedCount,
                                 std::uint32_t expectedAreaBits,
                                 std::uint32_t expectedBboxBits,
                                 std::uint64_t expectedHash,
                                 const char* label) {
        const auto shape = jaguar::SimplePolygon::Create(std::move(vertices));
        if (!Expect(shape.HasValue(), output,
                    "offset boundary reference fixture must construct"))
        {
            return false;
        }
        const auto offset = jaguar::OffsetShape(shape.Value(), mode, distance);
        if (!Expect(offset.HasValue(), output,
                    "offset boundary reference fixture must offset"))
        {
            return false;
        }

        const jaguar::SimplePolygon& polygon = offset.Value();
        const std::uint32_t areaBits =
            FloatBits(static_cast<float>(polygon.Area()));
        const std::uint32_t bboxBits = FloatBits(static_cast<float>(
            polygon.BoundingBox().AreaRustF32()));
        const std::uint64_t hash = PolygonF32BitsHash(polygon);
        if (!Expect(polygon.VertexCount() == expectedCount &&
                        areaBits == expectedAreaBits &&
                        bboxBits == expectedBboxBits && hash == expectedHash,
                    output, "offset boundary contour bits must match Rust"))
        {
            output << "  " << label << " count: " << polygon.VertexCount()
                   << " hash: " << std::hex << hash
                   << " area bits: " << areaBits
                   << " bbox bits: " << bboxBits << std::dec << '\n';
            return false;
        }
        return true;
    };

    if (!check({{-1.0, 2.0}, {2.0, 4.0}, {5.0, 2.0},
                {4.0, 0.0}, {0.0, 0.0}},
               jaguar::ShapeModifyMode::Deflate, 0.3, 5U,
               0x413a00b7U, 0x418ba0c6U, 0x1710858bdb9fdb5fULL,
               "clockwise convex pentagon") ||
        !check({{0.0, 0.0}, {2.0, 0.0}, {4.0, 0.0}, {5.0, 2.0},
                {2.0, 4.0}, {-1.0, 2.0}},
               jaguar::ShapeModifyMode::Deflate, 0.3, 5U,
               0x413a00b7U, 0x418ba0c6U, 0x1710858bdb9fdb5fULL,
               "collinear convex pentagon"))
    {
        return false;
    }

    const double sideBelow = static_cast<double>(FloatFromBits(0x4635008cU));
    const double sideAbove = static_cast<double>(FloatFromBits(0x4635008eU));
    const auto square = [](double side) {
        return std::vector<jaguar::Point2d>{{0.0, 0.0}, {side, 0.0},
                                             {side, side}, {0.0, side}};
    };
    return check(square(sideBelow), jaguar::ShapeModifyMode::Inflate, 0.5,
                 68U, 0x4cfffedbU, 0x4cfffedcU, 0x2b698cbf3a05d425ULL,
                 "scale below adapter exponent boundary") &&
           check(square(sideAbove), jaguar::ShapeModifyMode::Inflate, 0.5,
                 68U, 0x4cfffee1U, 0x4cfffee2U, 0x4248ff4142d7c2d5ULL,
                 "scale above adapter exponent boundary");
}

bool TestSppImportedOffsetReachesSeparationEvaluator(std::ostream& output)
{
    std::ifstream fixture(
        "SparrowCore/Tests/Fixtures/ShapeOffsetEvaluatorSpp.json");
    if (!fixture.is_open())
    {
        fixture.open(
            "../SparrowCore/Tests/Fixtures/ShapeOffsetEvaluatorSpp.json");
    }
    if (!Expect(fixture.is_open(), output,
                "SPP offset/evaluator reference fixture could not be opened"))
    {
        return false;
    }

    const std::string json{std::istreambuf_iterator<char>(fixture),
                           std::istreambuf_iterator<char>()};
    const jaguar::Importer importer(sparrow::MakeRustDefaultImporterConfig());
    const auto imported = importer.ImportSPInstanceJson(json);
    if (!Expect(imported.HasValue(), output,
                "SPP offset/evaluator reference fixture import failed"))
    {
        output << "  import error: " << imported.GetError().message << '\n';
        return false;
    }

    const jaguar::SimplePolygon& collisionShape =
        imported.Value().GetItem(1U).CollisionShape();
    if (!Expect(collisionShape.VertexCount() == 10U &&
                    FloatBits(static_cast<float>(collisionShape.Area())) ==
                        0x41d1a88cU &&
                    FloatBits(static_cast<float>(
                        collisionShape.BoundingBox().AreaRustF32())) ==
                        0x42290000U &&
                    PolygonF32BitsHash(collisionShape) ==
                        0x1bed85a4ace8dc97ULL,
                output,
                "SPP import must create the Rust offset collision contour"))
    {
        output << "  imported collision shape count: "
               << collisionShape.VertexCount() << " hash: " << std::hex
               << PolygonF32BitsHash(collisionShape) << " area bits: "
               << FloatBits(static_cast<float>(collisionShape.Area()))
               << " bbox bits: "
               << FloatBits(static_cast<float>(
                      collisionShape.BoundingBox().AreaRustF32()))
               << std::dec << '\n';
        return false;
    }

    std::vector<jaguar::Item> items;
    std::vector<std::size_t> demands;
    items.reserve(imported.Value().ItemCount());
    demands.reserve(imported.Value().ItemCount());
    for (std::size_t index = 0U; index < imported.Value().ItemCount(); ++index)
    {
        items.push_back(imported.Value().GetItem(index));
        demands.push_back(imported.Value().ItemDemand(index));
    }
    jaguar::Strip strip = imported.Value().BaseStrip();
    if (!Expect(strip.SetWidth(16.0).HasValue(), output,
                "SPP offset/evaluator strip width setup failed"))
    {
        return false;
    }
    const auto explicitWidthInstance = jaguar::SPInstance::Create(
        std::move(items), std::move(demands), strip);
    if (!Expect(explicitWidthInstance.HasValue(), output,
                "SPP offset/evaluator explicit-width instance creation failed"))
    {
        return false;
    }
    auto problemResult = jaguar::SPProblem::Create(explicitWidthInstance.Value());
    if (!Expect(problemResult.HasValue(), output,
                "SPP offset/evaluator problem creation failed"))
    {
        return false;
    }
    jaguar::SPProblem problem = std::move(problemResult.Value());
    const jaguar::Transform2d transform =
        jaguar::Transform2d::FromRotationTranslation(0.0, {5.0, 4.0});
    const auto firstHandle = problem.PlaceItem({0U, transform});
    const auto movingHandle = problem.PlaceItem({1U, transform});
    if (!Expect(firstHandle.HasValue() && movingHandle.HasValue(), output,
                "SPP offset/evaluator item placements failed"))
    {
        return false;
    }

    const jaguar::SPSolution solution = problem.Save();
    jaguar::Layout layout(solution.layoutSnapshot.container);
    if (!Expect(layout.Restore(solution.layoutSnapshot).HasValue(), output,
                "SPP offset/evaluator layout restore failed"))
    {
        return false;
    }
    auto trackerResult = sparrow::CollisionTracker::Create(layout);
    if (!Expect(trackerResult.HasValue(), output,
                "SPP offset/evaluator tracker creation failed"))
    {
        return false;
    }
    sparrow::CollisionTracker tracker = std::move(trackerResult.Value());
    sparrow::SeparationEvaluator evaluator(
        layout, problem.Instance().GetItem(1U), movingHandle.Value(), tracker);
    const sparrow::SampleEval evaluation = evaluator.Evaluate(transform, std::nullopt);
    return Expect(evaluation.kind == sparrow::SampleKind::SAMPLE_KIND_COLLISION &&
                      FloatBits(evaluation.loss) == 0x4262ee64U &&
                      FloatBits(tracker.PairLoss(0U, 1U)) == 0x4221ca2fU,
                  output,
                  "SPP-imported offset geometry must preserve the Rust evaluator result");
}

bool TestFiniteSeparationOffsetFinalMultiplyBoundary(std::ostream& output)
{
    const float radius = std::sqrt(std::numeric_limits<float>::max() * 0.15F);
    const float separation = radius * 2.0F;
    jaguar::ImporterConfig importerConfig =
        sparrow::MakeRustDefaultImporterConfig();
    importerConfig.minItemSeparation = static_cast<double>(separation);
    importerConfig.cdeConfig.quadtreeDepth = 0U;
    importerConfig.cdeConfig.itemSurrogateConfig.poleLimits =
        {{{2U, 0.0}, {2U, 0.8}, {2U, 0.9}}};
    const jaguar::Importer importer(importerConfig);

    jaguar::ExternalItem external{};
    external.id = 3U;
    external.rotation = jaguar::RotationPolicy::Continuous();
    external.shape = jaguar::ExternalShape::Rectangle(
        -5.0e11, -5.0e11, 1.0e12, 1.0e12);
    const auto item = importer.ImportItem(external, 0U);
    if (!Expect(item.HasValue(), output,
                "Rust imports the finite offset polygon and reaches Collision(+Inf)"))
    {
        if (!item.HasValue())
            output << "  finite-separation C++ item import error: "
                   << item.GetError().message << '\n';
        return false;
    }

    const jaguar::OriginalShape& original = item.Value().GetOriginalShape();
    if (!Expect(original.shape.VertexCount() == 4U &&
                    PolygonF32BitsHash(original.shape) == 0x08cf09db1f0044d5ULL,
                output,
                "original item geometry bits must match the Rust input stage"))
    {
        return false;
    }
    const auto transformed =
        original.shape.TransformedRustF32(original.preTransform);
    if (!transformed.HasValue())
        return false;
    if (!Expect(transformed.Value().VertexCount() == 4U &&
                    PolygonF32BitsHash(transformed.Value()) == 0x08cf09db1f0044d5ULL,
                output,
                "transformed item geometry bits must match the Rust transform stage"))
    {
        return false;
    }
    if (!Expect(original.modifyConfig.offset.has_value(),
                output,
                "finite separation must install an offset in OriginalShape"))
    {
        return false;
    }
    const auto offset = jaguar::OffsetShape(
        transformed.Value(), original.modifyMode,
        *original.modifyConfig.offset);
    if (!offset.HasValue())
        return false;
    const std::uint64_t offsetHash = PolygonF32BitsHash(offset.Value());
    const float offsetArea = static_cast<float>(offset.Value().Area());
    const std::uint32_t offsetAreaBits = FloatBits(offsetArea);
    if (!Expect(offset.Value().VertexCount() == 68U &&
                    offsetHash == 0xd2af3ad80f009765ULL &&
                    offsetAreaBits == 0x7ef0e310U &&
                    FloatBits(static_cast<float>(
                        offset.Value().BoundingBox().AreaRustF32())) == 0x7f19999aU,
                output,
                "offset vertex sequence and metrics must match the Rust buffer stage"))
    {
        output << "  offset hash: " << std::hex << offsetHash
               << " area bits: " << offsetAreaBits
               << std::dec << '\n';
        return false;
    }
    const auto converted = original.ConvertToInternal();
    if (!converted.HasValue())
        return false;
    if (!Expect(converted.Value().VertexCount() == 50U &&
                    FloatBits(static_cast<float>(converted.Value().Area()))
                        == 0x7ef12454U &&
                    FloatBits(static_cast<float>(
                        converted.Value().BoundingBox().AreaRustF32())) == 0x7f19999aU &&
                    PolygonF32BitsHash(converted.Value()) == 0x43ff76136e8088d1ULL &&
                    PolygonF32BitsHash(item.Value().CollisionShape())
                        == 0x43ff76136e8088d1ULL,
                output,
                "converted and collision geometry bits must match the Rust stages"))
    {
        output << "  converted hash: " << std::hex
               << PolygonF32BitsHash(converted.Value()) << std::dec << '\n';
        return false;
    }

    const jaguar::SimplePolygon& itemShape = item.Value().CollisionShape();
    if (!Expect(!itemShape.Centroid().IsFinite()
                    && itemShape.Area() > 0.0
                    && std::isfinite(itemShape.Area())
                    && itemShape.BoundingBox().AreaRustF32()
                           > std::numeric_limits<float>::max() * 0.5F,
                output,
                "finite offset item must preserve Rust's non-finite centroid and finite geometry metrics"))
    {
        return false;
    }

    const auto containerPolygon = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {2.0, 0.0}, {2.0, 2.0}, {0.0, 2.0}});
    if (!Expect(containerPolygon.HasValue(), output,
                "small manual container polygon creation failed"))
    {
        return false;
    }
    const auto container = jaguar::Container::Create(
        1U,
        {containerPolygon.Value(), jaguar::Transform2d::Identity(),
         jaguar::ShapeModifyMode::Deflate, {}},
        {},
        importerConfig.cdeConfig);
    if (!Expect(container.HasValue(), output,
                "small manual container creation failed"))
    {
        return false;
    }

    jaguar::Layout layout(container.Value());
    const jaguar::Transform2d placement =
        jaguar::Transform2d::FromRotationTranslation(0.0, {1.0, 1.0});
    const auto movingHandle = layout.PlaceItem(item.Value(), placement);
    if (!Expect(movingHandle.HasValue(), output,
                "finite offset item placement failed"))
    {
        return false;
    }

    const auto candidateShape = itemShape.TransformedRustF32(placement);
    if (!Expect(candidateShape.HasValue(), output,
                "finite offset candidate transform failed"))
    {
        return false;
    }
    const jaguar::Rect2d& containerBounds =
        container.Value().CollisionShape().BoundingBox();
    const auto intersection =
        candidateShape.Value().BoundingBox().Intersection(containerBounds);
    if (!Expect(intersection.has_value(), output,
                "finite offset candidate must intersect the manual container bounds"))
    {
        return false;
    }
    const float bboxArea = static_cast<float>(
        candidateShape.Value().BoundingBox().AreaRustF32());
    const float shapeArea = static_cast<float>(itemShape.Area());
    const float overlap =
        (bboxArea - static_cast<float>(intersection->AreaRustF32()))
        + 0.0001F * bboxArea;
    const float hullArea = static_cast<float>(
        itemShape.GetSurrogate()->ConvexHullArea());
    const float penalty = std::sqrt(std::sqrt(hullArea) * std::sqrt(hullArea));
    const float firstProduct = 2.0F * std::sqrt(overlap);
    const float formulaLoss = sparrow::CalculateContainerPenalty(
        candidateShape.Value(), containerBounds);
    const std::uint32_t bboxBits = FloatBits(bboxArea);
    const std::uint32_t shapeBits = FloatBits(shapeArea);
    const std::uint32_t overlapBits = FloatBits(overlap);
    const std::uint32_t penaltyBits = FloatBits(penalty);
    const std::uint32_t firstProductBits = FloatBits(firstProduct);
    if (!Expect(std::isfinite(bboxArea)
                    && bboxArea > std::numeric_limits<float>::max() * 0.5F
                    && std::isfinite(shapeArea)
                    && std::isfinite(overlap) && overlap > 0.0F
                    && std::isfinite(penalty) && penalty > 0.0F
                    && std::isfinite(firstProduct) && firstProduct > 0.0F
                    && bboxBits == 0x7f19999aU
                    && shapeBits == 0x7ef12454U
                    && overlapBits == 0x7f199d89U
                    && penaltyBits == 0x5f2fb00cU
                    && firstProductBits == 0x5fc64e82U
                    && std::isinf(firstProduct * penalty)
                    && std::isinf(formulaLoss) && formulaLoss > 0.0F,
                output,
                "finite C++ formula operands must match Rust and overflow on the same final multiply"))
    {
        output << "  C++ bits: bbox=" << std::hex << bboxBits
               << " shape=" << shapeBits
               << " overlap=" << overlapBits
               << " penalty=" << penaltyBits
               << " first_product=" << firstProductBits << std::dec << '\n';
        return false;
    }

    const auto tracker = sparrow::CollisionTracker::Create(layout);
    if (!Expect(tracker.HasValue(), output,
                "finite offset collision tracker creation failed"))
    {
        return false;
    }

    sparrow::SeparationEvaluator evaluator(
        layout, item.Value(), movingHandle.Value(), tracker.Value());
    const sparrow::SampleEval evaluation = evaluator.Evaluate(placement, std::nullopt);
    return Expect(evaluation.kind == sparrow::SampleKind::SAMPLE_KIND_COLLISION
                      && std::isinf(evaluation.loss)
                      && evaluation.loss > 0.0F
                      && evaluator.EvaluationCount() == 1U,
                  output,
                  "finite offset item must reach the evaluator and match Rust Collision(+Inf)");
}

bool TestFiniteExteriorWeightOverflowMatchesRust(std::ostream& output)
{
    std::ifstream fixture(
        "SparrowCore/Tests/Fixtures/CollisionLossFiniteWeightOverflowRust.json");
    if (!fixture.is_open())
    {
        fixture.open(
            "../SparrowCore/Tests/Fixtures/CollisionLossFiniteWeightOverflowRust.json");
    }
    if (!Expect(fixture.is_open(), output,
                "finite-weight-overflow Rust fixture could not be opened"))
    {
        return false;
    }

    const std::string json{std::istreambuf_iterator<char>(fixture),
                           std::istreambuf_iterator<char>()};
    const jaguar::Importer importer(sparrow::MakeRustDefaultImporterConfig());
    const auto instance = importer.ImportSPInstanceJson(json);
    if (!Expect(instance.HasValue(), output,
                "finite-weight-overflow Rust fixture import failed"))
    {
        output << "  error: " << instance.GetError().message << '\n';
        return false;
    }
    auto problemResult = jaguar::SPProblem::Create(instance.Value());
    if (!Expect(problemResult.HasValue(), output,
                "finite-weight-overflow problem creation failed"))
    {
        return false;
    }
    jaguar::SPProblem problem = std::move(problemResult.Value());
    if (!Expect(problem.ChangeStripWidth(2.0).HasValue(), output,
                "finite-weight-overflow strip resize failed"))
    {
        return false;
    }
    const jaguar::Transform2d exteriorTransform =
        jaguar::Transform2d::FromRotationTranslation(0.0, {1.75, 1.0});
    const auto movingHandle = problem.PlaceItem({0U, exteriorTransform});
    if (!Expect(movingHandle.HasValue(), output,
                "finite-weight-overflow placement failed"))
    {
        return false;
    }

    const jaguar::SPSolution solution = problem.Save();
    jaguar::Layout layout(solution.layoutSnapshot.container);
    if (!Expect(layout.Restore(solution.layoutSnapshot).HasValue(), output,
                "finite-weight-overflow layout restore failed"))
    {
        return false;
    }
    auto trackerResult = sparrow::CollisionTracker::Create(layout);
    if (!Expect(trackerResult.HasValue(), output,
                "finite-weight-overflow tracker creation failed"))
    {
        return false;
    }
    sparrow::CollisionTracker tracker = std::move(trackerResult.Value());
    const float originalTotalLoss = tracker.TotalLoss();
    if (!Expect(std::isfinite(tracker.ContainerLoss(0U))
                    && tracker.ContainerLoss(0U) > 0.0F,
                output,
                "fixture must begin with a finite Exterior collision loss"))
    {
        return false;
    }
    for (std::size_t update = 0U; update < 128U; ++update)
    {
        tracker.UpdateWeights();
    }
    const float weight = tracker.ContainerWeight(0U);
    if (!Expect(std::isinf(weight) && weight > 0.0F
                    && std::isinf(tracker.TotalWeightedLoss()),
                output,
                "finite GLS updates must overflow the Exterior weight to positive infinity"))
    {
        return false;
    }

    const auto rawCandidate = instance.Value().GetItem(0U)
        .CollisionShape().TransformedRustF32(exteriorTransform);
    if (!Expect(rawCandidate.HasValue(), output,
                "finite-weight-overflow candidate transform failed"))
    {
        return false;
    }
    const float rawLoss = sparrow::CalculateContainerPenalty(
        rawCandidate.Value(), layout.Save().container.CollisionShape().BoundingBox());
    if (!Expect(std::isfinite(rawLoss) && rawLoss > 0.0F
                    && std::isinf(rawLoss * weight),
                output,
                "normal finite Exterior loss times the accumulated finite-state weight must overflow"))
    {
        return false;
    }

    sparrow::SeparationEvaluator evaluator(
        layout, instance.Value().GetItem(0U), movingHandle.Value(), tracker);
    bool debugThrew = false;
    std::string debugMessage;
    sparrow::SampleEval evaluation{};
    try
    {
        evaluation = evaluator.Evaluate(exteriorTransform, std::nullopt);
    }
    catch (const std::domain_error& error)
    {
        debugThrew = true;
        debugMessage = error.what();
    }
    if (!Expect(!debugThrew
                    && evaluation.kind == sparrow::SampleKind::SAMPLE_KIND_COLLISION
                    && std::isinf(evaluation.loss) && evaluation.loss > 0.0F,
                output,
                "finite-overlap weighted-loss overflow must remain Collision(+Inf) in both profiles"))
    {
        if (debugThrew)
        {
            output << "  unexpected Debug exception: " << debugMessage << '\n';
        }
        return false;
    }

    const jaguar::Transform2d recoveredTransform =
        jaguar::Transform2d::FromRotationTranslation(0.0, {1.0, 1.0});
    const sparrow::SampleEval recovered =
        evaluator.Evaluate(recoveredTransform, std::nullopt);
    const float finalTotalLoss = tracker.TotalLoss();
    return Expect(recovered.kind == sparrow::SampleKind::SAMPLE_KIND_CLEAR
                      && recovered.loss == 0.0F
                      && evaluator.EvaluationCount() == 2U
                      && finalTotalLoss == originalTotalLoss
                      && tracker.ContainerWeight(0U) == weight
                      && layout.Save().placements[0].placedItem.Transform()
                             .translation.x == exteriorTransform.translation.x
                      && layout.Save().placements[0].placedItem.Transform()
                             .translation.y == exteriorTransform.translation.y,
                  output,
                  "finite-weight-overflow evaluation must recover without mutating layout or tracker losses");
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

bool TestAlmostBboxContainmentDirectionalDiscoveryMatchesRust(std::ostream& output)
{
    const auto outer = jaguar::SimplePolygon::Create({
        {-1.0, -1.0}, {1.0, -1.0}, {1.0, 1.0}, {-1.0, 1.0}});
    const float xExtent = FloatFromBits(0x3f7fffffU);
    const float yExtent = FloatFromBits(0x3f7ffffeU);
    const float notchX = xExtent * 0.3F;
    const float notchY = -yExtent * 0.3F;
    const auto inner = jaguar::SimplePolygon::Create({
        {-xExtent, -yExtent},
        {xExtent, -yExtent},
        {xExtent, yExtent},
        {notchX, yExtent},
        {notchX, notchY},
        {-notchX, notchY},
        {-notchX, yExtent},
        {-xExtent, yExtent},
    });
    const auto bounds = jaguar::Rect2d::Create(-4.0, -4.0, 4.0, 4.0);
    if (!Expect(outer.HasValue() && inner.HasValue() && bounds.HasValue(),
                output,
                "directional containment sample geometry creation failed"))
    {
        return false;
    }

    if (!Expect(inner.Value().BoundingBox().AlmostRelationToRustF32(
                        outer.Value().BoundingBox())
                        == jaguar::RectRelation::Surrounding
                    && outer.Value().BoundingBox().AlmostRelationToRustF32(
                           inner.Value().BoundingBox())
                           == jaguar::RectRelation::Surrounding,
                output,
                "sample bboxes must compare as surrounding in both Rust-f32 directions"))
    {
        return false;
    }

    jaguar::CDEConfig config{};
    config.quadtreeDepth = 0U;
    config.cdThreshold = 64U;
    const auto collectTarget = [&](const jaguar::SimplePolygon& query,
                                   const jaguar::SimplePolygon& target,
                                   std::size_t zoneIndex) -> std::optional<bool> {
        jaguar::CollisionEngine engine(bounds.Value(), config);
        const jaguar::HazardEntity targetEntity{
            jaguar::HazardKind::Hole, 0U, 0U, zoneIndex};
        const auto registered = engine.RegisterHazard({
            targetEntity,
            std::make_shared<const jaguar::SimplePolygon>(target),
            true});
        if (!registered.HasValue())
            return std::nullopt;
        const auto collisions = engine.CollectPolygonCollisions(query);
        return std::any_of(collisions.begin(), collisions.end(), [&](const auto& entity) {
            return entity == targetEntity;
        });
    };

    const auto outerQueryFindsInner =
        collectTarget(outer.Value(), inner.Value(), 1U);
    const auto innerQueryFindsOuter =
        collectTarget(inner.Value(), outer.Value(), 2U);
    if (!Expect(outerQueryFindsInner.has_value() && innerQueryFindsOuter.has_value()
                    && !*outerQueryFindsInner && *innerQueryFindsOuter,
                output,
                "CDE must reproduce Rust's one-way POI-containment discovery for the U-shaped pair"))
    {
        return false;
    }

    const jaguar::Importer importer(sparrow::MakeRustDefaultImporterConfig());
    jaguar::ExternalContainer externalContainer{};
    externalContainer.id = 8800U;
    externalContainer.shape = jaguar::ExternalShape::Rectangle(-4.0, -4.0, 8.0, 8.0);
    const auto container = importer.ImportContainer(externalContainer);
    if (!Expect(container.HasValue(), output,
                "directional containment container import failed"))
    {
        return false;
    }

    const auto importItem = [&importer](jaguar::ItemId id,
                                        jaguar::ItemIndex index,
                                        const jaguar::SimplePolygon& shape) {
        jaguar::ExternalItem external{};
        external.id = id;
        external.rotation = jaguar::RotationPolicy::Continuous();
        external.shape = jaguar::ExternalShape::SimplePolygon(shape.Vertices());
        return importer.ImportItem(external, index);
    };
    const auto outerItem = importItem(12001U, 0U, outer.Value());
    const auto innerItem = importItem(12002U, 1U, inner.Value());
    if (!Expect(outerItem.HasValue() && innerItem.HasValue(),
                output,
                "directional containment items import failed"))
    {
        return false;
    }

    const jaguar::Rect2d& importedInnerBounds =
        innerItem.Value().CollisionShape().BoundingBox();
    const float innerCenterX =
        (static_cast<float>(importedInnerBounds.xMin)
         + static_cast<float>(importedInnerBounds.xMax))
        / 2.0F;
    const float innerCenterY =
        (static_cast<float>(importedInnerBounds.yMin)
         + static_cast<float>(importedInnerBounds.yMax))
        / 2.0F;
    jaguar::Layout layout(container.Value());
    const auto outerHandle = layout.PlaceItem(
        outerItem.Value(), jaguar::Transform2d::Identity());
    const auto innerHandle = layout.PlaceItem(
        innerItem.Value(), jaguar::Transform2d::FromRotationTranslation(
                               0.0,
                               {-static_cast<double>(innerCenterX),
                                -static_cast<double>(innerCenterY)}));
    if (!Expect(outerHandle.HasValue() && innerHandle.HasValue(),
                output,
                "directional containment layout placement failed"))
    {
        return false;
    }

    const jaguar::LayoutSnapshot layoutSnapshot = layout.Save();
    auto layoutEngine = layout.GetContainer().CreateCollisionEngine();
    if (!Expect(layoutEngine->Restore(layoutSnapshot.cdeSnapshot).HasValue(),
                output,
                "directional containment layout CDE snapshot restore failed"))
    {
        return false;
    }
    const auto collectLayoutTarget = [&layoutEngine](
                                         const jaguar::SimplePolygon& queryShape,
                                         jaguar::ItemIndex targetIndex)
        -> std::optional<bool> {
        const auto collisions = layoutEngine->CollectPolygonCollisions(
            queryShape, jaguar::NoFilter{});
        return std::any_of(collisions.begin(), collisions.end(), [&](const auto& entity) {
            return entity.kind == jaguar::HazardKind::PlacedItem
                   && entity.itemIndex == targetIndex;
        });
    };
    const auto layoutOuterToInner = collectLayoutTarget(
        layoutSnapshot.placements[0].placedItem.CollisionShape(),
        1U);
    const auto layoutInnerToOuter = collectLayoutTarget(
        layoutSnapshot.placements[1].placedItem.CollisionShape(),
        0U);
    if (!Expect(layoutOuterToInner.has_value() && layoutInnerToOuter.has_value()
                    && !*layoutOuterToInner && *layoutInnerToOuter,
                output,
                "restored Layout CDE must retain Rust's directional containment result"))
    {
        return false;
    }

    std::ostringstream trackerDiagnostic;
    std::streambuf* const originalClog = std::clog.rdbuf(trackerDiagnostic.rdbuf());
    const auto trackerResult = sparrow::CollisionTracker::Create(layout);
    std::clog.rdbuf(originalClog);
    if (!Expect(trackerResult.HasValue(), output,
                "directional containment tracker creation failed"))
    {
        return false;
    }
#ifndef NDEBUG
    if (!Expect(trackerDiagnostic.str().find("inconsistent loss")
                    != std::string::npos,
                output,
                "debug tracker consistency check must report the one-way CDE loss"))
    {
        return false;
    }
    if (!Expect(trackerDiagnostic.str().find(
                    "item 0 query found 0 hazards, reverse item 1 query found 1 hazards")
                    != std::string::npos,
                output,
                "debug tracker diagnostic must preserve the CDE discovery direction"))
    {
        return false;
    }
#else
    if (!Expect(trackerDiagnostic.str().empty(),
                output,
                "release tracker creation must not emit debug consistency diagnostics"))
    {
        return false;
    }
#endif
    const float pairLoss = trackerResult.Value().PairLoss(0U, 1U);
    return Expect(FloatBits(pairLoss) == 0x409a27bbU,
                  output,
                  "Tracker pair loss must match Rust's later-row overwrite bits");
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
    const jaguar::LayoutSnapshot layoutBeforeInvalidCandidate = layout.Save();
    const float trackerLossBeforeInvalidCandidate = tracker.TotalLoss();
    // Rust consumes f32 transforms; these finite double inputs overflow to the
    // same +/-Inf values after the evaluator's f32 conversion.
    const jaguar::Transform2d translationsInfiniteInRustF32[] = {
        jaguar::Transform2d::FromRotationTranslation(
            0.0, {std::numeric_limits<double>::infinity(), 0.5}),
        jaguar::Transform2d::FromRotationTranslation(
            0.0, {-std::numeric_limits<double>::infinity(), 0.5}),
        jaguar::Transform2d::FromRotationTranslation(
            0.0, {0.5, std::numeric_limits<double>::infinity()}),
        jaguar::Transform2d::FromRotationTranslation(
            0.0, {0.5, -std::numeric_limits<double>::infinity()}),
        jaguar::Transform2d::FromRotationTranslation(
            0.0,
            {static_cast<double>(std::numeric_limits<float>::max()) * 2.0, 0.5}),
        jaguar::Transform2d::FromRotationTranslation(
            0.0,
            {-static_cast<double>(std::numeric_limits<float>::max()) * 2.0, 0.5}),
        jaguar::Transform2d::FromRotationTranslation(
            0.0,
            {0.5, static_cast<double>(std::numeric_limits<float>::max()) * 2.0}),
        jaguar::Transform2d::FromRotationTranslation(
            0.0,
            {0.5, -static_cast<double>(std::numeric_limits<float>::max()) * 2.0}),
    };
#ifndef NDEBUG
    const auto matchesNonFiniteProfile = [&](const jaguar::Transform2d& candidate,
                                             const std::optional<sparrow::SampleEval>& bound) {
        try
        {
            static_cast<void>(evaluator.Evaluate(candidate, bound));
        }
        catch (const std::domain_error&)
        {
            return true;
        }
        return false;
    };
#else
    const auto matchesNonFiniteProfile = [&](const jaguar::Transform2d& candidate,
                                             const std::optional<sparrow::SampleEval>& bound) {
        const sparrow::SampleEval result = evaluator.Evaluate(candidate, bound);
        const bool unbounded = !bound.has_value()
            || bound->kind == sparrow::SampleKind::SAMPLE_KIND_INVALID
            || (bound->kind == sparrow::SampleKind::SAMPLE_KIND_COLLISION
                && (std::isnan(bound->loss)
                    || bound->loss == std::numeric_limits<float>::infinity()));
        if (unbounded)
        {
            return result.kind == sparrow::SampleKind::SAMPLE_KIND_COLLISION
                && std::isinf(result.loss) && result.loss > 0.0F;
        }
        return result.kind == sparrow::SampleKind::SAMPLE_KIND_INVALID;
    };
#endif
    const std::optional<sparrow::SampleEval> nonFiniteBounds[] = {
        std::nullopt,
        sparrow::SampleEval{
            sparrow::SampleKind::SAMPLE_KIND_COLLISION,
            std::numeric_limits<float>::infinity()},
        sparrow::SampleEval{
            sparrow::SampleKind::SAMPLE_KIND_COLLISION,
            std::numeric_limits<float>::quiet_NaN()},
        sparrow::SampleEval{sparrow::SampleKind::SAMPLE_KIND_INVALID, 0.0F},
        sparrow::SampleEval{sparrow::SampleKind::SAMPLE_KIND_COLLISION, 0.01F},
        sparrow::SampleEval{sparrow::SampleKind::SAMPLE_KIND_CLEAR, 0.0F},
    };
    bool nonFiniteBoundaryMatchesRust = true;
    for (const jaguar::Transform2d& candidate : translationsInfiniteInRustF32)
    {
        for (const auto& bound : nonFiniteBounds)
        {
            nonFiniteBoundaryMatchesRust = matchesNonFiniteProfile(candidate, bound)
                && nonFiniteBoundaryMatchesRust;
        }
    }
    const jaguar::LayoutSnapshot layoutAfterInvalidCandidate = layout.Save();
    bool placementsUnchanged =
        layoutAfterInvalidCandidate.placements.size()
        == layoutBeforeInvalidCandidate.placements.size();
    for (std::size_t index = 0U;
         placementsUnchanged && index < layoutBeforeInvalidCandidate.placements.size();
         ++index)
    {
        const jaguar::Transform2d& before =
            layoutBeforeInvalidCandidate.placements[index].placedItem.Transform();
        const jaguar::Transform2d& after =
            layoutAfterInvalidCandidate.placements[index].placedItem.Transform();
        placementsUnchanged = before.rotationRad == after.rotationRad
            && before.translation.x == after.translation.x
            && before.translation.y == after.translation.y;
    }
    if (!Expect(nonFiniteBoundaryMatchesRust
                    && evaluator.EvaluationCount() == 48U
                    && placementsUnchanged
                    && tracker.TotalLoss() == trackerLossBeforeInvalidCandidate,
                output,
                "translations that are infinite in Rust f32 must match its profile result without mutating state"))
    {
        return false;
    }

    const sparrow::SampleEval collision = evaluator.Evaluate(transform, std::nullopt);
    std::uint32_t lossBits = 0U;
    std::memcpy(&lossBits, &collision.loss, sizeof(lossBits));
    if (lossBits != 0x4ab5c931U)
    {
        output << "QUANTIFY TEST FAILURE: multi-hazard raw loss differs from Rust; got "
               << lossBits << '\n';
    }
    if (!Expect(collision.kind == sparrow::SampleKind::SAMPLE_KIND_COLLISION
                    && lossBits == 0x4ab5c931U
                    && evaluator.EvaluationCount() == 49U
                    && tracker.TotalLoss() == trackerLossBeforeInvalidCandidate,
                output,
                "multi-hazard weighted loss must match Rust raw bits after invalid-candidate recovery"))
    {
        return false;
    }

    sparrow::SeparationEvaluator infiniteRotationEvaluator(
        layout, instance.Value().GetItem(3U), handles[3], tracker);
    const jaguar::Transform2d infiniteRotations[] = {
        jaguar::Transform2d::FromRotationTranslation(
            std::numeric_limits<double>::infinity(), {0.5, 0.5}),
        jaguar::Transform2d::FromRotationTranslation(
            -std::numeric_limits<double>::infinity(), {0.5, 0.5}),
        jaguar::Transform2d::FromRotationTranslation(
            static_cast<double>(std::numeric_limits<float>::max()) * 2.0, {0.5, 0.5}),
        jaguar::Transform2d::FromRotationTranslation(
            -static_cast<double>(std::numeric_limits<float>::max()) * 2.0, {0.5, 0.5}),
    };
    bool infiniteRotationMatchesRust = true;
    for (const jaguar::Transform2d& candidate : infiniteRotations)
    {
        for (const auto& bound : nonFiniteBounds)
        {
            bool panickedLikeRust = false;
            try
            {
                static_cast<void>(infiniteRotationEvaluator.Evaluate(candidate, bound));
            }
            catch (const std::domain_error& error)
            {
                panickedLikeRust =
                    std::string(error.what()).find("cos is NaN") != std::string::npos;
            }
            infiniteRotationMatchesRust =
                panickedLikeRust && infiniteRotationMatchesRust;
        }
    }
    const jaguar::LayoutSnapshot layoutAfterInfiniteRotation = layout.Save();
    bool rotationFailurePreservedLayout =
        layoutAfterInfiniteRotation.placements.size()
        == layoutBeforeInvalidCandidate.placements.size();
    for (std::size_t index = 0U;
         rotationFailurePreservedLayout
         && index < layoutBeforeInvalidCandidate.placements.size();
         ++index)
    {
        const jaguar::Transform2d& before =
            layoutBeforeInvalidCandidate.placements[index].placedItem.Transform();
        const jaguar::Transform2d& after =
            layoutAfterInfiniteRotation.placements[index].placedItem.Transform();
        rotationFailurePreservedLayout = before.rotationRad == after.rotationRad
            && before.translation.x == after.translation.x
            && before.translation.y == after.translation.y;
    }
    if (!Expect(infiniteRotationMatchesRust
                    && infiniteRotationEvaluator.EvaluationCount() == 24U
                    && rotationFailurePreservedLayout
                    && tracker.TotalLoss() == trackerLossBeforeInvalidCandidate,
                output,
                "infinite candidate rotations must fail at transform composition like Rust"))
    {
        return false;
    }
    const sparrow::SampleEval rotationRecovered =
        infiniteRotationEvaluator.Evaluate(transform, std::nullopt);
    std::uint32_t rotationRecoveredBits = 0U;
    std::memcpy(&rotationRecoveredBits, &rotationRecovered.loss, sizeof(rotationRecoveredBits));
    if (!Expect(rotationRecovered.kind == sparrow::SampleKind::SAMPLE_KIND_COLLISION
                    && rotationRecoveredBits == 0x4ab5c931U
                    && infiniteRotationEvaluator.EvaluationCount() == 25U
                    && tracker.TotalLoss() == trackerLossBeforeInvalidCandidate,
                output,
                "finite candidate must recover after an infinite-rotation failure"))
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
        && TestExtremeEpsilonOverlapProxyBoundary(output)
        && TestSubnormalPolygonLossBranch(output)
        && TestFiniteTransformOverflowingContainerBboxAreaMatchesRust(output)
        && TestPositiveShoelaceZeroBboxCandidateIsRejectedBeforeLoss(output)
        && TestSubnormalContainerLossThroughEvaluator(output)
        && TestSubnormalLossThroughSeparationEvaluator(output)
        && TestCollisionTrackerAndSeparationEvaluator(output)
        && TestCollisionTrackerMoveRecomputesOnlyOneRow(output)
        && TestExteriorHazardRawLossMatchesRust(output)
        && TestFinalMultiplyOverflowSquareMatchesRustConstruction(output)
        && TestStrictConvexInflateContourOrderMatchesRust(output)
        && TestConcaveInflateContourBitsMatchRust(output)
        && TestConvexDeflateContourBitsMatchRust(output)
        && TestOffsetOrientationDegenerateScaleMatchesRust(output)
        && TestSppImportedOffsetReachesSeparationEvaluator(output)
        && TestFiniteSeparationOffsetFinalMultiplyBoundary(output)
        && TestFiniteExteriorCentroidDistanceOverflowMatchesRust(output)
        && TestFiniteExteriorWeightOverflowMatchesRust(output)
        && TestSeparationEvaluatorPreservesTinyCollisionLoss(output)
        && TestAlmostBboxContainmentDirectionalDiscoveryMatchesRust(output)
        && TestRustMultiHazardLossAndEarlyStop(output)
        && TestSeparationEvaluatorIgnoresItsCurrentHazard(output)
        && TestUnsupportedContainerHazardsMatchRustPanic(output);
}
