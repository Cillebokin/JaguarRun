#include "SparrowCore/Optimizer/BppLbfOptimizer.h"

#include "JaguarCore/Assertions.h"
#include "JaguarCore/Entities/Item.h"
#include "JaguarCore/IO/Importer.h"
#include "JaguarCore/IO/JsonCodec.h"
#include "JaguarCore/Problems/Bpp.h"

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
bool Expect(bool condition, std::ostream& output, const char* message)
{
    if (condition)
    {
        return true;
    }
    output << "BPP LBF TEST FAILURE: " << message << '\n';
    return false;
}

jaguar::Item MakeItem(jaguar::ItemIndex index,
                      jaguar::ItemId externalId,
                      double width,
                      double height,
                      std::optional<std::size_t> minQuality = std::nullopt,
                      jaguar::RotationRange rotation = {})
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
        minQuality,
        std::move(rotation));
    return item.Value();
}

jaguar::Container MakeContainer(jaguar::ContainerId id,
                                double width,
                                double height,
                                std::vector<jaguar::QualityZone> zones = {})
{
    const auto polygon = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {width, 0.0}, {width, height}, {0.0, height}});
    jaguar::CDEConfig config{};
    config.quadtreeDepth = 4;
    config.cdThreshold = 8;
    const auto container = jaguar::Container::Create(
        id,
        {polygon.Value(),
         jaguar::Transform2d::Identity(),
         jaguar::ShapeModifyMode::Deflate,
         {}},
        std::move(zones),
        config);
    return container.Value();
}

jaguar::Result<jaguar::BPInstance> MakeInstance(
    std::vector<jaguar::Item> items,
    std::vector<std::size_t> demands,
    std::vector<jaguar::Bin> bins)
{
    return jaguar::BPInstance::Create(
        std::move(items), std::move(demands), std::move(bins));
}

bool ValidateSolution(const jaguar::BPInstance& instance,
                      const jaguar::BPSolution& solution) noexcept
{
    auto problem = jaguar::BPProblem::Create(instance);
    if (!problem.HasValue())
    {
        return false;
    }
    return problem.Value().Restore(solution).HasValue()
        && jaguar::ValidateBPSolution(problem.Value(), solution).HasValue();
}

sparrow::LbfConfig TestConfig()
{
    sparrow::LbfConfig config;
    config.seed = 42U;
    config.samplesPerItem = 64U;
    config.localSearchFraction = 0.25;
    return config;
}

bool TestSmallBppFixture(std::ostream& output)
{
    const auto instance = MakeInstance(
        {MakeItem(0, 301, 2.0, 2.0)},
        {1},
        {{0, 401, MakeContainer(401, 6.0, 6.0), 1, 10}});
    if (!Expect(instance.HasValue(), output, "small BPP instance creation failed"))
    {
        return false;
    }

    const auto result = sparrow::SolveBppLbf(instance.Value(), TestConfig());
    if (!Expect(result.HasValue(), output, "small BPP LBF solve failed"))
    {
        return false;
    }
    std::ifstream metricsFile;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/BppLbfCppReference.tsv",
             "../SparrowCore/Tests/Fixtures/BppLbfCppReference.tsv",
             "../JaguarRun/SparrowCore/Tests/Fixtures/BppLbfCppReference.tsv",
             "..\\SparrowCore\\Tests\\Fixtures\\BppLbfCppReference.tsv"})
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
    if (!Expect(referenceFound,
                output,
                "C++ BPP metrics reference fixture must be readable"))
    {
        return false;
    }
    const bool expectedSmallSolution = result.Value().solution.layouts.size() == 1U
        && result.Value().solution.layouts.front().layoutSnapshot.placements.size() == 1U
        && result.Value().solution.Cost(instance.Value()) == 10U
        && result.Value().metrics.seed == referenceSeed
        && result.Value().metrics.sampleCount == referenceSamples
        && result.Value().metrics.placedItemCount == 1U
        && result.Value().metrics.placedItemCount == referencePlaced
        && result.Value().metrics.remainingDemand == 0U
        && result.Value().metrics.remainingDemand == referenceRemaining
        && result.Value().metrics.stripWidth == referenceWidth
        && std::abs(result.Value().metrics.density - referenceDensity) < 1.0e-9
        && result.Value().solution.Cost(instance.Value()) == referenceCost
        && result.Value().metrics.feasible == referenceFeasible
        && result.Value().metrics.feasible
        && std::isfinite(result.Value().metrics.density)
        && result.Value().metrics.density > 0.0;
    if (!expectedSmallSolution)
    {
        output << "BPP actual: layouts=" << result.Value().solution.layouts.size()
               << " placements="
               << (result.Value().solution.layouts.empty()
                       ? 0U
                       : result.Value().solution.layouts.front().layoutSnapshot
                             .placements.size())
               << " cost=" << result.Value().solution.Cost(instance.Value())
               << " samples=" << result.Value().metrics.sampleCount
               << " placed=" << result.Value().metrics.placedItemCount
               << " remaining=" << result.Value().metrics.remainingDemand
               << " width=" << result.Value().metrics.stripWidth
               << " density=" << result.Value().metrics.density
               << " feasible=" << result.Value().metrics.feasible << '\n'
               << "BPP expected: seed=" << referenceSeed
               << " samples=" << referenceSamples
               << " placed=" << referencePlaced
               << " remaining=" << referenceRemaining
               << " width=" << referenceWidth
               << " density=" << referenceDensity
               << " cost=" << referenceCost
               << " feasible=" << referenceFeasible << '\n';
    }
    if (!Expect(expectedSmallSolution,
                output,
                "small BPP LBF must place one item in one non-empty bin"))
    {
        return false;
    }
    return Expect(ValidateSolution(instance.Value(), result.Value().solution),
                  output,
                  "small BPP solution validator must accept the result");
}

bool TestOpenLayoutReuseAndInventory(std::ostream& output)
{
    const auto reuseInstance = MakeInstance(
        {MakeItem(0, 501, 2.0, 2.0)},
        {2},
        {{0, 601, MakeContainer(601, 6.0, 6.0), 1, 10}});
    if (!Expect(reuseInstance.HasValue(), output, "BPP open-layout setup failed"))
    {
        return false;
    }
    auto reused = sparrow::SolveBppLbf(reuseInstance.Value(), TestConfig());
    if (!Expect(reused.HasValue(), output, "BPP open-layout solve failed"))
    {
        return false;
    }
    if (!Expect(reused.Value().solution.layouts.size() == 1U
                    && reused.Value().solution.layouts.front().layoutSnapshot
                           .placements.size() == 2U
                    && reused.Value().solution.Cost(reuseInstance.Value()) == 10U
                    && reused.Value().metrics.remainingDemand == 0U,
                output,
                "BPP must reuse the current open layout before opening another bin"))
    {
        return false;
    }

    const auto stockInstance = MakeInstance(
        {MakeItem(0, 502, 4.0, 2.0)},
        {2},
        {{0, 602, MakeContainer(602, 5.0, 4.0), 1, 7},
         {1, 603, MakeContainer(603, 6.0, 4.0), 1, 11}});
    if (!Expect(stockInstance.HasValue(), output, "BPP stock setup failed"))
    {
        return false;
    }
    const auto stockResult = sparrow::SolveBppLbf(stockInstance.Value(), TestConfig());
    if (!Expect(stockResult.HasValue(), output, "BPP stock solve failed"))
    {
        return false;
    }
    if (!Expect(stockResult.Value().solution.layouts.size() == 2U
                    && stockResult.Value().solution.Cost(stockInstance.Value()) == 18U
                    && stockResult.Value().metrics.placedItemCount == 2U
                    && stockResult.Value().metrics.remainingDemand == 0U,
                output,
                "BPP must consume stock and cost for two non-empty bins"))
    {
        return false;
    }

    const auto multipleTypes = MakeInstance(
        {MakeItem(0, 503, 2.0, 2.0), MakeItem(1, 504, 1.0, 1.0)},
        {1, 1},
        {{0, 604, MakeContainer(604, 6.0, 6.0), 1, 5}});
    if (!Expect(multipleTypes.HasValue(), output, "BPP multiple-type setup failed"))
    {
        return false;
    }
    const auto multipleTypeResult = sparrow::SolveBppLbf(
        multipleTypes.Value(), TestConfig());
    if (!Expect(multipleTypeResult.HasValue(),
                output,
                "BPP multiple-type solve failed"))
    {
        return false;
    }
    return Expect(multipleTypeResult.Value().solution.layouts.size() == 1U
                      && multipleTypeResult.Value().solution.layouts.front()
                             .layoutSnapshot.placements.size() == 2U
                      && multipleTypeResult.Value().solution.Cost(multipleTypes.Value()) == 5U
                      && multipleTypeResult.Value().metrics.placedItemCount == 2U
                      && multipleTypeResult.Value().metrics.remainingDemand == 0U,
                  output,
                  "BPP must place multiple item types in one open layout");
}

bool TestClosedBinFallbackDoesNotEmitEmptyLayouts(std::ostream& output)
{
    const auto instance = MakeInstance(
        {MakeItem(0, 505, 4.0, 2.0)},
        {1},
        {{0, 605, MakeContainer(605, 3.0, 3.0), 1, 7},
         {1, 606, MakeContainer(606, 6.0, 4.0), 1, 11}});
    if (!Expect(instance.HasValue(), output, "BPP empty-layout setup failed"))
    {
        return false;
    }
    const auto result = sparrow::SolveBppLbf(instance.Value(), TestConfig());
    if (!Expect(result.HasValue(), output, "BPP empty-layout solve failed"))
    {
        return false;
    }
    auto problem = jaguar::BPProblem::Create(instance.Value());
    const bool stockPreserved = problem.HasValue()
        && problem.Value().Restore(result.Value().solution).HasValue()
        && problem.Value().RemainingBinStock(0U) == 1U
        && problem.Value().RemainingBinStock(1U) == 0U;
    return Expect(result.Value().solution.layouts.size() == 1U
                      && result.Value().solution.layouts.front().binIndex == 1U
                      && result.Value().solution.layouts.front().layoutSnapshot
                             .placements.size() == 1U
                      && result.Value().solution.Cost(instance.Value()) == 11U
                      && stockPreserved,
                  output,
                  "BPP must not retain an empty failed bin layout or consume its stock");
}

bool TestPartialInfeasibleQualityAndRotation(std::ostream& output)
{
    const auto impossible = MakeInstance(
        {MakeItem(0, 701, 5.0, 5.0)},
        {1},
        {{0, 801, MakeContainer(801, 4.0, 4.0), 1, 5}});
    if (!Expect(impossible.HasValue(), output, "BPP impossible setup failed"))
    {
        return false;
    }
    auto impossibleConfig = TestConfig();
    impossibleConfig.samplesPerItem = 16U;
    impossibleConfig.localSearchFraction = 0.0;
    const auto impossibleResult = sparrow::SolveBppLbf(
        impossible.Value(), impossibleConfig);
    if (!Expect(impossibleResult.HasValue()
                    && impossibleResult.Value().solution.layouts.empty()
                    && impossibleResult.Value().metrics.placedItemCount == 0U
                    && impossibleResult.Value().metrics.remainingDemand == 1U
                    && !impossibleResult.Value().metrics.feasible
                    && std::isnan(impossibleResult.Value().metrics.density)
                    && impossibleResult.Value().solution.Cost(impossible.Value()) == 0U,
                output,
                "BPP must preserve demand and stock when an item fits nowhere"))
    {
        return false;
    }

    const auto rotationInstance = MakeInstance(
        {MakeItem(0, 702, 4.0, 2.0, std::nullopt,
                  {jaguar::RotationMode::Discrete,
                   {0.5 * std::acos(-1.0)}})},
        {1},
        {{0, 802, MakeContainer(802, 3.0, 5.0), 1, 17}});
    if (!Expect(rotationInstance.HasValue(), output, "BPP rotation setup failed"))
    {
        return false;
    }
    const auto rotated = sparrow::SolveBppLbf(rotationInstance.Value(), TestConfig());
    if (!Expect(rotated.HasValue()
                    && rotated.Value().metrics.placedItemCount == 1U,
                output,
                "BPP must honor the allowed discrete rotation while searching"))
    {
        return false;
    }
    const double expectedRotation =
        static_cast<double>(static_cast<float>(0.5 * std::acos(-1.0)));
    if (!Expect(std::abs(rotated.Value().solution.layouts.front()
                             .layoutSnapshot.placements.front()
                             .placedItem.Transform().rotationRad
                         - expectedRotation)
                    < 1.0e-6,
                output,
                "BPP placement must use the allowed discrete rotation"))
    {
        return false;
    }

    const auto qualityShape = MakeContainer(804, 3.0, 3.0).CollisionShape();
    const auto lowQualityInstance = MakeInstance(
        {MakeItem(0, 703, 1.0, 1.0, 2U)},
        {1},
        {{0, 803,
          MakeContainer(803, 3.0, 3.0, {{1, {qualityShape}}}),
          1,
          19}});
    if (!Expect(lowQualityInstance.HasValue(),
                output,
                "BPP low-quality setup must be constructible"))
    {
        return false;
    }
    const auto lowQualityResult = sparrow::SolveBppLbf(
        lowQualityInstance.Value(), TestConfig());
    if (!Expect(lowQualityResult.HasValue()
                    && lowQualityResult.Value().metrics.placedItemCount == 1U
                    && lowQualityResult.Value().metrics.remainingDemand == 0U
                    && lowQualityResult.Value().metrics.feasible,
                output,
                "BPP must ignore a quality zone below minQuality"))
    {
        return false;
    }

    const auto equalQualityInstance = MakeInstance(
        {MakeItem(0, 704, 1.0, 1.0, 2U)},
        {1},
        {{0, 805,
          MakeContainer(805, 3.0, 3.0, {{2, {qualityShape}}}),
          1,
          23}});
    if (!Expect(equalQualityInstance.HasValue(),
                output,
                "BPP equal-quality setup must be constructible"))
    {
        return false;
    }
    const auto equalQualityResult = sparrow::SolveBppLbf(
        equalQualityInstance.Value(), TestConfig());
    if (!Expect(equalQualityResult.HasValue()
                    && equalQualityResult.Value().solution.layouts.empty()
                    && equalQualityResult.Value().metrics.placedItemCount == 0U
                    && equalQualityResult.Value().metrics.remainingDemand == 1U
                    && !equalQualityResult.Value().metrics.feasible
                    && equalQualityResult.Value().solution.Cost(equalQualityInstance.Value()) == 0U,
                output,
                "BPP must treat a quality zone equal to minQuality as a collision"))
    {
        return false;
    }
    const auto holeInstance = MakeInstance(
        {MakeItem(0, 705, 1.0, 1.0, 2U)},
        {1},
        {{0, 806,
          MakeContainer(806, 3.0, 3.0, {{0, {qualityShape}}}),
          1,
          29}});
    if (!Expect(holeInstance.HasValue(), output, "BPP hole setup must be constructible"))
    {
        return false;
    }
    const auto holeResult = sparrow::SolveBppLbf(holeInstance.Value(), TestConfig());
    if (!Expect(holeResult.HasValue()
                    && holeResult.Value().solution.layouts.empty()
                    && holeResult.Value().metrics.placedItemCount == 0U
                    && holeResult.Value().metrics.remainingDemand == 1U
                    && !holeResult.Value().metrics.feasible
                    && holeResult.Value().solution.Cost(holeInstance.Value()) == 0U,
                output,
                "BPP must always treat a Hole as a collision"))
    {
        return false;
    }
    auto equalProblem = jaguar::BPProblem::Create(equalQualityInstance.Value());
    auto holeProblem = jaguar::BPProblem::Create(holeInstance.Value());
    return Expect(equalProblem.HasValue() && holeProblem.HasValue()
                      && equalProblem.Value().Restore(equalQualityResult.Value().solution)
                             .HasValue()
                      && holeProblem.Value().Restore(holeResult.Value().solution).HasValue()
                      && equalProblem.Value().RemainingBinStock(0U) == 1U
                      && holeProblem.Value().RemainingBinStock(0U) == 1U,
                  output,
                  "quality and Hole rejection must not consume bin stock");
}

bool TestBppJsonRoundTrip(std::ostream& output)
{
    const auto instance = MakeInstance(
        {MakeItem(0, 901, 2.0, 2.0)},
        {1},
        {{0, 1001, MakeContainer(1001, 6.0, 6.0), 1, 10}});
    if (!Expect(instance.HasValue(), output, "BPP JSON setup failed"))
    {
        return false;
    }
    const auto solved = sparrow::SolveBppLbf(instance.Value(), TestConfig());
    if (!Expect(solved.HasValue(), output, "BPP JSON solve failed"))
    {
        return false;
    }
    const auto external = jaguar::ExternalBPSolutionFromSolution(
        instance.Value(), solved.Value().solution);
    if (!Expect(external.HasValue(), output, "BPP external solution conversion failed"))
    {
        return false;
    }
    const auto json = jaguar::SerializeExternalBPSolutionJson(external.Value());
    if (!Expect(json.HasValue(), output, "BPP solution JSON serialization failed"))
    {
        return false;
    }
    // jagua-rs 参考库的 BPP solution import 是 unimplemented!()（bpp import.rs:66）；
    // JaguarCore 为 Rust parity 返回 UnsupportedOperation。round-trip 只验证
    // 序列化产物可被解析，导入通道按参考行为拒绝。
    const auto reparsed = jaguar::ParseExternalBPSolutionJson(json.Value());
    if (!Expect(reparsed.HasValue(), output, "BPP solution JSON must reparse"))
    {
        return false;
    }
    const auto restored = jaguar::Importer{}.ImportBPSolutionJson(
        instance.Value(), json.Value());
    return Expect(!restored.HasValue()
                      && restored.GetError().code
                             == jaguar::ErrorCode::UnsupportedOperation
                      && reparsed.Value().layouts.size()
                             == solved.Value().solution.layouts.size()
                      && reparsed.Value().cost
                             == solved.Value().solution.Cost(instance.Value()),
                  output,
                  "BPP JSON import must follow the reference stub and preserve "
                  "layouts and cost through reparse");
}
}

bool RunBppLbfTests(std::ostream& output)
{
    return TestSmallBppFixture(output)
        && TestOpenLayoutReuseAndInventory(output)
        && TestClosedBinFallbackDoesNotEmitEmptyLayouts(output)
        && TestPartialInfeasibleQualityAndRotation(output)
        && TestBppJsonRoundTrip(output);
}
