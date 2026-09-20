#include "SparrowCore/optimizer/spp_lbf_optimizer.h"

#include "JaguarCore/assertions.h"
#include "JaguarCore/entities/item.h"
#include "JaguarCore/io/importer.h"
#include "JaguarCore/io/json_codec.h"
#include "JaguarCore/problems/spp.h"

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
bool expect(bool condition, std::ostream& output, const char* message)
{
    if (condition)
    {
        return true;
    }
    output << "SPP LBF TEST FAILURE: " << message << '\n';
    return false;
}

jaguar::Item makeItem(jaguar::ItemIndex index,
                      jaguar::ItemId externalId,
                      double width,
                      double height,
                      std::optional<std::size_t> minQuality = std::nullopt)
{
    const auto polygon = jaguar::SimplePolygon::create({
        {0.0, 0.0}, {width, 0.0}, {width, height}, {0.0, height}});
    jaguar::SurrogateConfig surrogateConfig{};
    surrogateConfig.poleLimits = {{{1, 0.0}, {0, 0.0}, {0, 0.0}}};
    const auto item = jaguar::Item::create(
        index,
        externalId,
        {polygon.value(),
         jaguar::Transform2d::identity(),
         jaguar::ShapeModifyMode::Inflate,
         {}},
        surrogateConfig,
        minQuality);
    return item.value();
}

jaguar::Result<jaguar::SPInstance> makeInstance(
    std::vector<jaguar::Item> items,
    std::vector<std::size_t> demands,
    double height = 4.0)
{
    return jaguar::SPInstance::create(
        std::move(items),
        std::move(demands),
        height,
        jaguar::CDEConfig{},
        jaguar::ShapeModifyConfig{});
}

bool testSmallSppFixture(std::ostream& output)
{
    const auto instance = makeInstance({makeItem(0, 201, 2.0, 2.0)}, {2});
    if (!expect(instance.hasValue(), output, "small SPP instance creation failed"))
    {
        return false;
    }

    sparrow::LbfConfig config;
    config.seed = 42U;
    config.samplesPerItem = 64U;
    config.localSearchFraction = 0.25;
    const auto result = sparrow::solveSppLbf(instance.value(), config);
    if (!expect(result.hasValue(), output, "small SPP LBF solve failed"))
    {
        return false;
    }
    std::ifstream metricsFile;
    for (const char* path : {
             "SparrowCore/tests/fixtures/spp_lbf_cpp_reference.tsv",
             "../SparrowCore/tests/fixtures/spp_lbf_cpp_reference.tsv",
             "..\\SparrowCore\\tests\\fixtures\\spp_lbf_cpp_reference.tsv"})
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
    if (!expect(referenceFound,
                output,
                "C++ SPP metrics reference fixture must be readable"))
    {
        return false;
    }
    if (!expect(result.value().metrics.sampleCount == referenceSamples
                    && result.value().metrics.placedItemCount == referencePlaced
                    && result.value().metrics.remainingDemand == referenceRemaining
                    && std::abs(result.value().metrics.stripWidth - referenceWidth)
                           < 1.0e-9
                    && std::abs(result.value().metrics.density - referenceDensity)
                           < 1.0e-9,
                output,
                "fixed seed SPP metrics must match the C++ reference fixture"))
    {
        return false;
    }
    if (!expect(result.value().metrics.placedItemCount == 2U
                    && result.value().metrics.remainingDemand == 0U
                    && result.value().solution.layoutSnapshot.placements.size() == 2U
                    && result.value().metrics.feasible
                    && std::isfinite(result.value().metrics.stripWidth)
                    && result.value().metrics.stripWidth > 0.0,
                output,
                "small SPP LBF must conserve demand and produce a feasible solution"))
    {
        return false;
    }
    auto validationProblem = jaguar::SPProblem::create(instance.value());
    if (!expect(validationProblem.hasValue()
                    && validationProblem.value().restore(result.value().solution).hasValue()
                    && jaguar::validateSPSolution(
                           validationProblem.value(), result.value().solution).hasValue(),
                output,
                "SPP solution validator must accept the produced solution"))
    {
        return false;
    }
    return true;
}

bool testSppDemandAndInvalidInputs(std::ostream& output)
{
    const auto emptyDemand = makeInstance({makeItem(0, 301, 1.0, 1.0)}, {0});
    if (!expect(!emptyDemand.hasValue()
                    && emptyDemand.error().code == jaguar::ErrorCode::InvalidInput,
                output,
                "SPP zero total demand must be rejected by JaguarCore"))
    {
        return false;
    }

    const auto impossible = makeInstance({makeItem(0, 302, 8.0, 8.0)}, {1}, 4.0);
    if (!expect(impossible.hasValue(), output, "impossible SPP setup failed"))
    {
        return false;
    }
    sparrow::LbfConfig config;
    config.seed = 3U;
    config.samplesPerItem = 16U;
    config.localSearchFraction = 0.0;
    config.maxWidthGrowthAttempts = 0U;
    const auto impossibleResult = sparrow::solveSppLbf(impossible.value(), config);
    if (!expect(!impossibleResult.hasValue()
                    && impossibleResult.error().code == jaguar::ErrorCode::Infeasible,
                output,
                "SPP width growth exhaustion must return Infeasible"))
    {
        return false;
    }

    const auto growthInstance = makeInstance({makeItem(0, 303, 4.0, 1.0)}, {1}, 2.0);
    if (!expect(growthInstance.hasValue(), output, "SPP width-growth setup failed"))
    {
        return false;
    }
    sparrow::LbfConfig growthConfig;
    growthConfig.seed = 7U;
    growthConfig.samplesPerItem = 16U;
    growthConfig.localSearchFraction = 0.0;
    growthConfig.maxWidthGrowthAttempts = 8U;
    growthConfig.widthGrowthRatio = 1.5;
    const auto grown = sparrow::solveSppLbf(growthInstance.value(), growthConfig);
    if (!expect(grown.hasValue()
                    && grown.value().metrics.placedItemCount == 1U
                    && grown.value().metrics.remainingDemand == 0U,
                output,
                "SPP must widen the strip and retry a placement"))
    {
        return false;
    }

    sparrow::LbfConfig invalidConfig = config;
    invalidConfig.samplesPerItem = 0U;
    const auto invalidResult = sparrow::solveSppLbf(impossible.value(), invalidConfig);
    return expect(!invalidResult.hasValue()
                      && invalidResult.error().code == jaguar::ErrorCode::InvalidInput,
                  output,
                  "SPP LBF must reject invalid configuration");
}

bool testSppJsonRoundTrip(std::ostream& output)
{
    const auto instance = makeInstance({makeItem(0, 401, 2.0, 2.0)}, {1});
    if (!expect(instance.hasValue(), output, "SPP JSON instance setup failed"))
    {
        return false;
    }
    sparrow::LbfConfig config;
    config.seed = 42U;
    config.samplesPerItem = 32U;
    config.localSearchFraction = 0.0;
    const auto solved = sparrow::solveSppLbf(instance.value(), config);
    if (!expect(solved.hasValue(), output, "SPP JSON solve setup failed"))
    {
        return false;
    }

    const auto external = jaguar::externalSPSolutionFromSolution(
        instance.value(), solved.value().solution);
    if (!expect(external.hasValue(), output, "SPP external solution conversion failed"))
    {
        return false;
    }
    const auto json = jaguar::serializeExternalSPSolutionJson(external.value());
    if (!expect(json.hasValue(), output, "SPP solution JSON serialization failed"))
    {
        return false;
    }
    const jaguar::Importer importer;
    const auto restored = importer.importSPSolutionJson(instance.value(), json.value());
    if (!expect(restored.hasValue(), output, "SPP solution JSON import failed"))
    {
        return false;
    }
    return expect(restored.value().layoutSnapshot.placements.size()
                      == solved.value().solution.layoutSnapshot.placements.size()
                      && std::abs(restored.value().strip.width
                                  - solved.value().solution.strip.width)
                             < 1.0e-9,
                  output,
                  "SPP JSON round-trip must preserve placement count and strip width");
}
}

bool runSppLbfTests(std::ostream& output)
{
    return testSmallSppFixture(output)
        && testSppDemandAndInvalidInputs(output)
        && testSppJsonRoundTrip(output);
}
