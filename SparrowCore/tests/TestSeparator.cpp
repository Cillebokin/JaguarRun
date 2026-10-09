#include "SparrowCore/Optimizer/OptimizationTerminator.h"
#include "SparrowCore/Optimizer/Separator.h"
#include "SparrowCore/Optimizer/SeparatorWorker.h"
#include "SparrowCore/Sampling/UniformRectSampler.h"
#include "../Source/Optimizer/SeparatorWorkerTrace.h"

#include "JaguarCore/Entities/Item.h"
#include "JaguarCore/IO/Importer.h"
#include "JaguarCore/Problems/Spp.h"

#include <cmath>
#include <chrono>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <optional>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
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
    output << "SEPARATOR TEST FAILURE: " << message << '\n';
    return false;
}

jaguar::Item MakeItem(jaguar::ItemIndex index)
{
    const auto polygon = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}, {0.0, 1.0}});
    jaguar::SurrogateConfig surrogateConfig{};
    surrogateConfig.poleLimits = {{{3U, 0.0}, {0U, 0.0}, {0U, 0.0}}};
    const auto item = jaguar::Item::Create(
        index,
        static_cast<jaguar::ItemId>(2000U + index),
        {polygon.Value(),
         jaguar::Transform2d::Identity(),
         jaguar::ShapeModifyMode::Inflate,
         {}},
        surrogateConfig);
    return item.Value();
}

jaguar::Importer MakeSeparatorTraceImporter()
{
    jaguar::ImporterConfig config;
    config.cdeConfig.quadtreeDepth = 4U;
    config.cdeConfig.cdThreshold = 64U;
    config.cdeConfig.itemSurrogateConfig.poleLimits = {
        {{64U, 0.0}, {16U, 0.8}, {8U, 0.9}}};
    config.cdeConfig.itemSurrogateConfig.failFastPoleAreaRatio = 0.5;
    return jaguar::Importer(config);
}

float FloatFromBits(std::uint32_t bits) noexcept
{
    float value = 0.0F;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

bool ParseSeparatorKind(const std::string& text,
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

bool ParseSeparatorPhase(const std::string& text,
                         sparrow::SeparatorSearchTracePhase& phase) noexcept
{
    if (text == "reference")
    {
        phase = sparrow::SeparatorSearchTracePhase::ReferencePlacement;
        return true;
    }
    if (text == "focused")
    {
        phase = sparrow::SeparatorSearchTracePhase::FocusedSample;
        return true;
    }
    if (text == "container")
    {
        phase = sparrow::SeparatorSearchTracePhase::ContainerSample;
        return true;
    }
    if (text == "prerefine")
    {
        phase = sparrow::SeparatorSearchTracePhase::PreRefine;
        return true;
    }
    if (text == "finalrefine")
    {
        phase = sparrow::SeparatorSearchTracePhase::FinalRefine;
        return true;
    }
    return false;
}

bool ReadRustSeparatorTrace(
    std::vector<sparrow::SeparatorSearchTraceEvent>& events,
    std::ostream& output)
{
    std::ifstream traceFile;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/SeparatorSearchRustTrace.tsv",
             "../SparrowCore/Tests/Fixtures/SeparatorSearchRustTrace.tsv",
             "../JaguarRun/SparrowCore/Tests/Fixtures/SeparatorSearchRustTrace.tsv",
             "..\\SparrowCore\\Tests\\Fixtures\\SeparatorSearchRustTrace.tsv"})
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
                "Rust separator search trace fixture must be readable"))
    {
        return false;
    }

    std::optional<std::size_t> lastReport;
    std::size_t expectedKeepCount = 0U;
    std::size_t lastReportOrdinal = 0U;
    sparrow::SeparatorSearchTracePhase lastReportPhase =
        sparrow::SeparatorSearchTracePhase::ReferencePlacement;
    std::array<std::size_t, 5> evalOrdinals{};
    std::array<std::size_t, 5> reportOrdinals{};
    bool sawResult = false;
    const auto phaseIndex = [](sparrow::SeparatorSearchTracePhase phase) {
        return static_cast<std::size_t>(phase);
    };
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
        if (record != "keep" && lastReport.has_value())
        {
            if (!Expect(events[*lastReport].retained.size() == expectedKeepCount,
                        output,
                        "Rust separator retained count differs from its report"))
            {
                return false;
            }
            lastReport.reset();
        }

        sparrow::SeparatorSearchTraceEvent event;
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
                || !ParseSeparatorPhase(phaseText, event.phase)
                || !ParseSeparatorKind(kindText, event.evaluation.kind)
                || ordinal != evalOrdinals[phaseIndex(event.phase)])
            {
                return Expect(false, output, "malformed Rust separator eval row");
            }
            ++evalOrdinals[phaseIndex(event.phase)];
            event.type = sparrow::SeparatorSearchTraceEvent::Type::Evaluation;
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
                || !ParseSeparatorPhase(phaseText, event.phase)
                || !ParseSeparatorKind(kindText, event.evaluation.kind)
                || accepted > 1U
                || ordinal != reportOrdinals[phaseIndex(event.phase)])
            {
                return Expect(false, output, "malformed Rust separator report row");
            }
            ++reportOrdinals[phaseIndex(event.phase)];
            event.type = sparrow::SeparatorSearchTraceEvent::Type::Report;
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
            lastReportPhase = events[*lastReport].phase;
        }
        else if (record == "keep")
        {
            std::string phaseText;
            std::size_t ordinal = 0U;
            std::size_t rank = 0U;
            if (!(row >> phaseText >> ordinal >> rank >> std::hex
                      >> rotationBits >> xBits >> yBits >> kindText >> lossBits)
                || !ParseSeparatorPhase(phaseText, event.phase)
                || !ParseSeparatorKind(kindText, event.evaluation.kind)
                || !lastReport.has_value()
                || ordinal != lastReportOrdinal
                || event.phase != lastReportPhase)
            {
                return Expect(false, output, "malformed Rust separator keep row");
            }
            auto& report = events[*lastReport];
            if (rank != report.retained.size())
            {
                return Expect(false,
                              output,
                              "Rust separator retained order is malformed");
            }
            const auto transform = jaguar::Transform2d::FromRotationTranslation(
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
            if (!(row >> event.evaluationCount >> present) || present > 1U)
            {
                return Expect(false,
                              output,
                              "malformed Rust separator result row");
            }
            event.type = sparrow::SeparatorSearchTraceEvent::Type::Result;
            event.phase = sparrow::SeparatorSearchTracePhase::FinalRefine;
            event.present = present != 0U;
            if (event.present)
            {
                if (!(row >> std::hex >> rotationBits >> xBits >> yBits
                          >> kindText >> lossBits)
                    || !ParseSeparatorKind(kindText, event.evaluation.kind))
                {
                    return Expect(false,
                                  output,
                                  "malformed Rust separator result sample");
                }
                event.transform = jaguar::Transform2d::FromRotationTranslation(
                    FloatFromBits(rotationBits),
                    {FloatFromBits(xBits), FloatFromBits(yBits)});
                event.evaluation.loss = FloatFromBits(lossBits);
            }
            events.push_back(std::move(event));
            sawResult = true;
        }
        else
        {
            return Expect(false, output, "unknown Rust separator trace row");
        }
    }
    if (lastReport.has_value()
        && !Expect(events[*lastReport].retained.size() == expectedKeepCount,
                   output,
                   "Rust separator final retained count differs from its report"))
    {
        return false;
    }
    return Expect(sawResult && !events.empty(),
                  output,
                  "Rust separator trace must contain a result event");
}

struct SeparatorRoundReference
{
    struct Progress
    {
        std::size_t iteration = 0U;
        double stripWidth = 0.0;
        double density = 0.0;
        float minimumLoss = 0.0F;
    };

    struct Report
    {
        std::string kind;
        std::size_t placementCount = 0U;
    };

    struct Placement
    {
        std::size_t rank = 0U;
        std::size_t itemIndex = 0U;
        double rotation = 0.0;
        double x = 0.0;
        double y = 0.0;
        float loss = 0.0F;
        float weightedLoss = 0.0F;
    };

    struct Pair
    {
        std::size_t firstRank = 0U;
        std::size_t secondRank = 0U;
        float loss = 0.0F;
        float weight = 0.0F;
    };

    std::vector<Progress> progress;
    std::vector<Report> reports;
    std::vector<Placement> placements;
    std::vector<Pair> pairs;
    bool hasResult = false;
    bool success = false;
    std::size_t evaluationCount = 0U;
    std::size_t moveCount = 0U;
    std::size_t iterations = 0U;
    bool hasState = false;
    float totalLoss = 0.0F;
    float totalWeightedLoss = 0.0F;
    bool separated = false;
    bool feasible = false;
};

bool ReadRustSeparatorRoundReference(SeparatorRoundReference& reference,
                                    const char* fixtureName,
                                    std::ostream& output)
{
    std::ifstream traceFile;
    const std::string fixturePath =
        std::string("SparrowCore/Tests/Fixtures/") + fixtureName;
    const std::string parentFixturePath =
        std::string("../SparrowCore/Tests/Fixtures/") + fixtureName;
    const std::string projectFixturePath =
        std::string("../JaguarRun/SparrowCore/Tests/Fixtures/") + fixtureName;
    const std::string windowsFixturePath =
        std::string("..\\SparrowCore\\Tests\\Fixtures\\") + fixtureName;
    for (const std::string& path : {fixturePath, parentFixturePath,
                                    projectFixturePath, windowsFixturePath})
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
                "Rust separator round fixture must be readable"))
    {
        return false;
    }

    std::string line;
    std::size_t expectedProgressOrdinal = 0U;
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
        if (record == "progress")
        {
            std::size_t ordinal = 0U;
            SeparatorRoundReference::Progress progress;
            if (!(row >> ordinal >> progress.iteration >> progress.stripWidth
                      >> progress.density >> progress.minimumLoss)
                || ordinal != expectedProgressOrdinal++)
            {
                return Expect(false,
                              output,
                              "malformed Rust separator progress row");
            }
            reference.progress.push_back(progress);
        }
        else if (record == "report")
        {
            SeparatorRoundReference::Report report;
            if (!(row >> report.kind >> report.placementCount))
            {
                return Expect(false,
                              output,
                              "malformed Rust separator improvement row");
            }
            reference.reports.push_back(std::move(report));
        }
        else if (record == "result")
        {
            unsigned success = 0U;
            if (!(row >> success >> reference.evaluationCount
                      >> reference.moveCount >> reference.iterations)
                || success > 1U || reference.hasResult)
            {
                return Expect(false,
                              output,
                              "malformed Rust separator result row");
            }
            reference.success = success != 0U;
            reference.hasResult = true;
        }
        else if (record == "state")
        {
            unsigned separated = 0U;
            unsigned feasible = 0U;
            if (!(row >> reference.totalLoss >> reference.totalWeightedLoss
                      >> separated >> feasible)
                || separated > 1U || feasible > 1U || reference.hasState)
            {
                return Expect(false,
                              output,
                              "malformed Rust separator final-state row");
            }
            reference.separated = separated != 0U;
            reference.feasible = feasible != 0U;
            reference.hasState = true;
        }
        else if (record == "placement")
        {
            SeparatorRoundReference::Placement placement;
            if (!(row >> placement.rank >> placement.itemIndex
                      >> placement.rotation >> placement.x >> placement.y
                      >> placement.loss >> placement.weightedLoss)
                || placement.rank != reference.placements.size())
            {
                return Expect(false,
                              output,
                              "malformed Rust separator placement row");
            }
            reference.placements.push_back(placement);
        }
        else if (record == "pair")
        {
            SeparatorRoundReference::Pair pair;
            if (!(row >> pair.firstRank >> pair.secondRank
                      >> pair.loss >> pair.weight)
                || pair.firstRank >= pair.secondRank)
            {
                return Expect(false,
                              output,
                              "malformed Rust separator pair row");
            }
            reference.pairs.push_back(pair);
        }
        else
        {
            return Expect(false,
                          output,
                          "unknown Rust separator round record");
        }
    }
    return Expect(reference.hasResult && reference.hasState
                      && !reference.progress.empty()
                      && reference.placements.size() == 9U
                      && reference.pairs.size() == 36U,
                  output,
                  "Rust separator round fixture is incomplete");
}

class SeparatorRoundRecordingListener final : public sparrow::OptimizationListener
{
public:
    void OnSolution(
        const sparrow::OptimizationSolutionReport& report) override
    {
        reports.push_back(report);
    }

    void OnSeparationProgress(
        const sparrow::SeparationProgress& event) override
    {
        progress.push_back(event);
    }

    void OnSeparationResult(
        const sparrow::SeparationResult& event) override
    {
        result = event;
    }

    std::vector<sparrow::OptimizationSolutionReport> reports;
    std::vector<sparrow::SeparationProgress> progress;
    std::optional<sparrow::SeparationResult> result;
};

bool TestRustSeparatorRound(std::size_t workerCount,
                            std::uint64_t seed,
                            const char* traceFixtureName,
                            std::ostream& output)
{
    SeparatorRoundReference expected;
    if (!ReadRustSeparatorRoundReference(expected, traceFixtureName, output))
        return false;

    std::ifstream inputFile;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/SeparatorRoundRustTrace.json",
             "../SparrowCore/Tests/Fixtures/SeparatorRoundRustTrace.json",
             "../JaguarRun/SparrowCore/Tests/Fixtures/SeparatorRoundRustTrace.json",
             "..\\SparrowCore\\Tests\\Fixtures\\SeparatorRoundRustTrace.json"})
    {
        inputFile.open(path);
        if (inputFile.is_open())
            break;
        inputFile.clear();
    }
    if (!Expect(inputFile.is_open(), output,
                "separator round input fixture must be readable"))
        return false;

    const std::string json{
        std::istreambuf_iterator<char>(inputFile),
        std::istreambuf_iterator<char>()};
    const auto instance = MakeSeparatorTraceImporter().ImportSPInstanceJson(json);
    if (!Expect(instance.HasValue(), output,
                "separator round input must import successfully"))
        return false;
    auto problemResult = jaguar::SPProblem::Create(instance.Value());
    if (!Expect(problemResult.HasValue(), output,
                "separator round problem creation failed"))
        return false;
    jaguar::SPProblem problem = std::move(problemResult.Value());
    if (!Expect(problem.ChangeStripWidth(9.0).HasValue(), output,
                "separator round strip resize failed"))
        return false;

    const auto initial = jaguar::Transform2d::FromRotationTranslation(
        0.0, {1.0, 1.0});
    for (std::size_t index = 0U; index < 9U; ++index)
    {
        if (!Expect(problem.PlaceItem({0U, initial}).HasValue(), output,
                    "separator round overlapping placement failed"))
            return false;
    }

    auto separatorResult = sparrow::Separator::Create(std::move(problem), seed);
    if (!Expect(separatorResult.HasValue(), output,
                "separator round creation failed"))
        return false;
    sparrow::Separator separator = std::move(separatorResult.Value());
    sparrow::SeparatorConfig config;
    config.strikeLimit = 1U;
    config.noImprovementLimit = 1U;
    config.nContainerSamples = 1U;
    config.nFocussedSamples = 0U;
    config.nCoordinateDescents = 1U;
    config.nWorkers = workerCount;

    SeparatorRoundRecordingListener listener;
    sparrow::OptimizationTerminatorConfig terminatorConfig;
    terminatorConfig.maxIterations = expected.iterations;
    sparrow::OptimizationTerminator terminator(terminatorConfig);
    const auto result = separator.Run(config, terminator, &listener);
    if (!Expect(result.HasValue(), output, "separator round run failed"))
        return false;

    if (!Expect(listener.progress.size() == expected.progress.size(), output,
                "Rust/C++ separator progress counts differ"))
    {
        output << "Rust progress=" << expected.progress.size()
               << " C++ progress=" << listener.progress.size() << '\n';
        for (std::size_t index = 0U; index < expected.progress.size(); ++index)
        {
            output << "Rust round " << expected.progress[index].iteration
                   << " min_loss=" << expected.progress[index].minimumLoss
                   << '\n';
        }
        for (const auto& progress : listener.progress)
        {
            output << "C++ round " << progress.iteration
                   << " min_loss=" << progress.minimumLoss << '\n';
        }
        return false;
    }
    for (std::size_t index = 0U; index < expected.progress.size(); ++index)
    {
        const auto& rust = expected.progress[index];
        const auto& cpp = listener.progress[index];
        if (!Expect(rust.iteration == cpp.iteration
                        && std::abs(rust.stripWidth - cpp.stripWidth) <= 1.0e-5
                        && std::abs(rust.density - cpp.density) <= 1.0e-4
                        && std::abs(rust.minimumLoss - cpp.minimumLoss) <= 2.0e-3F,
                    output, "Rust/C++ separator progress differs"))
        {
            output << std::setprecision(10) << "progress=" << index
                   << " Rust(loss=" << rust.minimumLoss << ") C++(loss="
                   << cpp.minimumLoss << ")\n";
            return false;
        }
    }

    if (!Expect(listener.reports.size() == expected.reports.size(), output,
                "Rust/C++ separator improvement report counts differ"))
    {
        output << "Rust reports=" << expected.reports.size()
               << " C++ reports=" << listener.reports.size() << '\n';
        return false;
    }
    for (std::size_t index = 0U; index < expected.reports.size(); ++index)
    {
        const auto& report = listener.reports[index];
        if (!Expect(expected.reports[index].kind == "expl_improving"
                        && report.type
                               == sparrow::OptimizationReportType::OPTIMIZATION_REPORT_EXPLORATION_IMPROVING
                        && report.solution.layoutSnapshot.placements.size()
                               == expected.reports[index].placementCount,
                    output, "Rust/C++ separator improvement reports differ"))
            return false;
    }

    if (!Expect(expected.hasResult && listener.result.has_value(), output,
                "separator run result was not recorded"))
        return false;
    const auto& rustResult = expected;
    const auto& cppResult = result.Value();
    if (!Expect(listener.result->success == rustResult.success
                    && cppResult.totalEvaluationCount == rustResult.evaluationCount
                    && cppResult.totalMoveCount == rustResult.moveCount
                    && cppResult.iterations == rustResult.iterations
                    && listener.result->totalEvaluations == rustResult.evaluationCount
                    && listener.result->totalMoves == rustResult.moveCount
                    && listener.result->iterations == rustResult.iterations,
                output, "Rust/C++ separator summary counters differ"))
    {
        output << "Rust(evals=" << rustResult.evaluationCount
               << ", moves=" << rustResult.moveCount
               << ", iterations=" << rustResult.iterations
               << ") C++(evals=" << cppResult.totalEvaluationCount
               << ", moves=" << cppResult.totalMoveCount
               << ", iterations=" << cppResult.iterations << ")\n";
        return false;
    }

    jaguar::Layout finalLayout(cppResult.solution.layoutSnapshot.container);
    if (!Expect(finalLayout.Restore(cppResult.solution.layoutSnapshot).HasValue(),
                output, "C++ separator final layout restore failed"))
        return false;
    const auto& tracker = separator.Tracker();
    if (!Expect(std::abs(tracker.TotalLoss() - rustResult.totalLoss) <= 2.0e-2F
                    && std::abs(tracker.TotalWeightedLoss()
                                - rustResult.totalWeightedLoss) <= 5.0e-2F
                    && tracker.IsSeparated() == rustResult.separated
                    && finalLayout.IsFeasible() == rustResult.feasible,
                output, "Rust/C++ separator final tracker state differs"))
    {
        output << std::setprecision(10) << "Rust(loss=" << rustResult.totalLoss
               << ", weighted=" << rustResult.totalWeightedLoss
               << ") C++(loss=" << tracker.TotalLoss()
               << ", weighted=" << tracker.TotalWeightedLoss() << ")\n";
        return false;
    }

    std::vector<const jaguar::LayoutPlacementSnapshot*> placements;
    for (const auto& placement : cppResult.solution.layoutSnapshot.placements)
        placements.push_back(&placement);
    std::sort(placements.begin(), placements.end(),
              [](const auto* first, const auto* second) {
                  const auto& firstTransform = first->placedItem.Transform();
                  const auto& secondTransform = second->placedItem.Transform();
                  return std::make_tuple(first->placedItem.GetItemIndex(),
                                         firstTransform.rotationRad,
                                         firstTransform.translation.x,
                                         firstTransform.translation.y)
                      < std::make_tuple(second->placedItem.GetItemIndex(),
                                        secondTransform.rotationRad,
                                        secondTransform.translation.x,
                                        secondTransform.translation.y);
              });
    if (!Expect(placements.size() == rustResult.placements.size(), output,
                "Rust/C++ separator final placement counts differ"))
        return false;
    std::vector<std::size_t> trackerIndices;
    trackerIndices.reserve(placements.size());
    for (std::size_t rank = 0U; rank < placements.size(); ++rank)
    {
        const auto& rust = rustResult.placements[rank];
        const auto& cpp = *placements[rank];
        const auto& transform = cpp.placedItem.Transform();
        const auto trackerIndex = tracker.IndexForHandle(cpp.handle);
        if (!Expect(trackerIndex.has_value(), output,
                    "C++ separator placement is absent from tracker"))
            return false;
        trackerIndices.push_back(*trackerIndex);
        if (!Expect(rust.rank == rank
                        && rust.itemIndex == cpp.placedItem.GetItemIndex()
                        && std::abs(rust.rotation - transform.rotationRad) <= 2.0e-5
                        && std::abs(rust.x - transform.translation.x) <= 2.0e-5
                        && std::abs(rust.y - transform.translation.y) <= 2.0e-5
                        && std::abs(rust.loss - tracker.ItemLoss(*trackerIndex))
                               <= 2.0e-2F
                        && std::abs(rust.weightedLoss
                                    - tracker.WeightedItemLoss(*trackerIndex))
                               <= 5.0e-2F,
                    output, "Rust/C++ separator final placements differ"))
        {
            output << std::setprecision(10) << "placement=" << rank
                   << " Rust=(" << rust.x << ',' << rust.y << ") C++=("
                   << transform.translation.x << ','
                   << transform.translation.y << ") loss Rust=" << rust.loss
                   << " C++=" << tracker.ItemLoss(*trackerIndex)
                   << " weighted Rust=" << rust.weightedLoss
                   << " C++=" << tracker.WeightedItemLoss(*trackerIndex)
                   << '\n';
            return false;
        }
    }
    for (const auto& pair : rustResult.pairs)
    {
        if (!Expect(pair.firstRank < trackerIndices.size()
                        && pair.secondRank < trackerIndices.size(),
                    output, "Rust separator pair ranks are out of range"))
            return false;
        const float cppLoss = tracker.PairLoss(
            trackerIndices[pair.firstRank], trackerIndices[pair.secondRank]);
        const float cppWeight = tracker.PairWeight(
            trackerIndices[pair.firstRank], trackerIndices[pair.secondRank]);
        if (!Expect(std::abs(pair.loss - cppLoss) <= 2.0e-2F
                        && std::abs(pair.weight - cppWeight) <= 5.0e-2F,
                    output, "Rust/C++ separator pair tracker differs"))
        {
            output << std::setprecision(10) << "pair ranks=("
                   << pair.firstRank << ',' << pair.secondRank
                   << ") Rust(loss=" << pair.loss << ", weight="
                   << pair.weight << ") C++(loss=" << cppLoss
                   << ", weight=" << cppWeight << ")\n";
            return false;
        }
    }
    return true;
}

bool SameSeparatorSample(
    const jaguar::Transform2d& actualTransform,
    const sparrow::SampleEval& actualEvaluation,
    const jaguar::Transform2d& expectedTransform,
    const sparrow::SampleEval& expectedEvaluation) noexcept
{
    return actualEvaluation.kind == expectedEvaluation.kind
        && std::abs(actualTransform.rotationRad - expectedTransform.rotationRad)
               <= 2.0e-5
        && std::abs(actualTransform.translation.x
                    - expectedTransform.translation.x)
               <= 2.0e-5
        && std::abs(actualTransform.translation.y
                    - expectedTransform.translation.y)
               <= 2.0e-5
        && (actualEvaluation.kind == sparrow::SampleKind::SAMPLE_KIND_INVALID
            || std::abs(actualEvaluation.loss - expectedEvaluation.loss)
                   <= 2.0e-3F);
}

bool TestRustSeparatorSearchTrace(std::ostream& output)
{
    std::vector<sparrow::SeparatorSearchTraceEvent> expected;
    if (!ReadRustSeparatorTrace(expected, output))
    {
        return false;
    }

    std::ifstream instanceFile;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/SeparatorRustTrace.json",
             "../SparrowCore/Tests/Fixtures/SeparatorRustTrace.json",
             "../JaguarRun/SparrowCore/Tests/Fixtures/SeparatorRustTrace.json",
             "..\\SparrowCore\\Tests\\Fixtures\\SeparatorRustTrace.json"})
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
                "shared separator trace input must be readable"))
    {
        return false;
    }
    const std::string instanceJson{
        std::istreambuf_iterator<char>(instanceFile),
        std::istreambuf_iterator<char>()};
    const jaguar::Importer importer = MakeSeparatorTraceImporter();
    const auto instance = importer.ImportSPInstanceJson(instanceJson);
    if (!Expect(instance.HasValue(),
                output,
                "shared separator trace input must import successfully"))
    {
        return false;
    }
    auto problemResult = jaguar::SPProblem::Create(instance.Value());
    if (!Expect(problemResult.HasValue(),
                output,
                "separator trace problem creation failed"))
    {
        return false;
    }
    jaguar::SPProblem problem = std::move(problemResult.Value());
    if (!Expect(problem.ChangeStripWidth(8.0).HasValue(),
                output,
                "separator trace strip resize failed"))
    {
        return false;
    }
    const jaguar::Transform2d initialTransform =
        jaguar::Transform2d::FromRotationTranslation(0.0, {1.0, 1.0});
    if (!Expect(problem.PlaceItem({0U, initialTransform}).HasValue()
                    && problem.PlaceItem({0U, initialTransform}).HasValue(),
                output,
                "separator trace overlapping placements failed"))
    {
        return false;
    }
    const jaguar::SPSolution initial = problem.Save();
    jaguar::Layout layout(initial.layoutSnapshot.container);
    if (!Expect(layout.Restore(initial.layoutSnapshot).HasValue(),
                output,
                "separator trace layout restore failed"))
    {
        return false;
    }
    auto trackerResult = sparrow::CollisionTracker::Create(layout);
    if (!Expect(trackerResult.HasValue(),
                output,
                "separator trace collision tracker creation failed"))
    {
        return false;
    }
    sparrow::CollisionTracker tracker = std::move(trackerResult.Value());
    sparrow::SeparatorConfig config;
    config.nContainerSamples = 16U;
    config.nFocussedSamples = 8U;
    config.nCoordinateDescents = 2U;
    sparrow::SparrowRng rng(42U);
    std::vector<sparrow::SeparatorSearchTraceEvent> actual;
    const auto result = sparrow::FindBestSeparatorMoveWithTrace(
        layout,
        tracker,
        initial.layoutSnapshot.placements[0].handle,
        config,
        rng,
        [&actual](const sparrow::SeparatorSearchTraceEvent& event) {
            actual.push_back(event);
        });
    if (!Expect(result.HasValue(),
                output,
                "C++ separator trace search must complete"))
    {
        return false;
    }
    const std::size_t sharedCount = std::min(actual.size(), expected.size());
    for (std::size_t index = 0U; index < sharedCount; ++index)
    {
        const auto& rustEvent = expected[index];
        const auto& cppEvent = actual[index];
        bool matches = rustEvent.type == cppEvent.type
            && rustEvent.phase == cppEvent.phase;
        if (matches
            && rustEvent.type == sparrow::SeparatorSearchTraceEvent::Type::Evaluation)
        {
            matches = SameSeparatorSample(
                cppEvent.transform,
                cppEvent.evaluation,
                rustEvent.transform,
                rustEvent.evaluation);
        }
        else if (matches
                 && rustEvent.type == sparrow::SeparatorSearchTraceEvent::Type::Report)
        {
            matches = rustEvent.accepted == cppEvent.accepted
                && SameSeparatorSample(
                    cppEvent.transform,
                    cppEvent.evaluation,
                    rustEvent.transform,
                    rustEvent.evaluation)
                && rustEvent.retained.size() == cppEvent.retained.size();
            for (std::size_t rank = 0U;
                 matches && rank < rustEvent.retained.size();
                 ++rank)
            {
                matches = SameSeparatorSample(
                    cppEvent.retained[rank].first,
                    cppEvent.retained[rank].second,
                    rustEvent.retained[rank].first,
                    rustEvent.retained[rank].second);
            }
        }
        else if (matches
                 && rustEvent.type == sparrow::SeparatorSearchTraceEvent::Type::Result)
        {
            matches = rustEvent.present == cppEvent.present
                && rustEvent.evaluationCount == cppEvent.evaluationCount
                && result.Value().evaluationCount == rustEvent.evaluationCount
                && result.Value().transform.has_value() == rustEvent.present;
            if (matches && rustEvent.present)
            {
                matches = SameSeparatorSample(
                    *result.Value().transform,
                    result.Value().evaluation,
                    rustEvent.transform,
                    rustEvent.evaluation)
                    && SameSeparatorSample(
                        cppEvent.transform,
                        cppEvent.evaluation,
                        rustEvent.transform,
                        rustEvent.evaluation);
            }
        }
        if (!Expect(matches,
                    output,
                    "Rust/C++ separator search trace differs at an event"))
        {
            output << "separator trace event=" << index
                   << " rust_type=" << static_cast<int>(rustEvent.type)
                   << " cpp_type=" << static_cast<int>(cppEvent.type)
                   << " rust_phase=" << static_cast<int>(rustEvent.phase)
                   << " cpp_phase=" << static_cast<int>(cppEvent.phase)
                   << " rust_events=" << expected.size()
                   << " cpp_events=" << actual.size() << '\n';
            output << std::setprecision(10)
                   << "rust sample kind="
                   << static_cast<int>(rustEvent.evaluation.kind)
                   << " loss=" << rustEvent.evaluation.loss
                   << " transform=(" << rustEvent.transform.rotationRad << ','
                   << rustEvent.transform.translation.x << ','
                   << rustEvent.transform.translation.y << ") accepted="
                   << rustEvent.accepted << " retained="
                   << rustEvent.retained.size() << '\n'
                   << "cpp sample kind="
                   << static_cast<int>(cppEvent.evaluation.kind)
                   << " loss=" << cppEvent.evaluation.loss
                   << " transform=(" << cppEvent.transform.rotationRad << ','
                   << cppEvent.transform.translation.x << ','
                   << cppEvent.transform.translation.y << ") accepted="
                   << cppEvent.accepted << " retained="
                   << cppEvent.retained.size() << '\n';
            return false;
        }
    }
    return Expect(actual.size() == expected.size(),
                  output,
                  "Rust/C++ separator event counts differ");
}

bool TestZeroSeparatorSampleBudgetUsesReferencePlacement(std::ostream& output)
{
    const auto instance = jaguar::SPInstance::Create(
        {MakeItem(0U)}, {1U}, 4.0, jaguar::CDEConfig{}, jaguar::ShapeModifyConfig{});
    if (!Expect(instance.HasValue(), output,
                "zero-sample separator instance creation failed"))
    {
        return false;
    }
    auto problemResult = jaguar::SPProblem::Create(instance.Value());
    if (!Expect(problemResult.HasValue(), output,
                "zero-sample separator problem creation failed"))
    {
        return false;
    }
    jaguar::SPProblem problem = std::move(problemResult.Value());
    if (!Expect(problem.ChangeStripWidth(8.0).HasValue(),
                output,
                "zero-sample separator strip resize failed"))
    {
        return false;
    }
    const auto moving = problem.PlaceItem({0U, {0.0, {1.0, 1.0}}});
    if (!Expect(moving.HasValue(), output,
                "zero-sample separator reference placement failed"))
    {
        return false;
    }
    const jaguar::SPSolution snapshot = problem.Save();
    jaguar::Layout layout(snapshot.layoutSnapshot.container);
    if (!Expect(layout.Restore(snapshot.layoutSnapshot).HasValue(),
                output,
                "zero-sample separator layout restore failed"))
    {
        return false;
    }
    const auto trackerResult = sparrow::CollisionTracker::Create(layout);
    if (!Expect(trackerResult.HasValue(), output,
                "zero-sample separator tracker creation failed"))
    {
        return false;
    }
    sparrow::SeparatorConfig config;
    config.nContainerSamples = 0U;
    config.nFocussedSamples = 0U;
    config.nCoordinateDescents = 1U;
    std::vector<sparrow::SeparatorSearchTraceEvent> events;
    sparrow::SparrowRng rng(42U);
    const auto result = sparrow::FindBestSeparatorMoveWithTrace(
        layout,
        trackerResult.Value(),
        moving.Value(),
        config,
        rng,
        [&events](const auto& event) { events.push_back(event); });
    if (!Expect(result.HasValue() && result.Value().transform.has_value()
                    && result.Value().evaluationCount > 0U,
                output,
                "zero sample loops must still evaluate and refine the current reference placement"))
    {
        return false;
    }
    if (!Expect(!events.empty()
                    && events.front().type
                           == sparrow::SeparatorSearchTraceEvent::Type::Evaluation
                    && events.front().phase
                           == sparrow::SeparatorSearchTracePhase::ReferencePlacement,
                output,
                "zero-sample search must begin with the Rust reference placement"))
    {
        return false;
    }
    return Expect(std::none_of(
                      events.begin(), events.end(), [](const auto& event) {
                          return event.phase
                                     == sparrow::SeparatorSearchTracePhase::FocusedSample
                              || event.phase
                                     == sparrow::SeparatorSearchTracePhase::ContainerSample;
                      }),
                  output,
                  "zero sample budgets must not emit focused or container sample events");
}

bool TestUnsupportedHazardPropagatesThroughWorkerSearch(std::ostream& output)
{
    jaguar::Importer importer;
    const auto outerShape = [] {
        return jaguar::ExternalShape::Polygon({
            {0.0, 0.0}, {20.0, 0.0}, {20.0, 20.0}, {0.0, 20.0}});
    };
    const jaguar::ExternalItem externalItem{
        9701U,
        jaguar::RotationPolicy::Continuous(),
        jaguar::ExternalShape::Rectangle(0.0, 0.0, 1.0, 1.0),
        std::nullopt};
    const auto item = importer.ImportItem(externalItem, 0U);
    if (!Expect(item.HasValue(), output,
                "worker propagation item import failed"))
    {
        return false;
    }

    const jaguar::ExternalContainer sampleExternal{
        9700U, outerShape(), {}};
    const auto sampleContainer = importer.ImportContainer(sampleExternal);
    if (!Expect(sampleContainer.HasValue(), output,
                "worker propagation sample container import failed"))
    {
        return false;
    }
    const auto samplerResult = sparrow::UniformRectSampler::Create(
        sampleContainer.Value().CollisionShape().BoundingBox(),
        item.Value(),
        sampleContainer.Value().CollisionShape().BoundingBox());
    if (!Expect(samplerResult.HasValue()
                    && samplerResult.Value().has_value(),
                output,
                "worker propagation sampler construction failed"))
    {
        return false;
    }
    sparrow::SparrowRng expectedRng(42U);
    const jaguar::Transform2d candidate =
        samplerResult.Value()->Sample(expectedRng);
    const std::uint64_t expectedRngAfterSample = expectedRng.NextU64();
    const auto candidateShape =
        item.Value().CollisionShape().TransformedRustF32(candidate);
    if (!Expect(candidateShape.HasValue(), output,
                "worker propagation candidate transform failed"))
    {
        return false;
    }
    const jaguar::Rect2d candidateBounds =
        candidateShape.Value().BoundingBox();
    jaguar::ExternalContainer hazardExternal{
        9702U,
        outerShape(),
        {{1U,
          jaguar::ExternalShape::Rectangle(
              candidateBounds.xMin,
              candidateBounds.yMin,
              candidateBounds.Width(),
              candidateBounds.Height())}}};
    const auto hazardContainer = importer.ImportContainer(hazardExternal);
    if (!Expect(hazardContainer.HasValue(), output,
                "worker propagation hazard container import failed"))
    {
        return false;
    }

    const std::array<jaguar::Point2d, 4> corners{{
        {2.0, 2.0}, {18.0, 2.0}, {2.0, 18.0}, {18.0, 18.0}}};
    const auto currentCorner = std::max_element(
        corners.begin(),
        corners.end(),
        [&candidate](const jaguar::Point2d& left,
                     const jaguar::Point2d& right) {
            const jaguar::Point2d sample = candidate.translation;
            return left.SquaredDistanceTo(sample)
                < right.SquaredDistanceTo(sample);
        });
    jaguar::Layout layout(hazardContainer.Value());
    const auto movingHandle = layout.PlaceItem(
        item.Value(),
        jaguar::Transform2d::FromRotationTranslation(0.0, *currentCorner));
    if (!Expect(movingHandle.HasValue(), output,
                "worker propagation reference placement failed"))
    {
        return false;
    }
    const auto trackerResult = sparrow::CollisionTracker::Create(layout);
    if (!Expect(trackerResult.HasValue(), output,
                "worker propagation tracker creation failed"))
    {
        return false;
    }

    const auto before = layout.Save();
    const auto beforeTransform =
        before.placements.front().placedItem.Transform();
    const float beforeLoss = trackerResult.Value().TotalLoss();
    if (!Expect(beforeLoss == 0.0F, output,
                "worker propagation reference placement must be clear"))
    {
        return false;
    }
    sparrow::SeparatorConfig config;
    config.nFocussedSamples = 0U;
    config.nContainerSamples = 1U;
    config.nCoordinateDescents = 1U;
    sparrow::SparrowRng rng(42U);
    std::vector<sparrow::SeparatorSearchTraceEvent> events;
    bool propagated = false;
    try
    {
        (void)sparrow::FindBestSeparatorMoveWithTrace(
            layout,
            trackerResult.Value(),
            movingHandle.Value(),
            config,
            rng,
            [&events](const sparrow::SeparatorSearchTraceEvent& event) {
                events.push_back(event);
            });
    }
    catch (const std::logic_error& error)
    {
        propagated = std::string(error.what()) == "unsupported hazard entity";
    }

    const auto after = layout.Save();
    const auto& afterTransform = after.placements.front().placedItem.Transform();
    const bool sameTransform =
        beforeTransform.rotationRad == afterTransform.rotationRad
        && beforeTransform.translation == afterTransform.translation;
    const std::uint64_t actualRngAfterFailure = rng.NextU64();
    const bool traceMatches = events.size() == 2U
        && events[0].type
               == sparrow::SeparatorSearchTraceEvent::Type::Evaluation
        && events[0].phase
               == sparrow::SeparatorSearchTracePhase::ReferencePlacement
        && events[0].evaluation.kind
               == sparrow::SampleKind::SAMPLE_KIND_CLEAR
        && events[1].type
               == sparrow::SeparatorSearchTraceEvent::Type::Report
        && events[1].phase
               == sparrow::SeparatorSearchTracePhase::ReferencePlacement;
    return Expect(propagated
                      && actualRngAfterFailure == expectedRngAfterSample
                      && layout.PlacedItemCount() == 1U
                      && sameTransform
                      && trackerResult.Value().TotalLoss() == beforeLoss
                      && traceMatches,
                  output,
                  "unsupported hazard must unwind the worker search after one sample without changing layout or emitting a candidate callback");
}
}

bool RunSeparatorTests(std::ostream& output)
{
    if (!TestRustSeparatorRound(
            1U, 42U, "SeparatorRoundRustTrace.tsv", output))
    {
        return false;
    }
    if (!TestRustSeparatorRound(
            2U, 42U, "SeparatorMultiWorkerRoundRustTrace.tsv", output))
    {
        return false;
    }
    if (!TestRustSeparatorRound(
            3U, 43U, "SeparatorThreeWorkerSeed43RoundRustTrace.tsv", output))
    {
        return false;
    }
    if (!TestRustSeparatorSearchTrace(output))
    {
        return false;
    }
    if (!TestZeroSeparatorSampleBudgetUsesReferencePlacement(output))
    {
        return false;
    }
    if (!TestUnsupportedHazardPropagatesThroughWorkerSearch(output))
    {
        return false;
    }

    const auto instance = jaguar::SPInstance::Create(
        {MakeItem(0U), MakeItem(1U)},
        {1U, 1U},
        4.0,
        jaguar::CDEConfig{},
        jaguar::ShapeModifyConfig{});
    if (!Expect(instance.HasValue(), output, "separator instance creation failed"))
    {
        return false;
    }
    auto problemResult = jaguar::SPProblem::Create(instance.Value());
    if (!Expect(problemResult.HasValue(), output, "separator problem creation failed"))
    {
        return false;
    }
    jaguar::SPProblem problem = std::move(problemResult.Value());
    if (!Expect(problem.ChangeStripWidth(8.0).HasValue(),
                output,
                "separator strip resize failed"))
    {
        return false;
    }
    if (!Expect(problem.PlaceItem({0U, {0.0, {0.0, 0.0}}}).HasValue()
                    && problem.PlaceItem({1U, {0.0, {0.0, 0.0}}}).HasValue(),
                output,
                "separator overlapping setup failed"))
    {
        return false;
    }
    const jaguar::SPSolution initial = problem.Save();
    jaguar::Layout initialLayout(initial.layoutSnapshot.container);
    if (!Expect(initialLayout.Restore(initial.layoutSnapshot).HasValue(),
                output,
                "separator tracker layout restore failed"))
    {
        return false;
    }
    const sparrow::SeparatorConfig defaultSeparatorConfig;
    if (!Expect(defaultSeparatorConfig.nWorkers == 3U
                    && defaultSeparatorConfig.nCoordinateDescents == 3U,
                output,
                "separator defaults must match Rust sampling configuration"))
    {
        return false;
    }
    auto weightTrackerResult = sparrow::CollisionTracker::Create(initialLayout);
    if (!Expect(weightTrackerResult.HasValue(),
                output,
                "separator weight tracker creation failed"))
    {
        return false;
    }
    sparrow::CollisionTracker weightTracker =
        std::move(weightTrackerResult.Value());
    const auto weightSnapshot = weightTracker.Save();
    weightTracker.UpdateWeights();
    if (!Expect(std::abs(weightTracker.PairWeight(0U, 1U) - 2.0F) < 1.0e-5F,
                output,
                "separator GLS weight must scale with maximum loss"))
    {
        return false;
    }
    weightTracker.UpdateWeights();
    const float currentWeight = weightTracker.PairWeight(0U, 1U);
    if (!Expect(weightTracker.RestoreButKeepWeights(
                    weightSnapshot, initialLayout)
                    .HasValue()
                    && std::abs(weightTracker.PairWeight(0U, 1U)
                                - currentWeight)
                           < 1.0e-5F,
                output,
                "separator rollback must preserve GLS weights"))
    {
        return false;
    }
    std::ifstream highWeightFixtureFile;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/CollisionTrackerWeightRust.json",
             "../SparrowCore/Tests/Fixtures/CollisionTrackerWeightRust.json",
             "../JaguarRun/SparrowCore/Tests/Fixtures/CollisionTrackerWeightRust.json",
             "..\\SparrowCore\\Tests\\Fixtures\\CollisionTrackerWeightRust.json"})
    {
        highWeightFixtureFile.open(path);
        if (highWeightFixtureFile.is_open())
        {
            break;
        }
        highWeightFixtureFile.clear();
    }
    if (!Expect(highWeightFixtureFile.is_open(),
                output,
                "separator high-weight Rust fixture must be readable"))
    {
        return false;
    }
    const std::string highWeightFixtureJson{
        std::istreambuf_iterator<char>(highWeightFixtureFile),
        std::istreambuf_iterator<char>()};
    const auto highWeightInstance =
        MakeSeparatorTraceImporter().ImportSPInstanceJson(highWeightFixtureJson);
    if (!Expect(highWeightInstance.HasValue(),
                output,
                "separator high-weight Rust fixture import failed"))
    {
        return false;
    }
    auto highWeightProblemResult =
        jaguar::SPProblem::Create(highWeightInstance.Value());
    if (!Expect(highWeightProblemResult.HasValue(),
                output,
                "separator high-weight problem creation failed"))
    {
        return false;
    }
    jaguar::SPProblem highWeightProblem =
        std::move(highWeightProblemResult.Value());
    if (!Expect(highWeightProblem.ChangeStripWidth(8.0).HasValue(),
                output,
                "separator high-weight strip resize failed")
        || !Expect(highWeightProblem.PlaceItem(
                       {0U, {0.0, {1.0, 1.0}}}).HasValue(),
                   output,
                   "separator high-weight first item placement failed")
        || !Expect(highWeightProblem.PlaceItem(
                       {1U, {0.0, {1.0, 1.0}}}).HasValue(),
                   output,
                   "separator high-weight second item placement failed")
        || !Expect(highWeightProblem.PlaceItem(
                       {2U, {0.0, {1.5, 1.0}}}).HasValue(),
                   output,
                   "separator high-weight partial item placement failed"))
    {
        return false;
    }
    const jaguar::SPSolution highWeightSolution = highWeightProblem.Save();
    jaguar::Layout highWeightLayout(highWeightSolution.layoutSnapshot.container);
    if (!Expect(highWeightLayout.Restore(
                    highWeightSolution.layoutSnapshot).HasValue(),
                output,
                "separator high-weight layout restore failed"))
    {
        return false;
    }
    auto highWeightTrackerResult =
        sparrow::CollisionTracker::Create(highWeightLayout);
    if (!Expect(highWeightTrackerResult.HasValue(),
                output,
                "separator high-weight tracker creation failed"))
    {
        return false;
    }
    sparrow::CollisionTracker highWeightTracker =
        std::move(highWeightTrackerResult.Value());
    for (std::size_t update = 0U; update < 20U; ++update)
    {
        highWeightTracker.UpdateWeights();
    }
    if (!Expect(highWeightTracker.PairWeight(0U, 1U) == 1'048'576.0F,
                output,
                "Rust GLS pair weight must grow beyond one million without a cap"))
    {
        return false;
    }
    const float partialWeight = highWeightTracker.PairWeight(0U, 2U);
    std::uint32_t partialWeightBits = 0U;
    std::memcpy(&partialWeightBits, &partialWeight, sizeof(partialWeightBits));
    const float partialLoss = highWeightTracker.PairLoss(0U, 2U);
    std::uint32_t partialLossBits = 0U;
    std::memcpy(&partialLossBits, &partialLoss, sizeof(partialLossBits));
    if (partialWeightBits != 0x4801'92D6U)
    {
        output << "SEPARATOR TEST FAILURE: partial-collision GLS weight bits 0x"
               << std::hex << partialWeightBits << std::dec
               << " (pair loss " << partialLoss << " bits 0x"
               << std::hex << partialLossBits << std::dec
               << ") do not match Rust 0x480192d6\n";
        return false;
    }
    sparrow::SeparatorConfig separatorConfig;
    separatorConfig.strikeLimit = 10U;
    separatorConfig.noImprovementLimit = 20U;
    separatorConfig.nContainerSamples = 256U;
    separatorConfig.nFocussedSamples = 64U;
    separatorConfig.nCoordinateDescents = 1U;
    separatorConfig.nWorkers = 2U;
    if (!Expect(separatorConfig.Validate().HasValue(),
                output,
                "separator worker configuration should be valid"))
    {
        return false;
    }
    sparrow::SeparatorConfig invalidSeparatorConfig = separatorConfig;
    invalidSeparatorConfig.nWorkers = 0U;
    if (!Expect(!invalidSeparatorConfig.Validate().HasValue(),
                output,
                "separator must reject zero workers"))
    {
        return false;
    }

    auto separatorResult = sparrow::Separator::Create(std::move(problem), 42U);
    if (!Expect(separatorResult.HasValue(), output, "separator creation failed"))
    {
        return false;
    }
    sparrow::Separator separator = std::move(separatorResult.Value());
    const float initialLoss = separator.Tracker().TotalLoss();
    sparrow::OptimizationTerminator terminator({
        20U,
        std::chrono::milliseconds{2000}});
    const auto result = separator.Run(separatorConfig, terminator);
    if (!Expect(result.HasValue(), output, "separator run failed"))
    {
        return false;
    }
    const std::array<const char*, 4> referencePaths = {
        "SparrowCore/Tests/Fixtures/SeparatorReference.tsv",
        "../SparrowCore/Tests/Fixtures/SeparatorReference.tsv",
        "../JaguarRun/SparrowCore/Tests/Fixtures/SeparatorReference.tsv",
        "..\\SparrowCore\\Tests\\Fixtures\\SeparatorReference.tsv",
    };
    std::ifstream referenceInput;
    for (const char* path : referencePaths)
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
                "separator reference fixture cannot be opened"))
    {
        return false;
    }
    std::array<bool, 2> referencePresent{{false, false}};
    std::array<double, 2> referenceRotation{};
    std::array<double, 2> referenceX{};
    std::array<double, 2> referenceY{};
    std::string line;
    while (std::getline(referenceInput, line))
    {
        if (line.empty() || line.front() == '#')
        {
            continue;
        }
        std::istringstream row(line);
        std::uint64_t seed = 0U;
        std::size_t workers = 0U;
        std::size_t item = 0U;
        if (!(row >> seed >> workers >> item)
            || seed != 42U || workers != 2U || item >= 2U
            || !(row >> referenceRotation[item]
                 >> referenceX[item]
                 >> referenceY[item]))
        {
            return Expect(false, output, "malformed separator reference row");
        }
        referencePresent[item] = true;
    }
    if (!Expect(referencePresent[0] && referencePresent[1],
                output,
                "separator reference fixture is incomplete"))
    {
        return false;
    }
    bool matchesAllReference = true;
    for (std::size_t index = 0U; index < 2U; ++index)
    {
        const auto& placement = result.Value().solution.layoutSnapshot
            .placements[index].placedItem;
        const auto& transform = placement.Transform();
        const bool matchesReference = std::abs(transform.rotationRad
                             - referenceRotation[index])
                            < 1.0e-6
                        && std::abs(transform.translation.x
                                    - referenceX[index])
                               < 1.0e-6
                        && std::abs(transform.translation.y
                                    - referenceY[index])
                               < 1.0e-6;
        if (!matchesReference)
        {
            output << std::setprecision(17) << "Separator actual item " << index
                   << ": rotation=" << transform.rotationRad
                   << " x=" << transform.translation.x
                   << " y=" << transform.translation.y
                   << "; expected rotation=" << referenceRotation[index]
                   << " x=" << referenceX[index]
                   << " y=" << referenceY[index] << '\n';
        }
        if (!matchesReference)
        {
            matchesAllReference = false;
        }
    }
    if (!Expect(matchesAllReference,
                output,
                "separator fixed-seed reference mismatch"))
    {
        return false;
    }
    return Expect(separator.Tracker().TotalLoss() <= initialLoss
                      && result.Value().solution.layoutSnapshot.placements.size()
                             == initial.layoutSnapshot.placements.size(),
                  output,
                  "separator must preserve placements and never worsen loss");
}
