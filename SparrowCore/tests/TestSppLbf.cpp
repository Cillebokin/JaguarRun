#include "SparrowCore/Optimizer/SppLbfOptimizer.h"
#include "SparrowCore/Optimizer/SparrowLbfBuilder.h"
#include "SparrowCore/Optimizer/SparrowConfig.h"

#include "JaguarCore/Assertions.h"
#include "JaguarCore/Entities/Item.h"
#include "JaguarCore/IO/Importer.h"
#include "JaguarCore/IO/JsonCodec.h"
#include "JaguarCore/Problems/Spp.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
constexpr double SPARROW_RUST_LBF_WIDTH_TOLERANCE = 4.0e-3;
constexpr double SPARROW_RUST_LBF_DENSITY_TOLERANCE = 1.0e-3;

bool Expect(bool condition, std::ostream& output, const char* message)
{
    if (condition)
    {
        return true;
    }
    output << "SPP LBF TEST FAILURE: " << message << '\n';
    return false;
}

jaguar::Item MakeItem(jaguar::ItemIndex index,
                      jaguar::ItemId externalId,
                      double width,
                      double height,
                      std::optional<std::size_t> minQuality = std::nullopt)
{
    const auto polygon = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {width, 0.0}, {width, height}, {0.0, height}});
    jaguar::SurrogateConfig surrogateConfig{};
    surrogateConfig.poleLimits = {{{1, 0.0}, {0, 0.0}, {0, 0.0}}};
    const auto item = jaguar::Item::Create(
        index,
        externalId,
        {polygon.Value(),
         jaguar::Transform2d::Identity(),
         jaguar::ShapeModifyMode::Inflate,
         {}},
        surrogateConfig,
        minQuality);
    return item.Value();
}

jaguar::Result<jaguar::SPInstance> MakeInstance(
    std::vector<jaguar::Item> items,
    std::vector<std::size_t> demands,
    double height = 4.0)
{
    return jaguar::SPInstance::Create(
        std::move(items),
        std::move(demands),
        height,
        jaguar::CDEConfig{},
        jaguar::ShapeModifyConfig{});
}

bool TestSmallSppFixture(std::ostream& output)
{
    const auto instance = MakeInstance({MakeItem(0, 201, 2.0, 2.0)}, {2});
    if (!Expect(instance.HasValue(), output, "small SPP instance creation failed"))
    {
        return false;
    }

    sparrow::LbfConfig config;
    config.seed = 42U;
    config.samplesPerItem = 64U;
    config.localSearchFraction = 0.25;
    const auto result = sparrow::SolveSppLbf(instance.Value(), config);
    if (!Expect(result.HasValue(), output, "small SPP LBF solve failed"))
    {
        return false;
    }
    std::ifstream metricsFile;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/SppLbfCppReference.tsv",
             "../SparrowCore/Tests/Fixtures/SppLbfCppReference.tsv",
             "../JaguarRun/SparrowCore/Tests/Fixtures/SppLbfCppReference.tsv",
             "..\\SparrowCore\\Tests\\Fixtures\\SppLbfCppReference.tsv"})
    {
        metricsFile.open(path);
        if (metricsFile.is_open())
        {
            break;
        }
        metricsFile.clear();
    }
    std::size_t referenceSamples = 0U;
    std::size_t referencePlaced = 0U;
    std::size_t referenceRemaining = 0U;
    double referenceWidth = 0.0;
    double referenceDensity = 0.0;
    bool referenceFound = false;
    std::string metricsLine;
    while (metricsFile.is_open() && std::getline(metricsFile, metricsLine))
    {
        if (metricsLine.empty() || metricsLine.front() == '#')
        {
            continue;
        }
        std::istringstream row(metricsLine);
        std::string problemName;
        std::uint64_t seed = 0U;
        std::size_t sampleCount = 0U;
        std::size_t placed = 0U;
        std::size_t remaining = 0U;
        double width = 0.0;
        double density = 0.0;
        if (row >> problemName >> seed >> sampleCount >> placed
                >> remaining >> width >> density
            && problemName == "lbf_spp_small_cpp")
        {
            referenceSamples = sampleCount;
            referencePlaced = placed;
            referenceRemaining = remaining;
            referenceWidth = width;
            referenceDensity = density;
            referenceFound = true;
            break;
        }
    }
    if (!Expect(referenceFound,
                output,
                "C++ SPP metrics reference fixture must be readable"))
    {
        return false;
    }
    if (!Expect(result.Value().metrics.sampleCount == referenceSamples
                    && result.Value().metrics.placedItemCount == referencePlaced
                    && result.Value().metrics.remainingDemand == referenceRemaining
                    && std::abs(result.Value().metrics.stripWidth - referenceWidth)
                           < 1.0e-9
                    && std::abs(result.Value().metrics.density - referenceDensity)
                           < 1.0e-9,
                output,
                "fixed seed SPP metrics must match the C++ reference fixture"))
    {
        const auto& metrics = result.Value().metrics;
        output << std::setprecision(17)
               << "SPP C++ reference expected: samples=" << referenceSamples
               << " placed=" << referencePlaced
               << " remaining=" << referenceRemaining
               << " width=" << referenceWidth
               << " density=" << referenceDensity << '\n'
               << "SPP C++ actual: samples=" << metrics.sampleCount
               << " placed=" << metrics.placedItemCount
               << " remaining=" << metrics.remainingDemand
               << " width=" << metrics.stripWidth
               << " density=" << metrics.density << '\n';
        return false;
    }
    if (!Expect(result.Value().metrics.placedItemCount == 2U
                    && result.Value().metrics.remainingDemand == 0U
                    && result.Value().solution.layoutSnapshot.placements.size() == 2U
                    && result.Value().metrics.feasible
                    && std::isfinite(result.Value().metrics.stripWidth)
                    && result.Value().metrics.stripWidth > 0.0,
                output,
                "small SPP LBF must conserve demand and produce a feasible solution"))
    {
        return false;
    }
    auto validationProblem = jaguar::SPProblem::Create(instance.Value());
    if (!Expect(validationProblem.HasValue()
                    && validationProblem.Value().Restore(result.Value().solution).HasValue()
                    && jaguar::ValidateSPSolution(
                           validationProblem.Value(), result.Value().solution).HasValue(),
                output,
                "SPP solution validator must accept the produced solution"))
    {
        return false;
    }
    return true;
}

bool TestSppDemandAndInvalidInputs(std::ostream& output)
{
    const auto emptyDemand = MakeInstance({MakeItem(0, 301, 1.0, 1.0)}, {0});
    if (!Expect(!emptyDemand.HasValue()
                    && emptyDemand.GetError().code == jaguar::ErrorCode::InvalidInput,
                output,
                "SPP zero total demand must be rejected by JaguarCore"))
    {
        return false;
    }

    const auto impossible = MakeInstance({MakeItem(0, 302, 2.0, 5.0)}, {1}, 4.0);
    if (!Expect(impossible.HasValue(), output, "impossible SPP setup failed"))
    {
        return false;
    }
    sparrow::LbfConfig config;
    config.seed = 3U;
    config.samplesPerItem = 16U;
    config.localSearchFraction = 0.0;
    config.maxWidthGrowthAttempts = 0U;
    const auto impossibleResult = sparrow::SolveSppLbf(impossible.Value(), config);
    if (!Expect(!impossibleResult.HasValue()
                    && impossibleResult.GetError().code == jaguar::ErrorCode::Infeasible
                    && impossibleResult.GetError().message
                           == "SPP strip width growth limit exceeded",
                output,
                "an item taller than the strip must map Rust's panic to a typed Infeasible error"))
    {
        return false;
    }

    const auto growthInstance = MakeInstance({MakeItem(0, 303, 4.0, 1.0)}, {1}, 2.0);
    if (!Expect(growthInstance.HasValue(), output, "SPP width-growth setup failed"))
    {
        return false;
    }
    sparrow::LbfConfig growthConfig;
    growthConfig.seed = 7U;
    growthConfig.samplesPerItem = 16U;
    growthConfig.localSearchFraction = 0.0;
    growthConfig.maxWidthGrowthAttempts = 8U;
    growthConfig.widthGrowthRatio = 1.5;
    const auto grown = sparrow::SolveSppLbf(growthInstance.Value(), growthConfig);
    if (!Expect(grown.HasValue()
                    && grown.Value().metrics.placedItemCount == 1U
                    && grown.Value().metrics.remainingDemand == 0U,
                output,
                "SPP must widen the strip and retry a placement"))
    {
        return false;
    }

    sparrow::LbfConfig invalidConfig = config;
    invalidConfig.samplesPerItem = 0U;
    const auto invalidResult = sparrow::SolveSppLbf(impossible.Value(), invalidConfig);
    return Expect(!invalidResult.HasValue()
                      && invalidResult.GetError().code == jaguar::ErrorCode::Infeasible,
                  output,
                  "zero samples must follow Rust's no-placement path and fail at the width ceiling");
}

bool TestLbfContinuousRotationUsesRustF32WidthGrowth(std::ostream& output)
{
    std::ifstream instanceFile;
    for (const char* path : {
             "JaguarCore/Tests/Fixtures/RustReference/l1_continuous_rotation_spp.json",
             "../JaguarCore/Tests/Fixtures/RustReference/l1_continuous_rotation_spp.json",
             "../JaguarRun/JaguarCore/Tests/Fixtures/RustReference/l1_continuous_rotation_spp.json",
             "..\\JaguarCore\\Tests\\Fixtures\\RustReference\\l1_continuous_rotation_spp.json"})
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
                "continuous-rotation LBF fixture must be readable"))
    {
        return false;
    }

    const std::string instanceJson{
        std::istreambuf_iterator<char>(instanceFile),
        std::istreambuf_iterator<char>()};
    jaguar::ImporterConfig importerConfig;
    importerConfig.cdeConfig.quadtreeDepth = 4;
    importerConfig.cdeConfig.cdThreshold = 32;
    importerConfig.cdeConfig.itemSurrogateConfig.poleLimits =
        {{{10, 0.0}, {0, 0.0}, {0, 0.0}}};
    importerConfig.cdeConfig.itemSurrogateConfig.failFastPoleAreaRatio = 0.5;
    importerConfig.cdeConfig.itemSurrogateConfig.failFastPiers = 0;
    importerConfig.shapeModifyConfig.simplifyTolerance.reset();
    importerConfig.shapeModifyConfig.narrowConcavityCutoff.reset();
    importerConfig.shapeModifyConfig.offset.reset();
    const jaguar::Importer importer{importerConfig};
    const auto instance = importer.ImportSPInstanceJson(instanceJson);
    if (!Expect(instance.HasValue(),
                output,
                "continuous-rotation LBF fixture must import"))
    {
        return false;
    }

    sparrow::LbfConfig config;
    config.seed = 42U;
    config.samplesPerItem = 64U;
    config.localSearchFraction = 0.25;
    const auto result = sparrow::SolveSppLbf(instance.Value(), config);
    if (!Expect(result.HasValue(),
                output,
                "continuous-rotation LBF fixture must solve"))
    {
        return false;
    }

    const auto floatBits = [](double input) {
        const float value = static_cast<float>(input);
        std::uint32_t bits = 0U;
        std::memcpy(&bits, &value, sizeof(bits));
        return bits;
    };
    const auto& solved = result.Value();
    const bool hasContinuousPlacement = std::any_of(
        solved.solution.layoutSnapshot.placements.begin(),
        solved.solution.layoutSnapshot.placements.end(),
        [](const jaguar::LayoutPlacementSnapshot& placement) {
            return std::abs(placement.placedItem.Transform().rotationRad) > 1.0e-6;
        });
    return Expect(solved.metrics.sampleCount == 672U
                      && solved.metrics.placedItemCount == 3U
                      && solved.metrics.remainingDemand == 0U
                      && floatBits(solved.metrics.stripWidth) == 0x40f10086U
                      && floatBits(solved.metrics.density) == 0x3ecbf2d0U
                      && hasContinuousPlacement,
                  output,
                  "continuous-rotation SPP must retain Rust f32 width-growth and terminal bits");
}

bool TestSppJsonRoundTrip(std::ostream& output)
{
    const auto instance = MakeInstance({MakeItem(0, 401, 2.0, 2.0)}, {1});
    if (!Expect(instance.HasValue(), output, "SPP JSON instance setup failed"))
    {
        return false;
    }
    sparrow::LbfConfig config;
    config.seed = 42U;
    config.samplesPerItem = 32U;
    config.localSearchFraction = 0.0;
    const auto solved = sparrow::SolveSppLbf(instance.Value(), config);
    if (!Expect(solved.HasValue(), output, "SPP JSON solve setup failed"))
    {
        return false;
    }

    const auto external = jaguar::ExternalSPSolutionFromSolution(
        instance.Value(), solved.Value().solution);
    if (!Expect(external.HasValue(), output, "SPP external solution conversion failed"))
    {
        return false;
    }
    const auto json = jaguar::SerializeExternalSPSolutionJson(external.Value());
    if (!Expect(json.HasValue(), output, "SPP solution JSON serialization failed"))
    {
        return false;
    }
    const jaguar::Importer importer;
    const auto restored = importer.ImportSPSolutionJson(instance.Value(), json.Value());
    if (!Expect(restored.HasValue(), output, "SPP solution JSON import failed"))
    {
        return false;
    }
    return Expect(restored.Value().layoutSnapshot.placements.size()
                      == solved.Value().solution.layoutSnapshot.placements.size()
                      && std::abs(restored.Value().strip.width
                                  - solved.Value().solution.strip.width)
                             < 1.0e-9,
                  output,
                  "SPP JSON round-trip must preserve placement count and strip width");
}

bool TestSparrowBuilderSmallInstance(std::ostream& output)
{
    const auto instance = MakeInstance({MakeItem(0, 501, 2.0, 2.0)}, {2});
    if (!Expect(instance.HasValue(), output, "Sparrow builder setup failed"))
    {
        return false;
    }

    sparrow::SparrowConstructorConfig config;
    config.seed = 42U;
    config.nContainerSamples = 64U;
    config.nFocussedSamples = 0U;
    config.nCoordinateDescents = 1U;
    const auto constructed =
        sparrow::ConstructSparrowInitialSolution(instance.Value(), config);
    if (!Expect(constructed.HasValue(),
                output,
                "Sparrow builder must construct a complete initial solution"))
    {
        return false;
    }

    auto validationProblem = jaguar::SPProblem::Create(instance.Value());
    if (!Expect(validationProblem.HasValue()
                    && validationProblem.Value().Restore(
                           constructed.Value().solution).HasValue()
                    && jaguar::ValidateSPSolution(
                           validationProblem.Value(),
                           constructed.Value().solution).HasValue(),
                output,
                "Sparrow builder solution must pass JaguarCore validation"))
    {
        return false;
    }

    return Expect(constructed.Value().metrics.placedItemCount == 2U
                      && constructed.Value().metrics.remainingDemand == 0U
                      && constructed.Value().metrics.feasible,
                  output,
                  "Sparrow builder must conserve demand and report feasibility");
}

bool TestSparrowBuilderZeroContainerSamplesMatchesRustError(
    std::ostream& output)
{
    const auto instance = MakeInstance({MakeItem(0U, 594U, 2.0, 2.0)}, {1U});
    if (!Expect(instance.HasValue(),
                output,
                "zero-sample Sparrow builder setup failed"))
    {
        return false;
    }

    sparrow::SparrowConstructorConfig config;
    config.seed = 42U;
    config.nContainerSamples = 0U;
    config.nFocussedSamples = 0U;
    config.nCoordinateDescents = 1U;
    const auto constructed =
        sparrow::ConstructSparrowInitialSolution(instance.Value(), config);
    return Expect(!constructed.HasValue()
                      && constructed.GetError().code
                          == jaguar::ErrorCode::Infeasible
                      && constructed.GetError().message
                          == "could not construct an initial placement for item 0",
                  output,
                  "zero container samples must match Rust's item-indexed construction failure");
}

bool TestSparrowBuilderFocusedSamplesStayAtRustZero(std::ostream& output)
{
    const auto instance = MakeInstance({MakeItem(0U, 595U, 2.0, 2.0)}, {1U});
    if (!Expect(instance.HasValue(),
                output,
                "focused-sample Sparrow builder setup failed"))
    {
        return false;
    }

    sparrow::SparrowConstructorConfig rustDefaults;
    rustDefaults.seed = 42U;
    rustDefaults.nContainerSamples = 64U;
    rustDefaults.nFocussedSamples = 0U;
    rustDefaults.nCoordinateDescents = 1U;
    sparrow::SparrowConstructorConfig positiveFocusedRequest = rustDefaults;
    positiveFocusedRequest.nFocussedSamples = 8U;

    const auto rustDefaultResult = sparrow::ConstructSparrowInitialSolution(
        instance.Value(), rustDefaults);
    const auto focusedResult = sparrow::ConstructSparrowInitialSolution(
        instance.Value(), positiveFocusedRequest);
    if (!Expect(rustDefaultResult.HasValue() && focusedResult.HasValue(),
                output,
                "focused-sample builder paths must both construct the item"))
    {
        return false;
    }

    const auto& expected = rustDefaultResult.Value();
    const auto& actual = focusedResult.Value();
    const auto& expectedPlacements = expected.solution.layoutSnapshot.placements;
    const auto& actualPlacements = actual.solution.layoutSnapshot.placements;
    if (!Expect(expected.metrics.sampleCount == actual.metrics.sampleCount
                    && expectedPlacements.size() == actualPlacements.size(),
                output,
                "Sparrow constructor must keep focused sampling disabled like Rust"))
    {
        return false;
    }
    for (std::size_t index = 0U; index < expectedPlacements.size(); ++index)
    {
        const auto& expectedPlacement = expectedPlacements[index].placedItem;
        const auto& actualPlacement = actualPlacements[index].placedItem;
        const jaguar::Transform2d expectedTransform = expectedPlacement.Transform();
        const jaguar::Transform2d actualTransform = actualPlacement.Transform();
        if (!Expect(expectedPlacement.GetItemIndex()
                            == actualPlacement.GetItemIndex()
                        && expectedTransform.rotationRad
                            == actualTransform.rotationRad
                        && expectedTransform.translation.x
                            == actualTransform.translation.x
                        && expectedTransform.translation.y
                            == actualTransform.translation.y,
                    output,
                    "focused-sample setting must not change the Rust-compatible placement"))
        {
            return false;
        }
    }
    return true;
}

bool TestSparrowBuilderGrowthBeyond32AttemptsMatchesRust(
    std::ostream& output)
{
    std::ifstream inputFile;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/SparrowLbfGrowthAttempt.json",
             "../SparrowCore/Tests/Fixtures/SparrowLbfGrowthAttempt.json",
             "../JaguarRun/SparrowCore/Tests/Fixtures/SparrowLbfGrowthAttempt.json",
             "..\\SparrowCore\\Tests\\Fixtures\\SparrowLbfGrowthAttempt.json"})
    {
        inputFile.open(path);
        if (inputFile.is_open())
        {
            break;
        }
        inputFile.clear();
    }
    if (!Expect(inputFile.is_open(),
                output,
                "growth-attempt shared input fixture must be readable"))
    {
        return false;
    }
    const std::string inputJson{
        std::istreambuf_iterator<char>(inputFile),
        std::istreambuf_iterator<char>()};
    const jaguar::Importer importer;
    const auto instance = importer.ImportSPInstanceJson(inputJson);
    if (!Expect(instance.HasValue(),
                output,
                "growth-attempt shared input must import"))
    {
        return false;
    }

    sparrow::SparrowConstructorConfig config;
    config.seed = 42U;
    config.nContainerSamples = 1U;
    config.nFocussedSamples = 0U;
    config.nCoordinateDescents = 1U;
    const auto result =
        sparrow::ConstructSparrowInitialSolution(instance.Value(), config);
    if (!Expect(result.HasValue(),
                output,
                "Sparrow builder must not stop at the C++-only 32-attempt cap"))
    {
        if (!result.HasValue())
        {
            output << "growth-attempt error=" << result.GetError().message << '\n';
        }
        return false;
    }

    std::ifstream referenceFile;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/SparrowLbfGrowthAttemptRustReference.tsv",
             "../SparrowCore/Tests/Fixtures/SparrowLbfGrowthAttemptRustReference.tsv",
             "../JaguarRun/SparrowCore/Tests/Fixtures/SparrowLbfGrowthAttemptRustReference.tsv",
             "..\\SparrowCore\\Tests\\Fixtures\\SparrowLbfGrowthAttemptRustReference.tsv"})
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
                "growth-attempt Rust reference must be readable"))
    {
        return false;
    }

    std::uint64_t expectedSeed = 0U;
    std::size_t expectedPlaced = 0U;
    std::size_t expectedRemaining = 0U;
    std::string fixture;
    std::uint32_t expectedWidthBits = 0U;
    std::uint32_t expectedRotationBits = 0U;
    std::uint32_t expectedXBits = 0U;
    std::uint32_t expectedYBits = 0U;
    bool expectedFeasible = false;
    bool referenceFound = false;
    std::string line;
    while (std::getline(referenceFile, line))
    {
        if (line.empty() || line.front() == '#')
        {
            continue;
        }
        std::istringstream row(line);
        if (row >> fixture >> expectedSeed >> expectedPlaced
                >> expectedRemaining >> std::hex >> expectedWidthBits
                >> expectedRotationBits >> expectedXBits >> expectedYBits
                >> std::dec >> std::boolalpha >> expectedFeasible
            && fixture == "growth_limit_38")
        {
            referenceFound = true;
            break;
        }
    }
    if (!Expect(referenceFound,
                output,
                "growth-attempt Rust reference row must be present"))
    {
        return false;
    }

    const auto& value = result.Value();
    const auto& metrics = value.metrics;
    const auto& placement = value.solution.layoutSnapshot.placements.front().placedItem;
    const auto transform = placement.Transform();
    const auto floatBits = [](double input) {
        const float value = static_cast<float>(input);
        std::uint32_t bits = 0U;
        std::memcpy(&bits, &value, sizeof(bits));
        return bits;
    };
    const bool matches = expectedSeed == config.seed
        && expectedPlaced == metrics.placedItemCount
        && expectedRemaining == metrics.remainingDemand
        && expectedFeasible == metrics.feasible
        && expectedWidthBits == floatBits(metrics.stripWidth)
        && expectedRotationBits == floatBits(transform.rotationRad)
        && expectedXBits == floatBits(transform.translation.x)
        && expectedYBits == floatBits(transform.translation.y);
    if (!matches)
    {
        output << "growth-attempt expected bits: width=0x" << std::hex
               << expectedWidthBits << " rotation=0x" << expectedRotationBits
               << " x=0x" << expectedXBits << " y=0x" << expectedYBits
               << "; actual: width=0x" << floatBits(metrics.stripWidth)
               << " rotation=0x" << floatBits(transform.rotationRad)
               << " x=0x" << floatBits(transform.translation.x)
               << " y=0x" << floatBits(transform.translation.y) << std::dec
               << "; placed=" << metrics.placedItemCount
               << " remaining=" << metrics.remainingDemand
               << " feasible=" << metrics.feasible << '\n';
    }
    return Expect(matches,
                  output,
                  "Sparrow LBF result after more than 32 growth attempts must match Rust bits");
}

bool TestSparrowBuilderRustF32SortTie(std::ostream& output)
{
    const auto instance = MakeInstance(
        {MakeItem(0U, 591U, 1.0, 1.0),
         MakeItem(1U, 592U, 0.50006104, 1.6449358)},
        {1U, 1U},
        2.0);
    if (!Expect(instance.HasValue(),
                output,
                "Rust f32 sort-tie instance creation failed"))
    {
        return false;
    }

    const jaguar::Item& first = instance.Value().GetItem(0U);
    const jaguar::Item& second = instance.Value().GetItem(1U);
    const auto* firstSurrogate = first.CollisionShape().GetSurrogate();
    const auto* secondSurrogate = second.CollisionShape().GetSurrogate();
    if (!Expect(firstSurrogate != nullptr && secondSurrogate != nullptr,
                output,
                "Rust f32 sort-tie items must have surrogates"))
    {
        return false;
    }

    const float firstRustKey =
        static_cast<float>(firstSurrogate->ConvexHullArea())
        * static_cast<float>(first.CollisionShape().Diameter());
    const float secondRustKey =
        static_cast<float>(secondSurrogate->ConvexHullArea())
        * static_cast<float>(second.CollisionShape().Diameter());
    const double firstCppKey = firstSurrogate->ConvexHullArea()
        * first.CollisionShape().Diameter();
    const double secondCppKey = secondSurrogate->ConvexHullArea()
        * second.CollisionShape().Diameter();
    if (firstRustKey != secondRustKey || !(firstCppKey < secondCppKey))
    {
        output << std::setprecision(17)
               << "Rust-f32 keys: " << firstRustKey << ", " << secondRustKey
               << "; C++-double keys: " << firstCppKey << ", " << secondCppKey
               << '\n';
        return Expect(false,
                      output,
                      "sort-tie geometry must tie in Rust f32 but differ in C++ double");
    }

    sparrow::SparrowConstructorConfig config;
    config.seed = 42U;
    config.nContainerSamples = 64U;
    config.nFocussedSamples = 0U;
    config.nCoordinateDescents = 1U;
    const auto constructed =
        sparrow::ConstructSparrowInitialSolution(instance.Value(), config);
    if (!Expect(constructed.HasValue(),
                output,
                "Rust f32 sort-tie construction failed"))
    {
        return false;
    }

    const auto& placements =
        constructed.Value().solution.layoutSnapshot.placements;
    return Expect(!placements.empty()
                      && placements.front().placedItem.GetItemIndex() == 0U,
                  output,
                  "construction ties must retain input order like Rust f32 sorting");
}

bool TestSparrowBuilderRejectsWidthCeilingEquality(std::ostream& output)
{
    const auto instance = MakeInstance({MakeItem(0U, 593U, 2.0, 2.0)}, {1U}, 4.0);
    if (!Expect(instance.HasValue(),
                output,
                "width-ceiling equality instance creation failed"))
    {
        return false;
    }

    const double widthCeiling = static_cast<double>(
        2.0F * static_cast<float>(
                   instance.Value().GetItem(0U).CollisionShape().Diameter()));
    const double baseWidth = instance.Value().BaseStrip().width;
    sparrow::SparrowConstructorConfig config;
    config.seed = 42U;
    config.nContainerSamples = 1U;
    config.nFocussedSamples = 0U;
    config.nCoordinateDescents = 1U;
    config.widthGrowthRatio = static_cast<double>(
        static_cast<float>(widthCeiling / baseWidth));
    const float nextWidth = static_cast<float>(baseWidth)
        * static_cast<float>(config.widthGrowthRatio);
    if (!Expect(nextWidth == static_cast<float>(widthCeiling),
                output,
                "width-growth test must land exactly on the configured ceiling"))
    {
        return false;
    }

    const auto constructed =
        sparrow::ConstructSparrowInitialSolution(instance.Value(), config);
    return Expect(!constructed.HasValue()
                      && constructed.GetError().code == jaguar::ErrorCode::Infeasible,
                  output,
                  "Rust width growth must stop when next width equals its ceiling");
}

bool TestLegacyLbfMetricsReferenceFixture(std::ostream& output)
{
    std::ifstream metricsFile;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/LbfMetricsReference.tsv",
             "../SparrowCore/Tests/Fixtures/LbfMetricsReference.tsv",
             "../JaguarRun/SparrowCore/Tests/Fixtures/LbfMetricsReference.tsv",
             "..\\SparrowCore\\Tests\\Fixtures\\LbfMetricsReference.tsv"})
    {
        metricsFile.open(path);
        if (metricsFile.is_open())
        {
            break;
        }
        metricsFile.clear();
    }
    if (!Expect(metricsFile.is_open(),
                output,
                "legacy jagua-rs LBF metrics reference must be readable"))
    {
        return false;
    }

    bool provenanceFound = false;
    bool schemaFound = false;
    bool sppFound = false;
    bool bppFound = false;
    std::string line;
    while (std::getline(metricsFile, line))
    {
        if (line.empty())
        {
            continue;
        }
        if (line.front() == '#')
        {
            provenanceFound = provenanceFound
                || line.find("jagua-rs/lbf") != std::string::npos;
            schemaFound = schemaFound
                || line.find("fixture problem seed samples placed remaining width density cost feasible")
                       != std::string::npos;
            continue;
        }

        std::istringstream row(line);
        std::string fixture;
        std::string problem;
        std::uint64_t seed = 0U;
        std::size_t samples = 0U;
        std::size_t placed = 0U;
        std::size_t remaining = 0U;
        double width = 0.0;
        double density = 0.0;
        double cost = 0.0;
        bool feasible = false;
        if (!(row >> fixture >> problem >> seed >> samples >> placed
                  >> remaining >> width >> density >> cost
                  >> std::boolalpha >> feasible))
        {
            return Expect(false,
                          output,
                          "legacy LBF metrics reference row must match its schema");
        }

        if (fixture == "lbf_spp_small")
        {
            sppFound = problem == "spp" && seed == 42U && samples == 128U
                && placed == 2U && remaining == 0U
                && std::abs(width - 4.276253) < 1.0e-6
                && std::abs(density - 0.46769914) < 1.0e-8
                && cost == 0.0 && feasible;
        }
        else if (fixture == "lbf_bpp_small")
        {
            bppFound = problem == "bpp" && seed == 42U && samples == 64U
                && placed == 1U && remaining == 0U && width == 0.0
                && std::abs(density - 0.11111111) < 1.0e-8
                && cost == 10.0 && feasible;
        }
    }

    return Expect(provenanceFound && schemaFound && sppFound && bppFound,
                  output,
                  "legacy jagua-rs LBF metrics provenance, schema, and values must be valid");
}

bool TestSparrowRustReferenceMetrics(std::ostream& output)
{
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
                "shared Sparrow Rust/C++ SPP input fixture must be readable"))
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
                "shared Sparrow Rust/C++ SPP input must import successfully"))
    {
        return false;
    }

    sparrow::SparrowConstructorConfig config;
    config.seed = 42U;
    config.nContainerSamples = 64U;
    config.nFocussedSamples = 0U;
    config.nCoordinateDescents = 1U;
    const auto result =
        sparrow::ConstructSparrowInitialSolution(instance.Value(), config);
    if (!Expect(result.HasValue(),
                output,
                "SparrowCore must construct the shared Rust reference instance"))
    {
        return false;
    }

    std::ifstream referenceFile;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/SparrowLbfRustReference.tsv",
             "../SparrowCore/Tests/Fixtures/SparrowLbfRustReference.tsv",
             "../JaguarRun/SparrowCore/Tests/Fixtures/SparrowLbfRustReference.tsv",
             "..\\SparrowCore\\Tests\\Fixtures\\SparrowLbfRustReference.tsv"})
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
                "live Sparrow Rust reference metrics fixture must be readable"))
    {
        return false;
    }

    bool referenceFound = false;
    std::uint64_t referenceSeed = 0U;
    std::size_t referencePlaced = 0U;
    std::size_t referenceRemaining = 0U;
    double referenceWidth = 0.0;
    double referenceDensity = 0.0;
    bool referenceFeasible = false;
    std::string line;
    while (std::getline(referenceFile, line))
    {
        if (line.empty() || line.front() == '#')
        {
            continue;
        }
        std::istringstream row(line);
        std::string fixture;
        if (row >> fixture >> referenceSeed >> referencePlaced
                >> referenceRemaining >> referenceWidth >> referenceDensity
                >> std::boolalpha >> referenceFeasible
            && fixture == "rust_spp_single_item")
        {
            referenceFound = true;
            break;
        }
    }
    if (!Expect(referenceFound,
                output,
                "Sparrow Rust reference must contain the shared SPP fixture"))
    {
        return false;
    }

    const sparrow::LbfMetrics& metrics = result.Value().metrics;
    const bool matches = referenceSeed == config.seed
        && referencePlaced == metrics.placedItemCount
        && referenceRemaining == metrics.remainingDemand
        && referenceFeasible == metrics.feasible
        // Both implementations stop final coordinate descent below 0.001 of
        // the item's minimum dimension; allow two terminal steps for the
        // independently rounded Rust/C++ geometry results.
        && std::abs(referenceWidth - metrics.stripWidth)
               < SPARROW_RUST_LBF_WIDTH_TOLERANCE
        && std::abs(referenceDensity - metrics.density)
               < SPARROW_RUST_LBF_DENSITY_TOLERANCE;
    if (!matches)
    {
        output << std::setprecision(17)
               << "Sparrow Rust reference: seed=" << referenceSeed
               << " placed=" << referencePlaced
               << " remaining=" << referenceRemaining
               << " width=" << referenceWidth
               << " density=" << referenceDensity
               << " feasible=" << referenceFeasible << '\n'
               << "SparrowCore result: seed=" << metrics.seed
               << " placed=" << metrics.placedItemCount
               << " remaining=" << metrics.remainingDemand
               << " width=" << metrics.stripWidth
               << " density=" << metrics.density
               << " feasible=" << metrics.feasible << '\n';
    }
    return Expect(matches,
                  output,
                  "SparrowCore initial metrics must match live Sparrow Rust output");
}

struct MultiInstancePlacementReference
{
    std::size_t itemIndex = 0U;
    double rotation = 0.0;
    double x = 0.0;
    double y = 0.0;
};

struct MultiInstanceCaseReference
{
    std::string name;
    std::uint64_t seed = 0U;
    std::size_t placed = 0U;
    std::size_t remaining = 0U;
    double width = 0.0;
    double density = 0.0;
    bool feasible = false;
    std::vector<MultiInstancePlacementReference> placements;
};

bool TestSparrowRustMultiInstanceLbfParity(std::ostream& output)
{
    std::ifstream referenceFile;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/SparrowLbfMultiInstanceRustReference.tsv",
             "../SparrowCore/Tests/Fixtures/SparrowLbfMultiInstanceRustReference.tsv",
             "../JaguarRun/SparrowCore/Tests/Fixtures/SparrowLbfMultiInstanceRustReference.tsv",
             "..\\SparrowCore\\Tests\\Fixtures\\SparrowLbfMultiInstanceRustReference.tsv"})
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
                "multi-instance Rust LBF reference fixture cannot be opened"))
    {
        return false;
    }

    std::vector<MultiInstanceCaseReference> references;
    MultiInstanceCaseReference* activeReference = nullptr;
    std::string line;
    while (std::getline(referenceFile, line))
    {
        if (line.empty() || line.front() == '#')
        {
            continue;
        }
        std::istringstream row(line);
        std::string record;
        row >> record;
        if (record == "case")
        {
            MultiInstanceCaseReference reference;
            if (!(row >> reference.name >> reference.seed >> reference.placed
                      >> reference.remaining >> reference.width
                      >> reference.density >> std::boolalpha
                      >> reference.feasible))
            {
                return Expect(false,
                              output,
                              "multi-instance Rust case row is malformed");
            }
            references.push_back(std::move(reference));
            activeReference = &references.back();
        }
        else if (record == "placement")
        {
            std::string caseName;
            MultiInstancePlacementReference placement;
            if (activeReference == nullptr
                || !(row >> caseName >> placement.itemIndex
                      >> placement.rotation >> placement.x >> placement.y)
                || caseName != activeReference->name)
            {
                return Expect(false,
                              output,
                              "multi-instance Rust placement row is malformed");
            }
            activeReference->placements.push_back(placement);
        }
        else
        {
            return Expect(false,
                          output,
                          "multi-instance Rust reference has an unknown row type");
        }
    }

    struct InputCase
    {
        const char* name;
        const char* path;
    };
    constexpr InputCase inputs[] = {
        {"single_item", "RustSppSingleItem.json"},
        {"three_square_cold_start", "RustSppOptimizeColdStart.json"},
        {"mixed_shape", "ExplorationPool.json"},
        {"three_square_boundary", "RustSppThreeSquareBoundary.json"},
    };
    const jaguar::Importer importer(sparrow::MakeRustDefaultImporterConfig());
    for (const InputCase& input : inputs)
    {
        const auto expected = std::find_if(
            references.begin(), references.end(), [&input](const auto& row) {
                return row.name == input.name;
            });
        if (!Expect(expected != references.end(),
                    output,
                    "multi-instance Rust reference is missing an input case"))
        {
            return false;
        }

        std::ifstream instanceFile;
        const std::string fixturePath =
            std::string{"SparrowCore/Tests/Fixtures/"} + input.path;
        const std::string parentFixturePath =
            std::string{"../SparrowCore/Tests/Fixtures/"} + input.path;
        const std::string alternateFixturePath =
            std::string{"../JaguarRun/SparrowCore/Tests/Fixtures/"} + input.path;
        const std::string windowsFixturePath =
            std::string{"..\\SparrowCore\\Tests\\Fixtures\\"} + input.path;
        for (const std::string& path : {fixturePath,
                                        parentFixturePath,
                                        alternateFixturePath,
                                        windowsFixturePath})
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
                    "multi-instance Rust input fixture cannot be opened"))
        {
            return false;
        }
        const std::string instanceJson{
            std::istreambuf_iterator<char>(instanceFile),
            std::istreambuf_iterator<char>()};
        const auto instance = importer.ImportSPInstanceJson(instanceJson);
        if (!Expect(instance.HasValue(),
                    output,
                    "multi-instance Rust input must import with Rust defaults"))
        {
            return false;
        }

        sparrow::SparrowConstructorConfig config;
        config.seed = 42U;
        config.nContainerSamples = 64U;
        config.nFocussedSamples = 0U;
        config.nCoordinateDescents = 1U;
        const auto result =
            sparrow::ConstructSparrowInitialSolution(instance.Value(), config);
        if (!Expect(result.HasValue(),
                    output,
                    "SparrowCore multi-instance LBF construction must succeed"))
        {
            return false;
        }
        const auto& metrics = result.Value().metrics;
        if (!(expected->seed == metrics.seed
              && expected->placed == metrics.placedItemCount
              && expected->remaining == metrics.remainingDemand
              && expected->feasible == metrics.feasible
              && std::abs(expected->width - metrics.stripWidth)
                     < SPARROW_RUST_LBF_WIDTH_TOLERANCE
              && std::abs(expected->density - metrics.density)
                     < SPARROW_RUST_LBF_DENSITY_TOLERANCE))
        {
            output << std::setprecision(17) << "multi-instance case "
                   << input.name << " Rust(width=" << expected->width
                   << ", density=" << expected->density << ", placed="
                   << expected->placed << ", remaining="
                   << expected->remaining << ", feasible="
                   << expected->feasible << ") C++(width="
                   << metrics.stripWidth << ", density=" << metrics.density
                   << ", placed=" << metrics.placedItemCount
                   << ", remaining=" << metrics.remainingDemand
                   << ", feasible=" << metrics.feasible << ")\n";
            return Expect(false,
                          output,
                          "multi-instance LBF metrics must match Rust within recorded tolerances");
        }

        std::vector<const jaguar::LayoutPlacementSnapshot*> placements;
        for (const auto& placement :
             result.Value().solution.layoutSnapshot.placements)
        {
            placements.push_back(&placement);
        }
        std::sort(placements.begin(), placements.end(), [](const auto* left,
                                                           const auto* right) {
            const auto leftIndex = left->placedItem.GetItemIndex();
            const auto rightIndex = right->placedItem.GetItemIndex();
            if (leftIndex != rightIndex)
            {
                return leftIndex < rightIndex;
            }
            const auto& leftTransform = left->placedItem.Transform();
            const auto& rightTransform = right->placedItem.Transform();
            if (leftTransform.translation.x != rightTransform.translation.x)
            {
                return leftTransform.translation.x < rightTransform.translation.x;
            }
            return leftTransform.translation.y < rightTransform.translation.y;
        });
        if (!Expect(placements.size() == expected->placements.size(),
                    output,
                    "multi-instance placed-item identity count must match Rust"))
        {
            return false;
        }
        for (std::size_t index = 0U; index < placements.size(); ++index)
        {
            const auto& actualPlacement =
                placements[index]->placedItem;
            const auto& actualTransform = actualPlacement.Transform();
            const auto& expectedPlacement = expected->placements[index];
            const bool placementMatches =
                actualPlacement.GetItemIndex() == expectedPlacement.itemIndex
                && std::abs(actualTransform.rotationRad
                            - expectedPlacement.rotation) < 1.0e-6
                && std::abs(actualTransform.translation.x
                            - expectedPlacement.x) < 5.0e-5
                && std::abs(actualTransform.translation.y
                            - expectedPlacement.y) < 5.0e-5;
            if (!placementMatches)
            {
                output << std::setprecision(17) << "multi-instance case "
                       << input.name << " placement " << index
                       << " differs: Rust(item=" << expectedPlacement.itemIndex
                       << ", rotation=" << expectedPlacement.rotation
                       << ", x=" << expectedPlacement.x << ", y="
                       << expectedPlacement.y << ") C++(item="
                       << actualPlacement.GetItemIndex() << ", rotation="
                       << actualTransform.rotationRad << ", x="
                       << actualTransform.translation.x << ", y="
                       << actualTransform.translation.y << ")\n";
                return Expect(false,
                              output,
                              "multi-instance LBF placements must match the Rust reference");
            }
        }
    }
    return true;
}

struct DefaultLbfCaseReference
{
    std::string name;
    std::uint64_t seed = 0U;
    std::size_t containerSamples = 0U;
    std::size_t focusedSamples = 0U;
    std::size_t coordinateDescents = 0U;
    std::uint32_t initialWidthBits = 0U;
    std::size_t placed = 0U;
    std::size_t remaining = 0U;
    double width = 0.0;
    double density = 0.0;
    bool feasible = false;
    std::vector<MultiInstancePlacementReference> placements;
};

bool TestSparrowRustDefaultLbfBuilderParity(std::ostream& output)
{
    std::ifstream referenceFile;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/SparrowLbfDefaultBuilderRustReference.tsv",
             "../SparrowCore/Tests/Fixtures/SparrowLbfDefaultBuilderRustReference.tsv",
             "../JaguarRun/SparrowCore/Tests/Fixtures/SparrowLbfDefaultBuilderRustReference.tsv",
             "..\\SparrowCore\\Tests\\Fixtures\\SparrowLbfDefaultBuilderRustReference.tsv"})
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
                "default Rust LBF builder reference fixture cannot be opened"))
    {
        return false;
    }

    std::vector<DefaultLbfCaseReference> references;
    DefaultLbfCaseReference* activeReference = nullptr;
    std::string line;
    while (std::getline(referenceFile, line))
    {
        if (line.empty() || line.front() == '#')
        {
            continue;
        }
        std::istringstream row(line);
        std::string record;
        row >> record;
        if (record == "case")
        {
            DefaultLbfCaseReference reference;
            if (!(row >> reference.name >> reference.seed
                      >> reference.containerSamples >> reference.focusedSamples
                      >> reference.coordinateDescents >> std::hex
                      >> reference.initialWidthBits >> std::dec
                      >> reference.placed >> reference.remaining >> reference.width
                      >> reference.density >> std::boolalpha >> reference.feasible))
            {
                return Expect(false,
                              output,
                              "default Rust LBF case row is malformed");
            }
            references.push_back(std::move(reference));
            activeReference = &references.back();
        }
        else if (record == "placement")
        {
            std::string caseName;
            MultiInstancePlacementReference placement;
            if (activeReference == nullptr
                || !(row >> caseName >> placement.itemIndex >> placement.rotation
                      >> placement.x >> placement.y)
                || caseName != activeReference->name)
            {
                return Expect(false,
                              output,
                              "default Rust LBF placement row is malformed");
            }
            activeReference->placements.push_back(placement);
        }
        else
        {
            return Expect(false,
                          output,
                          "default Rust LBF reference has an unknown row type");
        }
    }

    struct InputCase
    {
        const char* name;
        const char* path;
        std::optional<std::uint32_t> initialWidthBits;
    };
    constexpr std::uint32_t widthTwoBits = 0x40000000U;
    constexpr InputCase inputs[] = {
        {"default_three_square", "RustSppOptimizeColdStart.json", std::nullopt},
        {"width_predecessor", "RustSppSingleItem.json", widthTwoBits - 1U},
        {"width_exact", "RustSppSingleItem.json", widthTwoBits},
        {"width_successor", "RustSppSingleItem.json", widthTwoBits + 1U},
    };
    const jaguar::Importer importer(sparrow::MakeRustDefaultImporterConfig());
    for (const InputCase& input : inputs)
    {
        const auto expected = std::find_if(
            references.begin(), references.end(), [&input](const auto& row) {
                return row.name == input.name;
            });
        if (!Expect(expected != references.end(),
                    output,
                    "default Rust LBF reference is missing an input case"))
        {
            return false;
        }

        std::ifstream instanceFile;
        const std::string fixturePath =
            std::string{"SparrowCore/Tests/Fixtures/"} + input.path;
        const std::string parentFixturePath =
            std::string{"../SparrowCore/Tests/Fixtures/"} + input.path;
        const std::string alternateFixturePath =
            std::string{"../JaguarRun/SparrowCore/Tests/Fixtures/"} + input.path;
        const std::string windowsFixturePath =
            std::string{"..\\SparrowCore\\Tests\\Fixtures\\"} + input.path;
        for (const std::string& path : {fixturePath,
                                        parentFixturePath,
                                        alternateFixturePath,
                                        windowsFixturePath})
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
                    "default Rust LBF input fixture cannot be opened"))
        {
            return false;
        }
        const std::string instanceJson{
            std::istreambuf_iterator<char>(instanceFile),
            std::istreambuf_iterator<char>()};
        const auto imported = importer.ImportSPInstanceJson(instanceJson);
        if (!Expect(imported.HasValue(),
                    output,
                    "default Rust LBF input must import with Rust defaults"))
        {
            return false;
        }

        jaguar::Strip baseStrip = imported.Value().BaseStrip();
        if (input.initialWidthBits.has_value())
        {
            float initialWidth = 0.0F;
            const std::uint32_t bits = *input.initialWidthBits;
            static_assert(sizeof(initialWidth) == sizeof(bits));
            std::memcpy(&initialWidth, &bits, sizeof(initialWidth));
            baseStrip.width = static_cast<double>(initialWidth);
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
        const auto adjustedInstance = jaguar::SPInstance::Create(
            std::move(items), std::move(demands), baseStrip);
        if (!Expect(adjustedInstance.HasValue(),
                    output,
                    "default Rust LBF explicit-width instance must be valid"))
        {
            return false;
        }
        const float actualInitialWidth =
            static_cast<float>(adjustedInstance.Value().BaseStrip().width);
        std::uint32_t actualInitialWidthBits = 0U;
        std::memcpy(&actualInitialWidthBits,
                    &actualInitialWidth,
                    sizeof(actualInitialWidthBits));
        if (!Expect(expected->initialWidthBits == actualInitialWidthBits,
                    output,
                    "C++ initial strip width must match the Rust f32 input"))
        {
            return false;
        }

        sparrow::SparrowConstructorConfig config;
        config.seed = expected->seed;
        if (!Expect(config.nContainerSamples == expected->containerSamples
                        && config.nFocussedSamples == expected->focusedSamples
                        && config.nCoordinateDescents == expected->coordinateDescents,
                    output,
                    "Sparrow constructor defaults must match the Rust LBF budget"))
        {
            return false;
        }
        const auto result = sparrow::ConstructSparrowInitialSolution(
            adjustedInstance.Value(), config);
        if (!Expect(result.HasValue(),
                    output,
                    "default Sparrow LBF construction must succeed"))
        {
            return false;
        }

        const auto& metrics = result.Value().metrics;
        if (!(expected->seed == metrics.seed
              && expected->placed == metrics.placedItemCount
              && expected->remaining == metrics.remainingDemand
              && expected->feasible == metrics.feasible
              && std::abs(expected->width - metrics.stripWidth)
                     < SPARROW_RUST_LBF_WIDTH_TOLERANCE
              && std::abs(expected->density - metrics.density)
                     < SPARROW_RUST_LBF_DENSITY_TOLERANCE))
        {
            output << std::setprecision(17) << "default LBF case " << input.name
                   << " Rust(width=" << expected->width << ", density="
                   << expected->density << ", placed=" << expected->placed
                   << ", remaining=" << expected->remaining << ", feasible="
                   << expected->feasible << ") C++(width=" << metrics.stripWidth
                   << ", density=" << metrics.density << ", placed="
                   << metrics.placedItemCount << ", remaining="
                   << metrics.remainingDemand << ", feasible=" << metrics.feasible
                   << ")\n";
            return Expect(false,
                          output,
                          "default LBF metrics must match Rust within recorded tolerances");
        }

        std::vector<const jaguar::LayoutPlacementSnapshot*> placements;
        for (const auto& placement : result.Value().solution.layoutSnapshot.placements)
        {
            placements.push_back(&placement);
        }
        std::sort(placements.begin(), placements.end(), [](const auto* left,
                                                           const auto* right) {
            const auto leftIndex = left->placedItem.GetItemIndex();
            const auto rightIndex = right->placedItem.GetItemIndex();
            if (leftIndex != rightIndex)
            {
                return leftIndex < rightIndex;
            }
            const auto& leftTransform = left->placedItem.Transform();
            const auto& rightTransform = right->placedItem.Transform();
            if (leftTransform.translation.x != rightTransform.translation.x)
            {
                return leftTransform.translation.x < rightTransform.translation.x;
            }
            return leftTransform.translation.y < rightTransform.translation.y;
        });
        if (!Expect(placements.size() == expected->placements.size(),
                    output,
                    "default LBF placement count must match Rust"))
        {
            return false;
        }
        for (std::size_t index = 0U; index < placements.size(); ++index)
        {
            const auto& actualPlacement = placements[index]->placedItem;
            const auto& actualTransform = actualPlacement.Transform();
            const auto& expectedPlacement = expected->placements[index];
            if (!(actualPlacement.GetItemIndex() == expectedPlacement.itemIndex
                  && std::abs(actualTransform.rotationRad
                              - expectedPlacement.rotation) < 1.0e-6
                  && std::abs(actualTransform.translation.x
                              - expectedPlacement.x) < 5.0e-5
                  && std::abs(actualTransform.translation.y
                              - expectedPlacement.y) < 5.0e-5))
            {
                output << std::setprecision(17) << "default LBF case "
                       << input.name << " placement " << index
                       << " differs from Rust reference\n";
                return Expect(false,
                              output,
                              "default LBF placement transforms must match Rust");
            }
        }
    }

    return Expect(references.size() == 4U,
                  output,
                  "default Rust LBF fixture must contain exactly four cases");
}
}

bool RunSppLbfTests(std::ostream& output)
{
    return TestSmallSppFixture(output)
        && TestSppDemandAndInvalidInputs(output)
        && TestLbfContinuousRotationUsesRustF32WidthGrowth(output)
        && TestSppJsonRoundTrip(output)
        && TestSparrowBuilderSmallInstance(output)
        && TestSparrowBuilderZeroContainerSamplesMatchesRustError(output)
        && TestSparrowBuilderFocusedSamplesStayAtRustZero(output)
        && TestSparrowBuilderGrowthBeyond32AttemptsMatchesRust(output)
        && TestSparrowBuilderRustF32SortTie(output)
        && TestSparrowBuilderRejectsWidthCeilingEquality(output)
        && TestLegacyLbfMetricsReferenceFixture(output)
        && TestSparrowRustReferenceMetrics(output)
        && TestSparrowRustMultiInstanceLbfParity(output)
        && TestSparrowRustDefaultLbfBuilderParity(output);
}
