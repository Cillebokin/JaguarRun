#include "SparrowCore/Optimizer/SppLbfOptimizer.h"
#include "SparrowCore/Optimizer/SparrowLbfBuilder.h"

#include "JaguarCore/Assertions.h"
#include "JaguarCore/Entities/Item.h"
#include "JaguarCore/IO/Importer.h"
#include "JaguarCore/IO/JsonCodec.h"
#include "JaguarCore/Problems/Spp.h"

#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
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

    const auto impossible = MakeInstance({MakeItem(0, 302, 8.0, 8.0)}, {1}, 4.0);
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
                    && impossibleResult.GetError().code == jaguar::ErrorCode::Infeasible,
                output,
                "SPP width growth exhaustion must return Infeasible"))
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
                      && invalidResult.GetError().code == jaguar::ErrorCode::InvalidInput,
                  output,
                  "SPP LBF must reject invalid configuration");
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
}

bool RunSppLbfTests(std::ostream& output)
{
    return TestSmallSppFixture(output)
        && TestSppDemandAndInvalidInputs(output)
        && TestSppJsonRoundTrip(output)
        && TestSparrowBuilderSmallInstance(output);
}
