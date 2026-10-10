#include "SparrowCore/Optimizer/Compression.h"
#include "SparrowCore/Optimizer/Exploration.h"
#include "SparrowCore/Optimizer/Separator.h"
#include "SparrowCore/Optimizer/SparrowLbfBuilder.h"
#include "SparrowCore/Optimizer/SparrowOptimizer.h"
#include "SparrowCore/Quantify/CollisionTracker.h"
#include "SparrowCore/Random/SparrowRng.h"
#include "../Source/Optimizer/ExplorationNumeric.h"
#include "../Source/Optimizer/ExplorationNormalSampler.h"
#include "../Source/Optimizer/ExplorationTrace.h"
#include "../Source/Optimizer/CompressionTrace.h"

#include "JaguarCore/Entities/Item.h"
#include "JaguarCore/Assertions.h"
#include "JaguarCore/Collision/CollisionEngine.h"
#include "JaguarCore/IO/Importer.h"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <map>
#include <ostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
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

class RustOptimizeReferenceListener final : public sparrow::OptimizationListener
{
public:
    void OnPhase(sparrow::OptimizationPhase phase) override
    {
        if (phase == sparrow::OptimizationPhase::OPTIMIZATION_PHASE_EXPLORATION)
        {
            phases.emplace_back("exploration");
        }
        else if (phase == sparrow::OptimizationPhase::OPTIMIZATION_PHASE_COMPRESSION)
        {
            phases.emplace_back("compression");
        }
    }

    void OnSolution(const sparrow::OptimizationSolutionReport& report) override
    {
        switch (report.type)
        {
        case sparrow::OptimizationReportType::OPTIMIZATION_REPORT_EXPLORATION_FEASIBLE:
            reports.emplace_back("expl_feas");
            break;
        case sparrow::OptimizationReportType::OPTIMIZATION_REPORT_EXPLORATION_INFEASIBLE:
            reports.emplace_back("expl_infeas");
            break;
        case sparrow::OptimizationReportType::OPTIMIZATION_REPORT_EXPLORATION_IMPROVING:
            reports.emplace_back("expl_improving");
            break;
        case sparrow::OptimizationReportType::OPTIMIZATION_REPORT_COMPRESSION_FEASIBLE:
            reports.emplace_back("cmpr_feas");
            break;
        case sparrow::OptimizationReportType::OPTIMIZATION_REPORT_FINAL:
            reports.emplace_back("final");
            break;
        }
    }

    void OnCompressionProgress(double step) override
    {
        compressionSteps.push_back(step);
    }

    std::vector<std::string> phases;
    std::vector<std::string> reports;
    std::vector<double> compressionSteps;
};

struct ColdStartTraceEvent
{
    std::string type;
    std::string label;
    std::vector<double> values;
};

bool MatchesRustPhaseTrace(
    const std::vector<ColdStartTraceEvent>& actualEvents,
    const char* fixtureName,
    std::ostream& output,
    const char* failureMessage)
{
    const std::array<std::string, 4U> paths{
        std::string{"SparrowCore/Tests/Fixtures/"} + fixtureName,
        std::string{"../SparrowCore/Tests/Fixtures/"} + fixtureName,
        std::string{"../JaguarRun/SparrowCore/Tests/Fixtures/"} + fixtureName,
        std::string{"..\\SparrowCore\\Tests\\Fixtures\\"} + fixtureName};
    std::ifstream referenceFile;
    for (const auto& path : paths)
    {
        referenceFile.open(path);
        if (referenceFile.is_open())
        {
            break;
        }
        referenceFile.clear();
    }
    if (!Expect(referenceFile.is_open(), output,
                "Rust phase-timeout reference fixture cannot be opened"))
    {
        return false;
    }

    std::vector<ColdStartTraceEvent> expectedEvents;
    std::string line;
    while (std::getline(referenceFile, line))
    {
        if (line.empty() || line.front() == '#')
        {
            continue;
        }
        std::istringstream row(line);
        ColdStartTraceEvent event;
        row >> event.type >> event.label;
        double value = 0.0;
        while (row >> value)
        {
            event.values.push_back(value);
        }
        expectedEvents.push_back(std::move(event));
    }

    bool traceMatches = actualEvents.size() == expectedEvents.size();
    for (std::size_t index = 0U;
         traceMatches && index < actualEvents.size();
         ++index)
    {
        const auto& expected = expectedEvents[index];
        const auto& observed = actualEvents[index];
        if (expected.type != observed.type || expected.label != observed.label
            || expected.values.size() != observed.values.size())
        {
            traceMatches = false;
            break;
        }
        for (std::size_t valueIndex = 0U;
             valueIndex < expected.values.size();
             ++valueIndex)
        {
            const double tolerance = expected.type == "report"
                ? (valueIndex == 0U ? 0.0 : 1.0e-5)
                : expected.type == "placement"
                    ? (valueIndex == 0U ? 0.0
                       : valueIndex == 1U ? 1.0e-6 : 5.0e-5)
                    : expected.type == "step" ? 1.0e-6 : 0.0;
            if (std::abs(expected.values[valueIndex]
                         - observed.values[valueIndex]) > tolerance)
            {
                traceMatches = false;
                break;
            }
        }
    }
    return Expect(traceMatches, output, failureMessage);
}

const char* ColdStartReportName(sparrow::OptimizationReportType type)
{
    switch (type)
    {
    case sparrow::OptimizationReportType::OPTIMIZATION_REPORT_EXPLORATION_FEASIBLE:
        return "expl_feas";
    case sparrow::OptimizationReportType::OPTIMIZATION_REPORT_EXPLORATION_INFEASIBLE:
        return "expl_infeas";
    case sparrow::OptimizationReportType::OPTIMIZATION_REPORT_EXPLORATION_IMPROVING:
        return "expl_improving";
    case sparrow::OptimizationReportType::OPTIMIZATION_REPORT_COMPRESSION_FEASIBLE:
        return "cmpr_feas";
    case sparrow::OptimizationReportType::OPTIMIZATION_REPORT_FINAL:
        return "final";
    }
    return "unknown";
}

class ColdStartReferenceListener final : public sparrow::OptimizationListener
{
public:
    void OnPhase(sparrow::OptimizationPhase phase) override
    {
        if (phase == sparrow::OptimizationPhase::OPTIMIZATION_PHASE_EXPLORATION)
            events.push_back({"phase", "exploration", {}});
        else if (phase == sparrow::OptimizationPhase::OPTIMIZATION_PHASE_COMPRESSION)
            events.push_back({"phase", "compression", {}});
    }

    void OnSolution(const sparrow::OptimizationSolutionReport& report) override
    {
        const std::string label = ColdStartReportName(report.type);
        events.push_back({"report",
                          label,
                          {static_cast<double>(report.solution.layoutSnapshot.placements.size()),
                           report.stripWidth,
                           report.density}});
        struct PlacementObservation
        {
            std::size_t itemIndex;
            double rotation;
            double x;
            double y;
        };
        std::vector<PlacementObservation> placements;
        placements.reserve(report.solution.layoutSnapshot.placements.size());
        for (const auto& placement : report.solution.layoutSnapshot.placements)
        {
            const auto& transform = placement.placedItem.Transform();
            placements.push_back({placement.placedItem.GetItemIndex(),
                                  transform.rotationRad,
                                  transform.translation.x,
                                  transform.translation.y});
        }
        std::sort(placements.begin(), placements.end(), [](const auto& left,
                                                           const auto& right) {
            return left.itemIndex < right.itemIndex;
        });
        for (const auto& placement : placements)
        {
            events.push_back({"placement",
                              label,
                              {static_cast<double>(placement.itemIndex),
                               placement.rotation,
                               placement.x,
                               placement.y}});
        }
    }

    void OnCompressionProgress(double step) override
    {
        events.push_back({"step", {}, {step}});
    }

    std::vector<ColdStartTraceEvent> events;
};

class DefaultColdStartReferenceListener final : public sparrow::OptimizationListener
{
public:
    explicit DefaultColdStartReferenceListener(
        sparrow::OptimizationTerminator& terminator)
        : m_Terminator(terminator)
    {
    }

    void OnPhase(sparrow::OptimizationPhase phase) override
    {
        if (phase == sparrow::OptimizationPhase::OPTIMIZATION_PHASE_EXPLORATION)
        {
            m_CurrentPhase = "exploration";
        }
        else if (phase == sparrow::OptimizationPhase::OPTIMIZATION_PHASE_COMPRESSION)
        {
            m_CurrentPhase = "compression";
        }
        events.push_back({"phase", m_CurrentPhase, {}});
    }

    void OnSolution(const sparrow::OptimizationSolutionReport& report) override
    {
        const std::string label = ColdStartReportName(report.type);
        events.push_back({"report",
                          label,
                          {static_cast<double>(report.solution.layoutSnapshot.placements.size()),
                           report.stripWidth,
                           report.density}});
        std::vector<const jaguar::LayoutPlacementSnapshot*> placements;
        placements.reserve(report.solution.layoutSnapshot.placements.size());
        for (const auto& placement : report.solution.layoutSnapshot.placements)
        {
            placements.push_back(&placement);
        }
        std::sort(placements.begin(), placements.end(), [](const auto* left,
                                                           const auto* right) {
            return left->placedItem.GetItemIndex()
                < right->placedItem.GetItemIndex();
        });
        for (const auto* placement : placements)
        {
            const auto& transform = placement->placedItem.Transform();
            events.push_back({"placement",
                              label,
                              {static_cast<double>(placement->placedItem.GetItemIndex()),
                               transform.rotationRad,
                               transform.translation.x,
                               transform.translation.y}});
        }
    }

    void OnSeparationResult(const sparrow::SeparationResult& result) override
    {
        events.push_back({"separation",
                          m_CurrentPhase,
                          {result.success ? 1.0 : 0.0,
                           static_cast<double>(result.totalEvaluations),
                           static_cast<double>(result.totalMoves),
                           static_cast<double>(result.iterations)}});
        if (m_CurrentPhase == "exploration" && !stoppedExploration)
        {
            stoppedExploration = true;
            m_Terminator.RequestPhaseStop();
        }
        else if (m_CurrentPhase == "compression" && !stoppedCompression)
        {
            stoppedCompression = true;
            m_Terminator.RequestPhaseStop();
        }
    }

    void OnCompressionProgress(double step) override
    {
        compressionStepInDefaultRange = compressionStepInDefaultRange
            && step > 0.00001 && step <= 0.0005;
        events.push_back({"compression_step", "default_range", {}});
    }

    std::vector<ColdStartTraceEvent> events;
    bool stoppedExploration = false;
    bool stoppedCompression = false;
    bool compressionStepInDefaultRange = true;

private:
    sparrow::OptimizationTerminator& m_Terminator;
    std::string m_CurrentPhase;
};

class ConstructionFailureListener final : public sparrow::OptimizationListener
{
public:
    void OnPhase(sparrow::OptimizationPhase) override { ++callbackCount; }
    void OnProgress(const sparrow::OptimizationProgress&) override
    {
        ++callbackCount;
    }
    void OnImprovement(const sparrow::OptimizationProgress&) override
    {
        ++callbackCount;
    }
    void OnSolution(const sparrow::OptimizationSolutionReport&) override
    {
        ++callbackCount;
    }
    void OnSeparationProgress(const sparrow::SeparationProgress&) override
    {
        ++callbackCount;
    }
    void OnSeparationResult(const sparrow::SeparationResult&) override
    {
        ++callbackCount;
    }
    void OnCompressionProgress(double) override { ++callbackCount; }

    std::size_t callbackCount = 0U;
};

class ImmediateStopReferenceListener final : public sparrow::OptimizationListener
{
public:
    explicit ImmediateStopReferenceListener(
        sparrow::OptimizationTerminator& terminator)
        : m_Terminator(terminator)
    {
    }

    void OnPhase(sparrow::OptimizationPhase phase) override
    {
        if (phase == sparrow::OptimizationPhase::OPTIMIZATION_PHASE_EXPLORATION)
        {
            m_CurrentPhase = "exploration";
        }
        else if (phase == sparrow::OptimizationPhase::OPTIMIZATION_PHASE_COMPRESSION)
        {
            m_CurrentPhase = "compression";
        }
        events.push_back({"phase", m_CurrentPhase, {}});
    }

    void OnSolution(const sparrow::OptimizationSolutionReport& report) override
    {
        const std::string label = ColdStartReportName(report.type);
        events.push_back({"report",
                          label,
                          {static_cast<double>(report.solution.layoutSnapshot.placements.size()),
                           report.stripWidth,
                           report.density}});
        std::vector<const jaguar::LayoutPlacementSnapshot*> placements;
        placements.reserve(report.solution.layoutSnapshot.placements.size());
        for (const auto& placement : report.solution.layoutSnapshot.placements)
        {
            placements.push_back(&placement);
        }
        std::sort(placements.begin(), placements.end(), [](const auto* left,
                                                           const auto* right) {
            return left->placedItem.GetItemIndex()
                < right->placedItem.GetItemIndex();
        });
        for (const auto* placement : placements)
        {
            const auto& transform = placement->placedItem.Transform();
            events.push_back({"placement",
                              label,
                              {static_cast<double>(placement->placedItem.GetItemIndex()),
                               transform.rotationRad,
                               transform.translation.x,
                               transform.translation.y}});
        }
        if (m_CurrentPhase == "exploration"
            && report.type
                == sparrow::OptimizationReportType::OPTIMIZATION_REPORT_EXPLORATION_FEASIBLE
            && !stoppedExploration)
        {
            stoppedExploration = true;
            m_Terminator.RequestPhaseStop();
        }
    }

    void OnSeparationResult(const sparrow::SeparationResult& result) override
    {
        ++separationResultCount;
        events.push_back({"separation",
                          m_CurrentPhase,
                          {result.success ? 1.0 : 0.0,
                           static_cast<double>(result.totalEvaluations),
                           static_cast<double>(result.totalMoves),
                           static_cast<double>(result.iterations)}});
    }

    void OnCompressionProgress(double step) override
    {
        ++compressionStepCount;
        events.push_back({"step", "compression", {step}});
    }

    std::vector<ColdStartTraceEvent> events;
    bool stoppedExploration = false;
    std::size_t separationResultCount = 0U;
    std::size_t compressionStepCount = 0U;

private:
    sparrow::OptimizationTerminator& m_Terminator;
    std::string m_CurrentPhase;
};

class PhaseTimeoutReferenceListener final : public sparrow::OptimizationListener
{
public:
    explicit PhaseTimeoutReferenceListener(
        std::chrono::milliseconds explorationTimeoutCallbackDelay = {},
        std::chrono::milliseconds compressionTimeoutCallbackDelay = {})
        : explorationTimeoutCallbackDelay(explorationTimeoutCallbackDelay)
        , compressionTimeoutCallbackDelay(compressionTimeoutCallbackDelay)
    {
    }

    void OnPhase(sparrow::OptimizationPhase phase) override
    {
        if (phase == sparrow::OptimizationPhase::OPTIMIZATION_PHASE_EXPLORATION)
            currentPhase = "exploration";
        else if (phase == sparrow::OptimizationPhase::OPTIMIZATION_PHASE_COMPRESSION)
            currentPhase = "compression";
        events.push_back({"phase", currentPhase, {}});
    }

    void OnSolution(const sparrow::OptimizationSolutionReport& report) override
    {
        const std::string label = ColdStartReportName(report.type);
        events.push_back({"report",
                          label,
                          {static_cast<double>(report.solution.layoutSnapshot.placements.size()),
                           report.stripWidth,
                           report.density}});
        std::vector<const jaguar::LayoutPlacementSnapshot*> placements;
        placements.reserve(report.solution.layoutSnapshot.placements.size());
        for (const auto& placement : report.solution.layoutSnapshot.placements)
        {
            placements.push_back(&placement);
        }
        std::sort(placements.begin(), placements.end(), [](const auto* left,
                                                           const auto* right) {
            return left->placedItem.GetItemIndex()
                < right->placedItem.GetItemIndex();
        });
        for (const auto* placement : placements)
        {
            const auto& transform = placement->placedItem.Transform();
            events.push_back({"placement",
                              label,
                              {static_cast<double>(placement->placedItem.GetItemIndex()),
                               transform.rotationRad,
                               transform.translation.x,
                               transform.translation.y}});
        }
        if (explorationTimeoutCallbackDelay.count() > 0
            && currentPhase == "exploration"
            && report.type
                == sparrow::OptimizationReportType::OPTIMIZATION_REPORT_EXPLORATION_FEASIBLE
            && !explorationTimeoutCallbackDelayed)
        {
            explorationTimeoutCallbackDelayed = true;
            std::this_thread::sleep_for(explorationTimeoutCallbackDelay);
        }
    }

    void OnSeparationResult(const sparrow::SeparationResult& result) override
    {
        ++separationResultCount;
        events.push_back({"separation",
                          currentPhase,
                          {result.success ? 1.0 : 0.0,
                           static_cast<double>(result.totalEvaluations),
                           static_cast<double>(result.totalMoves),
                           static_cast<double>(result.iterations)}});
    }

    void OnCompressionProgress(double step) override
    {
        ++compressionStepCount;
        events.push_back({"step", "compression", {step}});
        if (compressionTimeoutCallbackDelay.count() > 0
            && currentPhase == "compression"
            && !compressionTimeoutCallbackDelayed)
        {
            compressionTimeoutCallbackDelayed = true;
            std::this_thread::sleep_for(compressionTimeoutCallbackDelay);
        }
    }

    std::string currentPhase;
    std::vector<ColdStartTraceEvent> events;
    std::size_t separationResultCount = 0U;
    std::size_t compressionStepCount = 0U;
    bool explorationTimeoutCallbackDelayed = false;
    bool compressionTimeoutCallbackDelayed = false;

private:
    std::chrono::milliseconds explorationTimeoutCallbackDelay;
    std::chrono::milliseconds compressionTimeoutCallbackDelay;
};

class TimeBasedCurveListener final : public sparrow::OptimizationListener
{
public:
    explicit TimeBasedCurveListener(
        sparrow::OptimizationTerminator& terminator)
        : m_Terminator(terminator)
    {
    }

    void OnCompressionProgress(double step) override
    {
        const auto now = std::chrono::steady_clock::now();
        if (steps.empty())
        {
            firstProgressTime = now;
        }
        steps.push_back(step);
        elapsedSeconds.push_back(
            std::chrono::duration<double>(now - firstProgressTime).count());
        if (steps.size() < 5U)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds{400});
        }
        else
        {
            m_Terminator.RequestPhaseStop();
        }
    }

    std::vector<double> steps;
    std::vector<double> elapsedSeconds;

private:
    sparrow::OptimizationTerminator& m_Terminator;
    std::chrono::steady_clock::time_point firstProgressTime{};
};

class TerminationHandoffListener final : public sparrow::OptimizationListener
{
public:
    explicit TerminationHandoffListener(
        sparrow::OptimizationTerminator& terminator)
        : m_Terminator(terminator)
    {
    }

    void OnSolution(const sparrow::OptimizationSolutionReport& report) override
    {
        reports.emplace_back(ColdStartReportName(report.type));
        if (!compressionPhase
            && report.type
                == sparrow::OptimizationReportType::OPTIMIZATION_REPORT_EXPLORATION_INFEASIBLE)
        {
            m_Terminator.RequestPhaseStop();
        }
    }

    void OnSeparationProgress(
        const sparrow::SeparationProgress& progress) override
    {
        separationProgress.push_back(progress);
    }

    void OnSeparationResult(
        const sparrow::SeparationResult& result) override
    {
        separationResults.push_back(result);
        if (compressionPhase)
            m_Terminator.RequestPhaseStop();
    }

    void OnCompressionProgress(double step) override
    {
        compressionSteps.push_back(step);
    }

    bool compressionPhase = false;
    std::vector<std::string> reports;
    std::vector<double> compressionSteps;
    std::vector<sparrow::SeparationProgress> separationProgress;
    std::vector<sparrow::SeparationResult> separationResults;

private:
    sparrow::OptimizationTerminator& m_Terminator;
};

class CompressionWorkerMoveListener final : public sparrow::OptimizationListener
{
public:
    void OnCompressionProgress(double step) override
    {
        compressionSteps.push_back(step);
    }

    void OnSolution(const sparrow::OptimizationSolutionReport& report) override
    {
        std::vector<jaguar::Transform2d> transforms;
        transforms.reserve(report.solution.layoutSnapshot.placements.size());
        for (const auto& placement : report.solution.layoutSnapshot.placements)
            transforms.push_back(placement.placedItem.Transform());
        std::sort(transforms.begin(), transforms.end(), [](const auto& left,
                                                           const auto& right) {
            return left.translation.x < right.translation.x;
        });
        reportTransforms.push_back(std::move(transforms));
        jaguar::Layout layout(report.solution.layoutSnapshot.container);
        const auto restored = layout.Restore(report.solution.layoutSnapshot);
        if (restored.HasValue())
        {
            const auto tracker = sparrow::CollisionTracker::Create(layout);
            if (tracker.HasValue())
                reportLosses.push_back(tracker.Value().TotalLoss());
        }
        switch (report.type)
        {
        case sparrow::OptimizationReportType::OPTIMIZATION_REPORT_EXPLORATION_FEASIBLE:
            reports.emplace_back("expl_feas");
            break;
        case sparrow::OptimizationReportType::OPTIMIZATION_REPORT_EXPLORATION_INFEASIBLE:
            reports.emplace_back("expl_infeas");
            break;
        case sparrow::OptimizationReportType::OPTIMIZATION_REPORT_EXPLORATION_IMPROVING:
            reports.emplace_back("expl_improving");
            break;
        case sparrow::OptimizationReportType::OPTIMIZATION_REPORT_COMPRESSION_FEASIBLE:
            reports.emplace_back("cmpr_feas");
            break;
        case sparrow::OptimizationReportType::OPTIMIZATION_REPORT_FINAL:
            reports.emplace_back("final");
            break;
        }
    }

    void OnSeparationProgress(
        const sparrow::SeparationProgress& progress) override
    {
        separationProgress.push_back(progress);
        eventOrder.emplace_back("progress");
    }

    void OnSeparationResult(
        const sparrow::SeparationResult& result) override
    {
        separationResults.push_back(result);
        eventOrder.emplace_back("result");
    }

    std::vector<double> compressionSteps;
    std::vector<std::string> reports;
    std::vector<std::vector<jaguar::Transform2d>> reportTransforms;
    std::vector<float> reportLosses;
    std::vector<sparrow::SeparationProgress> separationProgress;
    std::vector<sparrow::SeparationResult> separationResults;
    std::vector<std::string> eventOrder;
};

struct RustCompressionProgressReference
{
    std::size_t iteration = 0U;
    double stripWidth = 0.0;
    double density = 0.0;
    float minimumLoss = 0.0F;
};

struct RustCompressionResultReference
{
    bool success = false;
    std::size_t totalEvaluations = 0U;
    std::size_t totalMoves = 0U;
    std::size_t iterations = 0U;
};

struct RustCompressionPlacementReference
{
    std::size_t rank = 0U;
    std::size_t itemIndex = 0U;
    double rotation = 0.0;
    double x = 0.0;
    double y = 0.0;
};

struct RustCompressionReportStateReference
{
    std::size_t index = 0U;
    float totalLoss = 0.0F;
    std::vector<std::pair<double, double>> positions;
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
                                       const jaguar::ShapeModifyConfig& shapeModifyConfig,
                                       jaguar::RotationRange allowedRotation = {
                                           jaguar::RotationMode::None, {}})
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
               surrogateConfig,
               std::nullopt,
               std::move(allowedRotation))
        .Value();
}

bool TestColdStartOptimizeMatchesRustReference(std::ostream& output)
{
    std::ifstream instanceFile;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/RustSppOptimizeColdStart.json",
             "../SparrowCore/Tests/Fixtures/RustSppOptimizeColdStart.json",
             "../JaguarRun/SparrowCore/Tests/Fixtures/RustSppOptimizeColdStart.json",
             "..\\SparrowCore\\Tests\\Fixtures\\RustSppOptimizeColdStart.json"})
    {
        instanceFile.open(path);
        if (instanceFile.is_open())
            break;
        instanceFile.clear();
    }
    if (!Expect(instanceFile.is_open(), output,
                "Rust cold-start input fixture cannot be opened"))
    {
        return false;
    }
    const std::string instanceJson{
        std::istreambuf_iterator<char>(instanceFile),
        std::istreambuf_iterator<char>()};
    const jaguar::Importer importer(sparrow::MakeRustDefaultImporterConfig());
    const auto instance = importer.ImportSPInstanceJson(instanceJson);
    if (!Expect(instance.HasValue(), output,
                "Rust cold-start input must import with Rust defaults"))
    {
        return false;
    }

    sparrow::SparrowConfig config;
    config.seed = 42U;
    config.exploration.shrinkStep = 0.2;
    config.exploration.maxConsecutiveFailures = 1U;
    config.exploration.timeLimit = std::chrono::seconds{30};
    config.exploration.separator.noImprovementLimit = 0U;
    config.exploration.separator.strikeLimit = 1U;
    config.exploration.separator.nWorkers = 1U;
    config.exploration.separator.nContainerSamples = 8U;
    config.exploration.separator.nFocussedSamples = 0U;
    config.exploration.separator.nCoordinateDescents = 1U;
    config.compression.decayStrategy =
        sparrow::ShrinkDecayStrategy::SHRINK_DECAY_FAILURE_BASED;
    config.compression.shrinkMin = 0.01;
    config.compression.shrinkMax = 0.01;
    config.compression.failureDecay = 0.5;
    config.compression.timeLimit = std::chrono::seconds{30};
    config.compression.separator.noImprovementLimit = 0U;
    config.compression.separator.strikeLimit = 1U;
    config.compression.separator.nWorkers = 1U;
    config.compression.separator.nContainerSamples = 8U;
    config.compression.separator.nFocussedSamples = 0U;
    config.compression.separator.nCoordinateDescents = 1U;
    config.terminator.maxDuration = std::chrono::seconds{30};
    if (!Expect(!config.warmStart.has_value()
                    && config.constructor.nContainerSamples == 1000U
                    && config.constructor.nFocussedSamples == 0U
                    && config.constructor.nCoordinateDescents == 3U,
                output,
                "cold-start must use the default Sparrow LBF constructor budget"))
    {
        return false;
    }

    ColdStartReferenceListener listener;
    const auto actual = sparrow::Optimize(instance.Value(), config, &listener);
    if (!actual.HasValue())
    {
        output << "Rust cold-start reference Optimize error: "
               << actual.GetError().message << '\n';
    }
    if (!Expect(actual.HasValue(), output,
                "SparrowCore Optimize cold-start path must complete"))
    {
        return false;
    }
    if (!Expect(actual.Value().metrics.feasible
                    && actual.Value().metrics.placedItemCount == 3U
                    && actual.Value().metrics.remainingDemand == 0U,
                output,
                "cold-start result must satisfy all three item demands"))
    {
        return false;
    }

    std::ifstream referenceFile;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/OptimizeColdStartRustReference.tsv",
             "../SparrowCore/Tests/Fixtures/OptimizeColdStartRustReference.tsv",
             "../JaguarRun/SparrowCore/Tests/Fixtures/OptimizeColdStartRustReference.tsv",
             "..\\SparrowCore\\Tests\\Fixtures\\OptimizeColdStartRustReference.tsv"})
    {
        referenceFile.open(path);
        if (referenceFile.is_open())
            break;
        referenceFile.clear();
    }
    if (!Expect(referenceFile.is_open(), output,
                "Rust cold-start reference fixture cannot be opened"))
    {
        return false;
    }

    std::vector<ColdStartTraceEvent> expectedEvents;
    std::string line;
    while (std::getline(referenceFile, line))
    {
        if (line.empty() || line.front() == '#')
            continue;
        std::istringstream row(line);
        ColdStartTraceEvent event;
        row >> event.type >> event.label;
        if (event.type == "phase")
        {
            // Phase labels are the complete event payload.
        }
        else if (event.type == "step")
        {
            const double step = std::stod(event.label);
            event.label.clear();
            event.values = {step};
        }
        else if (event.type == "report")
        {
            double count = 0.0;
            double width = 0.0;
            double density = 0.0;
            row >> count >> width >> density;
            event.values = {count, width, density};
        }
        else if (event.type == "placement")
        {
            double itemIndex = 0.0;
            double rotation = 0.0;
            double x = 0.0;
            double y = 0.0;
            row >> itemIndex >> rotation >> x >> y;
            event.values = {itemIndex, rotation, x, y};
        }
        else
        {
            return Expect(false, output,
                          "Rust cold-start reference contains an unknown event");
        }
        if (!Expect(static_cast<bool>(row), output,
                    "Rust cold-start reference contains a malformed row"))
        {
            return false;
        }
        expectedEvents.push_back(std::move(event));
    }

    bool traceMatches = listener.events.size() == expectedEvents.size();
    for (std::size_t i = 0U;
         traceMatches && i < listener.events.size();
         ++i)
    {
        const auto& expected = expectedEvents[i];
        const auto& observed = listener.events[i];
        if (expected.type != observed.type || expected.label != observed.label
            || expected.values.size() != observed.values.size())
        {
            traceMatches = false;
            break;
        }
        for (std::size_t valueIndex = 0U;
             valueIndex < expected.values.size();
             ++valueIndex)
        {
            double tolerance = 1.0e-6;
            if (expected.type == "report")
            {
                tolerance = valueIndex == 0U ? 0.0 : 1.0e-5;
            }
            else if (expected.type == "placement")
            {
                tolerance = valueIndex == 0U ? 0.0
                    : valueIndex == 1U ? 1.0e-6 : 5.0e-5;
            }
            if (std::abs(expected.values[valueIndex]
                         - observed.values[valueIndex]) > tolerance)
            {
                traceMatches = false;
                break;
            }
        }
    }
    if (!traceMatches)
    {
        output << std::setprecision(17)
               << "Rust cold-start events=" << expectedEvents.size()
               << " C++ events=" << listener.events.size() << '\n';
        const std::size_t count = std::min(expectedEvents.size(),
                                           listener.events.size());
        for (std::size_t i = 0U; i < count; ++i)
        {
            output << i << " expected " << expectedEvents[i].type << ':'
                   << expectedEvents[i].label << " actual "
                   << listener.events[i].type << ':'
                   << listener.events[i].label << '\n';
        }
    }
    return Expect(traceMatches, output,
                  "cold-start construction, phase/report order, and placements must match Rust");
}

bool TestDefaultColdStartOptimizeMatchesRustReference(std::ostream& output)
{
    std::ifstream instanceFile;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/RustSppOptimizeColdStart.json",
             "../SparrowCore/Tests/Fixtures/RustSppOptimizeColdStart.json",
             "../JaguarRun/SparrowCore/Tests/Fixtures/RustSppOptimizeColdStart.json",
             "..\\SparrowCore\\Tests\\Fixtures\\RustSppOptimizeColdStart.json"})
    {
        instanceFile.open(path);
        if (instanceFile.is_open())
        {
            break;
        }
        instanceFile.clear();
    }
    if (!Expect(instanceFile.is_open(), output,
                "Rust default cold-start input fixture cannot be opened"))
    {
        return false;
    }
    const std::string instanceJson{
        std::istreambuf_iterator<char>(instanceFile),
        std::istreambuf_iterator<char>()};
    const jaguar::Importer importer(sparrow::MakeRustDefaultImporterConfig());
    const auto instance = importer.ImportSPInstanceJson(instanceJson);
    if (!Expect(instance.HasValue(), output,
                "Rust default cold-start input must import with Rust defaults"))
    {
        return false;
    }

    sparrow::SparrowConfig config;
    config.seed = 42U;
    const auto& explorationSamples = config.exploration.separator;
    const auto& compressionSamples = config.compression.separator;
    const bool defaultStageConfigsMatch =
        std::abs(config.exploration.shrinkStep - 0.001) < 1.0e-12
        && config.exploration.timeLimit == std::chrono::minutes{9}
        && config.exploration.maxIterations == 0U
        && !config.exploration.maxConsecutiveFailures.has_value()
        && explorationSamples.noImprovementLimit == 200U
        && explorationSamples.strikeLimit == 3U
        && explorationSamples.nWorkers == 3U
        && explorationSamples.nContainerSamples == 50U
        && explorationSamples.nFocussedSamples == 25U
        && explorationSamples.nCoordinateDescents == 3U
        && std::abs(config.compression.shrinkMax - 0.0005) < 1.0e-12
        && std::abs(config.compression.shrinkMin - 0.00001) < 1.0e-12
        && config.compression.decayStrategy
               == sparrow::ShrinkDecayStrategy::SHRINK_DECAY_TIME_BASED
        && config.compression.timeLimit == std::chrono::seconds{60}
        && config.compression.maxIterations == 0U
        && config.compression.maxConsecutiveFailures == 0U
        && compressionSamples.noImprovementLimit == 100U
        && compressionSamples.strikeLimit == 5U
        && compressionSamples.nWorkers == 3U
        && compressionSamples.nContainerSamples == 50U
        && compressionSamples.nFocussedSamples == 25U
        && compressionSamples.nCoordinateDescents == 3U
        && config.constructor.nContainerSamples == 1000U
        && config.constructor.nFocussedSamples == 0U
        && config.constructor.nCoordinateDescents == 3U;
    if (!Expect(defaultStageConfigsMatch,
                output,
                "cold-start reference must retain the Rust default exploration, compression, and LBF budgets"))
    {
        return false;
    }

    sparrow::OptimizationTerminator terminator(config.terminator);
    terminator.Start();
    DefaultColdStartReferenceListener listener(terminator);
    listener.events.push_back({
        "config",
        "exploration",
        {config.exploration.shrinkStep,
         static_cast<double>(config.exploration.timeLimit.count())
             / 1000000000.0,
         static_cast<double>(explorationSamples.noImprovementLimit),
         static_cast<double>(explorationSamples.strikeLimit),
         static_cast<double>(explorationSamples.nWorkers),
         static_cast<double>(explorationSamples.nContainerSamples),
         static_cast<double>(explorationSamples.nFocussedSamples),
         static_cast<double>(explorationSamples.nCoordinateDescents)}});
    listener.events.push_back({
        "config",
        "compression_time_based",
        {config.compression.shrinkMax,
         config.compression.shrinkMin,
         static_cast<double>(config.compression.timeLimit.count())
             / 1000000000.0,
         static_cast<double>(compressionSamples.noImprovementLimit),
         static_cast<double>(compressionSamples.strikeLimit),
         static_cast<double>(compressionSamples.nWorkers),
         static_cast<double>(compressionSamples.nContainerSamples),
         static_cast<double>(compressionSamples.nFocussedSamples),
         static_cast<double>(compressionSamples.nCoordinateDescents)}});

    const auto actual = sparrow::Optimize(
        instance.Value(), config, terminator, &listener);
    if (!actual.HasValue())
    {
        output << "Rust default cold-start reference Optimize error: "
               << actual.GetError().message << '\n';
    }
    if (!Expect(actual.HasValue(), output,
                "SparrowCore default cold-start optimization must complete"))
    {
        return false;
    }
    if (!Expect(listener.stoppedExploration
                    && listener.stoppedCompression
                    && listener.compressionStepInDefaultRange
                    && actual.Value().metrics.feasible
                    && actual.Value().metrics.placedItemCount == 3U,
                output,
                "default cold-start must stop after one separation attempt per phase and retain a feasible solution"))
    {
        return false;
    }

    std::ifstream referenceFile;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/OptimizeColdStartDefaultRustReference.tsv",
             "../SparrowCore/Tests/Fixtures/OptimizeColdStartDefaultRustReference.tsv",
             "../JaguarRun/SparrowCore/Tests/Fixtures/OptimizeColdStartDefaultRustReference.tsv",
             "..\\SparrowCore\\Tests\\Fixtures\\OptimizeColdStartDefaultRustReference.tsv"})
    {
        referenceFile.open(path);
        if (referenceFile.is_open())
        {
            break;
        }
        referenceFile.clear();
    }
    if (!Expect(referenceFile.is_open(), output,
                "Rust default cold-start reference fixture cannot be opened"))
    {
        return false;
    }

    std::vector<ColdStartTraceEvent> expectedEvents;
    std::string line;
    while (std::getline(referenceFile, line))
    {
        if (line.empty() || line.front() == '#')
        {
            continue;
        }
        std::istringstream row(line);
        ColdStartTraceEvent event;
        row >> event.type >> event.label;
        double value = 0.0;
        while (row >> value)
        {
            event.values.push_back(value);
        }
        if (!Expect(event.type == "config" || event.type == "phase"
                        || event.type == "report" || event.type == "placement"
                        || event.type == "separation"
                        || event.type == "compression_step",
                    output,
                    "Rust default cold-start fixture contains an unknown event"))
        {
            return false;
        }
        expectedEvents.push_back(std::move(event));
    }

    bool traceMatches = listener.events.size() == expectedEvents.size();
    for (std::size_t index = 0U;
         traceMatches && index < listener.events.size();
         ++index)
    {
        const auto& expected = expectedEvents[index];
        const auto& observed = listener.events[index];
        if (expected.type != observed.type || expected.label != observed.label
            || expected.values.size() != observed.values.size())
        {
            traceMatches = false;
            break;
        }
        for (std::size_t valueIndex = 0U;
             valueIndex < expected.values.size();
             ++valueIndex)
        {
            double tolerance = 1.0e-6;
            if (expected.type == "config" || expected.type == "separation")
            {
                tolerance = 0.0;
            }
            else if (expected.type == "report")
            {
                tolerance = valueIndex == 0U ? 0.0 : 1.0e-5;
            }
            else if (expected.type == "placement")
            {
                tolerance = valueIndex == 0U ? 0.0
                    : valueIndex == 1U ? 1.0e-6 : 5.0e-5;
            }
            if (std::abs(expected.values[valueIndex]
                         - observed.values[valueIndex]) > tolerance)
            {
                traceMatches = false;
                break;
            }
        }
        if (!traceMatches)
        {
            output << std::setprecision(17)
                   << "Default Rust cold-start mismatch at event " << index
                   << ": expected " << expected.type << ':' << expected.label
                   << " actual " << observed.type << ':' << observed.label << '\n';
            const std::size_t valueCount = std::min(expected.values.size(),
                                                    observed.values.size());
            for (std::size_t valueIndex = 0U;
                 valueIndex < valueCount;
                 ++valueIndex)
            {
                output << "  value " << valueIndex << " expected "
                       << expected.values[valueIndex] << " actual "
                       << observed.values[valueIndex] << '\n';
            }
        }
    }
    return Expect(traceMatches,
                  output,
                  "default cold-start phase, config, report, and placement trace must match Rust");
}

bool TestColdStartConstructionFailureMatchesRustReference(std::ostream& output)
{
    std::ifstream instanceFile;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/RustSppConstructionFailure.json",
             "../SparrowCore/Tests/Fixtures/RustSppConstructionFailure.json",
             "../JaguarRun/SparrowCore/Tests/Fixtures/RustSppConstructionFailure.json",
             "..\\SparrowCore\\Tests\\Fixtures\\RustSppConstructionFailure.json"})
    {
        instanceFile.open(path);
        if (instanceFile.is_open())
        {
            break;
        }
        instanceFile.clear();
    }
    if (!Expect(instanceFile.is_open(), output,
                "Rust construction-failure input fixture cannot be opened"))
    {
        return false;
    }
    const std::string instanceJson{
        std::istreambuf_iterator<char>(instanceFile),
        std::istreambuf_iterator<char>()};
    const jaguar::Importer importer(sparrow::MakeRustDefaultImporterConfig());
    const auto instance = importer.ImportSPInstanceJson(instanceJson);
    if (!Expect(instance.HasValue(), output,
                "Rust construction-failure input must import with Rust defaults"))
    {
        return false;
    }

    std::ifstream referenceFile;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/OptimizeConstructionFailureRustReference.tsv",
             "../SparrowCore/Tests/Fixtures/OptimizeConstructionFailureRustReference.tsv",
             "../JaguarRun/SparrowCore/Tests/Fixtures/OptimizeConstructionFailureRustReference.tsv",
             "..\\SparrowCore\\Tests\\Fixtures\\OptimizeConstructionFailureRustReference.tsv"})
    {
        referenceFile.open(path);
        if (referenceFile.is_open())
        {
            break;
        }
        referenceFile.clear();
    }
    if (!Expect(referenceFile.is_open(), output,
                "Rust construction-failure reference fixture cannot be opened"))
    {
        return false;
    }
    std::string expectedMessage;
    std::size_t expectedCallbackCount = static_cast<std::size_t>(-1);
    std::string line;
    while (std::getline(referenceFile, line))
    {
        if (line.empty() || line.front() == '#')
        {
            continue;
        }
        std::istringstream row(line);
        std::string field;
        std::string value;
        std::getline(row, field, '\t');
        std::getline(row, value);
        if (field == "error_message")
        {
            expectedMessage = value;
        }
        else if (field == "callback_count")
        {
            std::istringstream(value) >> expectedCallbackCount;
        }
    }
    if (!Expect(!expectedMessage.empty()
                    && expectedCallbackCount != static_cast<std::size_t>(-1),
                output,
                "Rust construction-failure reference is incomplete"))
    {
        return false;
    }

    sparrow::SparrowConfig config;
    config.seed = 42U;
    ConstructionFailureListener listener;
    const auto actual = sparrow::Optimize(instance.Value(), config, &listener);
    const bool matches = !actual.HasValue()
        && actual.GetError().code == jaguar::ErrorCode::Infeasible
        && actual.GetError().message == expectedMessage
        && listener.callbackCount == expectedCallbackCount;
    if (!matches)
    {
        output << "Construction failure actual=";
        if (actual.HasValue())
        {
            output << "success";
        }
        else
        {
            output << static_cast<int>(actual.GetError().code) << ':'
                   << actual.GetError().message;
        }
        output << " expected_message=" << expectedMessage
               << " callbacks=" << listener.callbackCount
               << " expected_callbacks=" << expectedCallbackCount << '\n';
    }
    return Expect(matches,
                  output,
                  "cold-start construction failure must preserve Rust item identity and precede phase callbacks");
}

bool TestImmediateStopOptimizeMatchesRustReference(std::ostream& output)
{
    std::ifstream instanceFile;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/RustSppOptimizeImmediateStop.json",
             "../SparrowCore/Tests/Fixtures/RustSppOptimizeImmediateStop.json",
             "../JaguarRun/SparrowCore/Tests/Fixtures/RustSppOptimizeImmediateStop.json",
             "..\\SparrowCore\\Tests\\Fixtures\\RustSppOptimizeImmediateStop.json"})
    {
        instanceFile.open(path);
        if (instanceFile.is_open())
        {
            break;
        }
        instanceFile.clear();
    }
    if (!Expect(instanceFile.is_open(), output,
                "Rust immediate-stop input fixture cannot be opened"))
    {
        return false;
    }
    const std::string instanceJson{
        std::istreambuf_iterator<char>(instanceFile),
        std::istreambuf_iterator<char>()};
    const jaguar::Importer importer(sparrow::MakeRustDefaultImporterConfig());
    const auto instance = importer.ImportSPInstanceJson(instanceJson);
    if (!Expect(instance.HasValue(), output,
                "Rust immediate-stop input must import with Rust defaults"))
    {
        return false;
    }

    std::ifstream referenceFile;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/OptimizeImmediateStopRustReference.tsv",
             "../SparrowCore/Tests/Fixtures/OptimizeImmediateStopRustReference.tsv",
             "../JaguarRun/SparrowCore/Tests/Fixtures/OptimizeImmediateStopRustReference.tsv",
             "..\\SparrowCore\\Tests\\Fixtures\\OptimizeImmediateStopRustReference.tsv"})
    {
        referenceFile.open(path);
        if (referenceFile.is_open())
        {
            break;
        }
        referenceFile.clear();
    }
    if (!Expect(referenceFile.is_open(), output,
                "Rust immediate-stop reference fixture cannot be opened"))
    {
        return false;
    }
    std::vector<ColdStartTraceEvent> expectedEvents;
    std::string line;
    while (std::getline(referenceFile, line))
    {
        if (line.empty() || line.front() == '#')
        {
            continue;
        }
        std::istringstream row(line);
        ColdStartTraceEvent event;
        row >> event.type >> event.label;
        double value = 0.0;
        while (row >> value)
        {
            event.values.push_back(value);
        }
        expectedEvents.push_back(std::move(event));
    }

    sparrow::SparrowConfig config;
    config.seed = 42U;
    sparrow::OptimizationTerminator terminator(config.terminator);
    terminator.Start();
    ImmediateStopReferenceListener listener(terminator);
    const auto actual = sparrow::Optimize(
        instance.Value(), config, terminator, &listener);
    if (!Expect(actual.HasValue()
                    && actual.Value().metrics.feasible
                    && actual.Value().metrics.placedItemCount == 1U
                    && listener.stoppedExploration
                    && listener.compressionStepCount == 1U
                    && listener.separationResultCount == 1U,
                output,
                "phase stop at the initial report must reset for compression, which exits at minimum width"))
    {
        return false;
    }

    bool traceMatches = listener.events.size() == expectedEvents.size();
    for (std::size_t index = 0U;
         traceMatches && index < listener.events.size();
         ++index)
    {
        const auto& expected = expectedEvents[index];
        const auto& observed = listener.events[index];
        if (expected.type != observed.type || expected.label != observed.label
            || expected.values.size() != observed.values.size())
        {
            traceMatches = false;
            break;
        }
        for (std::size_t valueIndex = 0U;
             valueIndex < expected.values.size();
             ++valueIndex)
        {
            double tolerance = 0.0;
            if (expected.type == "report")
            {
                tolerance = valueIndex == 0U ? 0.0 : 1.0e-5;
            }
            else if (expected.type == "placement")
            {
                tolerance = valueIndex == 0U ? 0.0
                    : valueIndex == 1U ? 1.0e-6 : 5.0e-5;
            }
            else if (expected.type == "step")
            {
                tolerance = 1.0e-6;
            }
            if (std::abs(expected.values[valueIndex]
                         - observed.values[valueIndex]) > tolerance)
            {
                traceMatches = false;
                break;
            }
        }
    }
    return Expect(traceMatches,
                  output,
                  "immediate-stop, compression handoff, and minimum-width trace must match Rust");
}

bool TestZeroPhaseTimeoutSemantics(std::ostream& output)
{
    sparrow::OptimizationTerminator terminator;
    terminator.Start();
    if (!Expect(!terminator.ShouldTerminate(0U),
                output,
                "Start without an active phase must not install a phase deadline"))
    {
        return false;
    }
    terminator.StartPhase(std::chrono::milliseconds{0});
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
    return Expect(terminator.ShouldTerminate(0U),
                  output,
                  "zero phase timeout must expire on a poll after its deadline, matching Rust BasicTerminator");
}

bool TestZeroTimeoutOptimizeMatchesRustReference(std::ostream& output)
{
    std::ifstream instanceFile;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/RustSppOptimizeImmediateStop.json",
             "../SparrowCore/Tests/Fixtures/RustSppOptimizeImmediateStop.json",
             "../JaguarRun/SparrowCore/Tests/Fixtures/RustSppOptimizeImmediateStop.json",
             "..\\SparrowCore\\Tests\\Fixtures\\RustSppOptimizeImmediateStop.json"})
    {
        instanceFile.open(path);
        if (instanceFile.is_open())
        {
            break;
        }
        instanceFile.clear();
    }
    if (!Expect(instanceFile.is_open(), output,
                "Rust zero-timeout input fixture cannot be opened"))
    {
        return false;
    }
    const std::string instanceJson{
        std::istreambuf_iterator<char>(instanceFile),
        std::istreambuf_iterator<char>()};
    const jaguar::Importer importer(sparrow::MakeRustDefaultImporterConfig());
    const auto instance = importer.ImportSPInstanceJson(instanceJson);
    if (!Expect(instance.HasValue(), output,
                "Rust zero-timeout input must import with Rust defaults"))
    {
        return false;
    }

    sparrow::SparrowConfig config;
    config.seed = 42U;
    config.exploration.timeLimit = std::chrono::milliseconds{0};
    config.compression.timeLimit = std::chrono::milliseconds{0};
    sparrow::OptimizationTerminator terminator(config.terminator);
    terminator.Start();
    PhaseTimeoutReferenceListener listener;
    const auto actual = sparrow::Optimize(
        instance.Value(), config, terminator, &listener);
    if (!Expect(actual.HasValue()
                    && actual.Value().metrics.feasible
                    && actual.Value().metrics.placedItemCount == 1U
                    && listener.separationResultCount == 0U
                    && listener.compressionStepCount == 0U,
                output,
                "zero-duration exploration and compression must skip separator work"))
    {
        return false;
    }
    listener.events.push_back({"separation_count",
                               std::to_string(listener.separationResultCount),
                               {}});
    listener.events.push_back({"compression_steps",
                               std::to_string(listener.compressionStepCount),
                               {}});

    std::ifstream referenceFile;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/OptimizeZeroTimeoutRustReference.tsv",
             "../SparrowCore/Tests/Fixtures/OptimizeZeroTimeoutRustReference.tsv",
             "../JaguarRun/SparrowCore/Tests/Fixtures/OptimizeZeroTimeoutRustReference.tsv",
             "..\\SparrowCore\\Tests\\Fixtures\\OptimizeZeroTimeoutRustReference.tsv"})
    {
        referenceFile.open(path);
        if (referenceFile.is_open())
        {
            break;
        }
        referenceFile.clear();
    }
    if (!Expect(referenceFile.is_open(), output,
                "Rust zero-timeout reference fixture cannot be opened"))
    {
        return false;
    }
    std::vector<ColdStartTraceEvent> expectedEvents;
    std::string line;
    while (std::getline(referenceFile, line))
    {
        if (line.empty() || line.front() == '#')
        {
            continue;
        }
        std::istringstream row(line);
        ColdStartTraceEvent event;
        row >> event.type >> event.label;
        double value = 0.0;
        while (row >> value)
        {
            event.values.push_back(value);
        }
        expectedEvents.push_back(std::move(event));
    }

    bool traceMatches = listener.events.size() == expectedEvents.size();
    for (std::size_t index = 0U;
         traceMatches && index < listener.events.size();
         ++index)
    {
        const auto& expected = expectedEvents[index];
        const auto& observed = listener.events[index];
        if (expected.type != observed.type || expected.label != observed.label
            || expected.values.size() != observed.values.size())
        {
            traceMatches = false;
            break;
        }
        for (std::size_t valueIndex = 0U;
             valueIndex < expected.values.size();
             ++valueIndex)
        {
            const double tolerance = expected.type == "report"
                ? (valueIndex == 0U ? 0.0 : 1.0e-5)
                : expected.type == "placement"
                    ? (valueIndex == 0U ? 0.0
                       : valueIndex == 1U ? 1.0e-6 : 5.0e-5)
                    : 0.0;
            if (std::abs(expected.values[valueIndex]
                         - observed.values[valueIndex]) > tolerance)
            {
                traceMatches = false;
                break;
            }
        }
    }
    return Expect(traceMatches,
                  output,
                  "zero-timeout phase, report, and final-layout trace must match Rust");
}

bool TestPositiveTimeoutOptimizeMatchesRustReference(std::ostream& output)
{
    std::ifstream instanceFile;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/RustSppOptimizeCompressSingleItem.json",
             "../SparrowCore/Tests/Fixtures/RustSppOptimizeCompressSingleItem.json",
             "../JaguarRun/SparrowCore/Tests/Fixtures/RustSppOptimizeCompressSingleItem.json",
             "..\\SparrowCore\\Tests\\Fixtures\\RustSppOptimizeCompressSingleItem.json"})
    {
        instanceFile.open(path);
        if (instanceFile.is_open())
        {
            break;
        }
        instanceFile.clear();
    }
    if (!Expect(instanceFile.is_open(), output,
                "Rust positive-timeout input fixture cannot be opened"))
    {
        return false;
    }
    const std::string instanceJson{
        std::istreambuf_iterator<char>(instanceFile),
        std::istreambuf_iterator<char>()};
    const jaguar::Importer importer;
    const auto instance = importer.ImportSPInstanceJson(instanceJson);
    if (!Expect(instance.HasValue(), output,
                "Rust positive-timeout input must import"))
    {
        return false;
    }

    auto problemResult = jaguar::SPProblem::Create(instance.Value());
    if (!Expect(problemResult.HasValue(), output,
                "Rust positive-timeout warm-start problem must be created"))
    {
        return false;
    }
    jaguar::SPProblem warmProblem = std::move(problemResult.Value());
    const auto widthSet = warmProblem.ChangeStripWidth(4.0);
    const auto itemPlaced = warmProblem.PlaceItem({
        0U,
        jaguar::Transform2d::FromRotationTranslation(0.0, {1.003, 1.01})});
    if (!Expect(widthSet.HasValue() && itemPlaced.HasValue(),
                output,
                "Rust positive-timeout warm-start layout must be constructible"))
    {
        return false;
    }

    sparrow::SparrowConfig config;
    config.seed = 3U;
    config.warmStart = warmProblem.Save();
    const auto phaseTimeout = std::chrono::milliseconds{250};
    config.exploration.timeLimit = phaseTimeout;
    config.exploration.shrinkStep = 0.2;
    config.exploration.separator.noImprovementLimit = 0U;
    config.exploration.separator.strikeLimit = 1U;
    config.exploration.separator.nWorkers = 1U;
    config.exploration.separator.nContainerSamples = 8U;
    config.exploration.separator.nFocussedSamples = 0U;
    config.exploration.separator.nCoordinateDescents = 1U;
    config.compression.timeLimit = phaseTimeout;
    config.compression.decayStrategy =
        sparrow::ShrinkDecayStrategy::SHRINK_DECAY_FAILURE_BASED;
    config.compression.shrinkMin = 0.01;
    config.compression.shrinkMax = 0.01;
    config.compression.failureDecay = 0.5;
    config.compression.separator.noImprovementLimit = 0U;
    config.compression.separator.strikeLimit = 1U;
    config.compression.separator.nWorkers = 1U;
    config.compression.separator.nContainerSamples = 8U;
    config.compression.separator.nFocussedSamples = 0U;
    config.compression.separator.nCoordinateDescents = 1U;

    sparrow::OptimizationTerminator terminator(config.terminator);
    terminator.Start();
    PhaseTimeoutReferenceListener listener(
        std::chrono::milliseconds{300},
        std::chrono::milliseconds{300});
    const auto actual = sparrow::Optimize(
        instance.Value(), config, terminator, &listener);
    if (!Expect(actual.HasValue()
                    && actual.Value().metrics.feasible
                    && actual.Value().metrics.placedItemCount == 1U
                    && std::abs(actual.Value().metrics.stripWidth - 3.96) < 1.0e-5
                    && listener.explorationTimeoutCallbackDelayed
                    && listener.compressionTimeoutCallbackDelayed
                    && listener.separationResultCount == 1U
                    && listener.compressionStepCount == 1U
                    && terminator.ShouldTerminate(0U),
                output,
                "positive phase timeouts must expire after the controlled callbacks and preserve the completed compression attempt"))
    {
        return false;
    }

    listener.events.push_back({"separation_count",
                               std::to_string(listener.separationResultCount),
                               {}});
    listener.events.push_back({"compression_steps",
                               std::to_string(listener.compressionStepCount),
                               {}});
    listener.events.push_back({"timeout_exploration_delay",
                               listener.explorationTimeoutCallbackDelayed ? "1" : "0",
                               {}});
    listener.events.push_back({"timeout_compression_delay",
                               listener.compressionTimeoutCallbackDelayed ? "1" : "0",
                               {}});
    return MatchesRustPhaseTrace(
        listener.events,
        "OptimizePositiveTimeoutRustReference.tsv",
        output,
        "positive-timeout phase, termination, report, and final-layout trace must match Rust");
}

bool TestPositiveTimeBasedTimeoutOptimizeMatchesRustReference(
    std::ostream& output)
{
    std::ifstream instanceFile;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/RustSppOptimizeCompressSingleItem.json",
             "../SparrowCore/Tests/Fixtures/RustSppOptimizeCompressSingleItem.json",
             "../JaguarRun/SparrowCore/Tests/Fixtures/RustSppOptimizeCompressSingleItem.json",
             "..\\SparrowCore\\Tests\\Fixtures\\RustSppOptimizeCompressSingleItem.json"})
    {
        instanceFile.open(path);
        if (instanceFile.is_open())
        {
            break;
        }
        instanceFile.clear();
    }
    if (!Expect(instanceFile.is_open(), output,
                "Rust positive TimeBased-timeout input fixture cannot be opened"))
    {
        return false;
    }
    const std::string instanceJson{
        std::istreambuf_iterator<char>(instanceFile),
        std::istreambuf_iterator<char>()};
    const jaguar::Importer importer;
    const auto instance = importer.ImportSPInstanceJson(instanceJson);
    if (!Expect(instance.HasValue(), output,
                "Rust positive TimeBased-timeout input must import"))
    {
        return false;
    }

    auto problemResult = jaguar::SPProblem::Create(instance.Value());
    if (!Expect(problemResult.HasValue(), output,
                "Rust positive TimeBased-timeout warm-start problem must be created"))
    {
        return false;
    }
    jaguar::SPProblem warmProblem = std::move(problemResult.Value());
    const auto widthSet = warmProblem.ChangeStripWidth(4.0);
    const auto itemPlaced = warmProblem.PlaceItem({
        0U,
        jaguar::Transform2d::FromRotationTranslation(0.0, {1.003, 1.01})});
    if (!Expect(widthSet.HasValue() && itemPlaced.HasValue(),
                output,
                "Rust positive TimeBased-timeout warm-start layout must be constructible"))
    {
        return false;
    }

    sparrow::SparrowConfig config;
    config.seed = 3U;
    config.warmStart = warmProblem.Save();
    config.exploration.timeLimit = std::chrono::milliseconds{0};
    config.compression.timeLimit = std::chrono::seconds{2};
    config.compression.decayStrategy =
        sparrow::ShrinkDecayStrategy::SHRINK_DECAY_TIME_BASED;
    config.compression.shrinkMin = 0.02;
    config.compression.shrinkMax = 0.08;
    config.compression.separator.noImprovementLimit = 0U;
    config.compression.separator.strikeLimit = 1U;
    config.compression.separator.nWorkers = 1U;
    config.compression.separator.nContainerSamples = 8U;
    config.compression.separator.nFocussedSamples = 0U;
    config.compression.separator.nCoordinateDescents = 1U;

    sparrow::OptimizationTerminator terminator(config.terminator);
    terminator.Start();
    PhaseTimeoutReferenceListener listener(
        std::chrono::milliseconds{0},
        std::chrono::milliseconds{2100});
    const auto actual = sparrow::Optimize(
        instance.Value(), config, terminator, &listener);
    if (!Expect(actual.HasValue()
                    && actual.Value().metrics.feasible
                    && actual.Value().metrics.placedItemCount == 1U
                    && listener.explorationTimeoutCallbackDelayed == false
                    && listener.compressionTimeoutCallbackDelayed
                    && listener.separationResultCount == 1U
                    && listener.compressionStepCount == 1U
                    && terminator.ShouldTerminate(0U),
                output,
                "the TimeBased compression deadline must expire during the first attempt"))
    {
        return false;
    }

    const auto stepEvent = std::find_if(
        listener.events.begin(), listener.events.end(), [](const auto& event) {
            return event.type == "step" && event.label == "compression";
        });
    if (!Expect(stepEvent != listener.events.end()
                    && stepEvent->values.size() == 1U
                    && stepEvent->values.front() > 0.07
                    && stepEvent->values.front() <= 0.08
                    && std::abs(actual.Value().metrics.stripWidth
                                - 4.0 * (1.0 - stepEvent->values.front())) < 1.0e-5,
                output,
                "final TimeBased layout width must reflect the completed first shrink step"))
    {
        return false;
    }

    listener.events.push_back({"separation_count",
                               std::to_string(listener.separationResultCount),
                               {}});
    listener.events.push_back({"compression_steps",
                               std::to_string(listener.compressionStepCount),
                               {}});
    listener.events.push_back({"timeout_exploration_delay",
                               listener.explorationTimeoutCallbackDelayed ? "1" : "0",
                               {}});
    listener.events.push_back({"timeout_compression_delay",
                               listener.compressionTimeoutCallbackDelayed ? "1" : "0",
                               {}});
    listener.events.push_back({"timeout_expired",
                               terminator.ShouldTerminate(0U) ? "1" : "0",
                               {}});
    return MatchesRustPhaseTrace(
        listener.events,
        "OptimizeTimeBasedPositiveTimeoutRustReference.tsv",
        output,
        "positive-time TimeBased timeout, attempt cleanup, and final layout must match Rust");
}

bool TestPositiveTimeBasedCompressionCurveMatchesRustReference(
    std::ostream& output)
{
    std::ifstream instanceFile;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/RustSppOptimizeCompressSingleItem.json",
             "../SparrowCore/Tests/Fixtures/RustSppOptimizeCompressSingleItem.json",
             "../JaguarRun/SparrowCore/Tests/Fixtures/RustSppOptimizeCompressSingleItem.json",
             "..\\SparrowCore\\Tests\\Fixtures\\RustSppOptimizeCompressSingleItem.json"})
    {
        instanceFile.open(path);
        if (instanceFile.is_open())
        {
            break;
        }
        instanceFile.clear();
    }
    if (!Expect(instanceFile.is_open(), output,
                "Rust TimeBased curve input fixture cannot be opened"))
    {
        return false;
    }
    const std::string instanceJson{
        std::istreambuf_iterator<char>(instanceFile),
        std::istreambuf_iterator<char>()};
    const jaguar::Importer importer;
    const auto instance = importer.ImportSPInstanceJson(instanceJson);
    if (!Expect(instance.HasValue(), output,
                "Rust TimeBased curve input must import"))
    {
        return false;
    }

    auto problemResult = jaguar::SPProblem::Create(instance.Value());
    if (!Expect(problemResult.HasValue(), output,
                "Rust TimeBased curve warm-start problem must be created"))
    {
        return false;
    }
    jaguar::SPProblem warmProblem = std::move(problemResult.Value());
    const auto widthSet = warmProblem.ChangeStripWidth(4.0);
    const auto itemPlaced = warmProblem.PlaceItem({
        0U,
        jaguar::Transform2d::FromRotationTranslation(0.0, {1.003, 1.01})});
    if (!Expect(widthSet.HasValue() && itemPlaced.HasValue(),
                output,
                "Rust TimeBased curve warm-start layout must be constructible"))
    {
        return false;
    }

    sparrow::SparrowConfig config;
    config.seed = 3U;
    config.warmStart = warmProblem.Save();
    config.exploration.timeLimit = std::chrono::milliseconds{0};
    config.compression.timeLimit = std::chrono::seconds{2};
    config.compression.decayStrategy =
        sparrow::ShrinkDecayStrategy::SHRINK_DECAY_TIME_BASED;
    config.compression.shrinkMin = 0.02;
    config.compression.shrinkMax = 0.08;
    config.compression.separator.noImprovementLimit = 0U;
    config.compression.separator.strikeLimit = 1U;
    config.compression.separator.nWorkers = 1U;
    config.compression.separator.nContainerSamples = 8U;
    config.compression.separator.nFocussedSamples = 0U;
    config.compression.separator.nCoordinateDescents = 1U;

    sparrow::OptimizationTerminator terminator(config.terminator);
    terminator.Start();
    TimeBasedCurveListener listener(terminator);
    const auto actual = sparrow::Optimize(
        instance.Value(), config, terminator, &listener);
    if (!Expect(actual.HasValue()
                    && actual.Value().metrics.feasible
                    && actual.Value().metrics.placedItemCount == 1U
                    && listener.steps.size() == 5U
                    && terminator.ShouldTerminate(0U),
                output,
                "positive-time TimeBased compression must emit five controlled shrink steps"))
    {
        return false;
    }

    std::ifstream referenceFile;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/CompressionTimeBasedPositiveTimeoutRustReference.tsv",
             "../SparrowCore/Tests/Fixtures/CompressionTimeBasedPositiveTimeoutRustReference.tsv",
             "../JaguarRun/SparrowCore/Tests/Fixtures/CompressionTimeBasedPositiveTimeoutRustReference.tsv",
             "..\\SparrowCore\\Tests\\Fixtures\\CompressionTimeBasedPositiveTimeoutRustReference.tsv"})
    {
        referenceFile.open(path);
        if (referenceFile.is_open())
        {
            break;
        }
        referenceFile.clear();
    }
    if (!Expect(referenceFile.is_open(), output,
                "Rust TimeBased curve reference fixture cannot be opened"))
    {
        return false;
    }

    std::size_t expectedSampleCount = 0U;
    double maxStep = 0.0;
    double minStep = 0.0;
    double timeLimitSeconds = 0.0;
    double slopeTolerance = 0.0;
    std::string line;
    bool referenceIsValid = true;
    while (std::getline(referenceFile, line))
    {
        if (line.empty() || line.front() == '#')
        {
            continue;
        }
        std::istringstream row(line);
        std::string kind;
        std::string extra;
        if (!(row >> kind >> expectedSampleCount >> maxStep >> minStep
                  >> timeLimitSeconds >> slopeTolerance)
            || kind != "curve"
            || (row >> extra))
        {
            referenceIsValid = false;
            break;
        }
    }
    bool curveMatches = referenceIsValid && expectedSampleCount == 5U
        && listener.steps.size() == expectedSampleCount
        && listener.elapsedSeconds.size() == expectedSampleCount
        && timeLimitSeconds > 0.0 && maxStep > minStep
        && slopeTolerance > 0.0
        && std::abs(listener.steps.front() - maxStep) <= 0.001;
    const double expectedSlope = (minStep - maxStep) / timeLimitSeconds;
    std::vector<double> observedSlopes;
    for (std::size_t index = 1U;
         curveMatches && index < listener.steps.size();
         ++index)
    {
        const double elapsed = listener.elapsedSeconds[index]
            - listener.elapsedSeconds[index - 1U];
        if (elapsed <= 0.0 || listener.steps[index] >= listener.steps[index - 1U])
        {
            curveMatches = false;
            break;
        }
        const double slope = (listener.steps[index]
                              - listener.steps[index - 1U])
            / elapsed;
        observedSlopes.push_back(slope);
        curveMatches = std::abs(slope - expectedSlope) <= slopeTolerance;
    }
    if (!curveMatches)
    {
        output << "TimeBased curve samples (elapsed, step):" << '\n';
        for (std::size_t index = 0U; index < listener.steps.size(); ++index)
        {
            output << index << ": " << listener.elapsedSeconds[index] << ", "
                   << listener.steps[index] << '\n';
        }
        output << "expected slope=" << expectedSlope << " observed slopes=";
        for (const double slope : observedSlopes)
            output << slope << ' ';
        output << '\n';
    }
    return Expect(curveMatches,
                  output,
                  "positive-time TimeBased shrink curve must match the Rust linear rate over measured elapsed time");
}

bool TestOptimizeCompressionMatchesRustReference(std::ostream& output)
{
    std::ifstream instanceFile;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/RustSppOptimizeCompressSingleItem.json",
             "../SparrowCore/Tests/Fixtures/RustSppOptimizeCompressSingleItem.json",
             "../JaguarRun/SparrowCore/Tests/Fixtures/RustSppOptimizeCompressSingleItem.json",
             "..\\SparrowCore\\Tests\\Fixtures\\RustSppOptimizeCompressSingleItem.json"})
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
                "Rust optimize reference input cannot be opened"))
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
                "Rust optimize reference input must import"))
    {
        return false;
    }

    auto problemResult = jaguar::SPProblem::Create(instance.Value());
    if (!Expect(problemResult.HasValue(),
                output,
                "Rust optimize warm-start problem must be created"))
    {
        return false;
    }
    jaguar::SPProblem warmProblem = std::move(problemResult.Value());
    const auto widthSet = warmProblem.ChangeStripWidth(4.0);
    const auto itemPlaced = warmProblem.PlaceItem({
        0U,
        jaguar::Transform2d::FromRotationTranslation(0.0, {1.003, 1.01})});
    if (!Expect(widthSet.HasValue() && itemPlaced.HasValue(),
                output,
                "Rust optimize warm-start layout must be constructible"))
    {
        return false;
    }

    sparrow::SparrowConfig config;
    config.seed = 3U;
    const jaguar::SPSolution warmStart = warmProblem.Save();
    config.warmStart = warmStart;
    config.exploration.shrinkStep = 0.2;
    config.exploration.timeLimit = std::chrono::seconds{30};
  config.exploration.separator.noImprovementLimit = 0U;
    config.exploration.separator.strikeLimit = 1U;
    config.exploration.separator.nWorkers = 1U;
    config.exploration.separator.nContainerSamples = 8U;
    config.exploration.separator.nFocussedSamples = 0U;
    config.exploration.separator.nCoordinateDescents = 1U;
    config.compression.decayStrategy =
        sparrow::ShrinkDecayStrategy::SHRINK_DECAY_FAILURE_BASED;
    config.compression.shrinkMin = 0.01;
    config.compression.shrinkMax = 0.01;
    config.compression.failureDecay = 0.5;
    config.compression.timeLimit = std::chrono::seconds{30};
  config.compression.separator.noImprovementLimit = 0U;
    config.compression.separator.strikeLimit = 1U;
    config.compression.separator.nWorkers = 1U;
    config.compression.separator.nContainerSamples = 8U;
    config.compression.separator.nFocussedSamples = 0U;
    config.compression.separator.nCoordinateDescents = 1U;
    config.terminator.maxDuration = std::chrono::seconds{30};

    RustOptimizeReferenceListener listener;
    const auto actual = sparrow::Optimize(instance.Value(), config, &listener);
    if (!actual.HasValue())
    {
        output << "Rust reference scenario Optimize error: "
               << actual.GetError().message << '\n';
    }
    if (!Expect(actual.HasValue(),
                output,
                "SparrowCore optimize warm-start path must complete"))
    {
        return false;
    }

    std::ifstream referenceFile;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/OptimizeCompressRustReference.tsv",
             "../SparrowCore/Tests/Fixtures/OptimizeCompressRustReference.tsv",
             "../JaguarRun/SparrowCore/Tests/Fixtures/OptimizeCompressRustReference.tsv",
             "..\\SparrowCore\\Tests\\Fixtures\\OptimizeCompressRustReference.tsv"})
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
                "Rust optimize reference fixture cannot be opened"))
    {
        return false;
    }

    std::string line;
    std::string caseName;
    std::uint64_t seed = 0U;
    std::size_t placedCount = 0U;
    std::size_t remaining = 0U;
    double expectedWidth = 0.0;
    double expectedDensity = 0.0;
    double expectedRotation = 0.0;
    double expectedX = 0.0;
    double expectedY = 0.0;
    std::string expectedPhases;
    std::string expectedReports;
    std::size_t expectedCompressionSteps = 0U;
    bool referenceFound = false;
    while (std::getline(referenceFile, line))
    {
        if (line.empty() || line.front() == '#')
        {
            continue;
        }
        std::istringstream row(line);
        if (row >> caseName >> seed >> placedCount >> remaining
                >> expectedWidth >> expectedDensity >> expectedRotation
                >> expectedX >> expectedY >> expectedPhases
                >> expectedReports >> expectedCompressionSteps
            && caseName == "optimize_compress_single_warm")
        {
            referenceFound = true;
            break;
        }
    }
    if (!Expect(referenceFound,
                output,
                "Rust optimize reference row is missing or malformed"))
    {
        return false;
    }

    std::vector<std::string> expectedPhaseList;
    std::istringstream phaseStream(expectedPhases);
    std::string event;
    while (std::getline(phaseStream, event, ','))
    {
        expectedPhaseList.push_back(event);
    }
    std::vector<std::string> expectedReportList;
    std::istringstream reportStream(expectedReports);
    while (std::getline(reportStream, event, ','))
    {
        expectedReportList.push_back(event);
    }

    const auto& result = actual.Value();
    const auto& placements = result.solution.layoutSnapshot.placements;
    bool placementMatches = placements.size() == placedCount;
    if (placementMatches && !placements.empty())
    {
        const auto& placement = placements.front();
        const auto& transform = placement.placedItem.Transform();
        placementMatches = placement.placedItem.GetItemIndex() == 0U
            && std::abs(transform.rotationRad - expectedRotation) < 5.0e-7
            && std::abs(transform.translation.x - expectedX) < 5.0e-6
            && std::abs(transform.translation.y - expectedY) < 5.0e-6;
    }
    const bool metricsMatch = seed == config.seed
        && result.metrics.placedItemCount == placedCount
        && result.metrics.remainingDemand == remaining
        && std::abs(result.metrics.stripWidth - expectedWidth) < 5.0e-7
        && std::abs(result.metrics.density - expectedDensity) < 5.0e-7
        && result.metrics.feasible;
    const bool compressionStepsMatch =
        listener.compressionSteps.size() == expectedCompressionSteps
        && std::all_of(listener.compressionSteps.begin(),
                       listener.compressionSteps.end(),
                       [](double step) { return std::abs(step - 0.01) < 1.0e-6; });
    const bool eventsMatch = listener.phases == expectedPhaseList
        && listener.reports == expectedReportList;
    if (!metricsMatch || !placementMatches || !compressionStepsMatch || !eventsMatch)
    {
        output << std::setprecision(17)
               << "Rust expected width=" << expectedWidth
               << " density=" << expectedDensity << " position=(" << expectedX
               << ',' << expectedY << ") phases=" << expectedPhases
               << " reports=" << expectedReports << " compression_steps="
               << expectedCompressionSteps << '\n'
               << "C++ actual width=" << result.metrics.stripWidth
               << " density=" << result.metrics.density << " position=("
               << (placements.empty() ? 0.0
                                      : placements.front().placedItem.Transform().translation.x)
               << ','
               << (placements.empty() ? 0.0
                                      : placements.front().placedItem.Transform().translation.y)
               << ") phases=";
        for (const auto& phase : listener.phases)
        {
            output << phase << ',';
        }
        output << " reports=";
        for (const auto& report : listener.reports)
        {
            output << report << ',';
        }
        output << " compression_steps=" << listener.compressionSteps.size() << '\n';
    }
    return Expect(metricsMatch && placementMatches && compressionStepsMatch
                      && eventsMatch,
                  output,
                  "optimize and compression result/events must match Rust reference");
}

bool TestCompressionWorkerMoveRetryMatchesRustReference(
    std::ostream& output,
    const std::string& fixtureName,
    const std::string& expectedCaseName,
    std::size_t workerCount)
{
    std::ifstream referenceFile;
    const std::string fixtureRelativePath =
        "SparrowCore/Tests/Fixtures/" + fixtureName;
    for (const std::string& path : {
             fixtureRelativePath,
             "../" + fixtureRelativePath,
             "../JaguarRun/" + fixtureRelativePath,
             "..\\" + fixtureRelativePath})
    {
        referenceFile.open(path);
        if (referenceFile.is_open())
            break;
        referenceFile.clear();
    }
    if (!Expect(referenceFile.is_open(), output,
                "Rust compression worker-move reference must be readable"))
    {
        return false;
    }

    std::uint64_t expectedSeed = 0U;
    double expectedWidth = 0.0;
    double expectedDensity = 0.0;
    std::size_t expectedPlacementCount = 0U;
    std::size_t expectedStepCount = 0U;
    bool expectedFeasible = false;
    std::vector<double> expectedSteps;
    std::vector<std::string> expectedReports;
    std::vector<RustCompressionProgressReference> expectedProgress;
    std::vector<RustCompressionResultReference> expectedResults;
    std::vector<RustCompressionPlacementReference> expectedPlacements;
    std::vector<RustCompressionReportStateReference> expectedReportStates;
    std::vector<std::string> expectedEventOrder;
    bool referenceFound = false;
    std::string line;
    while (std::getline(referenceFile, line))
    {
        if (line.empty() || line.front() == '#')
            continue;
        std::istringstream row(line);
        std::string kind;
        row >> kind;
        if (kind == "case")
        {
            std::string caseName;
            std::string reportList;
            if (!(row >> caseName >> expectedSeed >> expectedWidth
                      >> expectedDensity >> expectedPlacementCount
                      >> expectedStepCount >> expectedFeasible >> reportList)
                || caseName != expectedCaseName)
            {
                return Expect(false, output,
                              "Rust compression case row is malformed");
            }
            std::istringstream reports(reportList);
            std::string report;
            while (std::getline(reports, report, ','))
                expectedReports.push_back(report);
            referenceFound = true;
        }
        else if (kind == "step")
        {
            double step = 0.0;
            if (!(row >> step))
                return Expect(false, output,
                              "Rust compression step row is malformed");
            expectedSteps.push_back(step);
        }
        else if (kind == "report_state")
        {
            RustCompressionReportStateReference state;
            double firstX = 0.0;
            double firstY = 0.0;
            double secondX = 0.0;
            double secondY = 0.0;
            if (!(row >> state.index >> state.totalLoss >> firstX >> firstY
                      >> secondX >> secondY))
            {
                return Expect(false, output,
                              "Rust compression report state row is malformed");
            }
            state.positions = {{firstX, firstY}, {secondX, secondY}};
            expectedReportStates.push_back(std::move(state));
        }
        else if (kind == "progress")
        {
            RustCompressionProgressReference progress;
            if (!(row >> progress.iteration >> progress.stripWidth
                      >> progress.density >> progress.minimumLoss))
            {
                return Expect(false, output,
                              "Rust compression progress row is malformed");
            }
            expectedProgress.push_back(progress);
            expectedEventOrder.emplace_back("progress");
        }
        else if (kind == "result")
        {
            unsigned int success = 0U;
            RustCompressionResultReference result;
            if (!(row >> success >> result.totalEvaluations
                      >> result.totalMoves >> result.iterations)
                || success > 1U)
            {
                return Expect(false, output,
                              "Rust compression result row is malformed");
            }
            result.success = success != 0U;
            expectedResults.push_back(result);
            expectedEventOrder.emplace_back("result");
        }
        else if (kind == "placement")
        {
            RustCompressionPlacementReference placement;
            if (!(row >> placement.rank >> placement.itemIndex
                      >> placement.rotation >> placement.x >> placement.y))
            {
                return Expect(false, output,
                              "Rust compression placement row is malformed");
            }
            expectedPlacements.push_back(placement);
        }
        else
        {
            return Expect(false, output,
                          "unknown Rust compression reference row");
        }
    }
    if (!Expect(referenceFound
                    && expectedSteps.size() == expectedStepCount
                    && expectedPlacements.size() == expectedPlacementCount
                    && !expectedProgress.empty()
                    && !expectedResults.empty(),
                output,
                "Rust compression worker-move reference is incomplete"))
    {
        return false;
    }

    std::ifstream instanceFile;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/RustSppCompressWorkerMove.json",
             "../SparrowCore/Tests/Fixtures/RustSppCompressWorkerMove.json",
             "../JaguarRun/SparrowCore/Tests/Fixtures/RustSppCompressWorkerMove.json",
             "..\\SparrowCore\\Tests\\Fixtures\\RustSppCompressWorkerMove.json"})
    {
        instanceFile.open(path);
        if (instanceFile.is_open())
            break;
        instanceFile.clear();
    }
    if (!Expect(instanceFile.is_open(), output,
                "Rust compression worker-move input must be readable"))
    {
        return false;
    }
    const std::string json{
        std::istreambuf_iterator<char>(instanceFile),
        std::istreambuf_iterator<char>()};
    const jaguar::Importer importer(sparrow::MakeRustDefaultImporterConfig());
    const auto instance = importer.ImportSPInstanceJson(json);
    if (!Expect(instance.HasValue(), output,
                "Rust compression worker-move input must import"))
    {
        return false;
    }

    auto problemResult = jaguar::SPProblem::Create(instance.Value());
    if (!Expect(problemResult.HasValue(), output,
                "Rust compression worker-move problem must be created"))
    {
        return false;
    }
    jaguar::SPProblem problem = std::move(problemResult.Value());
    const auto widthSet = problem.ChangeStripWidth(4.2);
    const auto firstPlaced = problem.PlaceItem({
        0U, jaguar::Transform2d::FromRotationTranslation(0.0, {1.05, 0.55})});
    const auto secondPlaced = problem.PlaceItem({
        0U, jaguar::Transform2d::FromRotationTranslation(0.0, {3.15, 0.55})});
    if (!Expect(widthSet.HasValue() && firstPlaced.HasValue()
                    && secondPlaced.HasValue(),
                output,
                "Rust compression worker-move initial layout must be constructible"))
    {
        return false;
    }
    const jaguar::SPSolution initialSolution = problem.Save();
    jaguar::Layout initialLayout(initialSolution.layoutSnapshot.container);
    const auto initialLayoutRestored =
        initialLayout.Restore(initialSolution.layoutSnapshot);
    if (!Expect(initialLayoutRestored.HasValue() && initialLayout.IsFeasible(),
                output,
                "Rust compression worker-move warm start must be feasible"))
    {
        return false;
    }

    sparrow::CompressionConfig config;
    config.shrinkMin = 0.1;
    config.shrinkMax = 0.2;
    config.failureDecay = 0.5;
    config.decayStrategy =
        sparrow::ShrinkDecayStrategy::SHRINK_DECAY_FAILURE_BASED;
    config.timeLimit = std::chrono::seconds{30};
    config.separator.noImprovementLimit = 1U;
    config.separator.strikeLimit = 1U;
    config.separator.nWorkers = workerCount;
    config.separator.nContainerSamples = 8U;
    config.separator.nFocussedSamples = 0U;
    config.separator.nCoordinateDescents = 1U;
    sparrow::OptimizationTerminator terminator({
        0U, std::chrono::seconds{30}});
    terminator.StartPhase(std::chrono::seconds{30});
    CompressionWorkerMoveListener listener;
    const auto actual = sparrow::Compress(
        instance.Value(), initialSolution, config, expectedSeed,
        terminator, &listener);
    if (!actual.HasValue())
    {
        output << "Rust compression worker-move scenario error: "
               << actual.GetError().message << '\n';
    }
    if (!Expect(actual.HasValue(), output,
                "SparrowCore compression worker-move scenario must complete"))
    {
        return false;
    }

    const auto& result = actual.Value();
    struct PlacementValue
    {
        std::size_t itemIndex = 0U;
        double rotation = 0.0;
        double x = 0.0;
        double y = 0.0;
    };
    std::vector<PlacementValue> placements;
    placements.reserve(result.solution.layoutSnapshot.placements.size());
    for (const auto& placement : result.solution.layoutSnapshot.placements)
    {
        const auto& transform = placement.placedItem.Transform();
        placements.push_back({placement.placedItem.GetItemIndex(),
                              transform.rotationRad,
                              transform.translation.x,
                              transform.translation.y});
    }
    std::sort(placements.begin(), placements.end(), [](const auto& left,
                                                       const auto& right) {
        return left.x < right.x;
    });

    bool placementsMatch = placements.size() == expectedPlacements.size();
    if (placementsMatch)
    {
        for (std::size_t rank = 0U; rank < placements.size(); ++rank)
        {
            const auto& actualPlacement = placements[rank];
            const auto& expectedPlacement = expectedPlacements[rank];
            if (expectedPlacement.rank != rank
                || actualPlacement.itemIndex != expectedPlacement.itemIndex
                || std::abs(actualPlacement.rotation - expectedPlacement.rotation)
                       >= 5.0e-7
                || std::abs(actualPlacement.x - expectedPlacement.x) >= 5.0e-6
                || std::abs(actualPlacement.y - expectedPlacement.y) >= 5.0e-6)
            {
                placementsMatch = false;
                break;
            }
        }
    }

    const bool metricsMatch = result.metrics.seed == expectedSeed
        && result.metrics.feasible == expectedFeasible
        && result.metrics.placedItemCount == expectedPlacementCount
        && result.metrics.remainingDemand == 0U
        && std::abs(result.metrics.stripWidth - expectedWidth) < 1.0e-6
        && std::abs(result.metrics.density - expectedDensity) < 1.0e-6;
    const bool stepsMatch = listener.compressionSteps.size() == expectedSteps.size()
        && std::equal(listener.compressionSteps.begin(),
                      listener.compressionSteps.end(),
                      expectedSteps.begin(),
                      [](double actualStep, double expectedStep) {
                          return std::abs(actualStep - expectedStep) < 1.0e-7;
                      });
    bool reportsMatch = listener.reports == expectedReports;
    bool progressMatch =
        listener.separationProgress.size() == expectedProgress.size();
    if (progressMatch)
    {
        for (std::size_t index = 0U;
             index < expectedProgress.size();
             ++index)
        {
            const auto& actualProgress = listener.separationProgress[index];
            const auto& expected = expectedProgress[index];
            if (actualProgress.iteration != expected.iteration
                || std::abs(actualProgress.stripWidth - expected.stripWidth)
                       >= 1.0e-6
                || std::abs(actualProgress.density - expected.density) >= 5.0e-6
                || std::abs(actualProgress.minimumLoss - expected.minimumLoss)
                       >= 1.0e-4)
            {
                progressMatch = false;
                break;
            }
        }
    }
    bool resultsMatch = listener.separationResults.size() == expectedResults.size();
    if (resultsMatch)
    {
        for (std::size_t index = 0U; index < expectedResults.size(); ++index)
        {
            const auto& actualResult = listener.separationResults[index];
            const auto& expected = expectedResults[index];
            if (actualResult.success != expected.success
                || actualResult.totalEvaluations != expected.totalEvaluations
                || actualResult.totalMoves != expected.totalMoves
                || actualResult.iterations != expected.iterations
                || actualResult.totalMoves == 0U
                || actualResult.success)
            {
                resultsMatch = false;
                break;
            }
        }
    }
    bool reportStatesMatch =
        listener.reportTransforms.size() == expectedReportStates.size()
        && listener.reportLosses.size() == expectedReportStates.size();
    if (reportStatesMatch)
    {
        for (std::size_t index = 0U; index < expectedReportStates.size(); ++index)
        {
            const auto& expected = expectedReportStates[index];
            const auto& transforms = listener.reportTransforms[index];
            if (expected.index != index
                || transforms.size() != expected.positions.size()
                || std::abs(listener.reportLosses[index] - expected.totalLoss)
                       >= 1.0e-4)
            {
                reportStatesMatch = false;
                break;
            }
            for (std::size_t position = 0U;
                 position < transforms.size();
                 ++position)
            {
                if (std::abs(transforms[position].translation.x
                             - expected.positions[position].first) >= 5.0e-6
                    || std::abs(transforms[position].translation.y
                                - expected.positions[position].second) >= 5.0e-6)
                {
                    reportStatesMatch = false;
                    break;
                }
            }
            if (!reportStatesMatch)
                break;
        }
    }
    const bool eventOrderMatches = listener.eventOrder == expectedEventOrder;
    if (!metricsMatch || !placementsMatch || !stepsMatch || !reportsMatch
        || !progressMatch || !resultsMatch || !reportStatesMatch
        || !eventOrderMatches)
    {
        output << std::setprecision(17)
               << "Rust expected final width=" << expectedWidth
               << " density=" << expectedDensity
               << " steps=" << expectedSteps.size()
               << " reports=" << expectedReports.size()
               << " progress=" << expectedProgress.size()
               << " results=" << expectedResults.size() << '\n'
               << "C++ actual final width=" << result.metrics.stripWidth
               << " density=" << result.metrics.density
               << " steps=" << listener.compressionSteps.size()
               << " reports=" << listener.reports.size()
               << " progress=" << listener.separationProgress.size()
               << " results=" << listener.separationResults.size() << '\n';
        output << "match flags metrics=" << metricsMatch
               << " placements=" << placementsMatch
               << " steps=" << stepsMatch << " reports=" << reportsMatch
               << " progress=" << progressMatch << " results=" << resultsMatch
               << " report_states=" << reportStatesMatch
               << " order=" << eventOrderMatches << '\n';
        for (std::size_t index = 0U; index < placements.size(); ++index)
            output << "final placement[" << index << "] item="
                   << placements[index].itemIndex << " rotation="
                   << placements[index].rotation << " xy=(" << placements[index].x
                   << ',' << placements[index].y << ")\n";
        for (const auto& separation : listener.separationResults)
        {
            output << "C++ attempt success=" << separation.success
                   << " evaluations=" << separation.totalEvaluations
                   << " moves=" << separation.totalMoves
                   << " iterations=" << separation.iterations << '\n';
        }
        for (std::size_t index = 0U;
             index < std::min(expectedProgress.size(),
                              listener.separationProgress.size());
             ++index)
        {
            const auto& expected = expectedProgress[index];
            const auto& actualProgress = listener.separationProgress[index];
            output << "progress[" << index << "] Rust(iter="
                   << expected.iteration << ", width=" << expected.stripWidth
                   << ", density=" << expected.density << ", loss="
                   << expected.minimumLoss << ") C++(iter="
                   << actualProgress.iteration << ", width="
                   << actualProgress.stripWidth << ", density="
                   << actualProgress.density << ", loss="
                   << actualProgress.minimumLoss << ")\n";
        }
        output << "Rust reports=";
        for (const auto& report : expectedReports)
            output << report << ',';
        output << " C++ reports=";
        for (const auto& report : listener.reports)
            output << report << ',';
        output << '\n';
        for (std::size_t index = 0U;
             index < listener.reportTransforms.size();
             ++index)
        {
            output << "C++ report[" << index << "] transforms=";
            for (const auto& transform : listener.reportTransforms[index])
                output << '(' << transform.translation.x << ','
                       << transform.translation.y << ','
                       << transform.rotationRad << ')';
            if (index < listener.reportLosses.size())
                output << " loss=" << listener.reportLosses[index];
            output << '\n';
        }
    }
    return Expect(metricsMatch && placementsMatch && stepsMatch
                      && reportsMatch && progressMatch && resultsMatch
                      && reportStatesMatch && eventOrderMatches,
                  output,
                  "compression failed-retry worker trace must match Rust reference");
}

bool TestTerminationHandoffMatchesRustReference(std::ostream& output)
{
    std::ifstream referenceFile;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/OptimizerTerminationHandoffRustReference.tsv",
             "../SparrowCore/Tests/Fixtures/OptimizerTerminationHandoffRustReference.tsv",
             "../JaguarRun/SparrowCore/Tests/Fixtures/OptimizerTerminationHandoffRustReference.tsv",
             "..\\SparrowCore\\Tests\\Fixtures\\OptimizerTerminationHandoffRustReference.tsv"})
    {
        referenceFile.open(path);
        if (referenceFile.is_open())
            break;
        referenceFile.clear();
    }
    if (!Expect(referenceFile.is_open(),
                output,
                "Rust termination handoff reference must be readable"))
    {
        return false;
    }

    std::vector<ColdStartTraceEvent> expectedEvents;
    std::string line;
    while (std::getline(referenceFile, line))
    {
        if (line.empty() || line.front() == '#')
            continue;
        std::istringstream row(line);
        ColdStartTraceEvent event;
        row >> event.type >> event.label;
        if (event.type == "solution")
        {
            double width = 0.0;
            double density = 0.0;
            double itemCount = 0.0;
            double feasible = 0.0;
            row >> width >> density >> itemCount >> feasible;
            event.values = {width, density, itemCount, feasible};
        }
        else if (event.type == "placement")
        {
            double itemIndex = 0.0;
            double rotation = 0.0;
            double x = 0.0;
            double y = 0.0;
            row >> itemIndex >> rotation >> x >> y;
            event.values = {itemIndex, rotation, x, y};
        }
        else if (event.type == "step")
        {
            event.values = {std::stod(event.label)};
            event.label.clear();
        }
        else if (event.type == "report")
        {
            // The report label is the complete payload.
        }
        else if (event.type == "progress")
        {
            double iteration = std::stod(event.label);
            double width = 0.0;
            double density = 0.0;
            double minimumLoss = 0.0;
            row >> width >> density >> minimumLoss;
            event.values = {iteration, width, density, minimumLoss};
            event.label.clear();
        }
        else if (event.type == "result")
        {
            double success = std::stod(event.label);
            double evaluations = 0.0;
            double moves = 0.0;
            double iterations = 0.0;
            row >> evaluations >> moves >> iterations;
            event.values = {success, evaluations, moves, iterations};
            event.label.clear();
        }
        else
        {
            return Expect(false,
                          output,
                          "Rust termination handoff reference has an unknown row");
        }
        if (!Expect(static_cast<bool>(row),
                    output,
                    "Rust termination handoff reference contains a malformed row"))
        {
            return false;
        }
        expectedEvents.push_back(std::move(event));
    }

    std::ifstream instanceFile;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/ExplorationPool.json",
             "../SparrowCore/Tests/Fixtures/ExplorationPool.json",
             "../JaguarRun/SparrowCore/Tests/Fixtures/ExplorationPool.json",
             "..\\SparrowCore\\Tests\\Fixtures\\ExplorationPool.json"})
    {
        instanceFile.open(path);
        if (instanceFile.is_open())
            break;
        instanceFile.clear();
    }
    if (!Expect(instanceFile.is_open(),
                output,
                "Rust termination handoff input must be readable"))
    {
        return false;
    }
    const std::string instanceJson{
        std::istreambuf_iterator<char>(instanceFile),
        std::istreambuf_iterator<char>()};
    const jaguar::Importer importer(sparrow::MakeRustDefaultImporterConfig());
    const auto instance = importer.ImportSPInstanceJson(instanceJson);
    if (!Expect(instance.HasValue(),
                output,
                "Rust termination handoff input must import"))
    {
        return false;
    }

    auto problemResult = jaguar::SPProblem::Create(instance.Value());
    if (!Expect(problemResult.HasValue(),
                output,
                "Rust termination handoff problem must be created"))
    {
        return false;
    }
    jaguar::SPProblem problem = std::move(problemResult.Value());
    if (!Expect(problem.ChangeStripWidth(8.0).HasValue()
                    && problem.PlaceItem({0U, {0.0, {2.6, 2.0}}}).HasValue()
                    && problem.PlaceItem({1U, {0.0, {6.5, 2.0}}}).HasValue(),
                output,
                "Rust termination handoff warm start must be constructible"))
    {
        return false;
    }
    const jaguar::SPSolution initialSolution = problem.Save();

    sparrow::ExplorationConfig exploration;
    exploration.shrinkStep = 0.1875;
    exploration.maxConsecutiveFailures = std::nullopt;
    exploration.solutionPoolDistributionStddev = 1.0;
    exploration.timeLimit = std::chrono::seconds{30};
    exploration.separator.strikeLimit = 0U;
    exploration.separator.nWorkers = 1U;
    sparrow::OptimizationTerminator terminator({
        0U, std::chrono::seconds{30}});
    terminator.Start();
    terminator.StartPhase(exploration.timeLimit);
    TerminationHandoffListener listener(terminator);
    const auto explored = sparrow::Explore(
        instance.Value(), initialSolution, exploration, 43U,
        terminator, &listener);
    if (!Expect(explored.HasValue(),
                output,
                "Rust termination handoff exploration must complete"))
    {
        return false;
    }
    const auto& bestSolution = explored.Value().solution;
    const auto& separatorState = explored.Value().separatorState;
    if (!Expect(terminator.ShouldTerminate(0U)
                    && std::abs(bestSolution.StripWidth() - 8.0) < 1.0e-6
                    && std::abs(separatorState.StripWidth() - 6.5) < 1.0e-6
                    && !SameSolution(bestSolution, separatorState),
                output,
                "exploration stop must retain the distinct post-disruption separator state"))
    {
        return false;
    }

    sparrow::CompressionConfig compression;
    compression.shrinkMin = 0.01;
    compression.shrinkMax = 0.05;
    compression.failureDecay = 0.5;
    compression.decayStrategy =
        sparrow::ShrinkDecayStrategy::SHRINK_DECAY_FAILURE_BASED;
    compression.timeLimit = std::chrono::seconds{30};
    compression.separator.noImprovementLimit = 0U;
    compression.separator.strikeLimit = 1U;
    compression.separator.nWorkers = 1U;
    compression.separator.nContainerSamples = 8U;
    compression.separator.nFocussedSamples = 0U;
    compression.separator.nCoordinateDescents = 1U;
    terminator.StartPhase(compression.timeLimit);
    listener.compressionPhase = true;
    const auto compressed = sparrow::Compress(
        instance.Value(), separatorState, bestSolution, compression, 42U,
        terminator, &listener);
    if (!Expect(compressed.HasValue(),
                output,
                "Rust termination handoff compression must complete"))
    {
        return false;
    }

    std::vector<ColdStartTraceEvent> actualEvents;
    const auto appendSolution = [&actualEvents](
                                    const std::string& label,
                                    const jaguar::SPSolution& solution) {
        jaguar::Layout layout(solution.layoutSnapshot.container);
        const auto restored = layout.Restore(solution.layoutSnapshot);
        actualEvents.push_back({
            "solution",
            label,
            {solution.StripWidth(),
             solution.Density(),
             static_cast<double>(solution.layoutSnapshot.placements.size()),
             restored.HasValue() && layout.IsFeasible() ? 1.0 : 0.0}});
        struct PlacementObservation
        {
            std::size_t itemIndex;
            double rotation;
            double x;
            double y;
        };
        std::vector<PlacementObservation> placements;
        for (const auto& placement : solution.layoutSnapshot.placements)
        {
            const auto& transform = placement.placedItem.Transform();
            placements.push_back({placement.placedItem.GetItemIndex(),
                                  transform.rotationRad,
                                  transform.translation.x,
                                  transform.translation.y});
        }
        std::sort(placements.begin(), placements.end(), [](const auto& left,
                                                           const auto& right) {
            return left.itemIndex < right.itemIndex;
        });
        for (const auto& placement : placements)
        {
            actualEvents.push_back({
                "placement",
                label,
                {static_cast<double>(placement.itemIndex),
                 placement.rotation,
                 placement.x,
                 placement.y}});
        }
    };
    appendSolution("best", bestSolution);
    appendSolution("separator", separatorState);
    appendSolution("final", compressed.Value().solution);
    for (const double step : listener.compressionSteps)
        actualEvents.push_back({"step", {}, {step}});
    for (const auto& report : listener.reports)
        actualEvents.push_back({"report", report, {}});
    for (const auto& progress : listener.separationProgress)
    {
        actualEvents.push_back({
            "progress",
            {},
            {static_cast<double>(progress.iteration),
             progress.stripWidth,
             progress.density,
             progress.minimumLoss}});
    }
    for (const auto& result : listener.separationResults)
    {
        actualEvents.push_back({
            "result",
            {},
            {result.success ? 1.0 : 0.0,
             static_cast<double>(result.totalEvaluations),
             static_cast<double>(result.totalMoves),
             static_cast<double>(result.iterations)}});
    }

    bool traceMatches = actualEvents.size() == expectedEvents.size();
    for (std::size_t eventIndex = 0U;
         traceMatches && eventIndex < actualEvents.size();
         ++eventIndex)
    {
        const auto& actual = actualEvents[eventIndex];
        const auto& expected = expectedEvents[eventIndex];
        if (actual.type != expected.type || actual.label != expected.label
            || actual.values.size() != expected.values.size())
        {
            traceMatches = false;
            break;
        }
        for (std::size_t valueIndex = 0U;
             valueIndex < actual.values.size();
             ++valueIndex)
        {
            double tolerance = 1.0e-5;
            if (actual.type == "solution" && valueIndex >= 2U)
                tolerance = 0.0;
            else if (actual.type == "placement" && valueIndex == 0U)
                tolerance = 0.0;
            else if (actual.type == "step")
                tolerance = 1.0e-7;
            else if (actual.type == "progress")
            {
                if (valueIndex == 0U)
                    tolerance = 0.0;
                else if (valueIndex == 3U)
                    tolerance = 1.0e-4;
            }
            else if (actual.type == "result")
                tolerance = 0.0;
            if (std::abs(actual.values[valueIndex]
                         - expected.values[valueIndex]) > tolerance)
            {
                traceMatches = false;
                break;
            }
        }
    }
    if (!traceMatches)
    {
        output << "Rust termination handoff events=" << expectedEvents.size()
               << " C++ events=" << actualEvents.size() << '\n';
        for (std::size_t eventIndex = 0U;
             eventIndex < std::min(actualEvents.size(), expectedEvents.size());
             ++eventIndex)
        {
            const auto& actual = actualEvents[eventIndex];
            const auto& expected = expectedEvents[eventIndex];
            output << eventIndex << " Rust " << expected.type << ':'
                   << expected.label << " C++ " << actual.type << ':'
                   << actual.label << '\n';
        }
    }
    return Expect(traceMatches,
                  output,
                  "termination handoff state and compression trace must match Rust");
}

bool TestDiscreteRotationLegalizationMatchesRust(std::ostream& output)
{
    const double radiansPerDegree = std::acos(-1.0) / 180.0;
    jaguar::SurrogateConfig surrogateConfig{};
    surrogateConfig.poleLimits = {{{3U, 0.0}, {0U, 0.0}, {0U, 0.0}}};
    const jaguar::ShapeModifyConfig shapeModifyConfig{};
    const jaguar::Item fixedRotationItem = MakeCenteredRectangleItem(
        0U,
        2.0,
        1.0,
        surrogateConfig,
        shapeModifyConfig,
        {jaguar::RotationMode::Discrete, {359.0 * radiansPerDegree}});
    const jaguar::Item choiceItem = MakeCenteredRectangleItem(
        1U,
        2.0,
        1.0,
        surrogateConfig,
        shapeModifyConfig,
        {jaguar::RotationMode::Discrete,
         {1.0 * radiansPerDegree, 350.0 * radiansPerDegree}});
    const auto instance = jaguar::SPInstance::Create(
        {fixedRotationItem, choiceItem},
        {1U, 1U},
        4.0,
        jaguar::CDEConfig{},
        shapeModifyConfig);
    if (!Expect(instance.HasValue(),
                output,
                "Rust rotation legalization instance creation failed"))
    {
        return false;
    }
    auto problemResult = jaguar::SPProblem::Create(instance.Value());
    if (!Expect(problemResult.HasValue(),
                output,
                "Rust rotation legalization problem creation failed"))
    {
        return false;
    }
    jaguar::SPProblem problem = std::move(problemResult.Value());
    if (!Expect(problem.ChangeStripWidth(4.0).HasValue()
                    && problem.PlaceItem({
                           0U,
                           jaguar::Transform2d::FromRotationTranslation(
                               359.0 * radiansPerDegree, {1.0, 1.0})})
                           .HasValue()
                    && problem.PlaceItem({
                           1U,
                           jaguar::Transform2d::FromRotationTranslation(
                               350.0 * radiansPerDegree, {1.0, 1.0})})
                           .HasValue(),
                output,
                "Rust rotation legalization initial placements failed"))
    {
        return false;
    }

    sparrow::ExplorationConfig config;
    config.maxIterations = 2U;
    config.maxConsecutiveFailures = 2U;
    config.separator.strikeLimit = 0U;
    sparrow::OptimizationTerminator terminator({
        32U,
        std::chrono::milliseconds{2000}});
    const auto result = sparrow::Explore(
        instance.Value(),
        problem.Save(),
        config,
        42U,
        terminator);
    if (!Expect(result.HasValue(),
                output,
                "Rust rotation legalization exploration failed"))
    {
        return false;
    }

    const auto disruptedReport = std::find_if(
        result.Value().history.begin(),
        result.Value().history.end(),
        [](const sparrow::OptimizationSolutionReport& report) {
            return report.phase
                       == sparrow::OptimizationPhase::OPTIMIZATION_PHASE_EXPLORATION
                && report.iteration == 1U
                && report.type
                       == sparrow::OptimizationReportType::OPTIMIZATION_REPORT_EXPLORATION_INFEASIBLE;
        });
    if (!Expect(disruptedReport != result.Value().history.end(),
                output,
                "second infeasible exploration report must expose the disrupted solution"))
    {
        return false;
    }

    double choiceRotation = std::numeric_limits<double>::quiet_NaN();
    for (const auto& placement : disruptedReport->solution.layoutSnapshot.placements)
    {
        if (placement.placedItem.GetItemIndex() == 1U)
        {
            choiceRotation = placement.placedItem.Transform().rotationRad;
            break;
        }
    }
    if (std::abs(choiceRotation - 350.0 * radiansPerDegree) >= 1.0e-6)
    {
        output << std::setprecision(17)
               << "Observed legalized choice rotation (radians): "
               << choiceRotation << ", difference from 350 degrees: "
               << std::abs(choiceRotation - 350.0 * radiansPerDegree)
               << '\n';
        return Expect(false,
                      output,
                      "Rust normalized angle delta must choose 350 degrees over 1 degree from 359 degrees");
    }
    return true;
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

namespace
{
bool TestExploreMinimumWidthInfinityMatchesRust(std::ostream& output)
{
    std::ifstream referenceFile;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/ExplorationMinimumWidthInfinityRustReference.tsv",
             "../SparrowCore/Tests/Fixtures/ExplorationMinimumWidthInfinityRustReference.tsv",
             "../JaguarRun/SparrowCore/Tests/Fixtures/ExplorationMinimumWidthInfinityRustReference.tsv",
             "..\\SparrowCore\\Tests\\Fixtures\\ExplorationMinimumWidthInfinityRustReference.tsv"})
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
                "Rust Explore minimum-width infinity reference cannot be opened"))
    {
        return false;
    }

    std::string referenceLine;
    while (std::getline(referenceFile, referenceLine)
           && (referenceLine.empty() || referenceLine.front() == '#'))
    {
    }
    std::istringstream referenceRow(referenceLine);
    std::string record;
    std::string caseName;
    std::uint32_t expectedMinimumWidthBits = 0U;
    std::uint32_t expectedBestWidthBits = 0U;
    std::uint32_t expectedSeparatorWidthBits = 0U;
    if (!(referenceRow >> record >> caseName >> std::hex
                        >> expectedMinimumWidthBits >> expectedBestWidthBits
                        >> expectedSeparatorWidthBits)
        || record != "case" || caseName != "all_discrete_rotations_too_tall"
        || expectedMinimumWidthBits != 0x7f800000U)
    {
        return Expect(false,
                      output,
                      "malformed Rust Explore minimum-width infinity reference");
    }

    std::ifstream fixture;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/ExplorationMinimumWidthInfinity.json",
             "../SparrowCore/Tests/Fixtures/ExplorationMinimumWidthInfinity.json",
             "../JaguarRun/SparrowCore/Tests/Fixtures/ExplorationMinimumWidthInfinity.json",
             "..\\SparrowCore\\Tests\\Fixtures\\ExplorationMinimumWidthInfinity.json"})
    {
        fixture.open(path);
        if (fixture.is_open())
        {
            break;
        }
        fixture.clear();
    }
    if (!Expect(fixture.is_open(),
                output,
                "Explore minimum-width infinity input fixture cannot be opened"))
    {
        return false;
    }
    const std::string instanceJson{
        std::istreambuf_iterator<char>(fixture),
        std::istreambuf_iterator<char>()};
    const jaguar::Importer importer(sparrow::MakeRustDefaultImporterConfig());
    const auto instance = importer.ImportSPInstanceJson(instanceJson);
    if (!Expect(instance.HasValue(),
                output,
                "Explore minimum-width infinity input must import"))
    {
        return false;
    }
    auto problemResult = jaguar::SPProblem::Create(instance.Value());
    if (!Expect(problemResult.HasValue(),
                output,
                "Explore minimum-width infinity problem must be created"))
    {
        return false;
    }
    jaguar::SPProblem problem = std::move(problemResult.Value());
    if (!Expect(problem.ChangeStripWidth(20.0).HasValue()
                    && problem.PlaceItem({0U, {0.0, {3.0, 2.0}}}).HasValue()
                    && problem.PlaceItem({1U, {0.0, {12.0, 2.0}}}).HasValue(),
                output,
                "Explore minimum-width infinity initial layout must be valid"))
    {
        return false;
    }

    sparrow::ExplorationConfig config;
    config.maxIterations = 1U;
    config.shrinkStep = 0.25;
    config.separator.strikeLimit = 0U;
    config.separator.nWorkers = 1U;
    config.separator.nContainerSamples = 0U;
    config.separator.nFocussedSamples = 0U;
    config.separator.nCoordinateDescents = 0U;
    sparrow::OptimizationTerminator terminator({0U, std::chrono::seconds{2}});
    terminator.Start();
    terminator.StartPhase(std::chrono::seconds{2});
    const auto result = sparrow::Explore(
        instance.Value(), problem.Save(), config, 42U, terminator);
    if (!Expect(result.HasValue(),
                output,
                "Explore minimum-width infinity phase must complete"))
    {
        return false;
    }
    const auto floatBits = [](float value) {
        std::uint32_t bits = 0U;
        std::memcpy(&bits, &value, sizeof(bits));
        return bits;
    };
    const float actualMinimumWidth =
        sparrow::detail::ExplorationMinimumStripWidthForTrace(
            instance.Value(), problem.Save());
    if (!Expect(floatBits(actualMinimumWidth) == expectedMinimumWidthBits
                    && std::isinf(actualMinimumWidth)
                    && actualMinimumWidth > 0.0F,
                output,
                "Explore minimum width must preserve Rust positive infinity"))
    {
        return false;
    }
    const bool matches =
        floatBits(static_cast<float>(result.Value().solution.StripWidth()))
            == expectedBestWidthBits
        && floatBits(static_cast<float>(result.Value().separatorState.StripWidth()))
            == expectedSeparatorWidthBits;
    if (!matches)
    {
        output << "Explore minimum-width infinity: best width bits=0x" << std::hex
               << floatBits(static_cast<float>(result.Value().solution.StripWidth()))
               << " separator width bits=0x"
               << floatBits(static_cast<float>(result.Value().separatorState.StripWidth()))
               << std::dec << '\n';
    }
    return Expect(matches,
                  output,
                  "Explore must retain the Rust best width when the minimum width is +infinity");
}

bool TestExploreRotationHeightBoundaryMatchesRust(std::ostream& output)
{
    struct Case
    {
        const char* name;
        const char* fixtureName;
        double seedY;
    };
    constexpr std::array<Case, 6U> cases{{
        {"below_rotation_height_tolerance",
         "ExplorationMinimumWidthRotationHeightBelow.json",
         3.0},
        {"first_f32_height_accepting_90_degrees",
         "ExplorationMinimumWidthRotationHeightBoundary.json",
         3.0},
        {"above_rotation_height_tolerance",
         "ExplorationMinimumWidthRotationHeightAbove.json",
         3.0},
        {"oblique_below_rotation_height_tolerance",
         "ExplorationMinimumWidthObliqueRotationHeightBelow.json",
         2.0},
        {"oblique_first_f32_height_accepting_37_degrees",
         "ExplorationMinimumWidthObliqueRotationHeightBoundary.json",
         2.0},
        {"oblique_above_rotation_height_tolerance",
         "ExplorationMinimumWidthObliqueRotationHeightAbove.json",
         2.0},
    }};

    std::ifstream referenceFile;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/ExplorationMinimumWidthRotationHeightRustReference.tsv",
             "../SparrowCore/Tests/Fixtures/ExplorationMinimumWidthRotationHeightRustReference.tsv",
             "../JaguarRun/SparrowCore/Tests/Fixtures/ExplorationMinimumWidthRotationHeightRustReference.tsv",
             "..\\SparrowCore\\Tests\\Fixtures\\ExplorationMinimumWidthRotationHeightRustReference.tsv"})
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
                "Rust Explore rotation-height reference cannot be opened"))
    {
        return false;
    }

    struct ReferenceCase
    {
        std::string name;
        std::uint32_t containerHeightBits;
        std::uint32_t minimumWidthBits;
    };
    std::vector<ReferenceCase> expectedCases;
    std::string referenceLine;
    while (std::getline(referenceFile, referenceLine))
    {
        if (referenceLine.empty() || referenceLine.front() == '#')
        {
            continue;
        }
        std::istringstream row(referenceLine);
        std::string record;
        std::string name;
        std::uint32_t containerHeightBits = 0U;
        std::uint32_t minimumWidthBits = 0U;
        if (!(row >> record >> name >> std::hex >> containerHeightBits
                  >> minimumWidthBits)
            || record != "case")
        {
            return Expect(false,
                          output,
                          "malformed Rust Explore rotation-height reference");
        }
        expectedCases.push_back(
            {std::move(name), containerHeightBits, minimumWidthBits});
    }
    if (!Expect(expectedCases.size() == cases.size(),
                output,
                "Rust Explore rotation-height reference case count changed"))
    {
        return false;
    }

    const jaguar::Importer importer(sparrow::MakeRustDefaultImporterConfig());
    const auto floatBits = [](float value) {
        std::uint32_t bits = 0U;
        std::memcpy(&bits, &value, sizeof(bits));
        return bits;
    };
    for (std::size_t index = 0U; index < cases.size(); ++index)
    {
        const Case& testCase = cases[index];
        if (!Expect(expectedCases[index].name == testCase.name,
                    output,
                    "Rust Explore rotation-height reference case order changed"))
        {
            return false;
        }

        std::ifstream fixture;
        const std::array<std::string, 4U> fixturePaths{
            std::string("SparrowCore/Tests/Fixtures/") + testCase.fixtureName,
            std::string("../SparrowCore/Tests/Fixtures/") + testCase.fixtureName,
            std::string("../JaguarRun/SparrowCore/Tests/Fixtures/")
                + testCase.fixtureName,
            std::string("..\\SparrowCore\\Tests\\Fixtures\\")
                + testCase.fixtureName};
        for (const std::string& path : fixturePaths)
        {
            fixture.open(path);
            if (fixture.is_open())
            {
                break;
            }
            fixture.clear();
        }
        if (!Expect(fixture.is_open(),
                    output,
                    "Explore rotation-height input fixture cannot be opened"))
        {
            return false;
        }
        const std::string instanceJson{
            std::istreambuf_iterator<char>(fixture),
            std::istreambuf_iterator<char>()};
        const auto instance = importer.ImportSPInstanceJson(instanceJson);
        if (!Expect(instance.HasValue(),
                    output,
                    "Explore rotation-height input must import"))
        {
            return false;
        }
        auto problemResult = jaguar::SPProblem::Create(instance.Value());
        if (!Expect(problemResult.HasValue(),
                    output,
                    "Explore rotation-height problem must be created"))
        {
            return false;
        }
        const std::uint32_t actualHeightBits = floatBits(static_cast<float>(
            instance.Value().BaseStrip().fixedHeight));
        if (!Expect(actualHeightBits == expectedCases[index].containerHeightBits,
                    output,
                    "Explore rotation-height fixture differs from Rust f32 height bits"))
        {
            output << "case=" << testCase.name << " height=0x" << std::hex
                   << actualHeightBits << " Rust=0x"
                   << expectedCases[index].containerHeightBits << std::dec << '\n';
            return false;
        }
        jaguar::SPProblem problem = std::move(problemResult.Value());
        if (!Expect(problem.ChangeStripWidth(20.0).HasValue()
                        && problem.PlaceItem({0U, {0.0, {5.0, testCase.seedY}}})
                               .HasValue(),
                    output,
                    "Explore rotation-height seed placement must be valid"))
        {
            return false;
        }
        const jaguar::SPSolution initial = problem.Save();
        if (!Expect(jaguar::ValidateSPSolution(problem, initial).HasValue(),
                    output,
                    "Explore rotation-height seed must pass solution validation"))
        {
            return false;
        }

        const float actualMinimumWidth =
            sparrow::detail::ExplorationMinimumStripWidthForTrace(
                instance.Value(), initial);
        const std::uint32_t actualBits = floatBits(actualMinimumWidth);
        if (!Expect(actualBits == expectedCases[index].minimumWidthBits,
                    output,
                    "Explore rotation-height minimum width differs from Rust f32 bits"))
        {
            output << "case=" << testCase.name << " actual=0x" << std::hex
                   << actualBits << " Rust=0x"
                   << expectedCases[index].minimumWidthBits
                   << std::dec << '\n';
            return false;
        }
    }

    for (std::size_t first = 0U; first < expectedCases.size(); first += 3U)
    {
        if (!Expect(expectedCases[first].minimumWidthBits
                            > expectedCases[first + 1U].minimumWidthBits
                        && expectedCases[first + 1U].minimumWidthBits
                            >= expectedCases[first + 2U].minimumWidthBits,
                    output,
                    "Rust rotation-height triplet must straddle a minimum-width transition"))
        {
            return false;
        }
    }
    return true;
}

bool TestRustF32ShrinkRoundingBoundary(std::ostream& output)
{
    std::ifstream fixture;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/RustSppSingleItem.json",
             "../SparrowCore/Tests/Fixtures/RustSppSingleItem.json",
             "../JaguarRun/SparrowCore/Tests/Fixtures/RustSppSingleItem.json",
             "..\\SparrowCore\\Tests\\Fixtures\\RustSppSingleItem.json"})
    {
        fixture.open(path);
        if (fixture.is_open())
        {
            break;
        }
        fixture.clear();
    }
    if (!Expect(fixture.is_open(),
                output,
                "Rust single-item shrink-boundary fixture cannot be opened"))
    {
        return false;
    }
    const std::string instanceJson{
        std::istreambuf_iterator<char>(fixture),
        std::istreambuf_iterator<char>()};
    const jaguar::Importer importer(sparrow::MakeRustDefaultImporterConfig());
    const auto instance = importer.ImportSPInstanceJson(instanceJson);
    if (!Expect(instance.HasValue(),
                output,
                "Rust single-item shrink-boundary fixture must import"))
    {
        return false;
    }
    auto problemResult = jaguar::SPProblem::Create(instance.Value());
    if (!Expect(problemResult.HasValue(),
                output,
                "shrink-boundary problem must be created"))
    {
        return false;
    }
    jaguar::SPProblem problem = std::move(problemResult.Value());
    if (!Expect(problem.ChangeStripWidth(4.0).HasValue()
                    && problem.PlaceItem({0U, {0.0, {2.0, 2.0}}}).HasValue(),
                output,
                "shrink-boundary initial placement must be valid"))
    {
        return false;
    }

    sparrow::ExplorationConfig config;
    config.shrinkStep = std::ldexp(1.0, -25);
    config.maxIterations = 2U;
    config.separator.strikeLimit = 0U;
    config.separator.nWorkers = 1U;
    sparrow::OptimizationTerminator terminator({
        0U, std::chrono::seconds{2}});
    terminator.Start();
    terminator.StartPhase(std::chrono::seconds{2});
    const auto result = sparrow::Explore(
        instance.Value(), problem.Save(), config, 42U, terminator);
    if (!Expect(result.HasValue(),
                output,
                "Rust f32 shrink-boundary exploration must complete"))
    {
        return false;
    }
    const std::size_t feasibleReports = static_cast<std::size_t>(std::count_if(
        result.Value().history.begin(),
        result.Value().history.end(),
        [](const sparrow::OptimizationSolutionReport& report) {
            return report.type
                == sparrow::OptimizationReportType::OPTIMIZATION_REPORT_EXPLORATION_FEASIBLE;
        }));
    return Expect(result.Value().separatorState.StripWidth() == 4.0
                      && feasibleReports == 1U,
                  output,
                  "Rust f32 rounding must stop before a sub-half-ULP shrink");
}

bool TestRustF32AreaCutoffBoundary(std::ostream& output)
{
    std::ifstream fixture;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/ExplorationAreaBoundary.json",
             "../SparrowCore/Tests/Fixtures/ExplorationAreaBoundary.json",
             "../JaguarRun/SparrowCore/Tests/Fixtures/ExplorationAreaBoundary.json",
             "..\\SparrowCore\\Tests\\Fixtures\\ExplorationAreaBoundary.json"})
    {
        fixture.open(path);
        if (fixture.is_open())
        {
            break;
        }
        fixture.clear();
    }
    if (!Expect(fixture.is_open(),
                output,
                "Rust area-boundary fixture cannot be opened"))
    {
        return false;
    }
    const std::string instanceJson{
        std::istreambuf_iterator<char>(fixture),
        std::istreambuf_iterator<char>()};
    const jaguar::Importer importer(sparrow::MakeRustDefaultImporterConfig());
    const auto instance = importer.ImportSPInstanceJson(instanceJson);
    if (!Expect(instance.HasValue(),
                output,
                "Rust area-boundary fixture must import"))
    {
        return false;
    }
    auto problemResult = jaguar::SPProblem::Create(instance.Value());
    if (!Expect(problemResult.HasValue(),
                output,
                "area-boundary problem must be created"))
    {
        return false;
    }
    jaguar::SPProblem problem = std::move(problemResult.Value());
    if (!Expect(problem.ChangeStripWidth(10000.0).HasValue()
                    && problem.PlaceItem({0U, {0.0, {2050.0, 2050.0}}}).HasValue()
                    && problem.PlaceItem({1U, {0.0, {5000.0, 100.0}}}).HasValue()
                    && problem.PlaceItem({2U, {0.0, {2050.0, 2050.0}}}).HasValue(),
                output,
                "area-boundary initial placements must be valid"))
    {
        return false;
    }
    jaguar::Layout initialLayout(problem.Save().layoutSnapshot.container);
    if (!Expect(initialLayout.Restore(problem.Save().layoutSnapshot).HasValue()
                    && !initialLayout.IsFeasible(),
                output,
                "area-boundary fixture must start with a real overlap to trigger disruption"))
    {
        return false;
    }
    sparrow::ExplorationConfig config;
    config.shrinkStep = 0.0005;
    config.maxIterations = 1U;
    config.largeItemChAreaCutoffPercentile = 0.9999999;
    config.separator.strikeLimit = 0U;
    config.separator.nWorkers = 1U;
    config.separator.nContainerSamples = 0U;
    config.separator.nFocussedSamples = 0U;
    config.separator.nCoordinateDescents = 0U;
    sparrow::OptimizationTerminator terminator({16U, std::chrono::seconds{2}});
    terminator.Start();
    terminator.StartPhase(std::chrono::seconds{2});
    const auto result = sparrow::Explore(
        instance.Value(), problem.Save(), config, 3U, terminator);
    if (!Expect(result.HasValue(),
                output,
                "Rust f32 area-boundary exploration must complete"))
    {
        return false;
    }
    const auto small = std::find_if(
        result.Value().separatorState.layoutSnapshot.placements.begin(),
        result.Value().separatorState.layoutSnapshot.placements.end(),
        [](const jaguar::LayoutPlacementSnapshot& placement) {
            return placement.placedItem.GetItemIndex() == 1U;
        });
    if (!Expect(small
                    != result.Value().separatorState.layoutSnapshot.placements.end(),
                output,
                "area-boundary small item must remain placed"))
    {
        return false;
    }
    const double smallX = small->placedItem.Transform().translation.x;
    return Expect(std::abs(smallX - 5000.0) > 1.0,
                  output,
                  "f32 cutoff rounding must keep the small item eligible for disruption");
}

bool TestRustF32SimilarityBoundary(std::ostream& output)
{
    constexpr double candidateWidth = 0.0970306321978569;
    constexpr double candidateHeight = 10.40908432006836;
    const double firstArea = 1.0;
    const double candidateArea = candidateWidth * candidateHeight;
    const double firstDiameter = std::sqrt(2.0);
    const double candidateDiameter = std::hypot(
        candidateWidth, candidateHeight);
    const bool legacyDoubleDistinct =
        std::abs(candidateArea - firstArea) > firstArea * 0.01
        && std::abs(candidateDiameter - firstDiameter)
            > firstDiameter * 0.01;
    const bool rustF32Distinct = sparrow::detail::IsRustF32Distinct(
        candidateArea,
        firstArea,
        candidateDiameter,
        firstDiameter);
    return Expect(legacyDoubleDistinct && !rustF32Distinct,
                  output,
                  "Rust f32 rounding must treat the one-percent-area boundary as similar");
}

bool TestZeroExplorationFailureLimitMatchesRust(std::ostream& output)
{
    std::ifstream fixture;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/RustSppThreeSquareBoundary.json",
             "../SparrowCore/Tests/Fixtures/RustSppThreeSquareBoundary.json",
             "../JaguarRun/SparrowCore/Tests/Fixtures/RustSppThreeSquareBoundary.json",
             "..\\SparrowCore\\Tests\\Fixtures\\RustSppThreeSquareBoundary.json"})
    {
        fixture.open(path);
        if (fixture.is_open())
        {
            break;
        }
        fixture.clear();
    }
    if (!Expect(fixture.is_open(),
                output,
                "Rust zero-failure-limit fixture cannot be opened"))
    {
        return false;
    }
    const std::string instanceJson{
        std::istreambuf_iterator<char>(fixture),
        std::istreambuf_iterator<char>()};
    const jaguar::Importer importer(sparrow::MakeRustDefaultImporterConfig());
    const auto instance = importer.ImportSPInstanceJson(instanceJson);
    if (!Expect(instance.HasValue(),
                output,
                "Rust zero-failure-limit fixture must import"))
    {
        return false;
    }

    auto problemResult = jaguar::SPProblem::Create(instance.Value());
    if (!Expect(problemResult.HasValue(),
                output,
                "Rust zero-failure-limit problem must be created"))
    {
        return false;
    }
    jaguar::SPProblem problem = std::move(problemResult.Value());
    if (!Expect(problem.ChangeStripWidth(6.8).HasValue()
                    && problem.PlaceItem(
                           {0U, {0.0, {1.2, 2.0}}}).HasValue()
                    && problem.PlaceItem(
                           {0U, {0.0, {3.4, 2.0}}}).HasValue()
                    && problem.PlaceItem(
                           {0U, {0.0, {5.6, 2.0}}}).HasValue(),
                output,
                "Rust zero-failure-limit initial layout must be constructible"))
    {
        return false;
    }

    sparrow::ExplorationConfig config;
    config.shrinkStep = 0.45;
    config.maxConsecutiveFailures = 0U;
    config.separator.strikeLimit = 0U;
    config.separator.nWorkers = 1U;
    config.separator.nContainerSamples = 0U;
    config.separator.nFocussedSamples = 0U;
    config.separator.nCoordinateDescents = 1U;
    sparrow::OptimizationTerminator terminator({
        32U, std::chrono::seconds{2}});
    terminator.Start();
    terminator.StartPhase(std::chrono::seconds{2});
    RustOptimizeReferenceListener listener;
    const auto result = sparrow::Explore(
        instance.Value(), problem.Save(), config, 42U, terminator, &listener);
    if (!Expect(result.HasValue(),
                output,
                "Rust zero-failure-limit exploration must complete"))
    {
        return false;
    }
    return Expect(listener.reports
                      == std::vector<std::string>{"expl_feas", "expl_infeas"},
                  output,
                  "Some(0) must stop after one failed shrink attempt, matching Rust");
}

bool TestEqualLossPoolInsertionMatchesRust(std::ostream& output)
{
    std::ifstream traceInput;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/ExplorationEqualLossPoolRustTrace.tsv",
             "../SparrowCore/Tests/Fixtures/ExplorationEqualLossPoolRustTrace.tsv",
             "../JaguarRun/SparrowCore/Tests/Fixtures/ExplorationEqualLossPoolRustTrace.tsv",
             "..\\SparrowCore\\Tests\\Fixtures\\ExplorationEqualLossPoolRustTrace.tsv"})
    {
        traceInput.open(path);
        if (traceInput.is_open())
        {
            break;
        }
        traceInput.clear();
    }
    if (!Expect(traceInput.is_open(),
                output,
                "Rust equal-loss exploration trace cannot be opened"))
    {
        return false;
    }

    std::string line;
    while (std::getline(traceInput, line)
           && (line.empty() || line.front() == '#'))
    {
    }
    std::istringstream header(line);
    std::string record;
    std::string caseName;
    std::uint64_t seed = 0U;
    double initialWidth = 0.0;
    double shrinkStep = 0.0;
    std::size_t iterationLimit = 0U;
    double poolStddev = 0.0;
    std::size_t failureCount = 0U;
    std::size_t itemCount = 0U;
    if (!Expect(static_cast<bool>(header >> record >> caseName >> seed
                                  >> initialWidth >> shrinkStep
                                  >> iterationLimit >> poolStddev
                                  >> failureCount >> itemCount)
                    && record == "case" && caseName == "equal_loss"
                    && failureCount == 4U && itemCount == 2U,
                output,
                "malformed Rust equal-loss exploration header"))
    {
        return false;
    }

    struct ExpectedPlacement
    {
        std::size_t itemIndex = 0U;
        jaguar::Rect2d bounds;
    };
    struct ExpectedFailure
    {
        double loss = 0.0;
        std::vector<ExpectedPlacement> placements;
    };
    std::vector<ExpectedFailure> expectedFailures;
    expectedFailures.reserve(failureCount);
    for (std::size_t failureIndex = 0U;
         failureIndex < failureCount;
         ++failureIndex)
    {
        if (!std::getline(traceInput, line))
        {
            return Expect(false,
                          output,
                          "Rust equal-loss exploration trace is incomplete");
        }
        std::istringstream row(line);
        std::string failureRecord;
        std::size_t ordinal = 0U;
        ExpectedFailure failure;
        if (!(row >> failureRecord >> ordinal >> failure.loss)
            || failureRecord != "failure" || ordinal != failureIndex)
        {
            return Expect(false,
                          output,
                          "malformed Rust equal-loss failure row");
        }
        for (std::size_t item = 0U; item < itemCount; ++item)
        {
            ExpectedPlacement placement;
            if (!(row >> placement.itemIndex >> placement.bounds.xMin
                      >> placement.bounds.yMin >> placement.bounds.xMax
                      >> placement.bounds.yMax))
            {
                return Expect(false,
                              output,
                              "Rust equal-loss placement row is incomplete");
            }
            failure.placements.push_back(placement);
        }
        expectedFailures.push_back(std::move(failure));
    }

    std::ifstream instanceInput;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/ExplorationEqualLossPool.json",
             "../SparrowCore/Tests/Fixtures/ExplorationEqualLossPool.json",
             "../JaguarRun/SparrowCore/Tests/Fixtures/ExplorationEqualLossPool.json",
             "..\\SparrowCore\\Tests\\Fixtures\\ExplorationEqualLossPool.json"})
    {
        instanceInput.open(path);
        if (instanceInput.is_open())
        {
            break;
        }
        instanceInput.clear();
    }
    if (!Expect(instanceInput.is_open(),
                output,
                "Rust equal-loss exploration instance cannot be opened"))
    {
        return false;
    }
    const std::string instanceJson{
        std::istreambuf_iterator<char>(instanceInput),
        std::istreambuf_iterator<char>()};
    const jaguar::Importer importer(sparrow::MakeRustDefaultImporterConfig());
    const auto instance = importer.ImportSPInstanceJson(instanceJson);
    if (!Expect(instance.HasValue(),
                output,
                "Rust equal-loss exploration instance must import"))
    {
        return false;
    }

    auto problemResult = jaguar::SPProblem::Create(instance.Value());
    if (!Expect(problemResult.HasValue(),
                output,
                "Rust equal-loss exploration problem must be created"))
    {
        return false;
    }
    jaguar::SPProblem problem = std::move(problemResult.Value());
    if (!Expect(problem.ChangeStripWidth(initialWidth).HasValue()
                    && problem.PlaceItem({0U, {0.0, {1.5, 2.0}}}).HasValue()
                    && problem.PlaceItem({1U, {0.0, {6.0, 2.0}}}).HasValue(),
                output,
                "Rust equal-loss initial layout must be constructible"))
    {
        return false;
    }

    sparrow::ExplorationConfig config;
    config.maxIterations = iterationLimit;
    config.shrinkStep = shrinkStep;
    config.solutionPoolDistributionStddev = poolStddev;
    config.maxConsecutiveFailures = std::nullopt;
    config.separator.strikeLimit = 0U;
    config.separator.nWorkers = 1U;
    config.separator.nContainerSamples = 0U;
    config.separator.nFocussedSamples = 0U;
    sparrow::OptimizationTerminator terminator({
        32U, std::chrono::milliseconds{2000}});
    std::vector<sparrow::detail::ExplorationTraceEvent> samplerEvents;
    const auto result = sparrow::detail::ExploreWithTrace(
        instance.Value(),
        problem.Save(),
        config,
        seed,
        terminator,
        nullptr,
        [&samplerEvents](const auto& event) {
            samplerEvents.push_back(event);
        });
    if (!Expect(result.HasValue(),
                output,
                "Rust equal-loss exploration must complete"))
    {
        return false;
    }

    std::ostringstream actualSamplerTrace;
    actualSamplerTrace << std::fixed << std::setprecision(9)
                       << "case equal_loss " << seed << ' ' << initialWidth << ' '
                       << shrinkStep << ' ' << iterationLimit << ' ' << poolStddev
                       << '\n';
    for (const auto& event : samplerEvents)
    {
        std::ostringstream row;
        if (event.type == sparrow::detail::ExplorationTraceEvent::Type::PoolInsert)
        {
            row << "insert " << event.failureId << ' '
                << event.insertionIndex << ' ' << event.poolOrder.size();
            for (const std::size_t candidateId : event.poolOrder)
            {
                row << ' ' << candidateId;
            }
        }
        else if (event.type
                 == sparrow::detail::ExplorationTraceEvent::Type::PoolSample)
        {
            std::uint32_t rawSampleBits = 0U;
            std::uint32_t mappedSampleBits = 0U;
            std::memcpy(&rawSampleBits,
                        &event.rawSample,
                        sizeof(rawSampleBits));
            std::memcpy(&mappedSampleBits,
                        &event.mappedSample,
                        sizeof(mappedSampleBits));
            row << "sample " << event.failureId << ' ' << std::hex
                << std::setw(8) << std::setfill('0') << rawSampleBits << ' '
                << std::setw(8) << std::setfill('0') << mappedSampleBits
                << std::dec << ' ' << event.selectedIndex << ' '
                << event.consumedU64Words << ' ' << std::hex << std::setw(2)
                << std::setfill('0') << event.firstLayerIndex;
            for (const std::uint64_t value : event.followingU64)
            {
                row << ' ' << std::setw(16) << std::setfill('0') << value;
            }
        }
        else
        {
            continue;
        }
        actualSamplerTrace << row.str() << '\n';
    }

    std::ifstream samplerTraceInput;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/ExplorationEqualLossPoolSamplerRustTrace.tsv",
             "../SparrowCore/Tests/Fixtures/ExplorationEqualLossPoolSamplerRustTrace.tsv",
             "../JaguarRun/SparrowCore/Tests/Fixtures/ExplorationEqualLossPoolSamplerRustTrace.tsv",
             "..\\SparrowCore\\Tests\\Fixtures\\ExplorationEqualLossPoolSamplerRustTrace.tsv"})
    {
        samplerTraceInput.open(path);
        if (samplerTraceInput.is_open())
        {
            break;
        }
        samplerTraceInput.clear();
    }
    if (!Expect(samplerTraceInput.is_open(),
                output,
                "Rust equal-loss sampler trace cannot be opened"))
    {
        return false;
    }
    std::string expectedSamplerTrace;
    while (std::getline(samplerTraceInput, line))
    {
        if (line.empty() || line.front() == '#')
        {
            continue;
        }
        expectedSamplerTrace += line;
        expectedSamplerTrace.push_back('\n');
    }
    if (!Expect(actualSamplerTrace.str() == expectedSamplerTrace,
                output,
                "C++ Explore pool/sample/RNG trace differs from actual Rust Explore"))
    {
        output << "actual sampler trace:\n" << actualSamplerTrace.str()
               << "expected sampler trace:\n" << expectedSamplerTrace;
        return false;
    }

    std::vector<const sparrow::OptimizationSolutionReport*> actualFailures;
    for (const auto& report : result.Value().history)
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
        output << "Equal-loss pool failures actual=" << actualFailures.size()
               << " expected=" << expectedFailures.size() << '\n';
        for (std::size_t index = 0U; index < actualFailures.size(); ++index)
        {
            const auto& actual = *actualFailures[index];
            output << "  actual[" << index << "] loss=" << actual.collisionLoss;
            for (const auto& placement : actual.solution.layoutSnapshot.placements)
            {
                const auto bounds = placement.placedItem.CollisionShape().BoundingBox();
                output << " item" << placement.placedItem.GetItemIndex()
                       << "=[" << bounds.xMin << ',' << bounds.yMin << ','
                       << bounds.xMax << ',' << bounds.yMax << ']';
            }
            output << '\n';
        }
    }
    if (!Expect(actualFailures.size() == expectedFailures.size(),
                output,
                "equal-loss exploration failure count differs from Rust"))
    {
        return false;
    }
    const float exactTieLoss = actualFailures.front()->collisionLoss;
    if (!Expect(std::all_of(
                    actualFailures.begin(),
                    actualFailures.end(),
                    [exactTieLoss](const auto* failure) {
                        return failure->collisionLoss == exactTieLoss;
                    }),
                output,
                "all C++ reference pool candidates must have exactly equal losses"))
    {
        return false;
    }

    std::vector<std::vector<ExpectedPlacement>> actualPlacements;
    actualPlacements.reserve(actualFailures.size());
    for (std::size_t failureIndex = 0U;
         failureIndex < expectedFailures.size();
         ++failureIndex)
    {
        const auto& expected = expectedFailures[failureIndex];
        const auto& actual = *actualFailures[failureIndex];
        std::vector<ExpectedPlacement> placements;
        placements.reserve(itemCount);
        bool matches = actual.solution.layoutSnapshot.placements.size() == itemCount;
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
                matches = false;
                break;
            }
            const jaguar::Rect2d bounds =
                placement->placedItem.CollisionShape().BoundingBox();
            placements.push_back({expectedPlacement.itemIndex, bounds});
            matches = matches
                && std::abs(bounds.xMin - expectedPlacement.bounds.xMin) < 1.0e-4
                && std::abs(bounds.yMin - expectedPlacement.bounds.yMin) < 1.0e-4
                && std::abs(bounds.xMax - expectedPlacement.bounds.xMax) < 1.0e-4
                && std::abs(bounds.yMax - expectedPlacement.bounds.yMax) < 1.0e-4;
        }
        const bool lossMatches =
            std::abs(static_cast<double>(actual.collisionLoss) - expected.loss)
            < 1.0e-3;
        if (!matches || !lossMatches)
        {
            output << std::setprecision(12) << "Equal-loss pool failure "
                   << failureIndex << " actual loss=" << actual.collisionLoss
                   << " expected loss=" << expected.loss
                   << " placements-match=" << matches << '\n';
        }
        if (!Expect(matches && lossMatches,
                    output,
                    "equal-loss exploration state differs from Rust"))
        {
            return false;
        }
        actualPlacements.push_back(std::move(placements));
    }

    const auto sameLayout = [](const std::vector<ExpectedPlacement>& left,
                               const std::vector<ExpectedPlacement>& right) {
        if (left.size() != right.size())
        {
            return false;
        }
        for (std::size_t index = 0U; index < left.size(); ++index)
        {
            if (left[index].itemIndex != right[index].itemIndex
                || std::abs(left[index].bounds.xMin - right[index].bounds.xMin) >= 1.0e-5
                || std::abs(left[index].bounds.yMin - right[index].bounds.yMin) >= 1.0e-5
                || std::abs(left[index].bounds.xMax - right[index].bounds.xMax) >= 1.0e-5
                || std::abs(left[index].bounds.yMax - right[index].bounds.yMax) >= 1.0e-5)
            {
                return false;
            }
        }
        return true;
    };
    const bool sameLossAndDifferentLayouts =
        std::abs(expectedFailures[0].loss - expectedFailures[1].loss) < 1.0e-7
        && !sameLayout(expectedFailures[0].placements,
                       expectedFailures[1].placements);
    if (!Expect(!sameLayout(actualPlacements[0], actualPlacements[1]),
                output,
                "C++ equal-loss candidates must retain distinct identities"))
    {
        return false;
    }
    return Expect(sameLossAndDifferentLayouts,
                  output,
                  "Rust equal-loss fixture must distinguish candidate identities");
}

bool TestDisruptionTraceMatchesRust(
    const char* traceFilename,
    const char* instanceFilename,
    const char* expectedCaseName,
    std::uint64_t expectedSeed,
    double largeItemChAreaCutoffPercentile,
    const std::vector<std::pair<double, double>>& initialTranslations,
    const std::vector<std::size_t>& expectedDistinctCandidateCounts,
    std::size_t expectedFirstDistinctCandidateCount,
    const std::vector<std::pair<std::size_t, std::size_t>>& expectedFirstContainedOrder,
    std::ostream& output,
    bool requireFinalState = false,
    std::size_t expectedIterationLimit = 4U,
    const std::vector<double>& initialRotations = {},
    const std::vector<double>& expectedSortedHullAreas = {},
    std::optional<std::size_t> maxConsecutiveFailures = std::nullopt,
    bool verifyReportHistory = false,
    std::optional<jaguar::RotationMode> expectedRotationMode = std::nullopt,
    bool verifyFirstFailureContainedOrder = false)
{
    std::ifstream traceInput;
    for (const char* prefix : {
             "SparrowCore/Tests/Fixtures/",
             "../SparrowCore/Tests/Fixtures/",
             "../JaguarRun/SparrowCore/Tests/Fixtures/",
             "..\\SparrowCore\\Tests\\Fixtures\\"})
    {
        traceInput.open(std::string(prefix) + traceFilename);
        if (traceInput.is_open())
        {
            break;
        }
        traceInput.clear();
    }
    if (!Expect(traceInput.is_open(),
                output,
                "Rust disruption trace cannot be opened"))
    {
        return false;
    }

    std::vector<std::string> records;
    std::string line;
    while (std::getline(traceInput, line))
    {
        if (!line.empty() && line.front() != '#')
        {
            records.push_back(line);
        }
    }
    if (!Expect(!records.empty(),
                output,
                "Rust disruption trace must contain a case"))
    {
        return false;
    }

    std::istringstream caseRow(records.front());
    std::string record;
    std::string caseName;
    std::uint64_t seed = 0U;
    double initialWidth = 0.0;
    double shrinkStep = 0.0;
    std::size_t iterationLimit = 0U;
    double poolStddev = 0.0;
    std::size_t failureCount = 0U;
    std::size_t itemCount = 0U;
    const bool baseHeaderParsed = static_cast<bool>(
        caseRow >> record >> caseName >> seed >> initialWidth >> shrinkStep
                >> iterationLimit >> poolStddev >> failureCount >> itemCount);
    std::size_t traceMaxConsecutiveFailuresValue = 0U;
    const bool hasTraceMaxConsecutiveFailures =
        static_cast<bool>(caseRow >> traceMaxConsecutiveFailuresValue);
    const std::optional<std::size_t> traceMaxConsecutiveFailures =
        hasTraceMaxConsecutiveFailures
            ? std::optional<std::size_t>{traceMaxConsecutiveFailuresValue}
            : std::nullopt;
    std::string unexpectedCaseValue;
    const bool noUnexpectedCaseValues = !(caseRow >> unexpectedCaseValue);
    if (!Expect(baseHeaderParsed
                    && record == "case" && caseName == expectedCaseName
                    && seed == expectedSeed
                    && iterationLimit == expectedIterationLimit
                    && traceMaxConsecutiveFailures == maxConsecutiveFailures
                    && noUnexpectedCaseValues
                    && failureCount > 0U
                    && itemCount == initialTranslations.size()
                    && (initialRotations.empty()
                        || initialRotations.size() == itemCount)
                    && itemCount == expectedDistinctCandidateCounts.size(),
                output,
                "malformed Rust disruption trace header"))
    {
        return false;
    }

    struct ExpectedPose
    {
        std::size_t itemIndex = 0U;
        double rotation = 0.0;
        double x = 0.0;
        double y = 0.0;
        bool hasBounds = false;
        jaguar::Rect2d bounds;
    };
    struct ExpectedFailure
    {
        double loss = 0.0;
        std::vector<ExpectedPose> poses;
    };
    struct ExpectedReport
    {
        bool feasible = false;
        double stripWidth = 0.0;
        double loss = 0.0;
    };
    std::vector<ExpectedFailure> expectedFailures;
    std::optional<ExpectedFailure> expectedFinalState;
    std::vector<ExpectedReport> expectedReports;
    std::vector<std::string> expectedEvents;
    std::size_t recordIndex = 1U;
    for (std::size_t failureIndex = 0U;
         failureIndex < failureCount;
         ++failureIndex)
    {
        if (!Expect(recordIndex < records.size(),
                    output,
                    "Rust disruption failure row is missing"))
        {
            return false;
        }
        std::istringstream failureRow(records[recordIndex++]);
        std::string failureRecord;
        std::size_t ordinal = 0U;
        std::size_t poseCount = 0U;
        ExpectedFailure failure;
        if (!Expect(static_cast<bool>(failureRow >> failureRecord >> ordinal
                                      >> failure.loss >> poseCount)
                        && failureRecord == "failure"
                        && ordinal == failureIndex && poseCount == itemCount,
                    output,
                    "malformed Rust disruption failure row"))
        {
            return false;
        }
        for (std::size_t poseIndex = 0U; poseIndex < poseCount; ++poseIndex)
        {
            if (!Expect(recordIndex < records.size(),
                        output,
                        "Rust disruption pose row is missing"))
            {
                return false;
            }
            std::istringstream poseRow(records[recordIndex++]);
            std::string poseRecord;
            ExpectedPose pose;
            if (!Expect(static_cast<bool>(poseRow >> poseRecord >> pose.itemIndex
                                          >> pose.rotation >> pose.x >> pose.y)
                            && poseRecord == "pose",
                        output,
                        "malformed Rust disruption pose row"))
            {
                return false;
            }
            if (poseRow >> pose.bounds.xMin >> pose.bounds.yMin
                        >> pose.bounds.xMax >> pose.bounds.yMax)
            {
                pose.hasBounds = true;
            }
            else
            {
                poseRow.clear();
            }
            std::string trailingPoseValue;
            if (!Expect(!(poseRow >> trailingPoseValue),
                        output,
                        "Rust disruption pose row has unexpected trailing values"))
            {
                return false;
            }
            failure.poses.push_back(pose);
        }
        expectedFailures.push_back(std::move(failure));
    }
    while (recordIndex < records.size())
    {
        if (records[recordIndex].compare(0U, 7U, "report ") == 0)
        {
            std::istringstream reportRow(records[recordIndex++]);
            std::string reportRecord;
            std::string reportType;
            std::size_t feasible = 0U;
            ExpectedReport report;
            if (!Expect(static_cast<bool>(reportRow >> reportRecord >> reportType
                                          >> report.stripWidth >> report.loss
                                          >> feasible)
                            && reportRecord == "report"
                            && (reportType == "feasible"
                                || reportType == "infeasible")
                            && feasible <= 1U
                            && ((reportType == "feasible") == (feasible == 1U)),
                        output,
                        "malformed Rust exploration report row"))
            {
                return false;
            }
            report.feasible = feasible == 1U;
            expectedReports.push_back(report);
            continue;
        }
        if (records[recordIndex].compare(0U, 6U, "final ") != 0)
        {
            expectedEvents.push_back(records[recordIndex++]);
            continue;
        }

        std::istringstream finalRow(records[recordIndex++]);
        std::string finalRecord;
        std::size_t finalPoseCount = 0U;
        ExpectedFailure finalState;
        if (!Expect(!expectedFinalState.has_value()
                        && static_cast<bool>(finalRow >> finalRecord
                                             >> finalState.loss >> finalPoseCount)
                        && finalRecord == "final"
                        && finalPoseCount == itemCount,
                    output,
                    "malformed Rust disruption final-state row"))
        {
            return false;
        }
        for (std::size_t poseIndex = 0U;
             poseIndex < finalPoseCount;
             ++poseIndex)
        {
            if (!Expect(recordIndex < records.size(),
                        output,
                        "Rust disruption final pose row is missing"))
            {
                return false;
            }
            std::istringstream poseRow(records[recordIndex++]);
            std::string poseRecord;
            ExpectedPose pose;
            if (!Expect(static_cast<bool>(poseRow >> poseRecord >> pose.itemIndex
                                          >> pose.rotation >> pose.x >> pose.y
                                          >> pose.bounds.xMin >> pose.bounds.yMin
                                          >> pose.bounds.xMax >> pose.bounds.yMax)
                            && poseRecord == "pose",
                        output,
                        "malformed Rust disruption final pose row"))
            {
                return false;
            }
            pose.hasBounds = true;
            std::string trailingPoseValue;
            if (!Expect(!(poseRow >> trailingPoseValue),
                        output,
                        "Rust disruption final pose row has unexpected trailing values"))
            {
                return false;
            }
            finalState.poses.push_back(pose);
        }
        expectedFinalState = std::move(finalState);
        if (!Expect(recordIndex == records.size(),
                    output,
                    "Rust disruption events must precede the final-state snapshot"))
        {
            return false;
        }
    }

    std::ifstream instanceInput;
    for (const char* prefix : {
             "SparrowCore/Tests/Fixtures/",
             "../SparrowCore/Tests/Fixtures/",
             "../JaguarRun/SparrowCore/Tests/Fixtures/",
             "..\\SparrowCore\\Tests\\Fixtures\\"})
    {
        instanceInput.open(std::string(prefix) + instanceFilename);
        if (instanceInput.is_open())
        {
            break;
        }
        instanceInput.clear();
    }
    if (!Expect(instanceInput.is_open(),
                output,
                "Rust disruption instance cannot be opened"))
    {
        return false;
    }
    const std::string instanceJson{
        std::istreambuf_iterator<char>(instanceInput),
        std::istreambuf_iterator<char>()};
    const jaguar::Importer importer(sparrow::MakeRustDefaultImporterConfig());
    const auto instance = importer.ImportSPInstanceJson(instanceJson);
    if (!Expect(instance.HasValue() && instance.Value().ItemCount() == itemCount,
                output,
                "Rust disruption instance must import"))
    {
        return false;
    }
    if (expectedRotationMode.has_value())
    {
        bool allRotationModesMatch = true;
        for (std::size_t itemIndex = 0U; itemIndex < itemCount; ++itemIndex)
        {
            allRotationModesMatch = allRotationModesMatch
                && instance.Value().GetItem(itemIndex).AllowedRotation().mode
                    == *expectedRotationMode;
        }
        if (!Expect(allRotationModesMatch,
                    output,
                    "Rust disruption fixture items must import with the expected rotation mode"))
        {
            return false;
        }
    }

    auto problemResult = jaguar::SPProblem::Create(instance.Value());
    if (!Expect(problemResult.HasValue(),
                output,
                "Rust disruption problem must be created"))
    {
        return false;
    }
    jaguar::SPProblem problem = std::move(problemResult.Value());
    if (!Expect(problem.ChangeStripWidth(initialWidth).HasValue(),
                output,
                "Rust disruption strip width must be accepted"))
    {
        return false;
    }
    for (std::size_t itemIndex = 0U; itemIndex < itemCount; ++itemIndex)
    {
        const double initialRotation = initialRotations.empty()
            ? 0.0
            : initialRotations[itemIndex];
        if (!Expect(problem.PlaceItem({itemIndex,
                                       {initialRotation,
                                        {initialTranslations[itemIndex].first,
                                         initialTranslations[itemIndex].second}}})
                        .HasValue(),
                    output,
                    "Rust disruption placements must be constructible"))
        {
            return false;
        }
    }
    const jaguar::SPSolution initial = problem.Save();
    if (!Expect(initial.layoutSnapshot.placements.size() == itemCount,
                output,
                "Rust disruption trace must place every item"))
    {
        return false;
    }
    if (!expectedSortedHullAreas.empty())
    {
        std::vector<double> hullAreas;
        for (const auto& placement : initial.layoutSnapshot.placements)
        {
            const jaguar::Surrogate* surrogate =
                placement.placedItem.CollisionShape().GetSurrogate();
            if (!Expect(surrogate != nullptr,
                        output,
                    "Rust disruption fixture items must have convex-hull surrogates"))
            {
                return false;
            }
            hullAreas.push_back(surrogate->ConvexHullArea());
        }
        std::sort(hullAreas.begin(), hullAreas.end(), std::greater<double>());
        bool hullAreasMatch = hullAreas.size() == expectedSortedHullAreas.size();
        for (std::size_t areaIndex = 0U;
             hullAreasMatch && areaIndex < hullAreas.size();
             ++areaIndex)
        {
            hullAreasMatch = std::abs(hullAreas[areaIndex]
                                      - expectedSortedHullAreas[areaIndex])
                             < 1.0e-5;
        }
        if (!Expect(hullAreasMatch,
                    output,
                    "Rust disruption fixture hull areas must match the expected descending sequence"))
        {
            return false;
        }
        // These fixtures' descending hull areas sum exactly to the 100% cutoff,
        // so Rust's strict `cumulative > cutoff` branch leaves cutoffArea at zero.
    }
    for (const auto& firstPlacement : initial.layoutSnapshot.placements)
    {
        const auto& first = firstPlacement.placedItem;
        const float firstArea = static_cast<float>(first.Area());
        const float firstDiameter = static_cast<float>(
            first.CollisionShape().Diameter());
        if (!Expect(first.CollisionShape().GetSurrogate() != nullptr,
                    output,
                    "Rust disruption items must have convex-hull surrogates"))
        {
            return false;
        }
        std::size_t distinctCandidateCount = 0U;
        for (const auto& candidatePlacement : initial.layoutSnapshot.placements)
        {
            if (candidatePlacement.placedItem.GetItemIndex()
                == first.GetItemIndex())
            {
                continue;
            }
            const auto& candidate = candidatePlacement.placedItem;
            const float candidateArea = static_cast<float>(candidate.Area());
            const float candidateDiameter = static_cast<float>(
                candidate.CollisionShape().Diameter());
            if (std::abs(candidateArea - firstArea) > firstArea * 0.01F
                && std::abs(candidateDiameter - firstDiameter)
                    > firstDiameter * 0.01F)
            {
                ++distinctCandidateCount;
            }
        }
        if (!Expect(distinctCandidateCount
                        == expectedDistinctCandidateCounts[first.GetItemIndex()],
                    output,
                    "fixture does not provide the expected distinct candidate count for every first choice"))
        {
            return false;
        }
    }
    jaguar::Layout initialLayout(initial.layoutSnapshot.container);
    const auto restored = initialLayout.Restore(initial.layoutSnapshot);
    if (!Expect(restored.HasValue() && initialLayout.IsFeasible(),
                output,
                "Rust disruption initial layout must be feasible"))
    {
        return false;
    }

    sparrow::ExplorationConfig config;
    config.maxIterations = iterationLimit;
    config.shrinkStep = shrinkStep;
    config.largeItemChAreaCutoffPercentile =
        largeItemChAreaCutoffPercentile;
    config.solutionPoolDistributionStddev = poolStddev;
    config.maxConsecutiveFailures = maxConsecutiveFailures;
    config.separator.strikeLimit = 0U;
    config.separator.nWorkers = 1U;
    config.separator.nContainerSamples = 0U;
    config.separator.nFocussedSamples = 0U;
    config.separator.nCoordinateDescents = 0U;
    sparrow::OptimizationTerminator terminator({
        32U, std::chrono::milliseconds{2000}});
    std::vector<sparrow::detail::ExplorationTraceEvent> actualEvents;
    const auto result = sparrow::detail::ExploreWithTrace(
        instance.Value(),
        initial,
        config,
        seed,
        terminator,
        nullptr,
        [&actualEvents](const auto& event) {
            actualEvents.push_back(event);
        });
    if (!Expect(result.HasValue(),
                output,
                "C++ disruption Explore must complete"))
    {
        return false;
    }
    if (!Expect(!requireFinalState || expectedFinalState.has_value(),
                output,
                "Rust disruption trace must include its final-state snapshot"))
    {
        return false;
    }

    std::vector<std::string> actualEventRows;
    for (const auto& event : actualEvents)
    {
        std::ostringstream row;
        if (event.type == sparrow::detail::ExplorationTraceEvent::Type::PoolInsert)
        {
            row << "insert " << event.failureId << ' '
                << event.insertionIndex << ' ' << event.poolOrder.size();
            for (const std::size_t candidateId : event.poolOrder)
            {
                row << ' ' << candidateId;
            }
        }
        else if (event.type
                 == sparrow::detail::ExplorationTraceEvent::Type::PoolSample)
        {
            std::uint32_t rawSampleBits = 0U;
            std::uint32_t mappedSampleBits = 0U;
            std::memcpy(&rawSampleBits, &event.rawSample, sizeof(rawSampleBits));
            std::memcpy(&mappedSampleBits,
                        &event.mappedSample,
                        sizeof(mappedSampleBits));
            row << "sample " << event.failureId << ' ' << std::hex
                << std::setw(8) << std::setfill('0') << rawSampleBits << ' '
                << std::setw(8) << std::setfill('0') << mappedSampleBits
                << std::dec << ' ' << event.selectedIndex << ' '
                << event.consumedU64Words << ' ' << std::hex << std::setw(2)
                << std::setfill('0') << event.firstLayerIndex;
            for (const std::uint64_t value : event.followingU64)
            {
                row << ' ' << std::setw(16) << std::setfill('0') << value;
            }
        }
        else
        {
            const char* kind = event.disruptionMoveKind
                    == sparrow::detail::ExplorationTraceEvent::DisruptionMoveKind::Swap
                ? "swap"
                : "contained";
            row << std::fixed << std::setprecision(9)
                << "move " << event.failureId << ' ' << kind << ' '
                << event.itemIndex << ' ' << event.relatedItemIndex << ' '
                << event.fromPose[0] << ' ' << event.fromPose[1] << ' '
                << event.fromPose[2] << ' ' << event.toPose[0] << ' '
                << event.toPose[1] << ' ' << event.toPose[2];
        }
        actualEventRows.push_back(row.str());
    }
    if (actualEventRows != expectedEvents)
    {
        output << "C++ disruption event rows differ from Rust\n";
        for (const auto& row : actualEventRows)
        {
            output << "  actual " << row << '\n';
        }
        for (const auto& row : expectedEvents)
        {
            output << "expected " << row << '\n';
        }
        return Expect(false,
                      output,
                      "C++ disruption choices and subsequent pool/RNG trace differ from Rust");
    }

    if (verifyFirstFailureContainedOrder
        || !expectedFirstContainedOrder.empty())
    {
        std::vector<std::pair<std::size_t, std::size_t>> actualFirstContainedOrder;
        for (const auto& event : actualEvents)
        {
            if (event.type
                    == sparrow::detail::ExplorationTraceEvent::Type::DisruptionMove
                && event.failureId == 0U
                && event.disruptionMoveKind
                       == sparrow::detail::ExplorationTraceEvent::DisruptionMoveKind::ContainedRelocation)
            {
                actualFirstContainedOrder.emplace_back(
                    event.itemIndex, event.relatedItemIndex);
            }
        }
        if (!Expect(actualFirstContainedOrder == expectedFirstContainedOrder,
                    output,
                    "first failure contained-collection/move order differs from Rust"))
        {
            return false;
        }
    }

    std::vector<const sparrow::OptimizationSolutionReport*> actualFailures;
    for (const auto& report : result.Value().history)
    {
        if (report.phase == sparrow::OptimizationPhase::OPTIMIZATION_PHASE_EXPLORATION
            && report.type
                   == sparrow::OptimizationReportType::OPTIMIZATION_REPORT_EXPLORATION_INFEASIBLE)
        {
            actualFailures.push_back(&report);
        }
    }
    if (!Expect(actualFailures.size() == expectedFailures.size(),
                output,
                "C++ disruption failure count differs from Rust"))
    {
        return false;
    }
    for (std::size_t failureIndex = 0U;
         failureIndex < expectedFailures.size();
         ++failureIndex)
    {
        const auto& expected = expectedFailures[failureIndex];
        const auto& actual = *actualFailures[failureIndex];
        bool posesMatch = actual.solution.layoutSnapshot.placements.size() == itemCount;
        for (const auto& expectedPose : expected.poses)
        {
            const auto placement = std::find_if(
                actual.solution.layoutSnapshot.placements.begin(),
                actual.solution.layoutSnapshot.placements.end(),
                [&expectedPose](const auto& candidate) {
                    return candidate.placedItem.GetItemIndex()
                        == expectedPose.itemIndex;
                });
            if (placement == actual.solution.layoutSnapshot.placements.end())
            {
                posesMatch = false;
                break;
            }
            const auto& transform = placement->placedItem.Transform();
            const jaguar::Rect2d bounds =
                placement->placedItem.CollisionShape().BoundingBox();
            posesMatch = posesMatch
                && std::abs(transform.rotationRad - expectedPose.rotation) < 2.0e-5
                && std::abs(transform.translation.x - expectedPose.x) < 2.0e-5
                && std::abs(transform.translation.y - expectedPose.y) < 2.0e-5
                && (!expectedPose.hasBounds
                    || (std::abs(bounds.xMin - expectedPose.bounds.xMin) < 2.0e-4
                        && std::abs(bounds.yMin - expectedPose.bounds.yMin) < 2.0e-4
                        && std::abs(bounds.xMax - expectedPose.bounds.xMax) < 2.0e-4
                        && std::abs(bounds.yMax - expectedPose.bounds.yMax) < 2.0e-4));
        }
        const bool lossMatches =
            std::abs(static_cast<double>(actual.collisionLoss) - expected.loss)
            < 1.0e-3;
        if (!Expect(posesMatch && lossMatches,
                    output,
                    "C++ disruption failure pose/loss differs from Rust"))
        {
            output << std::setprecision(12) << "failure=" << failureIndex
                   << " actual loss=" << actual.collisionLoss
                   << " expected loss=" << expected.loss << '\n';
            return false;
        }
    }

    if (verifyReportHistory)
    {
        std::vector<const sparrow::OptimizationSolutionReport*> actualReports;
        for (const auto& report : result.Value().history)
        {
            if (report.phase == sparrow::OptimizationPhase::OPTIMIZATION_PHASE_EXPLORATION
                && (report.type
                        == sparrow::OptimizationReportType::OPTIMIZATION_REPORT_EXPLORATION_FEASIBLE
                    || report.type
                        == sparrow::OptimizationReportType::OPTIMIZATION_REPORT_EXPLORATION_INFEASIBLE))
            {
                actualReports.push_back(&report);
            }
        }
        if (!Expect(actualReports.size() == expectedReports.size()
                        && !expectedReports.empty(),
                    output,
                    "C++ exploration report count differs from Rust"))
        {
            return false;
        }
        for (std::size_t reportIndex = 0U;
             reportIndex < expectedReports.size();
             ++reportIndex)
        {
            const auto& expected = expectedReports[reportIndex];
            const auto& actual = *actualReports[reportIndex];
            const bool actualFeasible = actual.type
                == sparrow::OptimizationReportType::OPTIMIZATION_REPORT_EXPLORATION_FEASIBLE;
            if (!Expect(actualFeasible == expected.feasible
                            && actual.isFeasible == expected.feasible
                            && std::abs(actual.stripWidth - expected.stripWidth) < 1.0e-5
                            && std::abs(static_cast<double>(actual.collisionLoss)
                                        - expected.loss) < 1.0e-3,
                        output,
                        "C++ recovery/shrink report sequence differs from Rust"))
            {
                output << std::setprecision(12)
                       << "report=" << reportIndex
                       << " actual width=" << actual.stripWidth
                       << " loss=" << actual.collisionLoss
                       << " expected width=" << expected.stripWidth
                       << " loss=" << expected.loss << '\n';
                return false;
            }
        }
    }

    if (expectedFinalState.has_value())
    {
        const auto& finalSnapshot = result.Value().separatorState.layoutSnapshot;
        jaguar::Layout finalLayout(finalSnapshot.container);
        const auto restoredFinalLayout = finalLayout.Restore(finalSnapshot);
        const auto finalTracker = sparrow::CollisionTracker::Create(finalLayout);
        bool posesMatch = restoredFinalLayout.HasValue()
            && finalTracker.HasValue()
            && finalSnapshot.placements.size() == itemCount
            && expectedFinalState->poses.size() == itemCount;
        for (const auto& expectedPose : expectedFinalState->poses)
        {
            const auto placement = std::find_if(
                finalSnapshot.placements.begin(),
                finalSnapshot.placements.end(),
                [&expectedPose](const auto& candidate) {
                    return candidate.placedItem.GetItemIndex()
                        == expectedPose.itemIndex;
                });
            if (placement == finalSnapshot.placements.end())
            {
                posesMatch = false;
                break;
            }
            const auto& transform = placement->placedItem.Transform();
            const jaguar::Rect2d bounds =
                placement->placedItem.CollisionShape().BoundingBox();
            posesMatch = posesMatch
                && std::abs(transform.rotationRad - expectedPose.rotation) < 2.0e-5
                && std::abs(transform.translation.x - expectedPose.x) < 2.0e-5
                && std::abs(transform.translation.y - expectedPose.y) < 2.0e-5
                && (!expectedPose.hasBounds
                    || (std::abs(bounds.xMin - expectedPose.bounds.xMin) < 2.0e-4
                        && std::abs(bounds.yMin - expectedPose.bounds.yMin) < 2.0e-4
                        && std::abs(bounds.xMax - expectedPose.bounds.xMax) < 2.0e-4
                        && std::abs(bounds.yMax - expectedPose.bounds.yMax) < 2.0e-4));
        }
        const bool lossMatches = finalTracker.HasValue()
            && std::abs(static_cast<double>(finalTracker.Value().TotalLoss())
                        - expectedFinalState->loss) < 1.0e-3;
        if (!Expect(posesMatch && lossMatches,
                    output,
                    "C++ post-disruption final layout/loss differs from Rust"))
        {
            output << std::setprecision(12)
                   << "final actual loss="
                   << (finalTracker.HasValue()
                           ? finalTracker.Value().TotalLoss()
                           : -1.0)
                   << " expected loss=" << expectedFinalState->loss
                   << '\n';
            return false;
        }
    }

    const std::size_t firstFailureSwapCount = static_cast<std::size_t>(
        std::count_if(actualEvents.begin(), actualEvents.end(), [](const auto& event) {
            return event.type
                       == sparrow::detail::ExplorationTraceEvent::Type::DisruptionMove
                && event.failureId == 0U
                && event.disruptionMoveKind
                       == sparrow::detail::ExplorationTraceEvent::DisruptionMoveKind::Swap;
        }));
    if (!Expect(firstFailureSwapCount == 2U,
                output,
                "first disruption must emit both swap moves"))
    {
        return false;
    }
    if (expectedFirstDistinctCandidateCount > 0U)
    {
        const auto firstSwap = std::find_if(
            actualEvents.begin(),
            actualEvents.end(),
            [](const auto& event) {
                return event.type
                           == sparrow::detail::ExplorationTraceEvent::Type::DisruptionMove
                    && event.failureId == 0U
                    && event.disruptionMoveKind
                           == sparrow::detail::ExplorationTraceEvent::DisruptionMoveKind::Swap;
            });
        if (!Expect(firstSwap != actualEvents.end()
                        && firstSwap->itemIndex < expectedDistinctCandidateCounts.size()
                        && expectedDistinctCandidateCounts[firstSwap->itemIndex]
                            == expectedFirstDistinctCandidateCount,
                    output,
                    "first actual disruption must use the requested distinct candidate count"))
        {
            return false;
        }
    }
    return true;
}

bool TestDisruptionFallbackMatchesRust(std::ostream& output)
{
    return TestDisruptionTraceMatchesRust(
        "ExplorationDisruptionFallbackRustTrace.tsv",
        "ExplorationDisruptionFallback.json",
        "disruption_fallback",
        43U,
        0.75,
        {{0.75, 1.5}, {2.75, 1.5}, {4.75, 1.5}, {7.25, 1.5}},
        {0U, 0U, 0U, 0U},
        0U,
        {},
        output,
        false,
        4U,
        {},
        {},
        std::nullopt,
        false,
        std::nullopt,
        true);
}

bool TestContainedCollectorFilterGeometryWitness(std::ostream& output)
{
    std::ifstream instanceInput;
    for (const char* prefix : {
             "SparrowCore/Tests/Fixtures/",
             "../SparrowCore/Tests/Fixtures/",
             "../JaguarRun/SparrowCore/Tests/Fixtures/",
             "..\\SparrowCore\\Tests\\Fixtures\\"})
    {
        instanceInput.open(
            std::string(prefix) + "ExplorationContainedCollectorFilter.json");
        if (instanceInput.is_open())
        {
            break;
        }
        instanceInput.clear();
    }
    if (!Expect(instanceInput.is_open(),
                output,
                "contained-collector witness fixture cannot be opened"))
    {
        return false;
    }
    const std::string json{
        std::istreambuf_iterator<char>(instanceInput),
        std::istreambuf_iterator<char>()};
    const jaguar::Importer importer(sparrow::MakeRustDefaultImporterConfig());
    const auto instance = importer.ImportSPInstanceJson(json);
    if (!Expect(instance.HasValue() && instance.Value().ItemCount() == 4U,
                output,
                "contained-collector witness fixture must import"))
    {
        return false;
    }
    auto problemResult = jaguar::SPProblem::Create(instance.Value());
    if (!Expect(problemResult.HasValue(),
                output,
                "contained-collector witness problem must be created"))
    {
        return false;
    }
    jaguar::SPProblem problem = std::move(problemResult.Value());
    if (!Expect(problem.ChangeStripWidth(8.91).HasValue(),
                output,
                "contained-collector witness width must be accepted"))
    {
        return false;
    }

    const std::array<jaguar::Point2d, 4U> postSwapTranslations{{
        {6.61, 2.75}, {2.8, 4.2}, {8.51, 2.75}, {4.0, 2.75}}};
    std::vector<jaguar::PlacedItemHandle> handles;
    handles.reserve(postSwapTranslations.size());
    for (std::size_t itemIndex = 0U;
         itemIndex < postSwapTranslations.size();
         ++itemIndex)
    {
        const auto placed = problem.PlaceItem({
            itemIndex,
            jaguar::Transform2d::FromRotationTranslation(
                0.0, postSwapTranslations[itemIndex])});
        if (!Expect(placed.HasValue(),
                    output,
                    "contained-collector witness placement must succeed"))
        {
            return false;
        }
        handles.push_back(placed.Value());
    }

    const jaguar::SPSolution state = problem.Save();
    const auto hostPlacement = std::find_if(
        state.layoutSnapshot.placements.begin(),
        state.layoutSnapshot.placements.end(),
        [&handles](const auto& placement) {
            return placement.handle == handles[0];
        });
    if (!Expect(hostPlacement != state.layoutSnapshot.placements.end(),
                output,
                "contained-collector witness host must be present"))
    {
        return false;
    }
    auto engine = state.layoutSnapshot.container.CreateCollisionEngine();
    if (!Expect(engine != nullptr
                    && engine->Restore(state.layoutSnapshot.cdeSnapshot).HasValue(),
                output,
                "contained-collector witness collision engine must restore"))
    {
        return false;
    }
    jaguar::HazardFilter filter;
    filter.irrelevant = [hostHandle = handles[0]](
                            const jaguar::HazardEntity& entity) {
        return entity.kind == jaguar::HazardKind::PlacedItem
            && entity.placedItemHandle == hostHandle;
    };
    jaguar::HazardCollector collector;
    engine->CollectPolygonCollisionsUntil(
        hostPlacement->placedItem.CollisionShape(),
        collector,
        filter,
        [](const jaguar::HazardEntity&) { return false; });

    const auto hasRawPlacedItem = [&collector](std::size_t itemIndex) {
        return std::any_of(
            collector.Entries().begin(),
            collector.Entries().end(),
            [itemIndex](const auto& entry) {
                return entry.second.kind == jaguar::HazardKind::PlacedItem
                    && entry.second.itemIndex == itemIndex;
            });
    };
    const bool rawExterior = std::any_of(
        collector.Entries().begin(),
        collector.Entries().end(),
        [](const auto& entry) {
            return entry.second.kind == jaguar::HazardKind::Exterior;
        });
    const auto containedPlacement = std::find_if(
        state.layoutSnapshot.placements.begin(),
        state.layoutSnapshot.placements.end(),
        [&handles](const auto& placement) {
            return placement.handle == handles[2];
        });
    const auto boundaryPlacement = std::find_if(
        state.layoutSnapshot.placements.begin(),
        state.layoutSnapshot.placements.end(),
        [&handles](const auto& placement) {
            return placement.handle == handles[3];
        });
    const bool geometryMatches =
        containedPlacement != state.layoutSnapshot.placements.end()
        && boundaryPlacement != state.layoutSnapshot.placements.end()
        && hostPlacement->placedItem.CollisionShape().ContainsRustF32(
            containedPlacement->placedItem.CollisionShape()
                .PointOfInaccessibility().center)
        && !hostPlacement->placedItem.CollisionShape().ContainsRustF32(
            boundaryPlacement->placedItem.CollisionShape()
                .PointOfInaccessibility().center);
    return Expect(hasRawPlacedItem(1U)
                      && hasRawPlacedItem(2U)
                      && hasRawPlacedItem(3U)
                      && rawExterior
                      && geometryMatches,
                  output,
                  "raw collisions must include exterior, counterpart, contained child, and filtered boundary child");
}

bool TestDisruptionPoolResetAfterRecoveryMatchesRust(std::ostream& output)
{
    return TestDisruptionTraceMatchesRust(
        "ExplorationPoolResetRustTrace.tsv",
        "ExplorationPoolReset.json",
        "pool_reset_after_recovery",
        42U,
        0.473,
        {{6.6, 2.0}, {4.5, 4.1}, {1.5, 2.0}},
        {1U, 2U, 1U},
        1U,
        {},
        output,
        true,
        4U,
        {},
        {},
        2U,
        true);
}

bool TestWarmStartRejectionBoundariesMatchRust(std::ostream& output)
{
    const auto readFixture = [](const char* fixtureName,
                                std::string& contents) {
        for (const std::string& path : {
                 std::string("SparrowCore/Tests/Fixtures/") + fixtureName,
                 std::string("../SparrowCore/Tests/Fixtures/") + fixtureName,
                 std::string("../JaguarRun/SparrowCore/Tests/Fixtures/")
                     + fixtureName,
                 std::string("..\\SparrowCore\\Tests\\Fixtures\\")
                     + fixtureName})
        {
            std::ifstream file(path);
            if (file.is_open())
            {
                contents.assign(std::istreambuf_iterator<char>(file),
                                std::istreambuf_iterator<char>());
                return true;
            }
        }
        return false;
    };

    std::string baseJson;
    std::string alternateJson;
    if (!Expect(readFixture("ExplorationEmptyLarge.json", baseJson)
                    && readFixture("WarmStartItemDefinitionMismatch.json",
                                   alternateJson),
                output,
                "warm-start boundary fixtures must be readable"))
    {
        return false;
    }

    const jaguar::Importer importer(sparrow::MakeRustDefaultImporterConfig());
    const auto baseInstance = importer.ImportSPInstanceJson(baseJson);
    const auto alternateInstance = importer.ImportSPInstanceJson(alternateJson);
    if (!Expect(baseInstance.HasValue() && alternateInstance.HasValue(),
                output,
                "warm-start boundary instances must import"))
    {
        return false;
    }

    auto sourceResult = jaguar::SPProblem::Create(baseInstance.Value());
    if (!Expect(sourceResult.HasValue(),
                output,
                "warm-start boundary source problem must be created"))
    {
        return false;
    }
    jaguar::SPProblem source = std::move(sourceResult).Value();
    if (!Expect(source.ChangeStripWidth(8.0).HasValue()
                    && source.PlaceItem({1U, {0.0, {1.5, 2.5}}}).HasValue(),
                output,
                "valid warm-start source must be constructible"))
    {
        return false;
    }
    const jaguar::SPSolution valid = source.Save();

    // Layout permits direct snapshot construction beyond SPProblem demand;
    // this mirrors the Rust reference's low-level fixture construction.
    jaguar::Layout overDemandLayout(valid.layoutSnapshot.container);
    if (!Expect(overDemandLayout.PlaceItem(
                    baseInstance.Value().GetItem(1U), {0.0, {1.5, 2.5}})
                    .HasValue()
                    && overDemandLayout.PlaceItem(
                           baseInstance.Value().GetItem(1U),
                           {0.0, {4.5, 2.5}})
                           .HasValue(),
                output,
                "over-demand snapshot layout must be constructible"))
    {
        return false;
    }
    jaguar::SPSolution overDemand = valid;
    overDemand.layoutSnapshot = overDemandLayout.Save();
    auto overDemandTargetResult = jaguar::SPProblem::Create(baseInstance.Value());
    if (!Expect(overDemandTargetResult.HasValue(),
                output,
                "over-demand target problem must be created"))
    {
        return false;
    }
    jaguar::SPProblem overDemandTarget =
        std::move(overDemandTargetResult).Value();
    const double originalOverDemandWidth = overDemandTarget.StripWidth();
    bool overDemandPanicked = false;
    bool overDemandRestoreSucceeded = false;
    try
    {
        overDemandRestoreSucceeded =
            overDemandTarget.Restore(overDemand).HasValue();
    }
    catch (const std::underflow_error&)
    {
        overDemandPanicked = true;
    }
#ifndef NDEBUG
    if (!Expect(overDemandPanicked
                    && overDemandTarget.PlacedItemCount() == 2U
                    && overDemandTarget.RemainingDemand(1U) == 0U
                    && overDemandTarget.StripWidth() == overDemand.StripWidth(),
                output,
                "Debug restore must match Rust's partial state before demand underflow"))
#else
    if (!Expect(!overDemandPanicked && overDemandRestoreSucceeded
                    && overDemandTarget.PlacedItemCount() == 2U
                    && overDemandTarget.RemainingDemand(1U)
                           == std::numeric_limits<std::size_t>::max()
                    && overDemandTarget.StripWidth() == overDemand.StripWidth(),
                output,
                "Release restore must match Rust's wrapping over-demand count"))
#endif
    {
        return false;
    }

    auto alternateProblemResult =
        jaguar::SPProblem::Create(alternateInstance.Value());
    if (!Expect(alternateProblemResult.HasValue(),
                output,
                "alternate warm-start problem must be created"))
    {
        return false;
    }
    jaguar::SPProblem alternateProblem =
        std::move(alternateProblemResult).Value();
    if (!Expect(alternateProblem
                    .PlaceItem({1U, {0.0, {1.5, 2.5}}})
                    .HasValue(),
                output,
                "alternate-definition warm start must be constructible"))
    {
        return false;
    }
    const jaguar::SPSolution alternateDefinition = alternateProblem.Save();
    const double alternateItemArea =
        alternateDefinition.layoutSnapshot.placements[0].placedItem.Area();

    auto identityTargetResult = jaguar::SPProblem::Create(baseInstance.Value());
    if (!Expect(identityTargetResult.HasValue(),
                output,
                "item-definition target problem must be created"))
    {
        return false;
    }
    jaguar::SPProblem identityTarget = std::move(identityTargetResult).Value();
    const auto identityRestore = identityTarget.Restore(alternateDefinition);
    if (!Expect(identityRestore.HasValue()
                    && identityTarget.PlacedItemCount() == 1U
                    && identityTarget.RemainingDemand(1U) == 0U
                    && identityTarget.Save().layoutSnapshot.placements[0]
                           .placedItem.Area()
                           == alternateItemArea,
                output,
                "restore must accept same-index Rust item-definition mismatch"))
    {
        return false;
    }

    sparrow::SparrowConfig config;
    config.seed = 42U;
    config.warmStart = alternateDefinition;
    config.exploration.timeLimit = std::chrono::nanoseconds{0};
    config.compression.timeLimit = std::chrono::nanoseconds{0};
    RustOptimizeReferenceListener listener;
    const auto optimized = sparrow::Optimize(
        baseInstance.Value(), config, &listener);
    if (!Expect(optimized.HasValue()
                    && listener.reports
                           == std::vector<std::string>{"expl_feas", "final"}
                    && optimized.Value().solution.layoutSnapshot.placements.size()
                           == 1U
                    && optimized.Value().solution.layoutSnapshot.placements[0]
                           .placedItem.Area()
                           == alternateItemArea,
                output,
                "optimize must preserve and report Rust's alternate definition warm start"))
    {
        return false;
    }

    jaguar::SPSolution malformedStructure = valid;
    malformedStructure.layoutSnapshot.cdeSnapshot.dynamicHazards.clear();
    auto malformedTargetResult = jaguar::SPProblem::Create(baseInstance.Value());
    if (!Expect(malformedTargetResult.HasValue(),
                output,
                "malformed-snapshot target problem must be created"))
    {
        return false;
    }
    jaguar::SPProblem malformedTarget =
        std::move(malformedTargetResult).Value();
    const double originalMalformedWidth = malformedTarget.StripWidth();
    bool malformedPanicked = false;
    bool malformedRestoreSucceeded = false;
    try
    {
        malformedRestoreSucceeded =
            malformedTarget.Restore(malformedStructure).HasValue();
    }
    catch (const std::logic_error&)
    {
        malformedPanicked = true;
    }
#ifndef NDEBUG
    if (!Expect(malformedPanicked
                    && malformedTarget.PlacedItemCount() == 0U
                    && malformedTarget.RemainingDemand(1U) == 1U
                    && malformedTarget.StripWidth() == originalMalformedWidth,
                output,
                "Debug restore must panic while rebuilding inconsistent CDE without replacing target"))
#else
    if (!Expect(!malformedPanicked && malformedRestoreSucceeded
                    && malformedTarget.PlacedItemCount() == 1U
                    && malformedTarget.RemainingDemand(1U) == 0U
                    && malformedTarget.StripWidth()
                           == malformedStructure.StripWidth()
                    && malformedTarget.Save().layoutSnapshot.cdeSnapshot
                           .dynamicHazards.empty(),
                output,
                "Release restore must preserve Rust's accepted incomplete CDE snapshot"))
#endif
    {
        return false;
    }

    sparrow::SparrowConfig malformedConfig;
    malformedConfig.seed = 42U;
    malformedConfig.warmStart = malformedStructure;
    malformedConfig.exploration.timeLimit = std::chrono::nanoseconds{0};
    malformedConfig.compression.timeLimit = std::chrono::nanoseconds{0};
    RecordingListener malformedListener;
    bool malformedOptimizePanicked = false;
    bool malformedOptimizeReturned = false;
    try
    {
        const auto malformedOptimized = sparrow::Optimize(
            baseInstance.Value(), malformedConfig, &malformedListener);
        malformedOptimizeReturned = malformedOptimized.HasValue();
    }
    catch (const std::logic_error&)
    {
        malformedOptimizePanicked = true;
    }
    if (!Expect(malformedOptimizePanicked && !malformedOptimizeReturned
                    && malformedListener.progressCount == 0U
                    && malformedListener.improvementCount == 0U
                    && malformedListener.solutionCount == 0U,
                output,
                "inconsistent CDE warm start must fail before callbacks as Rust does"))
    {
        return false;
    }

    auto partialSourceResult = jaguar::SPProblem::Create(baseInstance.Value());
    if (!Expect(partialSourceResult.HasValue(),
                output,
                "partial-hazard source problem must be created"))
    {
        return false;
    }
    jaguar::SPProblem partialSource = std::move(partialSourceResult).Value();
    if (!Expect(partialSource.ChangeStripWidth(8.0).HasValue()
                    && partialSource.PlaceItem(
                           {1U, {0.0, {1.5, 2.5}}}).HasValue()
                    && partialSource.PlaceItem(
                           {2U, {0.0, {4.5, 2.5}}}).HasValue(),
                output,
                "partial-hazard source placements must be constructible"))
    {
        return false;
    }
    jaguar::SPSolution partialHazard = partialSource.Save();
    if (!Expect(partialHazard.layoutSnapshot.placements.size() == 2U
                    && partialHazard.layoutSnapshot.cdeSnapshot
                           .dynamicHazards.size() == 2U,
                output,
                "partial-hazard source snapshot must contain two placements and hazards"))
    {
        return false;
    }
    partialHazard.layoutSnapshot.cdeSnapshot.dynamicHazards.erase(
        partialHazard.layoutSnapshot.cdeSnapshot.dynamicHazards.begin());

    auto partialTargetResult = jaguar::SPProblem::Create(baseInstance.Value());
    if (!Expect(partialTargetResult.HasValue(),
                output,
                "partial-hazard target problem must be created"))
    {
        return false;
    }
    jaguar::SPProblem partialTarget = std::move(partialTargetResult).Value();
    const double partialOriginalWidth = partialTarget.StripWidth();
    bool partialRestorePanicked = false;
    bool partialRestoreSucceeded = false;
    try
    {
        partialRestoreSucceeded = partialTarget.Restore(partialHazard).HasValue();
    }
    catch (const std::logic_error&)
    {
        partialRestorePanicked = true;
    }
#ifndef NDEBUG
    if (!Expect(partialRestorePanicked
                    && partialTarget.PlacedItemCount() == 0U
                    && partialTarget.RemainingDemand(1U) == 1U
                    && partialTarget.RemainingDemand(2U) == 1U
                    && partialTarget.StripWidth() == partialOriginalWidth,
                output,
                "Debug partial-hazard restore must fail before replacing target as Rust does"))
#else
    if (!Expect(!partialRestorePanicked && partialRestoreSucceeded
                    && partialTarget.PlacedItemCount() == 2U
                    && partialTarget.RemainingDemand(1U) == 0U
                    && partialTarget.RemainingDemand(2U) == 0U
                    && partialTarget.Save().layoutSnapshot.cdeSnapshot
                           .dynamicHazards.size() == 1U,
                output,
                "Release partial-hazard restore must preserve Rust's incomplete CDE snapshot"))
#endif
    {
        return false;
    }

    sparrow::SparrowConfig partialConfig;
    partialConfig.seed = 42U;
    partialConfig.warmStart = partialHazard;
    partialConfig.exploration.timeLimit = std::chrono::nanoseconds{0};
    partialConfig.compression.timeLimit = std::chrono::nanoseconds{0};
    RecordingListener partialListener;
    bool partialOptimizePanicked = false;
    bool partialOptimizeReturned = false;
    try
    {
        const auto partialOptimized = sparrow::Optimize(
            baseInstance.Value(), partialConfig, &partialListener);
        partialOptimizeReturned = partialOptimized.HasValue();
    }
    catch (const std::logic_error&)
    {
        partialOptimizePanicked = true;
    }
    if (!Expect(partialOptimizePanicked && !partialOptimizeReturned
                    && partialListener.progressCount == 0U
                    && partialListener.improvementCount == 0U
                    && partialListener.solutionCount == 0U,
                output,
                "partial CDE warm start must fail before callbacks as Rust does"))
    {
        return false;
    }

    auto outOfRangeSourceResult = jaguar::SPProblem::Create(baseInstance.Value());
    if (!Expect(outOfRangeSourceResult.HasValue(),
                output,
                "out-of-range source problem must be created"))
    {
        return false;
    }
    jaguar::SPProblem outOfRangeSource =
        std::move(outOfRangeSourceResult).Value();
    if (!Expect(outOfRangeSource.ChangeStripWidth(8.0).HasValue()
                    && outOfRangeSource.PlaceItem(
                           {2U, {0.0, {4.5, 2.5}}}).HasValue(),
                output,
                "out-of-range source snapshot must be constructible"))
    {
        return false;
    }
    const jaguar::SPSolution outOfRangeSourceSolution = outOfRangeSource.Save();
    const jaguar::Item& itemTemplate = baseInstance.Value().GetItem(2U);
    const jaguar::ItemIndex outOfRangeItemIndex =
        baseInstance.Value().ItemCount();
    const auto outOfRangeItemResult = jaguar::Item::Create(
        outOfRangeItemIndex,
        itemTemplate.ExternalId(),
        itemTemplate.GetOriginalShape(),
        itemTemplate.GetSurrogateConfig(),
        itemTemplate.MinQuality(),
        itemTemplate.AllowedRotation());
    if (!Expect(outOfRangeItemResult.HasValue(),
                output,
                "out-of-range snapshot item must be constructible"))
    {
        return false;
    }
    jaguar::Layout outOfRangeLayout(
        outOfRangeSourceSolution.layoutSnapshot.container);
    if (!Expect(outOfRangeLayout
                    .PlaceItem(outOfRangeItemResult.Value(),
                               {0.0, {4.5, 2.5}})
                    .HasValue(),
                output,
                "internally consistent out-of-range layout must be constructible"))
    {
        return false;
    }
    jaguar::SPSolution outOfRangeSolution = outOfRangeSourceSolution;
    outOfRangeSolution.layoutSnapshot = outOfRangeLayout.Save();

    auto outOfRangeTargetResult = jaguar::SPProblem::Create(baseInstance.Value());
    if (!Expect(outOfRangeTargetResult.HasValue(),
                output,
                "out-of-range target problem must be created"))
    {
        return false;
    }
    jaguar::SPProblem outOfRangeTarget =
        std::move(outOfRangeTargetResult).Value();
    const double outOfRangeInitialWidth = outOfRangeTarget.StripWidth();
    std::vector<std::size_t> outOfRangeInitialDemands;
    for (std::size_t itemIndex = 0U;
         itemIndex < baseInstance.Value().ItemCount();
         ++itemIndex)
    {
        outOfRangeInitialDemands.push_back(
            baseInstance.Value().ItemDemand(itemIndex));
    }
    bool outOfRangeRestoreThrew = false;
    bool outOfRangeRestoreSucceeded = false;
    try
    {
        outOfRangeRestoreSucceeded =
            outOfRangeTarget.Restore(outOfRangeSolution).HasValue();
    }
    catch (const std::out_of_range&)
    {
        outOfRangeRestoreThrew = true;
    }
    std::vector<std::size_t> outOfRangeRestoredDemands;
    for (std::size_t itemIndex = 0U;
         itemIndex < baseInstance.Value().ItemCount();
         ++itemIndex)
    {
        outOfRangeRestoredDemands.push_back(
            outOfRangeTarget.RemainingDemand(itemIndex));
    }
    const auto outOfRangeRestoredSnapshot = outOfRangeTarget.Save();
    if (!Expect(outOfRangeRestoreThrew && !outOfRangeRestoreSucceeded
                    && outOfRangeTarget.PlacedItemCount() == 1U
                    && outOfRangeRestoredSnapshot.layoutSnapshot.placements[0]
                           .placedItem.GetItemIndex()
                           == outOfRangeItemIndex
                    && outOfRangeRestoredDemands == outOfRangeInitialDemands
                    && outOfRangeTarget.StripWidth()
                           == outOfRangeSolution.StripWidth()
                    && outOfRangeTarget.StripWidth() != outOfRangeInitialWidth,
                output,
                "out-of-range restore must match Rust's post-layout partial state"))
    {
        return false;
    }

    sparrow::SparrowConfig outOfRangeConfig;
    outOfRangeConfig.seed = 42U;
    outOfRangeConfig.warmStart = outOfRangeSolution;
    outOfRangeConfig.exploration.timeLimit = std::chrono::nanoseconds{0};
    outOfRangeConfig.compression.timeLimit = std::chrono::nanoseconds{0};
    RecordingListener outOfRangeListener;
    bool outOfRangeOptimizeThrew = false;
    bool outOfRangeOptimizeReturned = false;
    try
    {
        const auto outOfRangeOptimized = sparrow::Optimize(
            baseInstance.Value(), outOfRangeConfig, &outOfRangeListener);
        outOfRangeOptimizeReturned = outOfRangeOptimized.HasValue();
    }
    catch (const std::out_of_range&)
    {
        outOfRangeOptimizeThrew = true;
    }
    if (!Expect(outOfRangeOptimizeThrew && !outOfRangeOptimizeReturned
                    && outOfRangeListener.progressCount == 0U
                    && outOfRangeListener.improvementCount == 0U
                    && outOfRangeListener.solutionCount == 0U,
                output,
                "out-of-range warm start must fail before callbacks as Rust does"))
    {
        return false;
    }

    auto orderedSourceResult = jaguar::SPProblem::Create(baseInstance.Value());
    if (!Expect(orderedSourceResult.HasValue(),
                output,
                "ordered out-of-range source problem must be created"))
    {
        return false;
    }
    jaguar::SPProblem orderedSource = std::move(orderedSourceResult).Value();
    if (!Expect(orderedSource.ChangeStripWidth(8.0).HasValue()
                    && orderedSource.PlaceItem(
                           {1U, {0.0, {1.5, 2.5}}}).HasValue()
                    && orderedSource.PlaceItem(
                           {2U, {0.0, {4.5, 2.5}}}).HasValue(),
                output,
                "ordered out-of-range source placements must be constructible"))
    {
        return false;
    }
    const jaguar::SPSolution orderedSourceSolution = orderedSource.Save();
    jaguar::Layout orderedLayout(
        orderedSourceSolution.layoutSnapshot.container);
    if (!Expect(orderedLayout
                    .PlaceItem(baseInstance.Value().GetItem(1U),
                               {0.0, {1.5, 2.5}})
                    .HasValue()
                    && orderedLayout
                           .PlaceItem(outOfRangeItemResult.Value(),
                                      {0.0, {4.5, 2.5}})
                           .HasValue(),
                output,
                "ordered layout must place valid item before rogue item"))
    {
        return false;
    }
    jaguar::SPSolution orderedSolution = orderedSourceSolution;
    orderedSolution.layoutSnapshot = orderedLayout.Save();

    auto orderedTargetResult = jaguar::SPProblem::Create(baseInstance.Value());
    if (!Expect(orderedTargetResult.HasValue(),
                output,
                "ordered out-of-range target problem must be created"))
    {
        return false;
    }
    jaguar::SPProblem orderedTarget = std::move(orderedTargetResult).Value();
    const double orderedInitialWidth = orderedTarget.StripWidth();
    bool orderedRestoreThrew = false;
    bool orderedRestoreSucceeded = false;
    try
    {
        orderedRestoreSucceeded = orderedTarget.Restore(orderedSolution).HasValue();
    }
    catch (const std::out_of_range&)
    {
        orderedRestoreThrew = true;
    }
    std::vector<std::size_t> orderedRestoredDemands;
    for (std::size_t itemIndex = 0U;
         itemIndex < baseInstance.Value().ItemCount();
         ++itemIndex)
    {
        orderedRestoredDemands.push_back(orderedTarget.RemainingDemand(itemIndex));
    }
    const auto orderedRestoredSnapshot = orderedTarget.Save();
    if (!Expect(orderedRestoreThrew && !orderedRestoreSucceeded
                    && orderedTarget.PlacedItemCount() == 2U
                    && orderedRestoredSnapshot.layoutSnapshot.placements[0]
                           .placedItem.GetItemIndex()
                           == 1U
                    && orderedRestoredSnapshot.layoutSnapshot.placements[1]
                           .placedItem.GetItemIndex()
                           == outOfRangeItemIndex
                    && orderedRestoredDemands
                           == std::vector<std::size_t>{1U, 0U, 1U}
                    && orderedTarget.StripWidth() == orderedSolution.StripWidth()
                    && orderedTarget.StripWidth() != orderedInitialWidth,
                output,
                "ordered restore must retain earlier demand decrement before out-of-range failure"))
    {
        return false;
    }

    sparrow::SparrowConfig orderedConfig = outOfRangeConfig;
    orderedConfig.warmStart = orderedSolution;
    RecordingListener orderedListener;
    bool orderedOptimizeThrew = false;
    bool orderedOptimizeReturned = false;
    try
    {
        const auto orderedOptimized = sparrow::Optimize(
            baseInstance.Value(), orderedConfig, &orderedListener);
        orderedOptimizeReturned = orderedOptimized.HasValue();
    }
    catch (const std::out_of_range&)
    {
        orderedOptimizeThrew = true;
    }
    return Expect(orderedOptimizeThrew && !orderedOptimizeReturned
                      && orderedListener.progressCount == 0U
                      && orderedListener.improvementCount == 0U
                      && orderedListener.solutionCount == 0U,
                  output,
                  "ordered out-of-range warm start must fail before callbacks as Rust does");
}

bool TestEmptyLargeAndPartialWarmStartMatchRust(
    std::ostream& output)
{
    std::ifstream instanceFile;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/ExplorationEmptyLarge.json",
             "../SparrowCore/Tests/Fixtures/ExplorationEmptyLarge.json",
             "../JaguarRun/SparrowCore/Tests/Fixtures/ExplorationEmptyLarge.json",
             "..\\SparrowCore\\Tests\\Fixtures\\ExplorationEmptyLarge.json"})
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
                "empty-large Rust/C++ fixture must be readable"))
    {
        return false;
    }
    const std::string instanceJson{
        std::istreambuf_iterator<char>(instanceFile),
        std::istreambuf_iterator<char>()};
    const jaguar::Importer importer(sparrow::MakeRustDefaultImporterConfig());
    const auto instance = importer.ImportSPInstanceJson(instanceJson);
    if (!Expect(instance.HasValue(),
                output,
                "empty-large fixture must import"))
    {
        return false;
    }

    auto problemResult = jaguar::SPProblem::Create(instance.Value());
    if (!Expect(problemResult.HasValue(),
                output,
                "empty-large problem must be created"))
    {
        return false;
    }
    jaguar::SPProblem problem = std::move(problemResult.Value());
    if (!Expect(problem.ChangeStripWidth(8.0).HasValue()
                    && problem.PlaceItem({1U, {0.0, {1.5, 2.5}}}).HasValue()
                    && problem.PlaceItem({2U, {0.0, {6.5, 2.5}}}).HasValue(),
                output,
                "empty-large partial layout must be constructible"))
    {
        return false;
    }
    const jaguar::SPSolution initial = problem.Save();

    std::ifstream referenceFile;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/ExplorationEmptyLargeRustTrace.tsv",
             "../SparrowCore/Tests/Fixtures/ExplorationEmptyLargeRustTrace.tsv",
             "../JaguarRun/SparrowCore/Tests/Fixtures/ExplorationEmptyLargeRustTrace.tsv",
             "..\\SparrowCore\\Tests\\Fixtures\\ExplorationEmptyLargeRustTrace.tsv"})
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
                "empty-large Rust trace must be readable"))
    {
        return false;
    }
    std::vector<std::string> expectedTrace;
    std::string line;
    while (std::getline(referenceFile, line))
    {
        if (!line.empty() && line.front() != '#')
        {
            expectedTrace.push_back(line);
        }
    }

    sparrow::ExplorationConfig config;
    config.maxIterations = 2U;
    config.shrinkStep = 0.5;
    config.largeItemChAreaCutoffPercentile = 0.75;
    config.maxConsecutiveFailures = std::nullopt;
    config.separator.strikeLimit = 0U;
    config.separator.nWorkers = 1U;
    config.separator.nContainerSamples = 0U;
    config.separator.nFocussedSamples = 0U;
    config.separator.nCoordinateDescents = 0U;
    sparrow::OptimizationTerminator terminator({
        32U, std::chrono::milliseconds{2000}});
    std::vector<sparrow::detail::ExplorationTraceEvent> events;
    class ReportCapture final : public sparrow::OptimizationListener
    {
    public:
        void OnSolution(
            const sparrow::OptimizationSolutionReport& report) override
        {
            reports.push_back(report);
        }

        std::vector<sparrow::OptimizationSolutionReport> reports;
    } listener;
    const auto result = sparrow::detail::ExploreWithTrace(
        instance.Value(),
        initial,
        config,
        42U,
        terminator,
        &listener,
        [&events](const auto& event) { events.push_back(event); });

    if (!Expect(!result.HasValue(),
                output,
                "empty large-item pool must fail instead of disrupting all placed items"))
    {
        output << "C++ returned a value and recorded " << events.size()
               << " trace events\n";
        return false;
    }
    if (!Expect(result.GetError().code == jaguar::ErrorCode::Infeasible
                    && result.GetError().message
                        == "[DSRP] failed to choose first item",
                output,
                "empty large-item pool must preserve the Rust disruption failure"))
    {
        output << "error code=" << static_cast<int>(result.GetError().code)
               << " message=" << result.GetError().message << '\n';
        return false;
    }
    const bool initialReportsMatchRust = Expect(
        listener.reports.size() == 2U
            && listener.reports[0].type
                == sparrow::OptimizationReportType::OPTIMIZATION_REPORT_EXPLORATION_FEASIBLE
            && !listener.reports[0].isFeasible
            && listener.reports[1].type
                == sparrow::OptimizationReportType::OPTIMIZATION_REPORT_EXPLORATION_INFEASIBLE,
        output,
        "partial initial report type must match Rust while retaining C++ demand feasibility");
    if (!initialReportsMatchRust)
    {
        for (const auto& report : listener.reports)
        {
            output << "  report type=" << static_cast<int>(report.type)
                   << " isFeasible=" << report.isFeasible << '\n';
        }
    }
    if (!Expect(events.size() == 2U
                    && events[0].type
                        == sparrow::detail::ExplorationTraceEvent::Type::PoolInsert
                    && events[1].type
                        == sparrow::detail::ExplorationTraceEvent::Type::PoolSample,
                output,
                "failure and pool sampling must occur before the empty-pool error"))
    {
        return false;
    }

    std::vector<std::string> actualTrace;
    for (const auto& event : events)
    {
        std::ostringstream row;
        if (event.type
            == sparrow::detail::ExplorationTraceEvent::Type::PoolInsert)
        {
            row << "insert " << event.failureId << ' '
                << event.insertionIndex << ' ' << event.poolOrder.size();
            for (const std::size_t candidateId : event.poolOrder)
            {
                row << ' ' << candidateId;
            }
        }
        else if (event.type
                 == sparrow::detail::ExplorationTraceEvent::Type::PoolSample)
        {
            std::uint32_t rawSampleBits = 0U;
            std::uint32_t mappedSampleBits = 0U;
            std::memcpy(&rawSampleBits, &event.rawSample, sizeof(rawSampleBits));
            std::memcpy(&mappedSampleBits,
                        &event.mappedSample,
                        sizeof(mappedSampleBits));
            row << "sample " << event.failureId << ' ' << std::hex
                << std::setw(8) << std::setfill('0') << rawSampleBits << ' '
                << std::setw(8) << std::setfill('0') << mappedSampleBits
                << std::dec << ' ' << event.selectedIndex << ' '
                << event.consumedU64Words << ' ' << std::hex << std::setw(2)
                << std::setfill('0') << event.firstLayerIndex;
            for (const std::uint64_t value : event.followingU64)
            {
                row << ' ' << std::setw(16) << std::setfill('0') << value;
            }
        }
        else
        {
            row << "move " << event.failureId;
        }
        actualTrace.push_back(row.str());
    }
    if (!Expect(actualTrace == expectedTrace,
                output,
                "C++ pre-error pool/RNG trace differs from Rust"))
    {
        for (const auto& row : actualTrace)
        {
            output << "  actual " << row << '\n';
        }
        for (const auto& row : expectedTrace)
        {
            output << "expected " << row << '\n';
        }
        return false;
    }

    const auto failureReport = std::find_if(
        listener.reports.begin(), listener.reports.end(),
        [](const sparrow::OptimizationSolutionReport& report) {
            return report.type
                       == sparrow::OptimizationReportType::OPTIMIZATION_REPORT_EXPLORATION_INFEASIBLE
                && report.collisionLoss > 0.0F;
        });
    if (!Expect(failureReport != listener.reports.end()
                    && std::abs(failureReport->stripWidth - 4.0) < 1.0e-6
                    && !failureReport->isFeasible
                    && failureReport->solution.layoutSnapshot.placements.size()
                        == 2U,
                output,
                "C++ must report the narrowed failed snapshot before disruption"))
    {
        return false;
    }
    std::map<std::size_t, std::pair<double, double>> failurePoses;
    for (const auto& placement :
         failureReport->solution.layoutSnapshot.placements)
    {
        const std::size_t itemIndex = placement.placedItem.GetItemIndex();
        const auto& translation = placement.placedItem.Transform().translation;
        failurePoses[itemIndex] = {translation.x, translation.y};
    }
    const bool failureSnapshotMatches = Expect(
        failurePoses.size() == 2U
            && std::abs(failurePoses[1U].first - 1.5) < 1.0e-6
            && std::abs(failurePoses[1U].second - 2.5) < 1.0e-6
            && std::abs(failurePoses[2U].first - 2.5) < 1.0e-6
            && std::abs(failurePoses[2U].second - 2.5) < 1.0e-6,
        output,
        "C++ failure snapshot must match Rust before the empty-pool error");

    sparrow::SparrowConfig warmStartConfig;
    warmStartConfig.seed = 42U;
    warmStartConfig.warmStart = initial;
    sparrow::OptimizationTerminator warmStartTerminator;
    warmStartTerminator.RequestStop();
    listener.reports.clear();
    const auto warmStarted = sparrow::Optimize(
        instance.Value(),
        warmStartConfig,
        warmStartTerminator,
        &listener);
    if (!Expect(warmStarted.HasValue(),
                output,
                "top-level optimizer must accept Rust-compatible partial warm starts"))
    {
        if (!warmStarted.HasValue())
        {
            output << "  error=" << warmStarted.GetError().message << '\n';
        }
        return false;
    }
    const auto& warmStartResult = warmStarted.Value();
    const bool warmStartMatchesRust = Expect(
        warmStartResult.metrics.placedItemCount == 2U
            && warmStartResult.metrics.remainingDemand == 1U
            && !warmStartResult.metrics.feasible
            && warmStartResult.solution.layoutSnapshot.placements.size() == 2U
            && listener.reports.size() == 2U
            && listener.reports[0].type
                == sparrow::OptimizationReportType::OPTIMIZATION_REPORT_EXPLORATION_FEASIBLE
            && !listener.reports[0].isFeasible
            && listener.reports[1].type
                == sparrow::OptimizationReportType::OPTIMIZATION_REPORT_FINAL
            && !listener.reports[1].isFeasible,
        output,
        "partial warm-start metrics, report types, and final layout must match Rust");
    if (!warmStartMatchesRust)
    {
        return false;
    }

    auto overlappingProblemResult = jaguar::SPProblem::Create(instance.Value());
    if (!Expect(overlappingProblemResult.HasValue(),
                output,
                "overlapping warm-start problem must be constructible"))
    {
        return false;
    }
    jaguar::SPProblem overlappingProblem =
        std::move(overlappingProblemResult.Value());
    if (!Expect(overlappingProblem.ChangeStripWidth(8.0).HasValue()
                    && overlappingProblem.PlaceItem(
                           {1U, {0.0, {1.5, 2.5}}}).HasValue()
                    && overlappingProblem.PlaceItem(
                           {2U, {0.0, {1.5, 2.5}}}).HasValue(),
                output,
                "overlapping warm-start layout must be restorable"))
    {
        return false;
    }
    const jaguar::SPSolution overlapping = overlappingProblem.Save();
    sparrow::SparrowConfig overlappingConfig;
    overlappingConfig.seed = 42U;
    overlappingConfig.warmStart = overlapping;
    sparrow::OptimizationTerminator overlappingTerminator;
    overlappingTerminator.RequestStop();
    listener.reports.clear();
    const auto overlappingResult = sparrow::Optimize(
        instance.Value(),
        overlappingConfig,
        overlappingTerminator,
        &listener);
    if (!Expect(overlappingResult.HasValue(),
                output,
                "top-level optimizer must accept a restorable overlapping warm start"))
    {
        if (!overlappingResult.HasValue())
        {
            output << "  error=" << overlappingResult.GetError().message << '\n';
        }
        return false;
    }
    const auto& overlappingSolution = overlappingResult.Value().solution;
    jaguar::Layout finalOverlappingLayout(
        overlappingSolution.layoutSnapshot.container);
    const auto restoredFinalLayout =
        finalOverlappingLayout.Restore(overlappingSolution.layoutSnapshot);
    const bool overlappingWarmStartMatchesRust = Expect(
        restoredFinalLayout.HasValue()
            && !finalOverlappingLayout.IsFeasible()
            && overlappingResult.Value().metrics.placedItemCount == 2U
            && overlappingResult.Value().metrics.remainingDemand == 1U
            && !overlappingResult.Value().metrics.feasible
            && listener.reports.size() == 2U
            && listener.reports[0].type
                == sparrow::OptimizationReportType::OPTIMIZATION_REPORT_EXPLORATION_FEASIBLE
            && !listener.reports[0].isFeasible
            && listener.reports[1].type
                == sparrow::OptimizationReportType::OPTIMIZATION_REPORT_FINAL
            && !listener.reports[1].isFeasible,
        output,
        "restorable overlapping warm start must retain its layout and actual infeasibility");
    return initialReportsMatchRust && failureSnapshotMatches
        && warmStartMatchesRust && overlappingWarmStartMatchesRust;
}

bool TestEmptyDiscreteRotationExplorePathMatchesRustFailureBoundary(
    std::ostream& output)
{
    std::ifstream instanceFile;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/ExplorationEmptyDiscreteRotation.json",
             "../SparrowCore/Tests/Fixtures/ExplorationEmptyDiscreteRotation.json",
             "../JaguarRun/SparrowCore/Tests/Fixtures/ExplorationEmptyDiscreteRotation.json",
             "..\\SparrowCore\\Tests\\Fixtures\\ExplorationEmptyDiscreteRotation.json"})
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
                "empty-discrete fixture must be readable"))
    {
        return false;
    }
    const std::string instanceJson{
        std::istreambuf_iterator<char>(instanceFile),
        std::istreambuf_iterator<char>()};
    const jaguar::Importer importer(sparrow::MakeRustDefaultImporterConfig());
    const auto imported = importer.ImportSPInstanceJson(instanceJson);
    if (!Expect(imported.HasValue(),
                output,
                "valid singleton-zero fixture must import before internal mutation"))
    {
        return false;
    }

    std::vector<jaguar::Item> items;
    items.reserve(imported.Value().ItemCount());
    for (std::size_t index = 0U;
         index < imported.Value().ItemCount();
         ++index)
    {
        const jaguar::Item& source = imported.Value().GetItem(index);
        jaguar::RotationRange rotation = source.AllowedRotation();
        if (index == 0U)
        {
            rotation = {jaguar::RotationMode::Discrete, {}};
        }
        const auto item = jaguar::Item::Create(
            source.Index(),
            source.ExternalId(),
            source.GetOriginalShape(),
            source.GetSurrogateConfig(),
            source.MinQuality(),
            std::move(rotation));
        if (!Expect(item.HasValue(),
                    output,
                    "internal empty-discrete item must be constructible"))
        {
            return false;
        }
        items.push_back(item.Value());
    }
    const auto instance = jaguar::SPInstance::Create(
        std::move(items), {1U, 1U}, imported.Value().BaseStrip());
    if (!Expect(instance.HasValue(),
                output,
                "internal empty-discrete instance must be constructible"))
    {
        return false;
    }
    if (!Expect(instance.Value().GetItem(0U).AllowedRotation().mode
                        == jaguar::RotationMode::Discrete
                    && instance.Value().GetItem(0U).AllowedRotation().radians.empty(),
                output,
                "test setup must reach Explore with an empty discrete rotation set"))
    {
        return false;
    }

    auto problemResult = jaguar::SPProblem::Create(instance.Value());
    if (!Expect(problemResult.HasValue(),
                output,
                "empty-discrete problem must be constructible"))
    {
        return false;
    }
    jaguar::SPProblem problem = std::move(problemResult.Value());
    if (!Expect(problem.ChangeStripWidth(5.0).HasValue()
                    && problem.PlaceItem({0U, {0.0, {2.5, 1.0}}}).HasValue()
                    && problem.PlaceItem({1U, {0.0, {2.5, 1.0}}}).HasValue(),
                output,
                "overlapping seed layout must be constructible"))
    {
        return false;
    }
    const jaguar::SPSolution initial = problem.Save();
    jaguar::Layout initialLayout(initial.layoutSnapshot.container);
    if (!Expect(initialLayout.Restore(initial.layoutSnapshot).HasValue()
                    && !initialLayout.IsFeasible(),
                output,
                "overlapping seed layout must force Explore into disruption"))
    {
        return false;
    }

    std::ifstream referenceFile;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/ExplorationEmptyDiscreteRotationRustTrace.tsv",
             "../SparrowCore/Tests/Fixtures/ExplorationEmptyDiscreteRotationRustTrace.tsv",
             "../JaguarRun/SparrowCore/Tests/Fixtures/ExplorationEmptyDiscreteRotationRustTrace.tsv",
             "..\\SparrowCore\\Tests\\Fixtures\\ExplorationEmptyDiscreteRotationRustTrace.tsv"})
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
                "empty-discrete Rust trace must be readable"))
    {
        return false;
    }
    std::vector<std::string> expectedTrace;
    std::string line;
    while (std::getline(referenceFile, line))
    {
        if (!line.empty() && line.front() != '#')
        {
            expectedTrace.push_back(line);
        }
    }

    sparrow::ExplorationConfig config;
    config.maxIterations = 2U;
    config.shrinkStep = 0.1;
    config.largeItemChAreaCutoffPercentile = 0.75;
    config.maxConsecutiveFailures = std::nullopt;
    config.separator.strikeLimit = 0U;
    config.separator.nWorkers = 1U;
    config.separator.nContainerSamples = 0U;
    config.separator.nFocussedSamples = 0U;
    config.separator.nCoordinateDescents = 0U;
    sparrow::OptimizationTerminator terminator({
        32U, std::chrono::milliseconds{2000}});
    std::vector<sparrow::detail::ExplorationTraceEvent> events;
    class ReportCapture final : public sparrow::OptimizationListener
    {
    public:
        void OnSolution(
            const sparrow::OptimizationSolutionReport& report) override
        {
            reports.push_back(report);
        }

        std::vector<sparrow::OptimizationSolutionReport> reports;
    } listener;
    const auto result = sparrow::detail::ExploreWithTrace(
        instance.Value(),
        initial,
        config,
        42U,
        terminator,
        &listener,
        [&events](const auto& event) { events.push_back(event); });

    if (!Expect(!result.HasValue(),
                output,
                "empty discrete rotation must fail during actual Explore disruption"))
    {
        output << "C++ returned a value and recorded " << events.size()
               << " trace events\n";
        return false;
    }
    if (!Expect(result.GetError().code == jaguar::ErrorCode::Infeasible
                    && result.GetError().message
                        == "called `Option::unwrap()` on a `None` value",
                output,
                "empty rotation legalization must map the Rust panic to an explicit error"))
    {
        output << "error code=" << static_cast<int>(result.GetError().code)
               << " message=" << result.GetError().message << '\n';
        return false;
    }
    if (!Expect(listener.reports.size() == 2U
                    && listener.reports[0].type
                        == sparrow::OptimizationReportType::OPTIMIZATION_REPORT_EXPLORATION_FEASIBLE
                    && !listener.reports[0].isFeasible
                    && listener.reports[1].type
                        == sparrow::OptimizationReportType::OPTIMIZATION_REPORT_EXPLORATION_INFEASIBLE
                    && !listener.reports[1].isFeasible,
                output,
                "failed layout must be reported before empty-angle legalization fails"))
    {
        return false;
    }
    if (!Expect(events.size() == 2U
                    && events[0].type
                        == sparrow::detail::ExplorationTraceEvent::Type::PoolInsert
                    && events[1].type
                        == sparrow::detail::ExplorationTraceEvent::Type::PoolSample,
                output,
                "failure-pool sampling must precede the rotation failure, with no moves"))
    {
        return false;
    }

    std::vector<std::string> actualTrace;
    for (const auto& event : events)
    {
        std::ostringstream row;
        if (event.type
            == sparrow::detail::ExplorationTraceEvent::Type::PoolInsert)
        {
            row << "insert " << event.failureId << ' '
                << event.insertionIndex << ' ' << event.poolOrder.size();
            for (const std::size_t candidateId : event.poolOrder)
            {
                row << ' ' << candidateId;
            }
        }
        else if (event.type
                 == sparrow::detail::ExplorationTraceEvent::Type::PoolSample)
        {
            std::uint32_t rawSampleBits = 0U;
            std::uint32_t mappedSampleBits = 0U;
            std::memcpy(&rawSampleBits, &event.rawSample, sizeof(rawSampleBits));
            std::memcpy(&mappedSampleBits,
                        &event.mappedSample,
                        sizeof(mappedSampleBits));
            row << "sample " << event.failureId << ' ' << std::hex
                << std::setw(8) << std::setfill('0') << rawSampleBits << ' '
                << std::setw(8) << std::setfill('0') << mappedSampleBits
                << std::dec << ' ' << event.selectedIndex << ' '
                << event.consumedU64Words << ' ' << std::hex << std::setw(2)
                << std::setfill('0') << event.firstLayerIndex;
            for (const std::uint64_t value : event.followingU64)
            {
                row << ' ' << std::setw(16) << std::setfill('0') << value;
            }
        }
        else
        {
            row << "move " << event.failureId;
        }
        actualTrace.push_back(row.str());
    }
    if (!Expect(actualTrace == expectedTrace,
                output,
                "pre-error pool and RNG trace must match the Rust oracle"))
    {
        for (const auto& row : actualTrace)
        {
            output << "  actual " << row << '\n';
        }
        for (const auto& row : expectedTrace)
        {
            output << "expected " << row << '\n';
        }
        return false;
    }

    const auto& failure = listener.reports.back();
    std::map<std::size_t, jaguar::Transform2d> poses;
    for (const auto& placement : failure.solution.layoutSnapshot.placements)
    {
        poses.emplace(placement.placedItem.GetItemIndex(),
                      placement.placedItem.Transform());
    }
    return Expect(failure.stripWidth == 5.0
                      && failure.collisionLoss == 2.602527618F
                      && poses.size() == 2U
                      && poses[0U].rotationRad == 0.0
                      && poses[1U].rotationRad == 0.0
                      && poses[0U].translation == jaguar::Point2d{2.5, 1.0}
                      && poses[1U].translation == jaguar::Point2d{2.5, 1.0},
                  output,
                  "failure snapshot must remain unchanged because legalization fails before moves");
}

bool TestDisruptionDistinctPoolMatchesRust(std::ostream& output)
{
    return TestDisruptionTraceMatchesRust(
        "ExplorationDistinctPoolRustTrace.tsv",
        "ExplorationDistinctPool.json",
        "distinct_pool",
        43U,
        1.0,
        {{0.75, 2.5}, {2.0, 2.5}, {4.0, 2.5}, {7.5, 2.5}},
        {2U, 2U, 3U, 3U},
        2U,
        {},
        output,
        false,
        4U,
        {},
        {16.0, 4.0, 1.0, 1.0});
}

bool TestDisruptionDistinctPoolSingleCandidateMatchesRust(
    std::ostream& output)
{
    return TestDisruptionTraceMatchesRust(
        "ExplorationDistinctPoolSingleCandidateRustTrace.tsv",
        "ExplorationDistinctPoolSingleCandidate.json",
        "distinct_pool_single",
        43U,
        1.0,
        {{6.0, 2.5}, {3.5, 2.5}},
        {1U, 1U},
        1U,
        {},
        output,
        false,
        4U,
        {},
        {9.0, 1.0});
}

bool TestDisruptionDistinctPoolThreeCandidatesMatchesRust(std::ostream& output)
{
    return TestDisruptionTraceMatchesRust(
        "ExplorationDistinctPoolThreeCandidatesRustTrace.tsv",
        "ExplorationDistinctPool.json",
        "distinct_pool_three",
        10U,
        1.0,
        {{0.75, 2.5}, {2.0, 2.5}, {4.0, 2.5}, {7.5, 2.5}},
        {2U, 2U, 3U, 3U},
        3U,
        {},
        output,
        false,
        4U,
        {},
        {16.0, 4.0, 1.0, 1.0});
}

bool TestDisruptionDistinctPoolFourCandidatesMatchesRust(std::ostream& output)
{
    return TestDisruptionTraceMatchesRust(
        "ExplorationDistinctPoolFourCandidatesRustTrace.tsv",
        "ExplorationDistinctPoolFourCandidates.json",
        "distinct_pool_four",
        0U,
        1.0,
        {{1.0, 9.0}, {2.75, 9.0}, {6.0, 9.0}, {12.25, 9.0}, {24.5, 9.0}},
        {4U, 4U, 4U, 4U, 4U},
        4U,
        {},
        output,
        false,
        4U,
        {},
        {256.0, 64.0, 16.0, 4.0, 1.0});
}

bool TestDisruptionDistinctPoolFourCandidatesNoneRotationMatchesRust(
    std::ostream& output)
{
    return TestDisruptionTraceMatchesRust(
        "ExplorationDistinctPoolFourCandidatesNoneRotationRustTrace.tsv",
        "ExplorationDistinctPoolFourCandidatesNoneRotation.json",
        "distinct_pool_four_none_rotation",
        0U,
        1.0,
        {{1.0, 9.0}, {2.75, 9.0}, {6.0, 9.0}, {12.25, 9.0}, {24.5, 9.0}},
        {4U, 4U, 4U, 4U, 4U},
        4U,
        {{0U, 3U}, {2U, 3U}},
        output,
        false,
        4U,
        {},
        {256.0, 64.0, 16.0, 4.0, 1.0},
        std::nullopt,
        false,
        jaguar::RotationMode::None);
}

bool TestContinuousRotationDisruptionAndContainedRelocationMatchesRust(
    std::ostream& output)
{
    constexpr float obliqueRotation = 0.645771801F;
    const float sinOblique = std::sin(obliqueRotation);
    const float cosOblique = std::cos(obliqueRotation);
    const float childX = 7.6F + (2.3F * cosOblique - 0.2F * sinOblique);
    const float childY = 2.75F + (2.3F * sinOblique + 0.2F * cosOblique);
    return TestDisruptionTraceMatchesRust(
        "ExplorationObliqueContainedRelocationContinuousRustTrace.tsv",
        "ExplorationObliqueContainedRelocationContinuous.json",
        "continuous_oblique_contained",
        43U,
        0.75,
        {{2.8, 2.75}, {7.6, 2.75}, {childX, childY}},
        {2U, 2U, 2U},
        2U,
        {},
        output,
        true,
        3U,
        {0.0, static_cast<double>(obliqueRotation), 0.0},
        {},
        std::nullopt,
        false,
        jaguar::RotationMode::Continuous);
}

bool TestDisruptionDistinctPoolFiveCandidatesMatchesRust(std::ostream& output)
{
    return TestDisruptionTraceMatchesRust(
        "ExplorationDistinctPoolFiveCandidatesRustTrace.tsv",
        "ExplorationDistinctPoolFiveCandidates.json",
        "distinct_pool_five",
        0U,
        1.0,
        {{1.0, 18.0},
         {2.75, 18.0},
         {6.0, 18.0},
         {12.25, 18.0},
         {24.5, 18.0},
         {48.75, 18.0}},
        {5U, 5U, 5U, 5U, 5U, 5U},
        5U,
        {},
        output,
        false,
        4U,
        {},
        {1024.0, 256.0, 64.0, 16.0, 4.0, 1.0});
}

bool TestDisruptionDistinctPoolSixCandidatesMatchesRust(std::ostream& output)
{
    return TestDisruptionTraceMatchesRust(
        "ExplorationDistinctPoolSixCandidatesRustTrace.tsv",
        "ExplorationDistinctPoolSixCandidates.json",
        "distinct_pool_six",
        0U,
        1.0,
        {{1.0, 34.0},
         {2.75, 34.0},
         {6.0, 34.0},
         {12.25, 34.0},
         {24.5, 34.0},
         {48.75, 34.0},
         {97.0, 34.0}},
        {6U, 6U, 6U, 6U, 6U, 6U, 6U},
        6U,
        {},
        output,
        false,
        4U,
        {},
        {4096.0, 1024.0, 256.0, 64.0, 16.0, 4.0, 1.0});
}

bool TestDisruptionDistinctPoolSevenCandidatesMatchesRust(std::ostream& output)
{
    return TestDisruptionTraceMatchesRust(
        "ExplorationDistinctPoolSevenCandidatesRustTrace.tsv",
        "ExplorationDistinctPoolSevenCandidates.json",
        "distinct_pool_seven",
        0U,
        1.0,
        {{1.0, 66.0},
         {2.75, 66.0},
         {6.0, 66.0},
         {12.25, 66.0},
         {24.5, 66.0},
         {48.75, 66.0},
         {97.0, 66.0},
         {193.25, 66.0}},
        {7U, 7U, 7U, 7U, 7U, 7U, 7U, 7U},
        7U,
        {},
        output,
        false,
        4U,
        {},
        {16384.0, 4096.0, 1024.0, 256.0, 64.0, 16.0, 4.0, 1.0});
}

bool TestDisruptionDistinctPoolEightCandidatesMatchesRust(std::ostream& output)
{
    return TestDisruptionTraceMatchesRust(
        "ExplorationDistinctPoolEightCandidatesRustTrace.tsv",
        "ExplorationDistinctPoolEightCandidates.json",
        "distinct_pool_eight",
        0U,
        1.0,
        {{1.0, 130.0},
         {2.75, 130.0},
         {6.0, 130.0},
         {12.25, 130.0},
         {24.5, 130.0},
         {48.75, 130.0},
         {97.0, 130.0},
         {193.25, 130.0},
         {385.5, 130.0}},
        {8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U},
        8U,
        {},
        output,
        false,
        4U,
        {},
         {65536.0, 16384.0, 4096.0, 1024.0, 256.0, 64.0, 16.0, 4.0, 1.0});
}

bool TestDisruptionDistinctPoolNineCandidatesMatchesRust(std::ostream& output)
{
    return TestDisruptionTraceMatchesRust(
        "ExplorationDistinctPoolNineCandidatesRustTrace.tsv",
        "ExplorationDistinctPoolNineCandidates.json",
        "distinct_pool_nine",
        0U,
        1.0,
        {{1.0, 258.0},
         {2.75, 258.0},
         {6.0, 258.0},
         {12.25, 258.0},
         {24.5, 258.0},
         {48.75, 258.0},
         {97.0, 258.0},
         {193.25, 258.0},
         {385.5, 258.0},
         {769.75, 258.0}},
        {9U, 9U, 9U, 9U, 9U, 9U, 9U, 9U, 9U, 9U},
        9U,
        {},
        output,
        false,
        4U,
        {},
        {262144.0, 65536.0, 16384.0, 4096.0, 1024.0, 256.0, 64.0, 16.0, 4.0, 1.0});
}

bool TestDisruptionDistinctPoolTenCandidatesMatchesRust(std::ostream& output)
{
    return TestDisruptionTraceMatchesRust(
        "ExplorationDistinctPoolTenCandidatesRustTrace.tsv",
        "ExplorationDistinctPoolTenCandidates.json",
        "distinct_pool_ten",
        0U,
        1.0,
        {{1.0, 514.0},
         {2.75, 514.0},
         {6.0, 514.0},
         {12.25, 514.0},
         {24.5, 514.0},
         {48.75, 514.0},
         {97.0, 514.0},
         {193.25, 514.0},
         {385.5, 514.0},
         {769.75, 514.0},
         {1538.0, 514.0}},
        {10U, 10U, 10U, 10U, 10U, 10U, 10U, 10U, 10U, 10U, 10U},
        10U,
        {},
        output,
        false,
        4U,
        {},
        {1048576.0,
         262144.0,
         65536.0,
         16384.0,
         4096.0,
         1024.0,
         256.0,
         64.0,
         16.0,
         4.0,
         1.0});
}

bool TestMultipleContainedRelocationsMatchRust(std::ostream& output)
{
    return TestDisruptionTraceMatchesRust(
        "ExplorationMultipleContainedRelocationsRustTrace.tsv",
        "ExplorationMultipleContainedRelocations.json",
        "multiple_contained",
        43U,
        0.9,
        {{2.8, 2.75}, {7.6, 2.75}, {9.5, 2.1}, {9.5, 3.35}},
        {3U, 3U, 2U, 2U},
        0U,
        {{2U, 0U}, {3U, 0U}},
        output);
}

bool TestContainedCollectorFiltersNoncontainedCollisionMatchesRust(
    std::ostream& output)
{
    if (!TestContainedCollectorFilterGeometryWitness(output))
    {
        return false;
    }
    return TestDisruptionTraceMatchesRust(
        "ExplorationContainedCollectorFilterRustTrace.tsv",
        "ExplorationContainedCollectorFilter.json",
        "contained_collector_filter",
        43U,
        0.9,
        {{2.8, 4.2}, {7.6, 2.75}, {9.5, 2.75}, {4.0, 2.75}},
        {3U, 3U, 2U, 2U},
        3U,
        {{2U, 0U}},
        output,
        true);
}

bool TestBothSidesContainedRelocationsMatchRust(std::ostream& output)
{
    return TestDisruptionTraceMatchesRust(
        "ExplorationBothSidesContainedRelocationsRustTrace.tsv",
        "ExplorationBothSidesContainedRelocations.json",
        "both_sides",
        43U,
        0.5,
        {{2.8, 2.75},
         {7.6, 2.75},
         {6.8, 0.95},
         {8.4, 0.95},
         {2.8, 2.75},
         {2.8, 3.5}},
        {5U, 5U, 2U, 2U, 2U, 2U},
        0U,
        {{2U, 0U}, {3U, 0U}, {4U, 1U}, {5U, 1U}},
        output,
        true);
}

bool TestBothSidesObliqueContainedRelocationsMatchRust(std::ostream& output)
{
    const float obliqueRotation = 0.6457718F;
    const float childOffset = 2.0F;
    const float childDx = childOffset * std::cos(obliqueRotation);
    const float childDy = childOffset * std::sin(obliqueRotation);
    return TestDisruptionTraceMatchesRust(
        "ExplorationBothSidesObliqueContainedRelocationsRustTrace.tsv",
        "ExplorationBothSidesObliqueContainedRelocations.json",
        "both_sides_oblique",
        43U,
        0.5,
        {{2.8, 2.75},
         {7.6, 2.75},
         {7.6F + childDx, 2.75F + childDy},
         {7.6F - childDx, 2.75F - childDy},
         {2.8, 2.75},
         {2.8, 3.5}},
        {5U, 5U, 2U, 2U, 2U, 2U},
        0U,
        {{2U, 0U}, {3U, 0U}, {4U, 1U}, {5U, 1U}},
        output,
        true,
        2U,
        {0.0, static_cast<double>(obliqueRotation), 0.0, 0.0, 0.0, 0.0});
}

bool TestBothSidesObliqueContainedRelocationsTwoRoundsMatchRust(
    std::ostream& output)
{
    const float obliqueRotation = 0.6457718F;
    const float childOffset = 2.0F;
    const float childDx = childOffset * std::cos(obliqueRotation);
    const float childDy = childOffset * std::sin(obliqueRotation);
    return TestDisruptionTraceMatchesRust(
        "ExplorationBothSidesObliqueContainedRelocationsTwoRoundsRustTrace.tsv",
        "ExplorationBothSidesObliqueContainedRelocations.json",
        "both_sides_oblique_two_rounds",
        43U,
        0.5,
        {{2.8, 2.75},
         {7.6, 2.75},
         {7.6F + childDx, 2.75F + childDy},
         {7.6F - childDx, 2.75F - childDy},
         {2.8, 2.75},
         {2.8, 3.5}},
        {5U, 5U, 2U, 2U, 2U, 2U},
        0U,
        {{2U, 0U}, {3U, 0U}, {4U, 1U}, {5U, 1U}},
        output,
        true,
        3U,
        {0.0, static_cast<double>(obliqueRotation), 0.0, 0.0, 0.0, 0.0});
}

bool TestBothSidesObliqueContainedRelocationsLimit4MatchesRustRelease(
    std::ostream& output)
{
    const float obliqueRotation = 0.6457718F;
    const float childOffset = 2.0F;
    const float childDx = childOffset * std::cos(obliqueRotation);
    const float childDy = childOffset * std::sin(obliqueRotation);
    return TestDisruptionTraceMatchesRust(
        "ExplorationBothSidesObliqueContainedRelocationsLimit4RustReleaseTrace.tsv",
        "ExplorationBothSidesObliqueContainedRelocations.json",
        "both_sides_oblique_limit4",
        43U,
        0.5,
        {{2.8, 2.75},
         {7.6, 2.75},
         {7.6F + childDx, 2.75F + childDy},
         {7.6F - childDx, 2.75F - childDy},
         {2.8, 2.75},
         {2.8, 3.5}},
        {5U, 5U, 2U, 2U, 2U, 2U},
        0U,
        {{2U, 0U}, {3U, 0U}, {4U, 1U}, {5U, 1U}},
        output,
        true,
        4U,
        {0.0, static_cast<double>(obliqueRotation), 0.0, 0.0, 0.0, 0.0});
}

bool TestBothSidesObliqueContainedRelocationsLimit5MatchesRustRelease(
    std::ostream& output)
{
    const float obliqueRotation = 0.6457718F;
    const float childOffset = 2.0F;
    const float childDx = childOffset * std::cos(obliqueRotation);
    const float childDy = childOffset * std::sin(obliqueRotation);
    return TestDisruptionTraceMatchesRust(
        "ExplorationBothSidesObliqueContainedRelocationsLimit5RustReleaseTrace.tsv",
        "ExplorationBothSidesObliqueContainedRelocations.json",
        "both_sides_oblique_limit5",
        43U,
        0.5,
        {{2.8, 2.75},
         {7.6, 2.75},
         {7.6F + childDx, 2.75F + childDy},
         {7.6F - childDx, 2.75F - childDy},
         {2.8, 2.75},
         {2.8, 3.5}},
        {5U, 5U, 2U, 2U, 2U, 2U},
        0U,
        {{2U, 0U}, {3U, 0U}, {4U, 1U}, {5U, 1U}},
        output,
        true,
        5U,
        {0.0, static_cast<double>(obliqueRotation), 0.0, 0.0, 0.0, 0.0});
}

bool TestBothSidesObliqueContainedRelocationsLimit6MatchesRustRelease(
    std::ostream& output)
{
    const float obliqueRotation = 0.6457718F;
    const float childOffset = 2.0F;
    const float childDx = childOffset * std::cos(obliqueRotation);
    const float childDy = childOffset * std::sin(obliqueRotation);
    return TestDisruptionTraceMatchesRust(
        "ExplorationBothSidesObliqueContainedRelocationsLimit6RustReleaseTrace.tsv",
        "ExplorationBothSidesObliqueContainedRelocations.json",
        "both_sides_oblique_limit6",
        43U,
        0.5,
        {{2.8, 2.75},
         {7.6, 2.75},
         {7.6F + childDx, 2.75F + childDy},
         {7.6F - childDx, 2.75F - childDy},
         {2.8, 2.75},
         {2.8, 3.5}},
        {5U, 5U, 2U, 2U, 2U, 2U},
        0U,
        {{2U, 0U}, {3U, 0U}, {4U, 1U}, {5U, 1U}},
        output,
        true,
        6U,
        {0.0, static_cast<double>(obliqueRotation), 0.0, 0.0, 0.0, 0.0});
}

bool TestBothSidesObliqueContainedRelocationsIndependentGeometryMatchesRust(
    std::ostream& output)
{
    const float obliqueRotation = 0.6457718F;
    const float sinAngle = std::sin(obliqueRotation);
    const float cosAngle = std::cos(obliqueRotation);
    return TestDisruptionTraceMatchesRust(
        "ExplorationBothSidesObliqueContainedRelocationsIndependentGeometryRustTrace.tsv",
        "ExplorationBothSidesObliqueContainedRelocationsIndependentGeometry.json",
        "independent_geometry_oblique_bilateral",
        10U,
        0.8,
        {{1.3, 2.75},
         {4.8, 2.75},
         {4.8F - 0.85F * cosAngle - 0.8F * sinAngle,
          2.75F - 0.85F * sinAngle + 0.8F * cosAngle},
         {4.8F + 0.85F * cosAngle - 0.8F * sinAngle,
          2.75F + 0.85F * sinAngle + 0.8F * cosAngle},
         {1.3, 2.75},
         {1.3, 3.45}},
        {5U, 5U, 2U, 2U, 2U, 2U},
        0U,
        {{2U, 0U}, {3U, 0U}, {4U, 1U}, {5U, 1U}},
        output,
        true,
        2U,
        {0.0,
         static_cast<double>(obliqueRotation),
         static_cast<double>(obliqueRotation),
         static_cast<double>(obliqueRotation),
         0.0,
         0.0});
}

bool TestBothSidesObliqueContainedRelocationsIndependentGeometryLimit3MatchesRust(
    std::ostream& output)
{
    const float obliqueRotation = 0.6457718F;
    const float sinAngle = std::sin(obliqueRotation);
    const float cosAngle = std::cos(obliqueRotation);
    return TestDisruptionTraceMatchesRust(
        "ExplorationBothSidesObliqueContainedRelocationsIndependentGeometryLimit3RustTrace.tsv",
        "ExplorationBothSidesObliqueContainedRelocationsIndependentGeometry.json",
        "independent_geometry_oblique_bilateral_limit3",
        10U,
        0.8,
        {{1.3, 2.75},
         {4.8, 2.75},
         {4.8F - 0.85F * cosAngle - 0.8F * sinAngle,
          2.75F - 0.85F * sinAngle + 0.8F * cosAngle},
         {4.8F + 0.85F * cosAngle - 0.8F * sinAngle,
          2.75F + 0.85F * sinAngle + 0.8F * cosAngle},
         {1.3, 2.75},
         {1.3, 3.45}},
        {5U, 5U, 2U, 2U, 2U, 2U},
        0U,
        {{2U, 0U}, {3U, 0U}, {4U, 1U}, {5U, 1U}},
        output,
        true,
        3U,
        {0.0,
         static_cast<double>(obliqueRotation),
         static_cast<double>(obliqueRotation),
         static_cast<double>(obliqueRotation),
         0.0,
         0.0});
}

bool TestBothSidesObliqueContainedRelocationsIndependentGeometryLimit4MatchesRust(
    std::ostream& output)
{
    const float obliqueRotation = 0.6457718F;
    const float sinAngle = std::sin(obliqueRotation);
    const float cosAngle = std::cos(obliqueRotation);
    return TestDisruptionTraceMatchesRust(
        "ExplorationBothSidesObliqueContainedRelocationsIndependentGeometryLimit4RustTrace.tsv",
        "ExplorationBothSidesObliqueContainedRelocationsIndependentGeometry.json",
        "independent_geometry_oblique_bilateral_limit4",
        10U,
        0.8,
        {{1.3, 2.75},
         {4.8, 2.75},
         {4.8F - 0.85F * cosAngle - 0.8F * sinAngle,
          2.75F - 0.85F * sinAngle + 0.8F * cosAngle},
         {4.8F + 0.85F * cosAngle - 0.8F * sinAngle,
          2.75F + 0.85F * sinAngle + 0.8F * cosAngle},
         {1.3, 2.75},
         {1.3, 3.45}},
        {5U, 5U, 2U, 2U, 2U, 2U},
        0U,
        {{2U, 0U}, {3U, 0U}, {4U, 1U}, {5U, 1U}},
        output,
        true,
        4U,
        {0.0,
         static_cast<double>(obliqueRotation),
         static_cast<double>(obliqueRotation),
         static_cast<double>(obliqueRotation),
         0.0,
         0.0});
}

bool TestBothSidesObliqueContainedRelocationsIndependentGeometryLimit5MatchesRust(
    std::ostream& output)
{
    const float obliqueRotation = 0.6457718F;
    const float sinAngle = std::sin(obliqueRotation);
    const float cosAngle = std::cos(obliqueRotation);
    return TestDisruptionTraceMatchesRust(
        "ExplorationBothSidesObliqueContainedRelocationsIndependentGeometryLimit5RustTrace.tsv",
        "ExplorationBothSidesObliqueContainedRelocationsIndependentGeometry.json",
        "independent_geometry_oblique_bilateral_limit5",
        10U,
        0.8,
        {{1.3, 2.75},
         {4.8, 2.75},
         {4.8F - 0.85F * cosAngle - 0.8F * sinAngle,
          2.75F - 0.85F * sinAngle + 0.8F * cosAngle},
         {4.8F + 0.85F * cosAngle - 0.8F * sinAngle,
          2.75F + 0.85F * sinAngle + 0.8F * cosAngle},
         {1.3, 2.75},
         {1.3, 3.45}},
        {5U, 5U, 2U, 2U, 2U, 2U},
        0U,
        {{2U, 0U}, {3U, 0U}, {4U, 1U}, {5U, 1U}},
        output,
        true,
        5U,
        {0.0,
         static_cast<double>(obliqueRotation),
         static_cast<double>(obliqueRotation),
         static_cast<double>(obliqueRotation),
         0.0,
         0.0});
}

bool TestBothSidesObliqueContainedRelocationsIndependentGeometryLimit6MatchesRust(
    std::ostream& output)
{
    const float obliqueRotation = 0.6457718F;
    const float sinAngle = std::sin(obliqueRotation);
    const float cosAngle = std::cos(obliqueRotation);
    return TestDisruptionTraceMatchesRust(
        "ExplorationBothSidesObliqueContainedRelocationsIndependentGeometryLimit6RustTrace.tsv",
        "ExplorationBothSidesObliqueContainedRelocationsIndependentGeometry.json",
        "independent_geometry_oblique_bilateral_limit6",
        10U,
        0.8,
        {{1.3, 2.75},
         {4.8, 2.75},
         {4.8F - 0.85F * cosAngle - 0.8F * sinAngle,
          2.75F - 0.85F * sinAngle + 0.8F * cosAngle},
         {4.8F + 0.85F * cosAngle - 0.8F * sinAngle,
          2.75F + 0.85F * sinAngle + 0.8F * cosAngle},
         {1.3, 2.75},
         {1.3, 3.45}},
        {5U, 5U, 2U, 2U, 2U, 2U},
        0U,
        {{2U, 0U}, {3U, 0U}, {4U, 1U}, {5U, 1U}},
        output,
        true,
        6U,
        {0.0,
         static_cast<double>(obliqueRotation),
         static_cast<double>(obliqueRotation),
         static_cast<double>(obliqueRotation),
         0.0,
         0.0});
}

bool TestBothSidesObliqueContainedRelocationsIndependentGeometrySeed11Limit6MatchesRust(
    std::ostream& output)
{
    const float obliqueRotation = 0.6457718F;
    const float sinAngle = std::sin(obliqueRotation);
    const float cosAngle = std::cos(obliqueRotation);
    return TestDisruptionTraceMatchesRust(
        "ExplorationBothSidesObliqueContainedRelocationsIndependentGeometrySeed11Limit6RustTrace.tsv",
        "ExplorationBothSidesObliqueContainedRelocationsIndependentGeometry.json",
        "independent_geometry_oblique_bilateral_seed11_limit6",
        11U,
        0.8,
        {{1.3, 2.75},
         {4.8, 2.75},
         {4.8F - 0.85F * cosAngle - 0.8F * sinAngle,
          2.75F - 0.85F * sinAngle + 0.8F * cosAngle},
         {4.8F + 0.85F * cosAngle - 0.8F * sinAngle,
          2.75F + 0.85F * sinAngle + 0.8F * cosAngle},
         {1.3, 2.75},
         {1.3, 3.45}},
        {5U, 5U, 2U, 2U, 2U, 2U},
        0U,
        {{2U, 0U}, {3U, 0U}, {4U, 1U}, {5U, 1U}},
        output,
        true,
        6U,
        {0.0,
         static_cast<double>(obliqueRotation),
         static_cast<double>(obliqueRotation),
         static_cast<double>(obliqueRotation),
         0.0,
         0.0});
}

bool TestBothSidesObliqueContainedRelocationsIndependentGeometrySeed12Limit6MatchesRust(
    std::ostream& output)
{
    const float obliqueRotation = 0.6457718F;
    const float sinAngle = std::sin(obliqueRotation);
    const float cosAngle = std::cos(obliqueRotation);
    return TestDisruptionTraceMatchesRust(
        "ExplorationBothSidesObliqueContainedRelocationsIndependentGeometrySeed12Limit6RustTrace.tsv",
        "ExplorationBothSidesObliqueContainedRelocationsIndependentGeometry.json",
        "independent_geometry_oblique_bilateral_seed12_limit6",
        12U,
        0.8,
        {{1.3, 2.75},
         {4.8, 2.75},
         {4.8F - 0.85F * cosAngle - 0.8F * sinAngle,
          2.75F - 0.85F * sinAngle + 0.8F * cosAngle},
         {4.8F + 0.85F * cosAngle - 0.8F * sinAngle,
          2.75F + 0.85F * sinAngle + 0.8F * cosAngle},
         {1.3, 2.75},
         {1.3, 3.45}},
        {5U, 5U, 2U, 2U, 2U, 2U},
        0U,
        {{4U, 1U}, {5U, 1U}, {2U, 0U}, {3U, 0U}},
        output,
        true,
        6U,
        {0.0,
         static_cast<double>(obliqueRotation),
         static_cast<double>(obliqueRotation),
         static_cast<double>(obliqueRotation),
         0.0,
         0.0});
}

bool TestBothSidesObliqueContainedRelocationsIndependentGeometrySeed13Limit6MatchesRust(
    std::ostream& output)
{
    const float obliqueRotation = 0.6457718F;
    const float sinAngle = std::sin(obliqueRotation);
    const float cosAngle = std::cos(obliqueRotation);
    return TestDisruptionTraceMatchesRust(
        "ExplorationBothSidesObliqueContainedRelocationsIndependentGeometrySeed13Limit6RustTrace.tsv",
        "ExplorationBothSidesObliqueContainedRelocationsIndependentGeometry.json",
        "independent_geometry_oblique_bilateral_seed13_limit6",
        13U,
        0.8,
        {{1.3, 2.75},
         {4.8, 2.75},
         {4.8F - 0.85F * cosAngle - 0.8F * sinAngle,
          2.75F - 0.85F * sinAngle + 0.8F * cosAngle},
         {4.8F + 0.85F * cosAngle - 0.8F * sinAngle,
          2.75F + 0.85F * sinAngle + 0.8F * cosAngle},
         {1.3, 2.75},
         {1.3, 3.45}},
        {5U, 5U, 2U, 2U, 2U, 2U},
        0U,
        {{2U, 0U}, {3U, 0U}, {4U, 1U}, {5U, 1U}},
        output,
        true,
        6U,
        {0.0,
         static_cast<double>(obliqueRotation),
         static_cast<double>(obliqueRotation),
         static_cast<double>(obliqueRotation),
         0.0,
         0.0});
}

bool TestBothSidesObliqueContainedRelocationsIndependentGeometrySeed14Limit6MatchesRust(
    std::ostream& output)
{
    const float obliqueRotation = 0.6457718F;
    const float sinAngle = std::sin(obliqueRotation);
    const float cosAngle = std::cos(obliqueRotation);
    return TestDisruptionTraceMatchesRust(
        "ExplorationBothSidesObliqueContainedRelocationsIndependentGeometrySeed14Limit6RustTrace.tsv",
        "ExplorationBothSidesObliqueContainedRelocationsIndependentGeometry.json",
        "independent_geometry_oblique_bilateral_seed14_limit6",
        14U,
        0.8,
        {{1.3, 2.75},
         {4.8, 2.75},
         {4.8F - 0.85F * cosAngle - 0.8F * sinAngle,
          2.75F - 0.85F * sinAngle + 0.8F * cosAngle},
         {4.8F + 0.85F * cosAngle - 0.8F * sinAngle,
          2.75F + 0.85F * sinAngle + 0.8F * cosAngle},
         {1.3, 2.75},
         {1.3, 3.45}},
        {5U, 5U, 2U, 2U, 2U, 2U},
        0U,
        {{2U, 0U}, {3U, 0U}, {4U, 1U}, {5U, 1U}},
        output,
        true,
        6U,
        {0.0,
         static_cast<double>(obliqueRotation),
         static_cast<double>(obliqueRotation),
         static_cast<double>(obliqueRotation),
         0.0,
         0.0});
}

bool TestSecondIndependentGeometryObliqueBilateralSeed10Limit6MatchesRust(
    std::ostream& output)
{
    const float obliqueRotation = 0.6457718F;
    const float sinAngle = std::sin(obliqueRotation);
    const float cosAngle = std::cos(obliqueRotation);
    return TestDisruptionTraceMatchesRust(
        "ExplorationBothSidesObliqueContainedRelocationsSecondIndependentGeometrySeed10Limit6RustTrace.tsv",
        "ExplorationBothSidesObliqueContainedRelocationsSecondIndependentGeometry.json",
        "second_independent_geometry_h_i_seed10_limit6",
        10U,
        0.8,
        {{1.6, 3.5},
         {6.8, 3.5},
         {6.8F - 0.9F * cosAngle, 3.5F - 0.9F * sinAngle},
         {6.8F + 0.9F * cosAngle, 3.5F + 0.9F * sinAngle},
         {1.6, 2.6},
         {1.6, 4.4}},
        {5U, 5U, 2U, 2U, 2U, 2U},
        5U,
        {{2U, 0U}, {3U, 0U}, {4U, 1U}, {5U, 1U}},
        output,
        true,
        6U,
        {0.0,
         static_cast<double>(obliqueRotation),
         static_cast<double>(obliqueRotation),
         static_cast<double>(obliqueRotation),
          0.0,
          0.0});
}

bool TestSecondIndependentGeometryObliqueBilateralSeed11Limit6MatchesRust(
    std::ostream& output)
{
    const float obliqueRotation = 0.6457718F;
    const float sinAngle = std::sin(obliqueRotation);
    const float cosAngle = std::cos(obliqueRotation);
    return TestDisruptionTraceMatchesRust(
        "ExplorationBothSidesObliqueContainedRelocationsSecondIndependentGeometrySeed11Limit6RustTrace.tsv",
        "ExplorationBothSidesObliqueContainedRelocationsSecondIndependentGeometry.json",
        "second_independent_geometry_h_i_seed11_limit6",
        11U,
        0.8,
        {{1.6, 3.5},
         {6.8, 3.5},
         {6.8F - 0.9F * cosAngle, 3.5F - 0.9F * sinAngle},
         {6.8F + 0.9F * cosAngle, 3.5F + 0.9F * sinAngle},
         {1.6, 2.6},
         {1.6, 4.4}},
        {5U, 5U, 2U, 2U, 2U, 2U},
        5U,
        {{4U, 1U}, {5U, 1U}, {2U, 0U}, {3U, 0U}},
        output,
        true,
        6U,
        {0.0,
         static_cast<double>(obliqueRotation),
         static_cast<double>(obliqueRotation),
         static_cast<double>(obliqueRotation),
         0.0,
         0.0});
}

bool TestSecondIndependentGeometryObliqueBilateralSeed12Limit6MatchesRust(
    std::ostream& output)
{
    const float obliqueRotation = 0.6457718F;
    const float sinAngle = std::sin(obliqueRotation);
    const float cosAngle = std::cos(obliqueRotation);
    return TestDisruptionTraceMatchesRust(
        "ExplorationBothSidesObliqueContainedRelocationsSecondIndependentGeometrySeed12Limit6RustTrace.tsv",
        "ExplorationBothSidesObliqueContainedRelocationsSecondIndependentGeometry.json",
        "second_independent_geometry_h_i_seed12_limit6",
        12U,
        0.8,
        {{1.6, 3.5},
         {6.8, 3.5},
         {6.8F - 0.9F * cosAngle, 3.5F - 0.9F * sinAngle},
         {6.8F + 0.9F * cosAngle, 3.5F + 0.9F * sinAngle},
         {1.6, 2.6},
         {1.6, 4.4}},
        {5U, 5U, 2U, 2U, 2U, 2U},
        5U,
        {{2U, 0U}, {3U, 0U}, {4U, 1U}, {5U, 1U}},
        output,
        true,
        6U,
        {0.0,
         static_cast<double>(obliqueRotation),
         static_cast<double>(obliqueRotation),
         static_cast<double>(obliqueRotation),
         0.0,
          0.0});
}

bool TestSecondIndependentGeometryObliqueBilateralSeed13Limit6MatchesRust(
    std::ostream& output)
{
    const float obliqueRotation = 0.6457718F;
    const float sinAngle = std::sin(obliqueRotation);
    const float cosAngle = std::cos(obliqueRotation);
    return TestDisruptionTraceMatchesRust(
        "ExplorationBothSidesObliqueContainedRelocationsSecondIndependentGeometrySeed13Limit6RustTrace.tsv",
        "ExplorationBothSidesObliqueContainedRelocationsSecondIndependentGeometry.json",
        "second_independent_geometry_h_i_seed13_limit6",
        13U,
        0.8,
        {{1.6, 3.5},
         {6.8, 3.5},
         {6.8F - 0.9F * cosAngle, 3.5F - 0.9F * sinAngle},
         {6.8F + 0.9F * cosAngle, 3.5F + 0.9F * sinAngle},
         {1.6, 2.6},
         {1.6, 4.4}},
        {5U, 5U, 2U, 2U, 2U, 2U},
        5U,
        {{4U, 1U}, {5U, 1U}, {2U, 0U}, {3U, 0U}},
        output,
        true,
        6U,
        {0.0,
         static_cast<double>(obliqueRotation),
         static_cast<double>(obliqueRotation),
         static_cast<double>(obliqueRotation),
         0.0,
         0.0});
}

bool TestSecondIndependentGeometryObliqueBilateralSeed14Limit6MatchesRust(
    std::ostream& output)
{
    const float obliqueRotation = 0.6457718F;
    const float sinAngle = std::sin(obliqueRotation);
    const float cosAngle = std::cos(obliqueRotation);
    return TestDisruptionTraceMatchesRust(
        "ExplorationBothSidesObliqueContainedRelocationsSecondIndependentGeometrySeed14Limit6RustTrace.tsv",
        "ExplorationBothSidesObliqueContainedRelocationsSecondIndependentGeometry.json",
        "second_independent_geometry_h_i_seed14_limit6",
        14U,
        0.8,
        {{1.6, 3.5},
         {6.8, 3.5},
         {6.8F - 0.9F * cosAngle, 3.5F - 0.9F * sinAngle},
         {6.8F + 0.9F * cosAngle, 3.5F + 0.9F * sinAngle},
         {1.6, 2.6},
         {1.6, 4.4}},
        {5U, 5U, 2U, 2U, 2U, 2U},
        5U,
        {{2U, 0U}, {3U, 0U}, {4U, 1U}, {5U, 1U}},
        output,
        true,
        6U,
        {0.0,
         static_cast<double>(obliqueRotation),
         static_cast<double>(obliqueRotation),
         static_cast<double>(obliqueRotation),
         0.0,
         0.0});
}

bool TestObliqueContainedRelocationMatchesRust(std::ostream& output)
{
    std::ifstream referenceInput;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/ExplorationObliqueContainedRelocationRustTrace.tsv",
             "../SparrowCore/Tests/Fixtures/ExplorationObliqueContainedRelocationRustTrace.tsv",
             "../JaguarRun/SparrowCore/Tests/Fixtures/ExplorationObliqueContainedRelocationRustTrace.tsv",
             "..\\SparrowCore\\Tests\\Fixtures\\ExplorationObliqueContainedRelocationRustTrace.tsv"})
    {
        referenceInput.open(path);
        if (referenceInput.is_open())
        {
            break;
        }
        referenceInput.clear();
    }
    if (!Expect(referenceInput.is_open(),
                output,
                "Rust oblique contained-relocation trace cannot be opened"))
    {
        return false;
    }

    std::vector<std::string> records;
    std::string line;
    while (std::getline(referenceInput, line))
    {
        if (!line.empty() && line.front() != '#')
        {
            records.push_back(line);
        }
    }
    if (!Expect(!records.empty(),
                output,
                "Rust oblique contained-relocation trace is empty"))
    {
        return false;
    }

    std::istringstream caseRow(records.front());
    std::string record;
    std::string caseName;
    std::uint64_t seed = 0U;
    double initialWidth = 0.0;
    double shrinkStep = 0.0;
    std::size_t iterationLimit = 0U;
    double poolStddev = 0.0;
    std::size_t failureCount = 0U;
    std::size_t itemCount = 0U;
    if (!Expect(static_cast<bool>(caseRow >> record >> caseName >> seed
                                  >> initialWidth >> shrinkStep
                                  >> iterationLimit >> poolStddev
                                  >> failureCount >> itemCount)
                    && record == "case" && caseName == "oblique_contained"
                    && seed == 43U && failureCount == 2U && itemCount == 3U
                    && iterationLimit == 3U,
                output,
                "malformed Rust oblique relocation trace header"))
    {
        return false;
    }

    struct ExpectedPose
    {
        std::size_t itemIndex = 0U;
        double rotation = 0.0;
        double x = 0.0;
        double y = 0.0;
        jaguar::Rect2d bounds;
    };
    struct ExpectedFailure
    {
        double loss = 0.0;
        std::vector<ExpectedPose> poses;
    };
    std::vector<ExpectedFailure> expectedFailures;
    std::vector<std::string> expectedEvents;
    std::size_t recordIndex = 1U;
    for (std::size_t failureIndex = 0U;
         failureIndex < failureCount;
         ++failureIndex)
    {
        if (!Expect(recordIndex < records.size(),
                    output,
                    "Rust oblique relocation failure row is missing"))
        {
            return false;
        }
        std::istringstream failureRow(records[recordIndex++]);
        std::string failureRecord;
        std::size_t ordinal = 0U;
        std::size_t poseCount = 0U;
        ExpectedFailure failure;
        if (!Expect(static_cast<bool>(failureRow >> failureRecord >> ordinal
                                      >> failure.loss >> poseCount)
                        && failureRecord == "failure"
                        && ordinal == failureIndex && poseCount == itemCount,
                    output,
                    "malformed Rust oblique relocation failure row"))
        {
            return false;
        }
        for (std::size_t poseIndex = 0U; poseIndex < poseCount; ++poseIndex)
        {
            if (!Expect(recordIndex < records.size(),
                        output,
                        "Rust oblique relocation pose row is missing"))
            {
                return false;
            }
            std::istringstream poseRow(records[recordIndex++]);
            std::string poseRecord;
            ExpectedPose pose;
            if (!Expect(static_cast<bool>(poseRow >> poseRecord >> pose.itemIndex
                                          >> pose.rotation >> pose.x >> pose.y
                                          >> pose.bounds.xMin >> pose.bounds.yMin
                                          >> pose.bounds.xMax >> pose.bounds.yMax)
                            && poseRecord == "pose",
                        output,
                        "malformed Rust oblique relocation pose row"))
            {
                return false;
            }
            failure.poses.push_back(pose);
        }
        expectedFailures.push_back(std::move(failure));
        while (recordIndex < records.size()
               && records[recordIndex].rfind("failure ", 0U) != 0U)
        {
            expectedEvents.push_back(records[recordIndex++]);
        }
    }
    if (!Expect(recordIndex == records.size(),
                output,
                "Rust oblique relocation trace has trailing rows"))
    {
        return false;
    }

    std::ifstream instanceInput;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/ExplorationObliqueContainedRelocation.json",
             "../SparrowCore/Tests/Fixtures/ExplorationObliqueContainedRelocation.json",
             "../JaguarRun/SparrowCore/Tests/Fixtures/ExplorationObliqueContainedRelocation.json",
             "..\\SparrowCore\\Tests\\Fixtures\\ExplorationObliqueContainedRelocation.json"})
    {
        instanceInput.open(path);
        if (instanceInput.is_open())
        {
            break;
        }
        instanceInput.clear();
    }
    if (!Expect(instanceInput.is_open(),
                output,
                "Rust oblique relocation instance cannot be opened"))
    {
        return false;
    }
    const std::string instanceJson{
        std::istreambuf_iterator<char>(instanceInput),
        std::istreambuf_iterator<char>()};
    const jaguar::Importer importer(sparrow::MakeRustDefaultImporterConfig());
    const auto instance = importer.ImportSPInstanceJson(instanceJson);
    if (!Expect(instance.HasValue(),
                output,
                "Rust oblique relocation instance must import"))
    {
        return false;
    }

    auto problemResult = jaguar::SPProblem::Create(instance.Value());
    if (!Expect(problemResult.HasValue(),
                output,
                "Rust oblique relocation problem must be created"))
    {
        return false;
    }
    jaguar::SPProblem problem = std::move(problemResult.Value());
    const float oblique = static_cast<float>(
        instance.Value().GetItem(1U).AllowedRotation().radians.at(1U));
    const float sinOblique = std::sin(oblique);
    const float cosOblique = std::cos(oblique);
    const float childX = 7.6F + (2.3F * cosOblique - 0.2F * sinOblique);
    const float childY = 2.75F + (2.3F * sinOblique + 0.2F * cosOblique);
    if (!Expect(problem.ChangeStripWidth(initialWidth).HasValue()
                    && problem.PlaceItem({0U, {0.0, {2.8, 2.75}}}).HasValue()
                    && problem.PlaceItem({1U, {oblique, {7.6, 2.75}}}).HasValue()
                    && problem.PlaceItem({2U, {0.0, {childX, childY}}}).HasValue(),
                output,
                "Rust oblique relocation initial placements must be constructible"))
    {
        return false;
    }
    const jaguar::SPSolution initial = problem.Save();
    jaguar::Layout initialLayout(initial.layoutSnapshot.container);
    const auto initialRestored = initialLayout.Restore(initial.layoutSnapshot);
    if (!Expect(initialRestored.HasValue() && initialLayout.IsFeasible(),
                output,
                "Rust oblique relocation initial layout must be feasible"))
    {
        return false;
    }

    sparrow::ExplorationConfig config;
    config.maxIterations = iterationLimit;
    config.shrinkStep = shrinkStep;
    config.solutionPoolDistributionStddev = poolStddev;
    config.maxConsecutiveFailures = std::nullopt;
    config.separator.strikeLimit = 0U;
    config.separator.nWorkers = 1U;
    config.separator.nContainerSamples = 0U;
    config.separator.nFocussedSamples = 0U;
    sparrow::OptimizationTerminator terminator({
        32U, std::chrono::milliseconds{2000}});
    std::vector<sparrow::detail::ExplorationTraceEvent> actualEvents;
    const auto result = sparrow::detail::ExploreWithTrace(
        instance.Value(),
        initial,
        config,
        seed,
        terminator,
        nullptr,
        [&actualEvents](const auto& event) {
            actualEvents.push_back(event);
        });
    if (!Expect(result.HasValue(),
                output,
                "C++ oblique relocation Explore must complete"))
    {
        return false;
    }

    std::vector<std::string> actualEventRows;
    for (const auto& event : actualEvents)
    {
        std::ostringstream row;
        if (event.type == sparrow::detail::ExplorationTraceEvent::Type::PoolInsert)
        {
            row << "insert " << event.failureId << ' '
                << event.insertionIndex << ' ' << event.poolOrder.size();
            for (const std::size_t candidateId : event.poolOrder)
            {
                row << ' ' << candidateId;
            }
        }
        else if (event.type
                 == sparrow::detail::ExplorationTraceEvent::Type::PoolSample)
        {
            std::uint32_t rawSampleBits = 0U;
            std::uint32_t mappedSampleBits = 0U;
            std::memcpy(&rawSampleBits, &event.rawSample, sizeof(rawSampleBits));
            std::memcpy(&mappedSampleBits,
                        &event.mappedSample,
                        sizeof(mappedSampleBits));
            row << "sample " << event.failureId << ' ' << std::hex
                << std::setw(8) << std::setfill('0') << rawSampleBits << ' '
                << std::setw(8) << std::setfill('0') << mappedSampleBits
                << std::dec << ' ' << event.selectedIndex << ' '
                << event.consumedU64Words << ' ' << std::hex << std::setw(2)
                << std::setfill('0') << event.firstLayerIndex;
            for (const std::uint64_t value : event.followingU64)
            {
                row << ' ' << std::setw(16) << std::setfill('0') << value;
            }
        }
        else
        {
            const char* kind = event.disruptionMoveKind
                    == sparrow::detail::ExplorationTraceEvent::DisruptionMoveKind::Swap
                ? "swap"
                : "contained";
            row << std::fixed << std::setprecision(9)
                << "move " << event.failureId << ' ' << kind << ' '
                << event.itemIndex << ' ' << event.relatedItemIndex << ' '
                << event.fromPose[0] << ' ' << event.fromPose[1] << ' '
                << event.fromPose[2] << ' ' << event.toPose[0] << ' '
                << event.toPose[1] << ' ' << event.toPose[2];
        }
        actualEventRows.push_back(row.str());
    }
    if (actualEventRows != expectedEvents)
    {
        output << "C++ disruption/pool event rows differ from Rust\n";
        for (const auto& row : actualEventRows)
        {
            output << "  actual " << row << '\n';
        }
        for (const auto& row : expectedEvents)
        {
            output << "expected " << row << '\n';
        }
        return Expect(false,
                      output,
                      "C++ Explore disruption moves and pool/RNG events differ from Rust");
    }

    std::vector<const sparrow::OptimizationSolutionReport*> actualFailures;
    for (const auto& report : result.Value().history)
    {
        if (report.phase == sparrow::OptimizationPhase::OPTIMIZATION_PHASE_EXPLORATION
            && report.type
                   == sparrow::OptimizationReportType::OPTIMIZATION_REPORT_EXPLORATION_INFEASIBLE)
        {
            actualFailures.push_back(&report);
        }
    }
    if (!Expect(actualFailures.size() == expectedFailures.size(),
                output,
                "C++ oblique relocation failure count differs from Rust"))
    {
        return false;
    }
    for (std::size_t failureIndex = 0U;
         failureIndex < expectedFailures.size();
         ++failureIndex)
    {
        const auto& expected = expectedFailures[failureIndex];
        const auto& actual = *actualFailures[failureIndex];
        bool posesMatch = actual.solution.layoutSnapshot.placements.size() == itemCount;
        for (const auto& expectedPose : expected.poses)
        {
            const auto placement = std::find_if(
                actual.solution.layoutSnapshot.placements.begin(),
                actual.solution.layoutSnapshot.placements.end(),
                [&expectedPose](const auto& candidate) {
                    return candidate.placedItem.GetItemIndex()
                        == expectedPose.itemIndex;
                });
            if (placement == actual.solution.layoutSnapshot.placements.end())
            {
                posesMatch = false;
                break;
            }
            const auto& transform = placement->placedItem.Transform();
            const jaguar::Rect2d bounds =
                placement->placedItem.CollisionShape().BoundingBox();
            posesMatch = posesMatch
                && std::abs(transform.rotationRad - expectedPose.rotation) < 2.0e-5
                && std::abs(transform.translation.x - expectedPose.x) < 2.0e-5
                && std::abs(transform.translation.y - expectedPose.y) < 2.0e-5
                && std::abs(bounds.xMin - expectedPose.bounds.xMin) < 2.0e-4
                && std::abs(bounds.yMin - expectedPose.bounds.yMin) < 2.0e-4
                && std::abs(bounds.xMax - expectedPose.bounds.xMax) < 2.0e-4
                && std::abs(bounds.yMax - expectedPose.bounds.yMax) < 2.0e-4;
        }
        const bool lossMatches =
            std::abs(static_cast<double>(actual.collisionLoss) - expected.loss)
            < 1.0e-3;
        if (!Expect(posesMatch && lossMatches,
                    output,
                    "C++ oblique relocation failure pose/loss differs from Rust"))
        {
            output << std::setprecision(12) << "failure=" << failureIndex
                   << " actual loss=" << actual.collisionLoss
                   << " expected loss=" << expected.loss << '\n';
            return false;
        }
    }

    const bool tracedContainedChild = std::any_of(
        actualEvents.begin(),
        actualEvents.end(),
        [](const auto& event) {
            return event.type
                       == sparrow::detail::ExplorationTraceEvent::Type::DisruptionMove
                && event.disruptionMoveKind
                       == sparrow::detail::ExplorationTraceEvent::DisruptionMoveKind::ContainedRelocation
                && event.itemIndex == 2U
                && event.relatedItemIndex == 0U;
        });
    return Expect(tracedContainedChild,
                  output,
                  "C++ Explore must trace the actual POI-contained child relocation");
}

bool TestNormalPoolSamplerMatchesRustReference(std::ostream& output)
{
    std::ifstream input;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/ExplorationNormalPoolSamplesRust.tsv",
             "../SparrowCore/Tests/Fixtures/ExplorationNormalPoolSamplesRust.tsv",
             "../JaguarRun/SparrowCore/Tests/Fixtures/ExplorationNormalPoolSamplesRust.tsv",
             "..\\SparrowCore\\Tests\\Fixtures\\ExplorationNormalPoolSamplesRust.tsv"})
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
                "Rust normal sampler fixture cannot be opened"))
    {
        return false;
    }

    const auto parseHex = [](const std::string& text, auto& value) {
        std::istringstream parser(text);
        parser >> std::hex >> value;
        return static_cast<bool>(parser) && parser.eof();
    };
    std::string line;
    std::size_t caseCount = 0U;
    struct PathCoverage
    {
        bool fast = false;
        bool rejection = false;
        bool tail = false;
        bool tailRetry = false;
    };
    std::map<std::string, PathCoverage> coverageByStddev;
    while (std::getline(input, line))
    {
        if (line.empty() || line.front() == '#')
        {
            continue;
        }
        std::istringstream header(line);
        std::string record;
        std::string caseName;
        std::uint64_t seed = 0U;
        std::string stddevBitsText;
        std::size_t sampleCount = 0U;
        if (!Expect(static_cast<bool>(header >> record >> caseName >> seed
                                      >> stddevBitsText >> sampleCount)
                        && record == "case" && sampleCount > 0U,
                    output,
                    "malformed Rust normal sampler case header"))
        {
            return false;
        }
        std::uint32_t stddevBits = 0U;
        if (!Expect(parseHex(stddevBitsText, stddevBits),
                    output,
                    "malformed Rust normal sampler standard deviation bits"))
        {
            return false;
        }
        float stddev = 0.0F;
        std::memcpy(&stddev, &stddevBits, sizeof(stddev));
        sparrow::SparrowRng rng(seed);
        const std::size_t seedMarker = caseName.find("_seed_");
        if (!Expect(seedMarker != std::string::npos,
                    output,
                    "Rust normal sampler case must name its seed group"))
        {
            return false;
        }
        PathCoverage& coverage =
            coverageByStddev[caseName.substr(0U, seedMarker)];

        for (std::size_t ordinal = 0U; ordinal < sampleCount; ++ordinal)
        {
            if (!std::getline(input, line))
            {
                return Expect(false,
                              output,
                              "Rust normal sampler fixture ended before all samples");
            }
            std::istringstream row(line);
            std::string sampleRecord;
            std::size_t expectedOrdinal = 0U;
            std::string expectedSampleBitsText;
            std::size_t expectedWords = 0U;
            std::string expectedLayerText;
            std::array<std::string, 4U> expectedNextWords{};
            if (!Expect(static_cast<bool>(row >> sampleRecord >> expectedOrdinal
                                          >> expectedSampleBitsText >> expectedWords
                                          >> expectedLayerText
                                          >> expectedNextWords[0]
                                          >> expectedNextWords[1]
                                          >> expectedNextWords[2]
                                          >> expectedNextWords[3])
                            && sampleRecord == "sample"
                            && expectedOrdinal == ordinal,
                        output,
                        "malformed Rust normal sampler sample row"))
            {
                return false;
            }

            std::uint32_t expectedSampleBits = 0U;
            std::uint32_t expectedLayer = 0U;
            std::array<std::uint64_t, 4U> expectedNext{};
            if (!Expect(parseHex(expectedSampleBitsText, expectedSampleBits)
                            && parseHex(expectedLayerText, expectedLayer)
                            && parseHex(expectedNextWords[0], expectedNext[0])
                            && parseHex(expectedNextWords[1], expectedNext[1])
                            && parseHex(expectedNextWords[2], expectedNext[2])
                            && parseHex(expectedNextWords[3], expectedNext[3]),
                        output,
                        "malformed Rust normal sampler sample bits"))
            {
                return false;
            }

            const sparrow::SparrowRng before = rng;
            const std::uint32_t actualLayer = static_cast<std::uint32_t>(
                sparrow::SparrowRng(before).NextU64() & 0xffU);
            const float actualSample = sparrow::detail::RustNormalF32(rng, stddev);
            std::uint32_t actualSampleBits = 0U;
            std::memcpy(&actualSampleBits, &actualSample, sizeof(actualSampleBits));
            sparrow::SparrowRng stateProbe = before;
            std::size_t actualWords = 0U;
            while (stateProbe.State() != rng.State() && actualWords <= 64U)
            {
                stateProbe.NextU64();
                ++actualWords;
            }
            if (actualSampleBits != expectedSampleBits)
            {
                output << "Rust normal sample mismatch in case " << caseName
                       << " at ordinal " << ordinal << ": actual=0x" << std::hex
                       << actualSampleBits << " expected=0x" << expectedSampleBits
                       << std::dec << " layer=" << actualLayer
                       << " words=" << actualWords
                       << " expected-words=" << expectedWords << '\n';
                return false;
            }

            if (!Expect(actualWords == expectedWords
                            && actualLayer == expectedLayer,
                        output,
                        "normal sample RNG consumption differs from Rust"))
            {
                return false;
            }

            sparrow::SparrowRng following = rng;
            std::array<std::uint64_t, 4U> actualNext{};
            for (std::uint64_t& value : actualNext)
            {
                value = following.NextU64();
            }
            if (!Expect(actualNext == expectedNext,
                        output,
                        "RNG stream after a normal sample differs from Rust"))
            {
                return false;
            }

            coverage.fast |= actualWords == 1U;
            coverage.rejection |= actualLayer != 0U && actualWords >= 2U;
            coverage.tail |= actualLayer == 0U && actualWords >= 3U;
            coverage.tailRetry |= actualLayer == 0U && actualWords >= 5U;
        }
        ++caseCount;
    }
    if (!Expect(caseCount == 20U && coverageByStddev.size() == 4U,
                output,
                "Rust normal sampler fixture must cover 5 seeds across 4 standard deviations"))
    {
        return false;
    }
    return Expect(std::all_of(
                      coverageByStddev.begin(),
                      coverageByStddev.end(),
                      [](const auto& entry) {
                          const PathCoverage& paths = entry.second;
                          return paths.fast && paths.rejection && paths.tail
                              && paths.tailRetry;
                      }),
                  output,
                  "normal sampler fixture must cover fast/rejection/tail/retry paths per standard deviation");
}

bool TestCompressionMinimumWidthMatchesRustBoundary(std::ostream& output)
{
    const auto floatBits = [](float value) {
        std::uint32_t bits = 0U;
        std::memcpy(&bits, &value, sizeof(bits));
        return bits;
    };
    struct Probe
    {
        std::string name;
        std::uint32_t stepBits = 0U;
        std::uint32_t nextWidthBits = 0U;
        bool shouldEnter = false;
        std::size_t progressCount = 0U;
    };
    std::ifstream traceInput;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/CompressionMinimumWidthRustReference.tsv",
             "../SparrowCore/Tests/Fixtures/CompressionMinimumWidthRustReference.tsv",
             "../JaguarRun/SparrowCore/Tests/Fixtures/CompressionMinimumWidthRustReference.tsv",
             "..\\SparrowCore\\Tests\\Fixtures\\CompressionMinimumWidthRustReference.tsv"})
    {
        traceInput.open(path);
        if (traceInput.is_open())
        {
            break;
        }
        traceInput.clear();
    }
    if (!Expect(traceInput.is_open(),
                output,
                "Rust compression minimum-width trace cannot be opened"))
    {
        return false;
    }
    const auto parseHex = [](const std::string& text, std::uint32_t& value) {
        std::istringstream parser(text);
        parser >> std::hex >> value;
        return static_cast<bool>(parser) && parser.eof();
    };
    std::uint32_t expectedMinimumWidthBits = 0U;
    bool foundMinimumWidth = false;
    std::vector<Probe> probes;
    std::string line;
    while (std::getline(traceInput, line))
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
            std::string name;
            std::string bits;
            if (!Expect(static_cast<bool>(row >> name >> bits)
                            && name == "square"
                            && parseHex(bits, expectedMinimumWidthBits),
                        output,
                        "malformed Rust compression minimum-width case"))
            {
                return false;
            }
            foundMinimumWidth = true;
        }
        else if (record == "probe")
        {
            Probe probe;
            std::string stepBits;
            std::string nextWidthBits;
            unsigned int shouldEnter = 0U;
            if (!Expect(static_cast<bool>(row >> probe.name >> stepBits
                                          >> nextWidthBits >> shouldEnter
                                          >> probe.progressCount)
                            && parseHex(stepBits, probe.stepBits)
                            && parseHex(nextWidthBits, probe.nextWidthBits)
                            && shouldEnter <= 1U,
                        output,
                        "malformed Rust compression minimum-width probe"))
            {
                return false;
            }
            probe.shouldEnter = shouldEnter != 0U;
            probes.push_back(std::move(probe));
        }
        else
        {
            return Expect(false,
                          output,
                          "unknown Rust compression minimum-width record");
        }
    }
    if (!Expect(foundMinimumWidth && probes.size() == 2U,
                output,
                "Rust compression minimum-width trace must contain both probes"))
    {
        return false;
    }

    std::ifstream instanceInput;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/RustSppSingleItem.json",
             "../SparrowCore/Tests/Fixtures/RustSppSingleItem.json",
             "../JaguarRun/SparrowCore/Tests/Fixtures/RustSppSingleItem.json",
             "..\\SparrowCore\\Tests\\Fixtures\\RustSppSingleItem.json"})
    {
        instanceInput.open(path);
        if (instanceInput.is_open())
        {
            break;
        }
        instanceInput.clear();
    }
    if (!Expect(instanceInput.is_open(),
                output,
                "Rust single-item compression fixture cannot be opened"))
    {
        return false;
    }
    const std::string instanceJson{
        std::istreambuf_iterator<char>(instanceInput),
        std::istreambuf_iterator<char>()};
    const jaguar::Importer importer(sparrow::MakeRustDefaultImporterConfig());
    const auto instance = importer.ImportSPInstanceJson(instanceJson);
    if (!Expect(instance.HasValue(),
                output,
                "Rust single-item compression fixture must import"))
    {
        return false;
    }
    auto problemResult = jaguar::SPProblem::Create(instance.Value());
    if (!Expect(problemResult.HasValue(),
                output,
                "compression minimum-width problem must be created"))
    {
        return false;
    }
    jaguar::SPProblem problem = std::move(problemResult.Value());
    if (!Expect(problem.ChangeStripWidth(4.0).HasValue()
                    && problem.PlaceItem({0U, {0.0, {2.0, 2.0}}}).HasValue(),
                output,
                "compression minimum-width initial placement must be valid"))
    {
        return false;
    }
    const jaguar::SPSolution initial = problem.Save();
    const float actualMinimumWidth =
        sparrow::detail::CompressionMinimumStripWidthForTrace(
            instance.Value(), initial);
    if (!Expect(floatBits(actualMinimumWidth) == expectedMinimumWidthBits,
                output,
                "C++ compression minimum width differs from Rust f32 bits"))
    {
        return false;
    }

    class Progress final : public sparrow::OptimizationListener
    {
    public:
        void OnCompressionProgress(double) override { ++count; }
        std::size_t count = 0U;
    };
    for (const Probe& probe : probes)
    {
        float step = 0.0F;
        std::memcpy(&step, &probe.stepBits, sizeof(step));
        const float nextWidth = static_cast<float>(
            initial.StripWidth() * (1.0 - static_cast<double>(step)));
        if (!Expect(floatBits(nextWidth) == probe.nextWidthBits,
                    output,
                    "compression boundary next width differs from Rust f32"))
        {
            return false;
        }

        sparrow::CompressionConfig config;
        config.decayStrategy =
            sparrow::ShrinkDecayStrategy::SHRINK_DECAY_FAILURE_BASED;
        config.maxIterations = 1U;
        config.maxConsecutiveFailures = 1U;
        config.shrinkMin = static_cast<double>(step);
        config.shrinkMax = static_cast<double>(step);
        config.separator.strikeLimit = 0U;
        config.separator.nWorkers = 1U;
        config.separator.nContainerSamples = 0U;
        config.separator.nFocussedSamples = 0U;
        config.separator.nCoordinateDescents = 0U;
        sparrow::OptimizationTerminator terminator({
            1U, std::chrono::seconds{2}});
        terminator.Start();
        terminator.StartPhase(std::chrono::seconds{2});
        Progress listener;
        const auto result = sparrow::Compress(
            instance.Value(), initial, config, 0U, terminator, &listener);
        if (!Expect(result.HasValue(),
                    output,
                    "compression minimum-width boundary run must complete"))
        {
            return false;
        }
        const std::size_t expectedProgress = probe.shouldEnter ? 1U : 0U;
        if (!Expect(listener.count == probe.progressCount
                        && listener.count == expectedProgress,
                    output,
                    "C++ compression boundary decision differs from Rust"))
        {
            output << "probe=" << probe.name << " progress=" << listener.count
                   << " Rust=" << probe.progressCount << '\n';
            return false;
        }
    }
    return true;
}
}

bool RunExplorationTests(std::ostream& output)
{
    const sparrow::SparrowConfig defaultSparrowConfig;
    if (!Expect(!defaultSparrowConfig.seed.has_value(),
                output,
                "default Sparrow seed must remain absent"))
    {
        return false;
    }
    sparrow::SparrowConfig explicitZeroSeedConfig;
    explicitZeroSeedConfig.seed = 0U;
    if (!Expect(explicitZeroSeedConfig.seed.has_value()
                    && *explicitZeroSeedConfig.seed == 0U,
                output,
                "explicit zero seed must remain distinguishable from absence"))
    {
        return false;
    }

    sparrow::ExplorationConfig subMillisecondExploration;
    subMillisecondExploration.timeLimit = std::chrono::microseconds{500};
    sparrow::CompressionConfig subMillisecondCompression;
    subMillisecondCompression.timeLimit = std::chrono::microseconds{500};
    sparrow::OptimizationTerminator subMillisecondTerminator({
        0U,
        std::chrono::microseconds{500}});
    subMillisecondTerminator.Start();
    subMillisecondTerminator.StartPhase(std::chrono::microseconds{500});
    std::this_thread::sleep_for(std::chrono::milliseconds{2});
    if (!Expect(subMillisecondExploration.timeLimit
                        == std::chrono::microseconds{500}
                    && subMillisecondCompression.timeLimit
                           == std::chrono::microseconds{500}
                    && subMillisecondTerminator.ShouldTerminate(0U),
                output,
                "sub-millisecond phase durations must be preserved and honored"))
    {
        return false;
    }

    if (!TestNormalPoolSamplerMatchesRustReference(output))
    {
        return false;
    }
    if (!TestEqualLossPoolInsertionMatchesRust(output))
    {
        return false;
    }
    if (!TestDisruptionFallbackMatchesRust(output))
    {
        return false;
    }
    if (!TestDisruptionPoolResetAfterRecoveryMatchesRust(output))
    {
        return false;
    }
    if (!TestWarmStartRejectionBoundariesMatchRust(output))
    {
        return false;
    }
    if (!TestEmptyLargeAndPartialWarmStartMatchRust(output))
    {
        return false;
    }
    if (!TestEmptyDiscreteRotationExplorePathMatchesRustFailureBoundary(output))
    {
        return false;
    }
    if (!TestDisruptionDistinctPoolMatchesRust(output))
    {
        return false;
    }
    if (!TestDisruptionDistinctPoolSingleCandidateMatchesRust(output))
    {
        return false;
    }
    if (!TestDisruptionDistinctPoolThreeCandidatesMatchesRust(output))
    {
        return false;
    }
    if (!TestDisruptionDistinctPoolFourCandidatesMatchesRust(output))
    {
        return false;
    }
    if (!TestDisruptionDistinctPoolFourCandidatesNoneRotationMatchesRust(output))
    {
        return false;
    }
    if (!TestContinuousRotationDisruptionAndContainedRelocationMatchesRust(output))
    {
        return false;
    }
    if (!TestDisruptionDistinctPoolFiveCandidatesMatchesRust(output))
    {
        return false;
    }
    if (!TestDisruptionDistinctPoolSixCandidatesMatchesRust(output))
    {
        return false;
    }
    if (!TestDisruptionDistinctPoolSevenCandidatesMatchesRust(output))
    {
        return false;
    }
    if (!TestDisruptionDistinctPoolEightCandidatesMatchesRust(output))
    {
        return false;
    }
    if (!TestDisruptionDistinctPoolNineCandidatesMatchesRust(output))
    {
        return false;
    }
    if (!TestDisruptionDistinctPoolTenCandidatesMatchesRust(output))
    {
        return false;
    }
    if (!TestMultipleContainedRelocationsMatchRust(output))
    {
        return false;
    }
    if (!TestContainedCollectorFiltersNoncontainedCollisionMatchesRust(output))
    {
        return false;
    }
    if (!TestBothSidesContainedRelocationsMatchRust(output))
    {
        return false;
    }
    if (!TestBothSidesObliqueContainedRelocationsMatchRust(output))
    {
        return false;
    }
    if (!TestBothSidesObliqueContainedRelocationsTwoRoundsMatchRust(output))
    {
        return false;
    }
    if (!TestBothSidesObliqueContainedRelocationsLimit4MatchesRustRelease(output))
    {
        return false;
    }
    if (!TestBothSidesObliqueContainedRelocationsLimit5MatchesRustRelease(output))
    {
        return false;
    }
    if (!TestBothSidesObliqueContainedRelocationsLimit6MatchesRustRelease(output))
    {
        return false;
    }
    if (!TestBothSidesObliqueContainedRelocationsIndependentGeometryMatchesRust(output))
    {
        return false;
    }
    if (!TestBothSidesObliqueContainedRelocationsIndependentGeometryLimit3MatchesRust(
            output))
    {
        return false;
    }
    if (!TestBothSidesObliqueContainedRelocationsIndependentGeometryLimit4MatchesRust(
            output))
    {
        return false;
    }
    if (!TestBothSidesObliqueContainedRelocationsIndependentGeometryLimit5MatchesRust(
            output))
    {
        return false;
    }
    if (!TestBothSidesObliqueContainedRelocationsIndependentGeometryLimit6MatchesRust(
            output))
    {
        return false;
    }
    if (!TestBothSidesObliqueContainedRelocationsIndependentGeometrySeed11Limit6MatchesRust(
            output))
    {
        return false;
    }
    if (!TestBothSidesObliqueContainedRelocationsIndependentGeometrySeed12Limit6MatchesRust(
            output))
    {
        return false;
    }
    if (!TestBothSidesObliqueContainedRelocationsIndependentGeometrySeed13Limit6MatchesRust(
            output))
    {
        return false;
    }
    if (!TestBothSidesObliqueContainedRelocationsIndependentGeometrySeed14Limit6MatchesRust(
            output))
    {
        return false;
    }
    if (!TestSecondIndependentGeometryObliqueBilateralSeed10Limit6MatchesRust(output))
    {
        return false;
    }
    if (!TestSecondIndependentGeometryObliqueBilateralSeed11Limit6MatchesRust(output))
    {
        return false;
    }
    if (!TestSecondIndependentGeometryObliqueBilateralSeed12Limit6MatchesRust(output))
    {
        return false;
    }
    if (!TestSecondIndependentGeometryObliqueBilateralSeed13Limit6MatchesRust(output))
    {
        return false;
    }
    if (!TestSecondIndependentGeometryObliqueBilateralSeed14Limit6MatchesRust(output))
    {
        return false;
    }
    if (!TestObliqueContainedRelocationMatchesRust(output))
    {
        return false;
    }
    if (!TestCompressionMinimumWidthMatchesRustBoundary(output))
    {
        return false;
    }
    if (!TestExploreMinimumWidthInfinityMatchesRust(output))
    {
        return false;
    }
    if (!TestExploreRotationHeightBoundaryMatchesRust(output))
    {
        return false;
    }
    if (!TestRustF32ShrinkRoundingBoundary(output))
    {
        return false;
    }
    if (!TestRustF32AreaCutoffBoundary(output))
    {
        return false;
    }
    if (!TestRustF32SimilarityBoundary(output))
    {
        return false;
    }
    if (!TestZeroExplorationFailureLimitMatchesRust(output))
    {
        return false;
    }
    if (!TestZeroPhaseTimeoutSemantics(output))
    {
        return false;
    }
    if (!TestZeroTimeoutOptimizeMatchesRustReference(output))
    {
        return false;
    }
    if (!TestPositiveTimeoutOptimizeMatchesRustReference(output))
    {
        return false;
    }
    if (!TestPositiveTimeBasedTimeoutOptimizeMatchesRustReference(output))
    {
        return false;
    }
    if (!TestPositiveTimeBasedCompressionCurveMatchesRustReference(output))
    {
        return false;
    }
    if (!TestColdStartConstructionFailureMatchesRustReference(output))
    {
        return false;
    }
    if (!TestImmediateStopOptimizeMatchesRustReference(output))
    {
        return false;
    }
    if (!TestColdStartOptimizeMatchesRustReference(output))
    {
        return false;
    }
    if (!TestDefaultColdStartOptimizeMatchesRustReference(output))
    {
        return false;
    }
    if (!TestOptimizeCompressionMatchesRustReference(output))
    {
        return false;
    }
    if (!TestCompressionWorkerMoveRetryMatchesRustReference(
            output,
            "CompressionWorkerMoveRustReference.tsv",
            "compression_worker_move",
            1U))
    {
        return false;
    }
    if (!TestCompressionWorkerMoveRetryMatchesRustReference(
            output,
            "CompressionMultiWorkerMoveRustReference.tsv",
            "compression_multi_worker_move",
            2U))
    {
        return false;
    }
    if (!TestTerminationHandoffMatchesRustReference(output))
    {
        return false;
    }
    if (!TestDiscreteRotationLegalizationMatchesRust(output))
    {
        return false;
    }

    const sparrow::ExplorationConfig defaultExplorationConfig;
    if (!Expect(std::abs(defaultExplorationConfig.solutionPoolDistributionStddev
                             - 0.25)
                        < 1.0e-12
                    && std::abs(defaultExplorationConfig
                                    .largeItemChAreaCutoffPercentile
                                - 0.75)
                           < 1.0e-12
                    && defaultExplorationConfig.maxIterations == 0U
                    && !defaultExplorationConfig.maxConsecutiveFailures.has_value()
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
    stoppedPhaseTerminator.RequestPhaseStop();
    stoppedPhaseTerminator.StartPhase(std::chrono::milliseconds{1000});
    if (!Expect(!stoppedPhaseTerminator.ShouldTerminate(0U),
                output,
                "phase reset must clear a phase-scoped interrupt request"))
    {
        return false;
    }
    stoppedPhaseTerminator.RequestStop();
    stoppedPhaseTerminator.StartPhase(std::chrono::milliseconds{1000});
    if (!Expect(stoppedPhaseTerminator.ShouldTerminate(0U),
                output,
                "phase reset must preserve persistent cancellation"))
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

    sparrow::SparrowConfig unseededConfig = optimizerConfig;
    unseededConfig.seed.reset();
    const auto unseeded = sparrow::Optimize(instance.Value(), unseededConfig);
    if (!Expect(unseeded.HasValue(),
                output,
                "an absent seed must obtain an effective replay seed"))
    {
        return false;
    }
    sparrow::SparrowConfig replayConfig = unseededConfig;
    replayConfig.seed = unseeded.Value().metrics.seed;
    const auto replayedUnseeded =
        sparrow::Optimize(instance.Value(), replayConfig);
    if (!Expect(replayedUnseeded.HasValue()
                    && replayedUnseeded.Value().metrics.seed
                           == unseeded.Value().metrics.seed
                    && SameSolution(unseeded.Value().solution,
                                    replayedUnseeded.Value().solution),
                output,
                "the effective seed from an unseeded run must reproduce it"))
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
