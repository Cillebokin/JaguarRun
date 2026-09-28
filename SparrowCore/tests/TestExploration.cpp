#include "SparrowCore/Optimizer/Compression.h"
#include "SparrowCore/Optimizer/Exploration.h"
#include "SparrowCore/Optimizer/Separator.h"
#include "SparrowCore/Optimizer/SparrowLbfBuilder.h"
#include "SparrowCore/Optimizer/SparrowOptimizer.h"

#include "JaguarCore/Entities/Item.h"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <array>
#include <cstdint>
#include <fstream>
#include <iomanip>
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
    output << "EXPLORATION TEST FAILURE: " << message << '\n';
    return false;
}

bool SameSolution(const jaguar::SPSolution& left,
                  const jaguar::SPSolution& right) noexcept
{
    if (left.StripWidth() != right.StripWidth()
        || left.layoutSnapshot.placements.size()
               != right.layoutSnapshot.placements.size())
    {
        return false;
    }
    for (std::size_t index = 0U;
         index < left.layoutSnapshot.placements.size();
         ++index)
    {
        const auto& first = left.layoutSnapshot.placements[index];
        const auto& second = right.layoutSnapshot.placements[index];
        if (!(first.handle == second.handle)
            || first.placedItem.GetItemIndex()
                   != second.placedItem.GetItemIndex()
            || first.placedItem.Transform().rotationRad
                   != second.placedItem.Transform().rotationRad
            || first.placedItem.Transform().translation
                   != second.placedItem.Transform().translation)
        {
            return false;
        }
    }
    return true;
}

class RecordingListener final : public sparrow::OptimizationListener
{
public:
    void OnProgress(const sparrow::OptimizationProgress&) override
    {
        ++progressCount;
    }

    void OnImprovement(const sparrow::OptimizationProgress&) override
    {
        ++improvementCount;
    }

    void OnSolution(const sparrow::OptimizationSolutionReport& report) override
    {
        ++solutionCount;
        lastReportType = report.type;
    }

    void OnSeparationProgress(
        const sparrow::SeparationProgress& progress) override
    {
        separationWidths.push_back(progress.stripWidth);
    }

    std::size_t progressCount = 0U;
    std::size_t improvementCount = 0U;
    std::size_t solutionCount = 0U;
    sparrow::OptimizationReportType lastReportType =
        sparrow::OptimizationReportType::OPTIMIZATION_REPORT_FINAL;
    std::vector<double> separationWidths;
};

jaguar::Item MakeSquareItem(jaguar::ItemIndex index, double sideLength)
{
    const auto polygon = jaguar::SimplePolygon::Create({
        {0.0, 0.0},
        {sideLength, 0.0},
        {sideLength, sideLength},
        {0.0, sideLength}});
    jaguar::SurrogateConfig surrogateConfig{};
    surrogateConfig.poleLimits = {{{3U, 0.0}, {0U, 0.0}, {0U, 0.0}}};
    return jaguar::Item::Create(
               index,
               static_cast<jaguar::ItemId>(3000U + index),
               {polygon.Value(),
                jaguar::Transform2d::Identity(),
                jaguar::ShapeModifyMode::Inflate,
                {}},
               surrogateConfig)
        .Value();
}

jaguar::Item MakeCenteredRectangleItem(jaguar::ItemIndex index,
                                       double width,
                                       double height,
                                       const jaguar::SurrogateConfig& surrogateConfig,
                                       const jaguar::ShapeModifyConfig& shapeModifyConfig)
{
    const auto polygon = jaguar::SimplePolygon::Create({
        {-width * 0.5, -height * 0.5},
        {width * 0.5, -height * 0.5},
        {width * 0.5, height * 0.5},
        {-width * 0.5, height * 0.5}});
    return jaguar::Item::Create(
               index,
               static_cast<jaguar::ItemId>(3000U + index),
               {polygon.Value(),
                jaguar::Transform2d::Identity(),
                jaguar::ShapeModifyMode::Inflate,
                shapeModifyConfig},
               surrogateConfig)
        .Value();
}

jaguar::Item MakeItem(jaguar::ItemIndex index)
{
    return MakeSquareItem(index, 1.0);
}

const char* ReportTypeName(sparrow::OptimizationReportType type) noexcept
{
    switch (type)
    {
    case sparrow::OptimizationReportType::OPTIMIZATION_REPORT_EXPLORATION_FEASIBLE:
        return "expl_feasible";
    case sparrow::OptimizationReportType::OPTIMIZATION_REPORT_EXPLORATION_INFEASIBLE:
        return "expl_infeasible";
    case sparrow::OptimizationReportType::OPTIMIZATION_REPORT_EXPLORATION_IMPROVING:
        return "expl_improving";
    case sparrow::OptimizationReportType::OPTIMIZATION_REPORT_COMPRESSION_FEASIBLE:
        return "compression_feasible";
    case sparrow::OptimizationReportType::OPTIMIZATION_REPORT_FINAL:
        return "final";
    }
    return "unknown";
}
}

bool RunExplorationTests(std::ostream& output)
{
    const sparrow::ExplorationConfig defaultExplorationConfig;
    if (!Expect(std::abs(defaultExplorationConfig.solutionPoolDistributionStddev
                             - 0.25)
                        < 1.0e-12
                    && std::abs(defaultExplorationConfig
                                    .largeItemChAreaCutoffPercentile
                                - 0.75)
                           < 1.0e-12
                    && defaultExplorationConfig.maxIterations == 0U
                    && defaultExplorationConfig.maxConsecutiveFailures == 0U
                    && defaultExplorationConfig.infeasiblePoolSize == 0U
                    && std::abs(defaultExplorationConfig.shrinkStep - 0.001)
                           < 1.0e-12
                    && defaultExplorationConfig.separator.noImprovementLimit
                           == 200U
                    && defaultExplorationConfig.separator.nContainerSamples
                           == 50U
                    && defaultExplorationConfig.separator.nFocussedSamples
                           == 25U,
                output,
                "exploration defaults must match Rust disruption settings"))
    {
        return false;
    }
    sparrow::ExplorationConfig invalidExplorationConfig =
        defaultExplorationConfig;
    invalidExplorationConfig.solutionPoolDistributionStddev = 0.0;
    if (!Expect(!invalidExplorationConfig.Validate().HasValue(),
                output,
                "exploration must reject a non-positive pool deviation"))
    {
        return false;
    }
    const sparrow::CompressionConfig defaultCompressionConfig;
    if (!Expect(defaultCompressionConfig.timeLimit
                        == std::chrono::milliseconds{60000},
                output,
                "compression must expose the Rust phase time limit"))
    {
        return false;
    }
    if (!Expect(defaultCompressionConfig.maxIterations == 0U
                    && defaultCompressionConfig.maxConsecutiveFailures == 0U
                    && std::abs(defaultCompressionConfig.shrinkMax - 0.0005)
                           < 1.0e-12
                    && std::abs(defaultCompressionConfig.shrinkMin - 0.00001)
                           < 1.0e-12
                    && std::abs(defaultCompressionConfig.failureDecay - 0.9)
                           < 1.0e-12
                    && defaultCompressionConfig.separator.noImprovementLimit
                           == 100U
                    && defaultCompressionConfig.separator.strikeLimit == 5U
                    && defaultCompressionConfig.separator.nContainerSamples
                           == 50U
                    && defaultCompressionConfig.separator.nFocussedSamples
                           == 25U,
                output,
                "compression defaults must match Rust phase settings"))
    {
        return false;
    }
    if (!Expect(defaultExplorationConfig.Validate().HasValue()
                    && defaultCompressionConfig.Validate().HasValue(),
                output,
                "Rust default zero limits must mean unlimited rather than invalid"))
    {
        return false;
    }
    const sparrow::OptimizationTerminatorConfig defaultTerminatorConfig;
    if (!Expect(defaultTerminatorConfig.maxIterations == 0U,
                output,
                "terminator iteration limit must be unlimited by default"))
    {
        return false;
    }

    const auto largePolygon = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {3.0, 0.0}, {3.0, 3.0}, {0.0, 3.0}});
    jaguar::SurrogateConfig largeSurrogateConfig{};
    largeSurrogateConfig.poleLimits = {{{3U, 0.0}, {0U, 0.0}, {0U, 0.0}}};
    const auto largeItem = jaguar::Item::Create(
        0U,
        3999U,
        {largePolygon.Value(),
         jaguar::Transform2d::Identity(),
         jaguar::ShapeModifyMode::Inflate,
         {}},
        largeSurrogateConfig);
    const auto impossibleInstance = jaguar::SPInstance::Create(
        {largeItem.Value()},
        {2U},
        3.0,
        jaguar::CDEConfig{},
        jaguar::ShapeModifyConfig{});
    if (!Expect(impossibleInstance.HasValue(),
                output,
                "failure-pool regression instance creation failed"))
    {
        return false;
    }
    auto impossibleProblemResult = jaguar::SPProblem::Create(
        impossibleInstance.Value());
    if (!Expect(impossibleProblemResult.HasValue()
                    && impossibleProblemResult.Value().ChangeStripWidth(3.0).HasValue()
                    && impossibleProblemResult.Value().PlaceItem(
                           {0U, {0.0, {0.0, 0.0}}}).HasValue()
                    && impossibleProblemResult.Value().PlaceItem(
                           {0U, {0.0, {0.0, 0.0}}}).HasValue(),
                output,
                "failure-pool regression setup failed"))
    {
        return false;
    }
    const auto impossibleSolution = impossibleProblemResult.Value().Save();
    sparrow::ExplorationConfig failurePoolConfig;
    failurePoolConfig.maxIterations = 5U;
    failurePoolConfig.maxConsecutiveFailures = 2U;
    failurePoolConfig.infeasiblePoolSize = 1U;
    failurePoolConfig.separator.strikeLimit = 1U;
    failurePoolConfig.separator.noImprovementLimit = 1U;
    failurePoolConfig.separator.nContainerSamples = 1U;
    failurePoolConfig.separator.nFocussedSamples = 0U;
    failurePoolConfig.separator.nCoordinateDescents = 1U;
    failurePoolConfig.separator.nWorkers = 1U;
    sparrow::OptimizationTerminator failurePoolTerminator({
        32U,
        std::chrono::milliseconds{2000}});
    RecordingListener failurePoolListener;
    const auto failurePoolResult = sparrow::Explore(
        impossibleInstance.Value(),
        impossibleSolution,
        failurePoolConfig,
        17U,
        failurePoolTerminator,
        &failurePoolListener);
    if (!Expect(failurePoolResult.HasValue()
                    && failurePoolListener.progressCount == 3U,
                output,
                "failure termination must count attempts independently of pool capacity"))
    {
        return false;
    }

    sparrow::CompressionConfig invalidCompressionConfig =
        defaultCompressionConfig;
    invalidCompressionConfig.timeLimit = std::chrono::milliseconds{-1};
    if (!Expect(!invalidCompressionConfig.Validate().HasValue(),
                output,
                "compression must reject a negative phase time limit"))
    {
        return false;
    }

    const auto instance = jaguar::SPInstance::Create(
        {MakeItem(0U)},
        {2U},
        4.0,
        jaguar::CDEConfig{},
        jaguar::ShapeModifyConfig{});
    if (!Expect(instance.HasValue(), output, "exploration instance failed"))
    {
        return false;
    }
    auto shiftProblemResult = jaguar::SPProblem::Create(instance.Value());
    if (!Expect(shiftProblemResult.HasValue(),
                output,
                "exploration shift problem creation failed"))
    {
        return false;
    }
    jaguar::SPProblem shiftProblem = std::move(shiftProblemResult.Value());
    if (!Expect(shiftProblem.ChangeStripWidth(6.0).HasValue()
                    && shiftProblem.PlaceItem({0U, {0.0, {0.5, 0.5}}}).HasValue()
                    && shiftProblem.PlaceItem({0U, {0.0, {3.5, 0.5}}}).HasValue(),
                output,
                "exploration shift setup failed"))
    {
        return false;
    }
    auto shiftSeparatorResult = sparrow::Separator::Create(
        std::move(shiftProblem), 42U);
    if (!Expect(shiftSeparatorResult.HasValue(),
                output,
                "exploration shift separator creation failed"))
    {
        return false;
    }
    sparrow::Separator shiftSeparator = std::move(shiftSeparatorResult.Value());
    if (!Expect(shiftSeparator.ChangeStripWidth(4.0, 1.0).HasValue(),
                output,
                "separator split-position resize failed"))
    {
        return false;
    }
    const auto shiftedSolution = shiftSeparator.SaveSolution();
    if (!Expect(shiftedSolution.layoutSnapshot.placements.size() == 2U
                    && shiftedSolution.layoutSnapshot.placements[1]
                               .placedItem.Transform()
                               .translation.x
                           == 1.5,
                output,
                "separator resize must shift items right of split"))
    {
        return false;
    }
    sparrow::OptimizationTerminator stoppedPhaseTerminator(
        sparrow::OptimizationTerminatorConfig{
            0U,
            std::chrono::milliseconds{0}});
    stoppedPhaseTerminator.Start();
    stoppedPhaseTerminator.RequestStop();
    stoppedPhaseTerminator.StartPhase(std::chrono::milliseconds{1000});
    if (!Expect(stoppedPhaseTerminator.ShouldTerminate(0U),
                output,
                "phase reset must preserve an external stop request"))
    {
        return false;
    }
    sparrow::SparrowConstructorConfig constructorConfig;
    constructorConfig.seed = 42U;
    constructorConfig.nContainerSamples = 64U;
    const auto constructed = sparrow::ConstructSparrowInitialSolution(
        instance.Value(), constructorConfig);
    if (!Expect(constructed.HasValue(), output, "exploration initial solution failed"))
    {
        return false;
    }

    sparrow::ExplorationConfig explorationConfig;
    explorationConfig.maxIterations = 2U;
    explorationConfig.maxConsecutiveFailures = 2U;
    explorationConfig.shrinkStep = 0.01;
    explorationConfig.infeasiblePoolSize = 2U;
    explorationConfig.disruptionCount = 1U;
    explorationConfig.containedRelocationCount = 1U;
    explorationConfig.separator.nContainerSamples = 32U;
    explorationConfig.separator.nFocussedSamples = 16U;
    RecordingListener listener;
    sparrow::OptimizationTerminator explorationTerminator({
        16U,
        std::chrono::milliseconds{2000}});
    const auto explored = sparrow::Explore(
        instance.Value(),
        constructed.Value().solution,
        explorationConfig,
        42U,
        explorationTerminator,
        &listener);
    if (!Expect(explored.HasValue() && explored.Value().metrics.feasible,
                output,
                "exploration must preserve a feasible best solution"))
    {
        return false;
    }

    sparrow::CompressionConfig compressionConfig;
    compressionConfig.maxIterations = 2U;
    compressionConfig.maxConsecutiveFailures = 2U;
    compressionConfig.shrinkMin = 0.001;
    compressionConfig.shrinkMax = 0.01;
    compressionConfig.separator.nContainerSamples = 32U;
    compressionConfig.separator.nFocussedSamples = 16U;
    sparrow::OptimizationTerminator compressionTerminator({
        16U,
        std::chrono::milliseconds{2000}});
    const auto compressed = sparrow::Compress(
        instance.Value(),
        explored.Value().solution,
        compressionConfig,
        42U,
        compressionTerminator,
        &listener);
    if (!Expect(compressed.HasValue() && compressed.Value().metrics.feasible
                    && listener.progressCount > 0U,
                  output,
                  "compression and listener must complete with valid metrics"))
    {
        return false;
    }

    sparrow::SparrowConfig optimizerConfig;
    optimizerConfig.seed = 42U;
    optimizerConfig.constructor.nContainerSamples = 32U;
    optimizerConfig.exploration.maxIterations = 1U;
    optimizerConfig.exploration.maxConsecutiveFailures = 1U;
    optimizerConfig.exploration.separator.nContainerSamples = 16U;
    optimizerConfig.exploration.separator.nFocussedSamples = 8U;
    optimizerConfig.compression.maxIterations = 1U;
    optimizerConfig.compression.maxConsecutiveFailures = 1U;
    optimizerConfig.compression.separator.nContainerSamples = 16U;
    optimizerConfig.compression.separator.nFocussedSamples = 8U;
    optimizerConfig.terminator.maxIterations = 32U;
    optimizerConfig.terminator.maxDuration = std::chrono::milliseconds{2000};
    const auto optimized = sparrow::Optimize(
        instance.Value(), optimizerConfig, &listener);
    if (!Expect(optimized.HasValue() && optimized.Value().metrics.feasible,
                output,
                "top-level Sparrow optimizer must return a feasible solution"))
    {
        return false;
    }
    if (!Expect(!optimized.Value().history.empty()
                    && optimized.Value().history.back().type
                           == sparrow::OptimizationReportType::OPTIMIZATION_REPORT_FINAL
                    && listener.solutionCount > 0U,
                output,
                "optimizer must expose Rust-compatible solution history"))
    {
        return false;
    }
    const std::array<const char*, 4> phaseReferencePaths = {
        "SparrowCore/Tests/Fixtures/PhaseReference.tsv",
        "../SparrowCore/Tests/Fixtures/PhaseReference.tsv",
        "../JaguarRun/SparrowCore/Tests/Fixtures/PhaseReference.tsv",
        "..\\SparrowCore\\Tests\\Fixtures\\PhaseReference.tsv",
    };
    std::ifstream phaseReferenceInput;
    for (const char* path : phaseReferencePaths)
    {
        phaseReferenceInput.open(path);
        if (phaseReferenceInput.is_open())
        {
            break;
        }
        phaseReferenceInput.clear();
    }
    if (!Expect(phaseReferenceInput.is_open(),
                output,
                "phase reference fixture cannot be opened"))
    {
        return false;
    }
    const std::array<const char*, 3> phaseNames = {
        "exploration", "compression", "optimize"};
    std::array<bool, 3> phasePresent{{false, false, false}};
    std::array<double, 3> referenceWidths{};
    std::array<double, 3> referenceDensities{};
    std::string phaseLine;
    while (std::getline(phaseReferenceInput, phaseLine))
    {
        if (phaseLine.empty() || phaseLine.front() == '#')
        {
            continue;
        }
        std::istringstream row(phaseLine);
        std::uint64_t referenceSeed = 0U;
        std::string phase;
        double width = 0.0;
        double density = 0.0;
        if (!(row >> referenceSeed >> phase >> width >> density)
            || referenceSeed != 42U)
        {
            return Expect(false, output, "malformed phase reference row");
        }
        for (std::size_t index = 0U; index < phaseNames.size(); ++index)
        {
            if (phase == phaseNames[index])
            {
                phasePresent[index] = true;
                referenceWidths[index] = width;
                referenceDensities[index] = density;
            }
        }
    }
    if (!Expect(phasePresent[0] && phasePresent[1] && phasePresent[2],
                output,
                "phase reference fixture is incomplete"))
    {
        return false;
    }
    const std::array<double, 3> actualWidths = {
        explored.Value().solution.StripWidth(),
        compressed.Value().solution.StripWidth(),
        optimized.Value().solution.StripWidth()};
    const std::array<double, 3> actualDensities = {
        explored.Value().metrics.density,
        compressed.Value().metrics.density,
        optimized.Value().metrics.density};
    for (std::size_t index = 0U; index < actualWidths.size(); ++index)
    {
        const bool matchesReference =
            std::abs(actualWidths[index] - referenceWidths[index]) < 1.0e-12
                        && std::abs(actualDensities[index]
                                   - referenceDensities[index])
                               < 1.0e-12;
        if (!matchesReference)
        {
            output << std::setprecision(17) << "Phase " << phaseNames[index]
                   << " actual width=" << actualWidths[index]
                   << " density=" << actualDensities[index]
                   << "; expected width=" << referenceWidths[index]
                   << " density=" << referenceDensities[index] << '\n';
        }
        if (!Expect(matchesReference,
                    output,
                    "phase fixed-seed reference mismatch"))
        {
            return false;
        }
    }

    const std::array<const char*, 4> explorationReferencePaths = {
        "SparrowCore/Tests/Fixtures/ExplorationRustTrace.tsv",
        "../SparrowCore/Tests/Fixtures/ExplorationRustTrace.tsv",
        "../JaguarRun/SparrowCore/Tests/Fixtures/ExplorationRustTrace.tsv",
        "..\\SparrowCore\\Tests\\Fixtures\\ExplorationRustTrace.tsv",
    };
    std::ifstream explorationReferenceInput;
    for (const char* path : explorationReferencePaths)
    {
        explorationReferenceInput.open(path);
        if (explorationReferenceInput.is_open())
        {
            break;
        }
        explorationReferenceInput.clear();
    }
    if (!Expect(explorationReferenceInput.is_open(),
                output,
                "Rust exploration trace fixture cannot be opened"))
    {
        return false;
    }
    std::size_t explorationCases = 0U;
    while (std::getline(explorationReferenceInput, phaseLine))
    {
        if (phaseLine.empty() || phaseLine.front() == '#')
        {
            continue;
        }
        std::istringstream row(phaseLine);
        std::string record;
        std::string caseName;
        std::size_t maxIterations = 0U;
        double initialWidth = 0.0;
        double shrinkStep = 0.0;
        double rustX = 0.0;
        double rustY = 0.0;
        std::size_t failureLimit = 0U;
        double expectedBestWidth = 0.0;
        double expectedDensity = 0.0;
        unsigned int expectedFeasible = 0U;
        jaguar::Rect2d expectedItemBounds;
        std::string expectedReports;
        std::size_t expectedProgressCount = 0U;
        if (!(row >> record >> caseName >> maxIterations >> initialWidth
                  >> shrinkStep >> rustX >> rustY >> failureLimit
                  >> expectedBestWidth >> expectedDensity
                  >> expectedFeasible >> expectedItemBounds.xMin
                  >> expectedItemBounds.yMin >> expectedItemBounds.xMax
                  >> expectedItemBounds.yMax >> expectedReports
                  >> expectedProgressCount)
            || record != "case")
        {
            return Expect(false, output, "malformed Rust exploration trace row");
        }
        std::vector<double> expectedProgressWidths;
        for (std::size_t index = 0U; index < expectedProgressCount; ++index)
        {
            double width = 0.0;
            if (!(row >> width))
            {
                return Expect(false,
                              output,
                              "Rust exploration progress trace is incomplete");
            }
            expectedProgressWidths.push_back(width);
        }
        std::string extra;
        if (row >> extra)
        {
            return Expect(false, output, "Rust exploration trace has extra fields");
        }

        const auto traceInstance = jaguar::SPInstance::Create(
            {MakeSquareItem(0U, 2.0)},
            {1U},
            4.0,
            jaguar::CDEConfig{},
            jaguar::ShapeModifyConfig{});
        if (!Expect(traceInstance.HasValue(),
                    output,
                    "Rust exploration trace instance creation failed"))
        {
            return false;
        }
        auto traceProblemResult = jaguar::SPProblem::Create(
            traceInstance.Value());
        if (!Expect(traceProblemResult.HasValue(),
                    output,
                    "Rust exploration trace problem creation failed"))
        {
            return false;
        }
        jaguar::SPProblem traceProblem = std::move(traceProblemResult.Value());
        if (!Expect(traceProblem.ChangeStripWidth(initialWidth).HasValue()
                    && traceProblem.PlaceItem(
                               {0U, {0.0, {rustX - 1.0, rustY - 1.0}}}).HasValue(),
                    output,
                    "Rust exploration trace initial placement failed"))
        {
            return false;
        }
        sparrow::ExplorationConfig traceConfig;
        traceConfig.maxIterations = maxIterations;
        traceConfig.maxConsecutiveFailures = failureLimit;
        traceConfig.shrinkStep = shrinkStep;
        traceConfig.separator.strikeLimit = 0U;
        traceConfig.separator.nWorkers = 1U;
        RecordingListener traceListener;
        sparrow::OptimizationTerminator traceTerminator({
            16U,
            std::chrono::milliseconds{2000}});
        const auto traceResult = sparrow::Explore(
            traceInstance.Value(),
            traceProblem.Save(),
            traceConfig,
            42U,
            traceTerminator,
            &traceListener);
        if (!Expect(traceResult.HasValue(),
                    output,
                    "Rust exploration trace C++ phase failed"))
        {
            return false;
        }

        std::string actualReports;
        for (const auto& report : traceResult.Value().history)
        {
            if (!actualReports.empty())
            {
                actualReports.push_back(',');
            }
            actualReports += ReportTypeName(report.type);
        }
        bool progressMatches = traceListener.separationWidths.size()
            == expectedProgressWidths.size();
        for (std::size_t index = 0U;
             progressMatches && index < expectedProgressWidths.size();
             ++index)
        {
            progressMatches = std::abs(
                traceListener.separationWidths[index]
                - expectedProgressWidths[index]) < 1.0e-5;
        }
        const auto& actualPlacements =
            traceResult.Value().solution.layoutSnapshot.placements;
        const bool placementMatches = actualPlacements.size() == 1U
            && std::abs(actualPlacements.front().placedItem.CollisionShape()
                            .BoundingBox().xMin
                        - expectedItemBounds.xMin) < 1.0e-5
            && std::abs(actualPlacements.front().placedItem.CollisionShape()
                            .BoundingBox().yMin
                        - expectedItemBounds.yMin) < 1.0e-5
            && std::abs(actualPlacements.front().placedItem.CollisionShape()
                            .BoundingBox().xMax
                        - expectedItemBounds.xMax) < 1.0e-5
            && std::abs(actualPlacements.front().placedItem.CollisionShape()
                            .BoundingBox().yMax
                        - expectedItemBounds.yMax) < 1.0e-5;
        const bool matchesReference =
            std::abs(traceResult.Value().solution.StripWidth()
                     - expectedBestWidth) < 1.0e-5
            && std::abs(traceResult.Value().metrics.density
                        - expectedDensity) < 1.0e-5
            && traceResult.Value().metrics.feasible
                   == (expectedFeasible != 0U)
            && actualReports == expectedReports
            && progressMatches
            && placementMatches;
        if (!matchesReference)
        {
            output << std::setprecision(12) << "Exploration case " << caseName
                   << " actual width=" << traceResult.Value().solution.StripWidth()
                   << " density=" << traceResult.Value().metrics.density
                   << " reports=" << actualReports << " progress-widths=";
            for (const double width : traceListener.separationWidths)
            {
                output << width << ',';
            }
            output << " expected width=" << expectedBestWidth
                   << " density=" << expectedDensity
                   << " reports=" << expectedReports << " progress-count="
                   << expectedProgressCount << " placement-match="
                   << placementMatches << '\n';
        }
        if (!Expect(matchesReference,
                    output,
                    "Rust exploration phase trace mismatch"))
        {
            return false;
        }
        ++explorationCases;
    }
    if (!Expect(explorationCases == 3U,
                output,
                "Rust exploration trace must cover shrink, minimum-width, and failure-pool paths"))
    {
        return false;
    }

    const std::array<const char*, 4> explorationPoolReferencePaths = {
        "SparrowCore/Tests/Fixtures/ExplorationPoolRustTrace.tsv",
        "../SparrowCore/Tests/Fixtures/ExplorationPoolRustTrace.tsv",
        "../JaguarRun/SparrowCore/Tests/Fixtures/ExplorationPoolRustTrace.tsv",
        "..\\SparrowCore\\Tests\\Fixtures\\ExplorationPoolRustTrace.tsv",
    };
    std::ifstream explorationPoolReferenceInput;
    for (const char* path : explorationPoolReferencePaths)
    {
        explorationPoolReferenceInput.open(path);
        if (explorationPoolReferenceInput.is_open())
        {
            break;
        }
        explorationPoolReferenceInput.clear();
    }
    if (!Expect(explorationPoolReferenceInput.is_open(),
                output,
                "Rust multi-candidate exploration trace cannot be opened"))
    {
        return false;
    }

    struct ExpectedPoolPlacement
    {
        std::size_t itemIndex = 0U;
        jaguar::Rect2d bounds;
    };
    struct ExpectedPoolFailure
    {
        double loss = 0.0;
        std::vector<ExpectedPoolPlacement> placements;
    };

    std::size_t poolCases = 0U;
    while (std::getline(explorationPoolReferenceInput, phaseLine))
    {
        if (phaseLine.empty() || phaseLine.front() == '#')
        {
            continue;
        }

        std::istringstream header(phaseLine);
        std::string record;
        std::string caseName;
        std::uint64_t poolSeed = 0U;
        double initialWidth = 0.0;
        double shrinkStep = 0.0;
        std::size_t iterationLimit = 0U;
        double poolStddev = 0.0;
        std::size_t expectedFailureCount = 0U;
        std::size_t itemCount = 0U;
        if (!(header >> record >> caseName >> poolSeed >> initialWidth
                      >> shrinkStep >> iterationLimit >> poolStddev
                      >> expectedFailureCount >> itemCount)
            || record != "case" || caseName != "weighted_pool"
            || itemCount != 2U || expectedFailureCount < 3U)
        {
            return Expect(false,
                          output,
                          "malformed Rust multi-candidate exploration header");
        }

        std::vector<ExpectedPoolFailure> expectedFailures;
        expectedFailures.reserve(expectedFailureCount);
        for (std::size_t failureIndex = 0U;
             failureIndex < expectedFailureCount;
             ++failureIndex)
        {
            std::string failureLine;
            if (!std::getline(explorationPoolReferenceInput, failureLine))
            {
                return Expect(false,
                              output,
                              "Rust multi-candidate failure trace is incomplete");
            }
            std::istringstream row(failureLine);
            std::string failureRecord;
            std::size_t ordinal = 0U;
            ExpectedPoolFailure failure;
            if (!(row >> failureRecord >> ordinal >> failure.loss)
                || failureRecord != "failure" || ordinal != failureIndex)
            {
                return Expect(false,
                              output,
                              "malformed Rust multi-candidate failure row");
            }
            for (std::size_t item = 0U; item < itemCount; ++item)
            {
                ExpectedPoolPlacement placement;
                if (!(row >> placement.itemIndex >> placement.bounds.xMin
                          >> placement.bounds.yMin >> placement.bounds.xMax
                          >> placement.bounds.yMax))
                {
                    return Expect(false,
                                  output,
                                  "Rust multi-candidate placement row is incomplete");
                }
                failure.placements.push_back(placement);
            }
            std::string extra;
            if (row >> extra)
            {
                return Expect(false,
                              output,
                              "Rust multi-candidate failure row has extra fields");
            }
            expectedFailures.push_back(std::move(failure));
        }

        jaguar::SurrogateConfig poolSurrogateConfig{};
        poolSurrogateConfig.poleLimits = {
            {{64U, 0.0}, {16U, 0.8}, {8U, 0.9}}};
        poolSurrogateConfig.failFastPoleAreaRatio = 0.5;
        jaguar::CDEConfig poolCdeConfig{};
        poolCdeConfig.cdThreshold = 64U;
        poolCdeConfig.itemSurrogateConfig = poolSurrogateConfig;
        jaguar::ShapeModifyConfig poolShapeModifyConfig{};
        poolShapeModifyConfig.simplifyTolerance = 0.001;
        poolShapeModifyConfig.narrowConcavityCutoff =
            std::make_pair(0.01, 0.01);
        const auto poolInstance = jaguar::SPInstance::Create(
            {MakeCenteredRectangleItem(
                 0U, 5.0, 2.0, poolSurrogateConfig, poolShapeModifyConfig),
             MakeCenteredRectangleItem(
                 1U, 1.0, 1.0, poolSurrogateConfig, poolShapeModifyConfig)},
            {1U, 1U},
            4.0,
            poolCdeConfig,
            poolShapeModifyConfig);
        if (!Expect(poolInstance.HasValue(),
                    output,
                    "multi-candidate exploration instance creation failed"))
        {
            return false;
        }
        auto poolProblemResult = jaguar::SPProblem::Create(poolInstance.Value());
        if (!Expect(poolProblemResult.HasValue(),
                    output,
                    "multi-candidate exploration problem creation failed"))
        {
            return false;
        }
        jaguar::SPProblem poolProblem = std::move(poolProblemResult.Value());
        if (!Expect(poolProblem.ChangeStripWidth(initialWidth).HasValue()
                        && poolProblem.PlaceItem(
                                   {0U, {0.0, {2.6, 2.0}}}).HasValue()
                        && poolProblem.PlaceItem(
                                   {1U, {0.0, {6.5, 2.0}}}).HasValue(),
                    output,
                    "multi-candidate exploration placements failed"))
        {
            return false;
        }

        sparrow::ExplorationConfig poolConfig;
        poolConfig.maxIterations = iterationLimit;
        poolConfig.shrinkStep = shrinkStep;
        poolConfig.solutionPoolDistributionStddev = poolStddev;
        poolConfig.separator.strikeLimit = 0U;
        poolConfig.separator.nWorkers = 1U;
        RecordingListener poolListener;
        sparrow::OptimizationTerminator poolTerminator({
            32U,
            std::chrono::milliseconds{2000}});
        const auto poolResult = sparrow::Explore(
            poolInstance.Value(),
            poolProblem.Save(),
            poolConfig,
            poolSeed,
            poolTerminator,
            &poolListener);
        if (!Expect(poolResult.HasValue(),
                    output,
                    "multi-candidate exploration phase failed"))
        {
            return false;
        }

        std::vector<const sparrow::OptimizationSolutionReport*> actualFailures;
        for (const auto& report : poolResult.Value().history)
        {
            if (report.phase == sparrow::OptimizationPhase::OPTIMIZATION_PHASE_EXPLORATION
                && report.type
                       == sparrow::OptimizationReportType::OPTIMIZATION_REPORT_EXPLORATION_INFEASIBLE)
            {
                actualFailures.push_back(&report);
            }
        }
        if (actualFailures.size() != expectedFailures.size())
        {
            output << "Exploration pool seed " << poolSeed << " actual failures="
                   << actualFailures.size() << " expected="
                   << expectedFailures.size() << '\n';
        }
        if (!Expect(actualFailures.size() == expectedFailures.size(),
                    output,
                    "multi-candidate exploration failure count differs from Rust"))
        {
            return false;
        }

        for (std::size_t failureIndex = 0U;
             failureIndex < expectedFailures.size();
             ++failureIndex)
        {
            const auto& expected = expectedFailures[failureIndex];
            const auto& actual = *actualFailures[failureIndex];
            bool placementsMatch =
                actual.solution.layoutSnapshot.placements.size() == itemCount;
            for (const auto& expectedPlacement : expected.placements)
            {
                const auto placement = std::find_if(
                    actual.solution.layoutSnapshot.placements.begin(),
                    actual.solution.layoutSnapshot.placements.end(),
                    [&expectedPlacement](const auto& candidate) {
                        return candidate.placedItem.GetItemIndex()
                            == expectedPlacement.itemIndex;
                    });
                if (placement == actual.solution.layoutSnapshot.placements.end())
                {
                    placementsMatch = false;
                    break;
                }
                const jaguar::Rect2d bounds =
                    placement->placedItem.CollisionShape().BoundingBox();
                placementsMatch = placementsMatch
                    && std::abs(bounds.xMin - expectedPlacement.bounds.xMin) < 1.0e-4
                    && std::abs(bounds.yMin - expectedPlacement.bounds.yMin) < 1.0e-4
                    && std::abs(bounds.xMax - expectedPlacement.bounds.xMax) < 1.0e-4
                    && std::abs(bounds.yMax - expectedPlacement.bounds.yMax) < 1.0e-4;
            }
            const bool lossMatches =
                std::abs(static_cast<double>(actual.collisionLoss) - expected.loss)
                < 1.0e-3;
            if (!placementsMatch || !lossMatches)
            {
                output << std::setprecision(12) << "Exploration pool seed "
                       << poolSeed << " failure " << failureIndex
                       << " actual loss=" << actual.collisionLoss
                       << " expected loss=" << expected.loss
                       << " placement-match=" << placementsMatch << '\n';
            }
            if (!Expect(placementsMatch && lossMatches,
                        output,
                        "multi-candidate exploration state differs from Rust"))
            {
                return false;
            }
        }

        const auto sameFailureState = [](const ExpectedPoolFailure& left,
                                         const ExpectedPoolFailure& right) {
            if (left.placements.size() != right.placements.size())
            {
                return false;
            }
            for (std::size_t index = 0U; index < left.placements.size(); ++index)
            {
                const auto& first = left.placements[index];
                const auto& second = right.placements[index];
                if (first.itemIndex != second.itemIndex
                    || std::abs(first.bounds.xMin - second.bounds.xMin) >= 1.0e-5
                    || std::abs(first.bounds.yMin - second.bounds.yMin) >= 1.0e-5
                    || std::abs(first.bounds.xMax - second.bounds.xMax) >= 1.0e-5
                    || std::abs(first.bounds.yMax - second.bounds.yMax) >= 1.0e-5)
                {
                    return false;
                }
            }
            return true;
        };
        const bool selectedAlternativePoolEntry =
            expectedFailures[0].loss < expectedFailures[1].loss
            && sameFailureState(expectedFailures[0], expectedFailures[2])
            && !sameFailureState(expectedFailures[1], expectedFailures[2]);
        if (!Expect(selectedAlternativePoolEntry,
                    output,
                    "Rust pool trace must expose selection of the higher-loss candidate"))
        {
            return false;
        }
        ++poolCases;
    }
    if (!Expect(poolCases == 1U,
                output,
                "Rust multi-candidate exploration trace must contain one deterministic case"))
    {
        return false;
    }

    const std::array<const char*, 4> explorationContainedReferencePaths = {
        "SparrowCore/Tests/Fixtures/ExplorationContainedRustTrace.tsv",
        "../SparrowCore/Tests/Fixtures/ExplorationContainedRustTrace.tsv",
        "../JaguarRun/SparrowCore/Tests/Fixtures/ExplorationContainedRustTrace.tsv",
        "..\\SparrowCore\\Tests\\Fixtures\\ExplorationContainedRustTrace.tsv",
    };
    std::ifstream explorationContainedReferenceInput;
    for (const char* path : explorationContainedReferencePaths)
    {
        explorationContainedReferenceInput.open(path);
        if (explorationContainedReferenceInput.is_open())
        {
            break;
        }
        explorationContainedReferenceInput.clear();
    }
    if (!Expect(explorationContainedReferenceInput.is_open(),
                output,
                "Rust contained-relocation trace cannot be opened"))
    {
        return false;
    }

    std::string containedHeaderLine;
    while (std::getline(explorationContainedReferenceInput, containedHeaderLine)
           && (containedHeaderLine.empty() || containedHeaderLine.front() == '#'))
    {
    }
    std::istringstream containedHeader(containedHeaderLine);
    std::string containedRecord;
    std::string containedCase;
    std::uint64_t containedSeed = 0U;
    double containedInitialWidth = 0.0;
    double containedShrinkStep = 0.0;
    std::size_t containedIterationLimit = 0U;
    double containedPoolStddev = 0.0;
    std::size_t containedFailureCount = 0U;
    std::size_t containedItemCount = 0U;
    if (!(containedHeader >> containedRecord >> containedCase >> containedSeed
                           >> containedInitialWidth >> containedShrinkStep
                           >> containedIterationLimit >> containedPoolStddev
                           >> containedFailureCount >> containedItemCount)
        || containedRecord != "case"
        || containedCase != "contained_relocation"
        || containedItemCount != 3U || containedFailureCount != 3U
        || containedIterationLimit != 3U
        || containedSeed != 43U
        || std::abs(containedInitialWidth - 10.0) >= 1.0e-6
        || std::abs(containedShrinkStep - 0.1) >= 1.0e-7
        || std::abs(containedPoolStddev - 0.25) >= 1.0e-7)
    {
        return Expect(false,
                      output,
                      "malformed Rust contained-relocation trace header");
    }

    std::vector<ExpectedPoolFailure> expectedContainedFailures;
    expectedContainedFailures.reserve(containedFailureCount);
    for (std::size_t failureIndex = 0U;
         failureIndex < containedFailureCount;
         ++failureIndex)
    {
        std::string failureLine;
        if (!std::getline(explorationContainedReferenceInput, failureLine))
        {
            return Expect(false,
                          output,
                          "Rust contained-relocation trace is incomplete");
        }
        std::istringstream row(failureLine);
        std::string failureRecord;
        std::size_t ordinal = 0U;
        ExpectedPoolFailure failure;
        if (!(row >> failureRecord >> ordinal >> failure.loss)
            || failureRecord != "failure" || ordinal != failureIndex)
        {
            return Expect(false,
                          output,
                          "malformed Rust contained-relocation failure row");
        }
        for (std::size_t item = 0U; item < containedItemCount; ++item)
        {
            ExpectedPoolPlacement placement;
            if (!(row >> placement.itemIndex >> placement.bounds.xMin
                      >> placement.bounds.yMin >> placement.bounds.xMax
                      >> placement.bounds.yMax))
            {
                return Expect(false,
                              output,
                              "Rust contained-relocation placement row is incomplete");
            }
            failure.placements.push_back(placement);
        }
        std::string extra;
        if (row >> extra)
        {
            return Expect(false,
                          output,
                          "Rust contained-relocation row has extra fields");
        }
        expectedContainedFailures.push_back(std::move(failure));
    }

    const auto expectedBounds = [](
        const ExpectedPoolFailure& failure,
        std::size_t itemIndex) -> const jaguar::Rect2d* {
        const auto placement = std::find_if(
            failure.placements.begin(),
            failure.placements.end(),
            [itemIndex](const ExpectedPoolPlacement& candidate) {
                return candidate.itemIndex == itemIndex;
            });
        return placement == failure.placements.end()
            ? nullptr
            : &placement->bounds;
    };
    const jaguar::Rect2d* childBefore = expectedBounds(
        expectedContainedFailures[0], 2U);
    const jaguar::Rect2d* childAfter = expectedBounds(
        expectedContainedFailures[1], 2U);
    if (!Expect(childBefore != nullptr && childAfter != nullptr
                    && std::abs(childBefore->xMin - 8.7) < 1.0e-4
                    && std::abs(childAfter->xMin - 4.4) < 1.0e-4
                    && std::abs((childBefore->xMin - childAfter->xMin) - 4.3)
                           < 1.0e-4,
                output,
                "Rust trace must relocate the POI-contained child into "
                "the host's vacated space"))
    {
        return false;
    }

    jaguar::SurrogateConfig containedSurrogateConfig{};
    containedSurrogateConfig.poleLimits = {
        {{64U, 0.0}, {16U, 0.8}, {8U, 0.9}}};
    containedSurrogateConfig.failFastPoleAreaRatio = 0.5;
    jaguar::CDEConfig containedCdeConfig{};
    containedCdeConfig.cdThreshold = 64U;
    containedCdeConfig.itemSurrogateConfig = containedSurrogateConfig;
    jaguar::ShapeModifyConfig containedShapeModifyConfig{};
    containedShapeModifyConfig.simplifyTolerance = 0.001;
    containedShapeModifyConfig.narrowConcavityCutoff =
        std::make_pair(0.01, 0.01);
    const auto containedInstance = jaguar::SPInstance::Create(
        {MakeCenteredRectangleItem(
             0U, 5.0, 2.0, containedSurrogateConfig,
             containedShapeModifyConfig),
         MakeCenteredRectangleItem(
             1U, 3.0, 2.0, containedSurrogateConfig,
             containedShapeModifyConfig),
         MakeCenteredRectangleItem(
             2U, 1.0, 1.0, containedSurrogateConfig,
             containedShapeModifyConfig)},
        {1U, 1U, 1U},
        4.0,
        containedCdeConfig,
        containedShapeModifyConfig);
    if (!Expect(containedInstance.HasValue(),
                output,
                "contained-relocation exploration instance creation failed"))
    {
        return false;
    }
    auto containedProblemResult =
        jaguar::SPProblem::Create(containedInstance.Value());
    if (!Expect(containedProblemResult.HasValue(),
                output,
                "contained-relocation exploration problem creation failed"))
    {
        return false;
    }
    jaguar::SPProblem containedProblem = std::move(containedProblemResult.Value());
    if (!Expect(containedProblem.ChangeStripWidth(containedInitialWidth).HasValue()
                    && containedProblem.PlaceItem(
                               {0U, {0.0, {2.5, 2.0}}}).HasValue()
                    && containedProblem.PlaceItem(
                               {1U, {0.0, {6.8, 2.0}}}).HasValue()
                    && containedProblem.PlaceItem(
                               {2U, {0.0, {9.2, 2.0}}}).HasValue(),
                output,
                "contained-relocation exploration placements failed"))
    {
        return false;
    }

    sparrow::ExplorationConfig containedConfig;
    containedConfig.maxIterations = containedIterationLimit;
    containedConfig.shrinkStep = containedShrinkStep;
    containedConfig.solutionPoolDistributionStddev = containedPoolStddev;
    containedConfig.separator.strikeLimit = 0U;
    containedConfig.separator.nWorkers = 1U;
    RecordingListener containedListener;
    sparrow::OptimizationTerminator containedTerminator({
        32U,
        std::chrono::milliseconds{2000}});
    const auto containedResult = sparrow::Explore(
        containedInstance.Value(),
        containedProblem.Save(),
        containedConfig,
        containedSeed,
        containedTerminator,
        &containedListener);
    if (!Expect(containedResult.HasValue(),
                output,
                "contained-relocation exploration phase failed"))
    {
        return false;
    }

    std::vector<const sparrow::OptimizationSolutionReport*> actualContainedFailures;
    const auto& containedHistory = containedResult.Value().history;
    for (std::size_t reportIndex = 1U;
         reportIndex < containedHistory.size();
         ++reportIndex)
    {
        const auto& report = containedHistory[reportIndex];
        if (report.phase == sparrow::OptimizationPhase::OPTIMIZATION_PHASE_EXPLORATION
            && report.type
                   == sparrow::OptimizationReportType::OPTIMIZATION_REPORT_EXPLORATION_INFEASIBLE)
        {
            actualContainedFailures.push_back(&report);
        }
    }
    if (!Expect(actualContainedFailures.size() == expectedContainedFailures.size(),
                output,
                "contained-relocation failure count differs from Rust"))
    {
        return false;
    }
    for (std::size_t failureIndex = 0U;
         failureIndex < expectedContainedFailures.size();
         ++failureIndex)
    {
        const auto& expected = expectedContainedFailures[failureIndex];
        const auto& actual = *actualContainedFailures[failureIndex];
        bool placementsMatch =
            actual.solution.layoutSnapshot.placements.size() == containedItemCount;
        for (const auto& expectedPlacement : expected.placements)
        {
            const auto placement = std::find_if(
                actual.solution.layoutSnapshot.placements.begin(),
                actual.solution.layoutSnapshot.placements.end(),
                [&expectedPlacement](const auto& candidate) {
                    return candidate.placedItem.GetItemIndex()
                        == expectedPlacement.itemIndex;
                });
            if (placement == actual.solution.layoutSnapshot.placements.end())
            {
                placementsMatch = false;
                break;
            }
            const jaguar::Rect2d bounds =
                placement->placedItem.CollisionShape().BoundingBox();
            placementsMatch = placementsMatch
                && std::abs(bounds.xMin - expectedPlacement.bounds.xMin) < 1.0e-4
                && std::abs(bounds.yMin - expectedPlacement.bounds.yMin) < 1.0e-4
                && std::abs(bounds.xMax - expectedPlacement.bounds.xMax) < 1.0e-4
                && std::abs(bounds.yMax - expectedPlacement.bounds.yMax) < 1.0e-4;
        }
        const bool lossMatches =
            std::abs(static_cast<double>(actual.collisionLoss) - expected.loss)
            < 1.0e-3;
        if (!placementsMatch || !lossMatches)
        {
            output << std::setprecision(12)
                   << "Contained relocation failure " << failureIndex
                   << " actual loss=" << actual.collisionLoss
                   << " expected loss=" << expected.loss
                   << " placement-match=" << placementsMatch << '\n';
            for (const auto& expectedPlacement : expected.placements)
            {
                const auto placement = std::find_if(
                    actual.solution.layoutSnapshot.placements.begin(),
                    actual.solution.layoutSnapshot.placements.end(),
                    [&expectedPlacement](const auto& candidate) {
                        return candidate.placedItem.GetItemIndex()
                            == expectedPlacement.itemIndex;
                    });
                if (placement != actual.solution.layoutSnapshot.placements.end())
                {
                    const jaguar::Rect2d bounds =
                        placement->placedItem.CollisionShape().BoundingBox();
                    output << "  item " << expectedPlacement.itemIndex
                           << " actual=[" << bounds.xMin << ',' << bounds.yMin
                           << ',' << bounds.xMax << ',' << bounds.yMax
                           << "] expected=[" << expectedPlacement.bounds.xMin
                           << ',' << expectedPlacement.bounds.yMin << ','
                           << expectedPlacement.bounds.xMax << ','
                           << expectedPlacement.bounds.yMax << "]\n";
                }
            }
        }
        if (!Expect(placementsMatch && lossMatches,
                    output,
                    "contained-relocation state differs from Rust"))
        {
            return false;
        }
    }

    const auto repeated = sparrow::Optimize(
        instance.Value(), optimizerConfig);
    if (!Expect(repeated.HasValue()
                    && SameSolution(optimized.Value().solution,
                                    repeated.Value().solution),
                output,
                "fixed seed optimization must be deterministic"))
    {
        return false;
    }

    auto warmProblemResult = jaguar::SPProblem::Create(instance.Value());
    if (!Expect(warmProblemResult.HasValue(),
                output,
                "warm-start problem creation failed"))
    {
        return false;
    }
    jaguar::SPProblem warmProblem = std::move(warmProblemResult.Value());
    if (!Expect(warmProblem.ChangeStripWidth(8.0).HasValue()
                    && warmProblem.PlaceItem({0U, {0.0, {1.0, 1.0}}}).HasValue()
                    && warmProblem.PlaceItem({0U, {0.0, {4.0, 1.0}}}).HasValue(),
                output,
                "warm-start solution construction failed"))
    {
        return false;
    }

    sparrow::SparrowConfig warmStartConfig = optimizerConfig;
    warmStartConfig.warmStart = warmProblem.Save();
    sparrow::OptimizationTerminator externalTerminator({
        32U,
        std::chrono::milliseconds{2000}});
    externalTerminator.Start();
    externalTerminator.RequestStop();
    const auto stopped = sparrow::Optimize(
        instance.Value(), warmStartConfig, externalTerminator);
    return Expect(stopped.HasValue() && stopped.Value().metrics.feasible
                      && stopped.Value().solution.StripWidth() == 8.0,
                  output,
                  "top-level optimizer must honor cancellation and warm start");
}
