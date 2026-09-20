#include "SparrowCore/optimizer/bpp_lbf_optimizer.h"

#include "JaguarCore/assertions.h"
#include "JaguarCore/entities/item.h"
#include "JaguarCore/io/importer.h"
#include "JaguarCore/io/json_codec.h"
#include "JaguarCore/problems/bpp.h"

#include <cmath>
#include <cstdint>
#include <fstream>
#include <optional>
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
    output << "BPP LBF TEST FAILURE: " << message << '\n';
    return false;
}

jaguar::Item makeItem(jaguar::ItemIndex index,
                      jaguar::ItemId externalId,
                      double width,
                      double height,
                      std::optional<std::size_t> minQuality = std::nullopt,
                      jaguar::RotationRange rotation = {})
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
        minQuality,
        std::move(rotation));
    return item.value();
}

jaguar::Container makeContainer(jaguar::ContainerId id,
                                double width,
                                double height,
                                std::vector<jaguar::QualityZone> zones = {})
{
    const auto polygon = jaguar::SimplePolygon::create({
        {0.0, 0.0}, {width, 0.0}, {width, height}, {0.0, height}});
    jaguar::CDEConfig config{};
    config.quadtreeDepth = 4;
    config.cdThreshold = 8;
    const auto container = jaguar::Container::create(
        id,
        {polygon.value(),
         jaguar::Transform2d::identity(),
         jaguar::ShapeModifyMode::Deflate,
         {}},
        std::move(zones),
        config);
    return container.value();
}

jaguar::Result<jaguar::BPInstance> makeInstance(
    std::vector<jaguar::Item> items,
    std::vector<std::size_t> demands,
    std::vector<jaguar::Bin> bins)
{
    return jaguar::BPInstance::create(
        std::move(items), std::move(demands), std::move(bins));
}

bool validateSolution(const jaguar::BPInstance& instance,
                      const jaguar::BPSolution& solution) noexcept
{
    auto problem = jaguar::BPProblem::create(instance);
    if (!problem.hasValue())
    {
        return false;
    }
    return problem.value().restore(solution).hasValue()
        && jaguar::validateBPSolution(problem.value(), solution).hasValue();
}

sparrow::LbfConfig testConfig()
{
    sparrow::LbfConfig config;
    config.seed = 42U;
    config.samplesPerItem = 64U;
    config.localSearchFraction = 0.25;
    return config;
}

bool testSmallBppFixture(std::ostream& output)
{
    const auto instance = makeInstance(
        {makeItem(0, 301, 2.0, 2.0)},
        {1},
        {{0, 401, makeContainer(401, 6.0, 6.0), 1, 10}});
    if (!expect(instance.hasValue(), output, "small BPP instance creation failed"))
    {
        return false;
    }

    const auto result = sparrow::solveBppLbf(instance.value(), testConfig());
    if (!expect(result.hasValue(), output, "small BPP LBF solve failed"))
    {
        return false;
    }
    std::ifstream metricsFile;
    for (const char* path : {
             "SparrowCore/tests/fixtures/bpp_lbf_cpp_reference.tsv",
             "../SparrowCore/tests/fixtures/bpp_lbf_cpp_reference.tsv",
             "..\\SparrowCore\\tests\\fixtures\\bpp_lbf_cpp_reference.tsv"})
    {
        metricsFile.open(path);
        if (metricsFile.is_open())
        {
            break;
        }
        metricsFile.clear();
    }
    std::uint64_t referenceSeed = 0U;
    std::size_t referenceSamples = 0U;
    std::size_t referencePlaced = 0U;
    std::size_t referenceRemaining = 0U;
    double referenceWidth = 0.0;
    double referenceDensity = 0.0;
    std::uint64_t referenceCost = 0U;
    bool referenceFeasible = false;
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
        std::string feasibleText;
        if (row >> problemName >> referenceSeed >> referenceSamples
                >> referencePlaced >> referenceRemaining >> referenceWidth
                >> referenceDensity >> referenceCost >> feasibleText
            && problemName == "lbf_bpp_small_cpp")
        {
            referenceFeasible = feasibleText == "true";
            referenceFound = true;
            break;
        }
    }
    if (!expect(referenceFound,
                output,
                "C++ BPP metrics reference fixture must be readable"))
    {
        return false;
    }
    if (!expect(result.value().solution.layouts.size() == 1U
                    && result.value().solution.layouts.front().layoutSnapshot
                           .placements.size() == 1U
                    && result.value().solution.cost(instance.value()) == 10U
                    && result.value().metrics.seed == referenceSeed
                    && result.value().metrics.sampleCount == referenceSamples
                    && result.value().metrics.placedItemCount == 1U
                    && result.value().metrics.placedItemCount == referencePlaced
                    && result.value().metrics.remainingDemand == 0U
                    && result.value().metrics.remainingDemand == referenceRemaining
                    && result.value().metrics.stripWidth == referenceWidth
                    && std::abs(result.value().metrics.density - referenceDensity)
                           < 1.0e-9
                    && result.value().solution.cost(instance.value()) == referenceCost
                    && result.value().metrics.feasible == referenceFeasible
                    && result.value().metrics.feasible
                    && std::isfinite(result.value().metrics.density)
                    && result.value().metrics.density > 0.0,
                output,
                "small BPP LBF must place one item in one non-empty bin"))
    {
        return false;
    }
    return expect(validateSolution(instance.value(), result.value().solution),
                  output,
                  "small BPP solution validator must accept the result");
}

bool testOpenLayoutReuseAndInventory(std::ostream& output)
{
    const auto reuseInstance = makeInstance(
        {makeItem(0, 501, 2.0, 2.0)},
        {2},
        {{0, 601, makeContainer(601, 6.0, 6.0), 1, 10}});
    if (!expect(reuseInstance.hasValue(), output, "BPP open-layout setup failed"))
    {
        return false;
    }
    auto reused = sparrow::solveBppLbf(reuseInstance.value(), testConfig());
    if (!expect(reused.hasValue(), output, "BPP open-layout solve failed"))
    {
        return false;
    }
    if (!expect(reused.value().solution.layouts.size() == 1U
                    && reused.value().solution.layouts.front().layoutSnapshot
                           .placements.size() == 2U
                    && reused.value().solution.cost(reuseInstance.value()) == 10U
                    && reused.value().metrics.remainingDemand == 0U,
                output,
                "BPP must reuse the current open layout before opening another bin"))
    {
        return false;
    }

    const auto stockInstance = makeInstance(
        {makeItem(0, 502, 4.0, 2.0)},
        {2},
        {{0, 602, makeContainer(602, 5.0, 4.0), 1, 7},
         {1, 603, makeContainer(603, 6.0, 4.0), 1, 11}});
    if (!expect(stockInstance.hasValue(), output, "BPP stock setup failed"))
    {
        return false;
    }
    const auto stockResult = sparrow::solveBppLbf(stockInstance.value(), testConfig());
    if (!expect(stockResult.hasValue(), output, "BPP stock solve failed"))
    {
        return false;
    }
    if (!expect(stockResult.value().solution.layouts.size() == 2U
                    && stockResult.value().solution.cost(stockInstance.value()) == 18U
                    && stockResult.value().metrics.placedItemCount == 2U
                    && stockResult.value().metrics.remainingDemand == 0U,
                output,
                "BPP must consume stock and cost for two non-empty bins"))
    {
        return false;
    }

    const auto multipleTypes = makeInstance(
        {makeItem(0, 503, 2.0, 2.0), makeItem(1, 504, 1.0, 1.0)},
        {1, 1},
        {{0, 604, makeContainer(604, 6.0, 6.0), 1, 5}});
    if (!expect(multipleTypes.hasValue(), output, "BPP multiple-type setup failed"))
    {
        return false;
    }
    const auto multipleTypeResult = sparrow::solveBppLbf(
        multipleTypes.value(), testConfig());
    if (!expect(multipleTypeResult.hasValue(),
                output,
                "BPP multiple-type solve failed"))
    {
        return false;
    }
    return expect(multipleTypeResult.value().solution.layouts.size() == 1U
                      && multipleTypeResult.value().solution.layouts.front()
                             .layoutSnapshot.placements.size() == 2U
                      && multipleTypeResult.value().solution.cost(multipleTypes.value()) == 5U
                      && multipleTypeResult.value().metrics.placedItemCount == 2U
                      && multipleTypeResult.value().metrics.remainingDemand == 0U,
                  output,
                  "BPP must place multiple item types in one open layout");
}

bool testClosedBinFallbackDoesNotEmitEmptyLayouts(std::ostream& output)
{
    const auto instance = makeInstance(
        {makeItem(0, 505, 4.0, 2.0)},
        {1},
        {{0, 605, makeContainer(605, 3.0, 3.0), 1, 7},
         {1, 606, makeContainer(606, 6.0, 4.0), 1, 11}});
    if (!expect(instance.hasValue(), output, "BPP empty-layout setup failed"))
    {
        return false;
    }
    const auto result = sparrow::solveBppLbf(instance.value(), testConfig());
    if (!expect(result.hasValue(), output, "BPP empty-layout solve failed"))
    {
        return false;
    }
    auto problem = jaguar::BPProblem::create(instance.value());
    const bool stockPreserved = problem.hasValue()
        && problem.value().restore(result.value().solution).hasValue()
        && problem.value().remainingBinStock(0U) == 1U
        && problem.value().remainingBinStock(1U) == 0U;
    return expect(result.value().solution.layouts.size() == 1U
                      && result.value().solution.layouts.front().binIndex == 1U
                      && result.value().solution.layouts.front().layoutSnapshot
                             .placements.size() == 1U
                      && result.value().solution.cost(instance.value()) == 11U
                      && stockPreserved,
                  output,
                  "BPP must not retain an empty failed bin layout or consume its stock");
}

bool testPartialInfeasibleQualityAndRotation(std::ostream& output)
{
    const auto impossible = makeInstance(
        {makeItem(0, 701, 8.0, 8.0)},
        {1},
        {{0, 801, makeContainer(801, 6.0, 6.0), 1, 13}});
    if (!expect(impossible.hasValue(), output, "BPP impossible setup failed"))
    {
        return false;
    }
    auto impossibleConfig = testConfig();
    impossibleConfig.samplesPerItem = 16U;
    impossibleConfig.localSearchFraction = 0.0;
    const auto impossibleResult = sparrow::solveBppLbf(
        impossible.value(), impossibleConfig);
    if (!expect(impossibleResult.hasValue()
                    && impossibleResult.value().solution.layouts.empty()
                    && impossibleResult.value().metrics.placedItemCount == 0U
                    && impossibleResult.value().metrics.remainingDemand == 1U
                    && !impossibleResult.value().metrics.feasible
                    && impossibleResult.value().solution.cost(impossible.value()) == 0U,
                output,
                "BPP must preserve demand and stock when an item fits nowhere"))
    {
        return false;
    }

    const auto rotationInstance = makeInstance(
        {makeItem(0, 702, 4.0, 2.0, std::nullopt,
                  {jaguar::RotationMode::Discrete,
                   {0.5 * std::acos(-1.0)}})},
        {1},
        {{0, 802, makeContainer(802, 3.0, 5.0), 1, 17}});
    if (!expect(rotationInstance.hasValue(), output, "BPP rotation setup failed"))
    {
        return false;
    }
    const auto rotated = sparrow::solveBppLbf(rotationInstance.value(), testConfig());
    if (!expect(rotated.hasValue()
                    && rotated.value().metrics.placedItemCount == 1U,
                output,
                "BPP must honor the allowed discrete rotation while searching"))
    {
        return false;
    }
    const double expectedRotation =
        static_cast<double>(static_cast<float>(0.5 * std::acos(-1.0)));
    if (!expect(std::abs(rotated.value().solution.layouts.front()
                             .layoutSnapshot.placements.front()
                             .placedItem.transform().rotationRad
                         - expectedRotation)
                    < 1.0e-6,
                output,
                "BPP placement must use the allowed discrete rotation"))
    {
        return false;
    }

    const auto qualityShape = makeContainer(804, 3.0, 3.0).collisionShape();
    const auto lowQualityInstance = makeInstance(
        {makeItem(0, 703, 1.0, 1.0, 2U)},
        {1},
        {{0, 803,
          makeContainer(803, 3.0, 3.0, {{1, {qualityShape}}}),
          1,
          19}});
    if (!expect(lowQualityInstance.hasValue(),
                output,
                "BPP low-quality setup must be constructible"))
    {
        return false;
    }
    const auto lowQualityResult = sparrow::solveBppLbf(
        lowQualityInstance.value(), testConfig());
    if (!expect(lowQualityResult.hasValue()
                    && lowQualityResult.value().metrics.placedItemCount == 1U
                    && lowQualityResult.value().metrics.remainingDemand == 0U
                    && lowQualityResult.value().metrics.feasible,
                output,
                "BPP must ignore a quality zone below minQuality"))
    {
        return false;
    }

    const auto equalQualityInstance = makeInstance(
        {makeItem(0, 704, 1.0, 1.0, 2U)},
        {1},
        {{0, 805,
          makeContainer(805, 3.0, 3.0, {{2, {qualityShape}}}),
          1,
          23}});
    if (!expect(equalQualityInstance.hasValue(),
                output,
                "BPP equal-quality setup must be constructible"))
    {
        return false;
    }
    const auto equalQualityResult = sparrow::solveBppLbf(
        equalQualityInstance.value(), testConfig());
    if (!expect(equalQualityResult.hasValue()
                    && equalQualityResult.value().solution.layouts.empty()
                    && equalQualityResult.value().metrics.placedItemCount == 0U
                    && equalQualityResult.value().metrics.remainingDemand == 1U
                    && !equalQualityResult.value().metrics.feasible
                    && equalQualityResult.value().solution.cost(equalQualityInstance.value()) == 0U,
                output,
                "BPP must treat a quality zone equal to minQuality as a collision"))
    {
        return false;
    }
    const auto holeInstance = makeInstance(
        {makeItem(0, 705, 1.0, 1.0, 2U)},
        {1},
        {{0, 806,
          makeContainer(806, 3.0, 3.0, {{0, {qualityShape}}}),
          1,
          29}});
    if (!expect(holeInstance.hasValue(), output, "BPP hole setup must be constructible"))
    {
        return false;
    }
    const auto holeResult = sparrow::solveBppLbf(holeInstance.value(), testConfig());
    if (!expect(holeResult.hasValue()
                    && holeResult.value().solution.layouts.empty()
                    && holeResult.value().metrics.placedItemCount == 0U
                    && holeResult.value().metrics.remainingDemand == 1U
                    && !holeResult.value().metrics.feasible
                    && holeResult.value().solution.cost(holeInstance.value()) == 0U,
                output,
                "BPP must always treat a Hole as a collision"))
    {
        return false;
    }
    auto equalProblem = jaguar::BPProblem::create(equalQualityInstance.value());
    auto holeProblem = jaguar::BPProblem::create(holeInstance.value());
    return expect(equalProblem.hasValue() && holeProblem.hasValue()
                      && equalProblem.value().restore(equalQualityResult.value().solution)
                             .hasValue()
                      && holeProblem.value().restore(holeResult.value().solution).hasValue()
                      && equalProblem.value().remainingBinStock(0U) == 1U
                      && holeProblem.value().remainingBinStock(0U) == 1U,
                  output,
                  "quality and Hole rejection must not consume bin stock");
}

bool testBppJsonRoundTrip(std::ostream& output)
{
    const auto instance = makeInstance(
        {makeItem(0, 901, 2.0, 2.0)},
        {1},
        {{0, 1001, makeContainer(1001, 6.0, 6.0), 1, 10}});
    if (!expect(instance.hasValue(), output, "BPP JSON setup failed"))
    {
        return false;
    }
    const auto solved = sparrow::solveBppLbf(instance.value(), testConfig());
    if (!expect(solved.hasValue(), output, "BPP JSON solve failed"))
    {
        return false;
    }
    const auto external = jaguar::externalBPSolutionFromSolution(
        instance.value(), solved.value().solution);
    if (!expect(external.hasValue(), output, "BPP external solution conversion failed"))
    {
        return false;
    }
    const auto json = jaguar::serializeExternalBPSolutionJson(external.value());
    if (!expect(json.hasValue(), output, "BPP solution JSON serialization failed"))
    {
        return false;
    }
    const auto restored = jaguar::Importer{}.importBPSolutionJson(
        instance.value(), json.value());
    if (!expect(restored.hasValue(), output, "BPP solution JSON import failed"))
    {
        return false;
    }
    return expect(restored.value().layouts.size() == solved.value().solution.layouts.size()
                      && restored.value().cost(instance.value())
                             == solved.value().solution.cost(instance.value()),
                  output,
                  "BPP JSON round-trip must preserve non-empty layouts and cost");
}
}

bool runBppLbfTests(std::ostream& output)
{
    return testSmallBppFixture(output)
        && testOpenLayoutReuseAndInventory(output)
        && testClosedBinFallbackDoesNotEmitEmptyLayouts(output)
        && testPartialInfeasibleQualityAndRotation(output)
        && testBppJsonRoundTrip(output);
}
