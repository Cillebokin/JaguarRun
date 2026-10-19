#include "SparrowCore/Optimizer/OptimizationTerminator.h"
#include "SparrowCore/Optimizer/Separator.h"
#include "SparrowCore/Optimizer/SeparatorWorker.h"
#include "SparrowCore/Optimizer/SparrowConfig.h"
#include "SparrowCore/Optimizer/SparrowOptimizer.h"
#include "SparrowCore/Sampling/UniformRectSampler.h"
#include "SparrowCore/Quantify/CollisionTracker.h"
#include "../Source/Optimizer/SeparatorWorkerTrace.h"
#include "../Source/Optimizer/WorkerLossSelector.h"
#include "../Source/Optimizer/WorkerRoundCollector.h"

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
#include <initializer_list>
#include <limits>
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

bool TestWorkerLossSelectorNonFiniteOrdering(std::ostream& output)
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float positiveInfinity = std::numeric_limits<float>::infinity();
    const float negativeInfinity = -positiveInfinity;
    const auto select = [](std::initializer_list<float> losses) {
        sparrow::detail::WorkerLossSelector selector;
        std::size_t workerIndex = 0U;
        for (const float loss : losses)
        {
            selector.Consider(workerIndex, loss);
            ++workerIndex;
        }
        return selector.BestWorkerIndex();
    };

    return Expect(select({nan, 4.0F, 2.0F, nan}) == 2U,
                  output,
                  "finite worker must beat earlier NaN keys; lowest finite loss wins")
        && Expect(select({4.0F, nan}) == 0U,
                  output,
                  "later NaN worker must not replace an earlier finite worker")
        && Expect(select({nan, nan}) == 0U,
                  output,
                  "equal NaN worker keys must retain the first worker")
        && Expect(select({nan, positiveInfinity, nan}) == 1U,
                  output,
                  "positive infinity must beat NaN worker keys")
        && Expect(select({positiveInfinity, nan, nan}) == 0U,
                  output,
                  "a later NaN worker must not replace positive infinity")
        && Expect(select({nan, negativeInfinity, positiveInfinity}) == 1U,
                  output,
                  "negative infinity must beat NaN and positive infinity")
        && Expect(select({positiveInfinity, positiveInfinity, nan}) == 0U,
                  output,
                  "equal positive infinity keys must retain the first worker");
}

jaguar::Item MakeItem(jaguar::ItemIndex index);

bool TestMixedNonFiniteWorkerResultCollection(std::ostream& output)
{
    const auto instance = jaguar::SPInstance::Create(
        {MakeItem(0U)},
        {2U},
        4.0,
        jaguar::CDEConfig{},
        jaguar::ShapeModifyConfig{});
    if (!Expect(instance.HasValue(),
                output,
                "worker aggregate fixture instance creation failed"))
    {
        return false;
    }
    auto problemResult = jaguar::SPProblem::Create(instance.Value());
    if (!Expect(problemResult.HasValue(),
                output,
                "worker aggregate fixture problem creation failed"))
    {
        return false;
    }
    jaguar::SPProblem problem = std::move(problemResult.Value());
    if (!problem.PlaceItem({0U, {0.0, {1.0, 1.0}}}).HasValue()
        || !problem.PlaceItem({0U, {0.0, {1.0, 1.0}}}).HasValue())
    {
        return Expect(false, output, "worker aggregate fixture placement failed");
    }

    const jaguar::SPSolution solution = problem.Save();
    jaguar::Layout layout(solution.layoutSnapshot.container);
    if (!Expect(layout.Restore(solution.layoutSnapshot).HasValue(),
                output,
                "worker aggregate fixture layout restore failed"))
    {
        return false;
    }
    auto baseTrackerResult = sparrow::CollisionTracker::Create(layout);
    if (!Expect(baseTrackerResult.HasValue(),
                output,
                "worker aggregate fixture tracker creation failed"))
    {
        return false;
    }
    const sparrow::CollisionTracker baseTracker =
        std::move(baseTrackerResult.Value());
    if (!Expect(baseTracker.ItemCount() == 2U,
                output,
                "worker aggregate fixture must contain two tracker rows"))
    {
        return false;
    }

    const float positiveInfinity = std::numeric_limits<float>::infinity();
    const std::array<std::array<float, 2U>, 4U> componentLosses = {{
        {{positiveInfinity, -positiveInfinity}},
        {{positiveInfinity, 0.0F}},
        {{-positiveInfinity, 0.0F}},
        {{2.0F, 0.0F}},
    }};
    struct WorkerRoundFixture
    {
        jaguar::SPSolution solution;
        sparrow::CollisionTrackerSnapshot trackerSnapshot;
        float weightedLoss = 0.0F;
        std::size_t surrogateRejectCount = 0U;
        std::size_t exactCollisionCount = 0U;
        std::size_t evaluationCount = 0U;
        std::size_t moveCount = 0U;
    };
    sparrow::detail::WorkerRoundCollector<WorkerRoundFixture> collector;
    std::array<float, 4U> aggregateKeys{};
    for (std::size_t workerIndex = 0U;
         workerIndex < componentLosses.size();
         ++workerIndex)
    {
        const auto& components = componentLosses[workerIndex];
        sparrow::CollisionTrackerSnapshot workerSnapshot = baseTracker.Save();
        for (sparrow::CollisionEntry& entry : workerSnapshot.containerEntries)
        {
            entry.loss = 0.0F;
            entry.weight = 1.0F;
        }
        for (std::size_t first = 0U;
             first < workerSnapshot.pairMatrix.Size();
             ++first)
        {
            for (std::size_t second = first;
                 second < workerSnapshot.pairMatrix.Size();
                 ++second)
            {
                sparrow::CollisionEntry& entry =
                    workerSnapshot.pairMatrix.At(first, second);
                entry.loss = 0.0F;
                entry.weight = 1.0F;
            }
        }
        workerSnapshot.pairMatrix.At(0U, 0U).loss = components[0U];
        workerSnapshot.pairMatrix.At(0U, 1U).loss = components[1U];
        workerSnapshot.isSeparated = false;

        sparrow::CollisionTracker workerTracker = baseTracker;
        if (!workerTracker.Restore(workerSnapshot, layout).HasValue())
        {
            return Expect(false,
                          output,
                          "worker aggregate tracker fixture restore failed");
        }
        aggregateKeys[workerIndex] = workerTracker.TotalWeightedLoss();
        const std::size_t ordinal = workerIndex + 1U;
        collector.Add(
            workerIndex,
            WorkerRoundFixture{
                solution,
                workerTracker.Save(),
                aggregateKeys[workerIndex],
                ordinal * 10U,
                ordinal,
                ordinal * 11U,
                ordinal});
    }

    if (!Expect(std::isnan(aggregateKeys[0U])
                    && aggregateKeys[1U] == positiveInfinity
                    && aggregateKeys[2U] == -positiveInfinity
                    && aggregateKeys[3U] == 2.0F,
                output,
                "tracker aggregation must produce NaN, +Inf, -Inf, and finite worker keys"))
    {
        return false;
    }
    if (!Expect(collector.SurrogateRejectCount() == 100U
                    && collector.ExactCollisionCount() == 10U
                    && collector.EvaluationCount() == 110U
                    && collector.MoveCount() == 10U,
                output,
                "worker result collection must aggregate counters from every successful worker"))
    {
        return false;
    }
    if (!Expect(collector.HasBest() && collector.BestWorkerIndex() == 2U,
                output,
                "worker result collection must select the -Inf aggregate"))
    {
        return false;
    }
    std::optional<WorkerRoundFixture> bestWorker = collector.TakeBest();
    if (!Expect(bestWorker.has_value()
                    && bestWorker->weightedLoss == -positiveInfinity
                    && bestWorker->moveCount == 3U,
                output,
                "collector must retain the selected worker result payload"))
    {
        return false;
    }

    if (!problem.Restore(bestWorker->solution).HasValue())
    {
        return Expect(false, output, "selected worker solution restore failed");
    }
    const jaguar::SPSolution selectedSolution = problem.Save();
    jaguar::Layout selectedLayout(selectedSolution.layoutSnapshot.container);
    if (!selectedLayout.Restore(selectedSolution.layoutSnapshot).HasValue())
    {
        return Expect(false, output, "selected worker layout rebuild failed");
    }
    sparrow::CollisionTracker selectedTracker = baseTracker;
    if (!selectedTracker.Restore(bestWorker->trackerSnapshot, selectedLayout).HasValue())
    {
        return Expect(false, output, "selected worker tracker restore failed");
    }
    return Expect(selectedTracker.TotalLoss() == -positiveInfinity
                      && selectedTracker.TotalWeightedLoss() == -positiveInfinity
                      && selectedTracker.PairLoss(0U, 0U) == -positiveInfinity,
                  output,
                  "post-selection tracker must contain the selected -Inf worker state");
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
    bool sawRngAfter = false;
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
        if (record == "rng_after")
        {
            std::string unexpected;
            if (!sawResult || sawRngAfter
                || !(row >> std::hex >> expectedNextU64AfterSearch)
                || (row >> unexpected))
            {
                return Expect(false,
                              output,
                              "malformed Rust separator post-search RNG row");
            }
            sawRngAfter = true;
            continue;
        }
        if (sawRngAfter)
        {
            return Expect(false,
                          output,
                          "Rust separator post-search RNG row must be final");
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
    return Expect(sawResult && sawRngAfter && !events.empty(),
                  output,
                  "Rust separator trace must contain a result and RNG-after event");
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

bool TestInitiallySeparatedRunCompletesEmptyWorkerRound(
    std::ostream& output)
{
    const auto instance = jaguar::SPInstance::Create(
        {MakeItem(0U)},
        {1U},
        4.0,
        jaguar::CDEConfig{},
        jaguar::ShapeModifyConfig{});
    if (!Expect(instance.HasValue(),
                output,
                "empty-round separator instance creation failed"))
    {
        return false;
    }
    auto problemResult = jaguar::SPProblem::Create(instance.Value());
    if (!Expect(problemResult.HasValue(),
                output,
                "empty-round separator problem creation failed"))
    {
        return false;
    }
    auto separatorResult = sparrow::Separator::Create(
        std::move(problemResult.Value()), 42U);
    if (!Expect(separatorResult.HasValue(),
                output,
                "empty-round separator creation failed"))
    {
        return false;
    }
    sparrow::Separator separator = std::move(separatorResult.Value());
    sparrow::SeparatorConfig config;
    config.strikeLimit = 1U;
    config.noImprovementLimit = 1U;
    config.nContainerSamples = 0U;
    config.nFocussedSamples = 0U;
    config.nCoordinateDescents = 1U;
    config.nWorkers = 1U;
    SeparatorRoundRecordingListener listener;
    sparrow::OptimizationTerminator terminator({});
    const auto result = separator.Run(config, terminator, &listener);
    if (!Expect(result.HasValue(), output, "empty-round separator run failed"))
    {
        return false;
    }

    const std::uint64_t nextRootRandom = separator.Random().NextU64();
    if (!Expect(listener.reports.empty()
                    && listener.progress.size() == 2U
                    && listener.progress[0].iteration == 0U
                    && listener.progress[1].iteration == 1U
                    && listener.progress[0].minimumLoss == 0.0F
                    && listener.progress[1].minimumLoss == 0.0F,
                output,
                "an initially separated Rust layout must report one completed empty worker round"))
    {
        output << "progress-count=" << listener.progress.size();
        for (const auto& event : listener.progress)
            output << " iteration=" << event.iteration
                   << " loss=" << event.minimumLoss;
        output << '\n';
        return false;
    }
    const auto& summary = listener.result;
    const auto& separatorResultValue = result.Value();
    return Expect(summary.has_value()
                      && summary->success
                      && summary->totalEvaluations == 0U
                      && summary->totalMoves == 0U
                      && summary->iterations == 0U
                      && separatorResultValue.totalEvaluationCount == 0U
                      && separatorResultValue.totalMoveCount == 0U
                      && separatorResultValue.iterations == 0U
                      && separator.Problem().PlacedItemCount() == 0U
                      && separator.Tracker().IsSeparated()
                      && nextRootRandom == 0x519e4174576f3791ULL,
                  output,
                  "empty worker round must preserve the Rust layout, counters, tracker, and root RNG state");
}

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

bool ReadSeparatorControlBoundaryTrace(
    std::vector<std::pair<std::size_t, float>>& progress,
    std::size_t& reportCount,
    bool& success,
    std::size_t& evaluationCount,
    std::size_t& moveCount,
    std::size_t& iterations,
    std::size_t& placementCount,
    float& totalLoss,
    float& totalWeightedLoss,
    std::ostream& output)
{
    std::ifstream traceFile;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/SeparatorThresholdIterationRustTrace.tsv",
             "../SparrowCore/Tests/Fixtures/SeparatorThresholdIterationRustTrace.tsv",
             "../JaguarRun/SparrowCore/Tests/Fixtures/SeparatorThresholdIterationRustTrace.tsv",
             "..\\SparrowCore\\Tests\\Fixtures\\SeparatorThresholdIterationRustTrace.tsv"})
    {
        traceFile.open(path);
        if (traceFile.is_open())
            break;
        traceFile.clear();
    }
    if (!Expect(traceFile.is_open(), output,
                "Rust separator control-boundary fixture must be readable"))
    {
        return false;
    }

    bool hasReports = false;
    bool hasResult = false;
    std::string line;
    while (std::getline(traceFile, line))
    {
        if (line.empty() || line.front() == '#')
            continue;
        std::istringstream row(line);
        std::string record;
        if (!(row >> record))
            continue;
        if (record == "progress")
        {
            std::size_t ordinal = 0U;
            std::size_t iteration = 0U;
            std::uint32_t lossBits = 0U;
            if (!(row >> ordinal >> iteration >> std::hex >> lossBits)
                || ordinal != progress.size())
            {
                return Expect(false, output,
                              "malformed Rust control-boundary progress row");
            }
            progress.emplace_back(iteration, FloatFromBits(lossBits));
        }
        else if (record == "reports")
        {
            std::string unexpected;
            if (hasReports || !(row >> reportCount) || (row >> unexpected))
            {
                return Expect(false, output,
                              "malformed Rust control-boundary report row");
            }
            hasReports = true;
        }
        else if (record == "result")
        {
            unsigned successValue = 0U;
            std::uint32_t lossBits = 0U;
            std::uint32_t weightedLossBits = 0U;
            std::string unexpected;
            if (hasResult
                || !(row >> successValue >> evaluationCount >> moveCount
                         >> iterations >> placementCount >> std::hex
                         >> lossBits >> weightedLossBits)
                || successValue > 1U || (row >> unexpected))
            {
                return Expect(false, output,
                              "malformed Rust control-boundary result row");
            }
            success = successValue != 0U;
            totalLoss = FloatFromBits(lossBits);
            totalWeightedLoss = FloatFromBits(weightedLossBits);
            hasResult = true;
        }
        else
        {
            return Expect(false, output,
                          "unknown Rust control-boundary fixture record");
        }
    }
    return Expect(!progress.empty() && hasReports && hasResult,
                  output,
                  "Rust separator control-boundary fixture is incomplete");
}

bool TestSeparatorThresholdAndIterationBoundaries(std::ostream& output)
{
    std::vector<std::pair<std::size_t, float>> expectedProgress;
    std::size_t expectedReports = 0U;
    bool expectedSuccess = false;
    std::size_t expectedEvaluations = 0U;
    std::size_t expectedMoves = 0U;
    std::size_t expectedIterations = 0U;
    std::size_t expectedPlacements = 0U;
    float expectedLoss = 0.0F;
    float expectedWeightedLoss = 0.0F;
    if (!ReadSeparatorControlBoundaryTrace(
            expectedProgress,
            expectedReports,
            expectedSuccess,
            expectedEvaluations,
            expectedMoves,
            expectedIterations,
            expectedPlacements,
            expectedLoss,
            expectedWeightedLoss,
            output))
    {
        return false;
    }

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
                "separator control-boundary input must be readable"))
    {
        return false;
    }
    const std::string json{
        std::istreambuf_iterator<char>(inputFile),
        std::istreambuf_iterator<char>()};
    const auto instance = MakeSeparatorTraceImporter().ImportSPInstanceJson(json);
    if (!Expect(instance.HasValue(), output,
                "separator control-boundary input must import"))
    {
        return false;
    }
    auto problemResult = jaguar::SPProblem::Create(instance.Value());
    if (!Expect(problemResult.HasValue(), output,
                "separator control-boundary problem creation failed"))
    {
        return false;
    }
    jaguar::SPProblem problem = std::move(problemResult.Value());
    if (!Expect(problem.ChangeStripWidth(9.0).HasValue(), output,
                "separator control-boundary strip resize failed"))
    {
        return false;
    }
    const auto initial = jaguar::Transform2d::FromRotationTranslation(
        0.0, {1.0, 1.0});
    for (std::size_t index = 0U; index < 9U; ++index)
    {
        if (!Expect(problem.PlaceItem({0U, initial}).HasValue(), output,
                    "separator control-boundary placement failed"))
        {
            return false;
        }
    }

    auto separatorResult = sparrow::Separator::Create(std::move(problem), 0U);
    if (!Expect(separatorResult.HasValue(), output,
                "separator control-boundary creation failed"))
    {
        return false;
    }
    sparrow::Separator separator = std::move(separatorResult.Value());
    sparrow::SeparatorConfig config;
    config.strikeLimit = 2U;
    config.noImprovementLimit = 2U;
    config.nContainerSamples = 1U;
    config.nFocussedSamples = 0U;
    config.nCoordinateDescents = 1U;
    config.nWorkers = 1U;
    sparrow::OptimizationTerminator terminator;
    SeparatorRoundRecordingListener listener;
    const auto result = separator.Run(config, terminator, &listener);
    if (!Expect(result.HasValue(), output,
                "separator control-boundary run failed"))
    {
        return false;
    }
    if (!Expect(listener.progress.size() == expectedProgress.size()
                    && listener.reports.size() == expectedReports
                    && listener.result.has_value(),
                output,
                "Rust/C++ threshold-boundary event counts differ"))
    {
        output << "Rust(progress=" << expectedProgress.size()
               << ", reports=" << expectedReports << ") C++(progress="
               << listener.progress.size() << ", reports="
               << listener.reports.size() << ")\n";
        return false;
    }
    for (std::size_t index = 0U; index < expectedProgress.size(); ++index)
    {
        if (!Expect(listener.progress[index].iteration
                            == expectedProgress[index].first
                        && std::abs(listener.progress[index].minimumLoss
                                    - expectedProgress[index].second)
                               <= 2.0e-3F,
                    output,
                    "Rust/C++ threshold-boundary progress differs"))
        {
            output << std::setprecision(10) << "progress=" << index
                   << " Rust=(" << expectedProgress[index].first << ','
                   << expectedProgress[index].second << ") C++=("
                   << listener.progress[index].iteration << ','
                   << listener.progress[index].minimumLoss << ")\n";
            return false;
        }
    }
    if (!Expect(expectedProgress.size() == 17U
                    && expectedProgress[5].second
                           < expectedProgress[4].second * 0.98F
                    && expectedProgress[11].second
                           < expectedProgress[10].second
                    && expectedProgress[11].second
                           >= expectedProgress[10].second * 0.98F,
                output,
                "Rust fixture must exercise both sides of the 0.98 threshold"))
    {
        return false;
    }
    const auto& summary = listener.result.value();
    const auto& value = result.Value();
    const auto& tracker = separator.Tracker();
    if (!Expect(value.iterations == expectedIterations
                    && value.totalEvaluationCount == expectedEvaluations
                    && value.totalMoveCount == expectedMoves
                    && listener.result->iterations == expectedIterations
                    && listener.result->totalEvaluations == expectedEvaluations
                    && listener.result->totalMoves == expectedMoves
                    && listener.result->success == expectedSuccess
                    && value.solution.layoutSnapshot.placements.size()
                           == expectedPlacements
                    && std::abs(tracker.TotalLoss() - expectedLoss) <= 2.0e-2F
                    && std::abs(tracker.TotalWeightedLoss()
                                - expectedWeightedLoss) <= 5.0e-2F,
                output,
                "Rust/C++ threshold and iteration-limit result differs"))
    {
        output << "Rust(iter=" << expectedIterations << ", evals="
               << expectedEvaluations << ", moves=" << expectedMoves
               << ", loss=" << expectedLoss << ", weighted="
               << expectedWeightedLoss << ") C++(iter=" << value.iterations
               << ", evals=" << value.totalEvaluationCount << ", moves="
               << value.totalMoveCount << ", loss=" << tracker.TotalLoss()
               << ", weighted=" << tracker.TotalWeightedLoss() << ")\n";
        return false;
    }
    return Expect(listener.progress.back().iteration == 16U
                      && summary.iterations == 16U,
                  output,
                  "inner iteration limit must stop after the second consecutive no-improvement round");
}

bool TestSeparatorZeroLimitsAndFourWorkerInvariant(std::ostream& output)
{
    const auto run = [&](std::size_t strikeLimit,
                         std::size_t noImprovementLimit,
                         std::size_t workerCount,
                         bool expectWork) {
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
                    "separator boundary input must be readable"))
            return false;
        const std::string json{
            std::istreambuf_iterator<char>(inputFile),
            std::istreambuf_iterator<char>()};
        const auto instance =
            MakeSeparatorTraceImporter().ImportSPInstanceJson(json);
        if (!Expect(instance.HasValue(), output,
                    "separator boundary input import failed"))
            return false;
        auto problemResult = jaguar::SPProblem::Create(instance.Value());
        if (!Expect(problemResult.HasValue(), output,
                    "separator boundary problem creation failed"))
            return false;
        jaguar::SPProblem problem = std::move(problemResult.Value());
        if (!Expect(problem.ChangeStripWidth(9.0).HasValue(), output,
                    "separator boundary strip resize failed"))
            return false;
        const auto initial = jaguar::Transform2d::FromRotationTranslation(
            0.0, {1.0, 1.0});
        const std::size_t placementCount = expectWork ? 0U : 9U;
        for (std::size_t index = 0U; index < placementCount; ++index)
        {
            if (!Expect(problem.PlaceItem({0U, initial}).HasValue(), output,
                        "separator boundary placement failed"))
                return false;
        }
        auto created = sparrow::Separator::Create(std::move(problem), 42U);
        if (!Expect(created.HasValue(), output,
                    "separator boundary creation failed"))
            return false;
        sparrow::Separator separator = std::move(created.Value());
        sparrow::SeparatorConfig config;
        config.strikeLimit = strikeLimit;
        config.noImprovementLimit = noImprovementLimit;
        config.nContainerSamples = expectWork ? 0U : 1U;
        config.nFocussedSamples = 0U;
        config.nCoordinateDescents = 1U;
        config.nWorkers = workerCount;
        sparrow::OptimizationTerminator terminator;
        SeparatorRoundRecordingListener listener;
        const auto result = separator.Run(config, terminator, &listener);
        if (!Expect(result.HasValue(), output,
                    "separator zero-limit or multiworker run failed"))
            return false;
        const auto& value = result.Value();
        if (!expectWork)
        {
            return Expect(listener.progress.size() == 1U
                              && listener.progress.front().iteration == 0U
                              && listener.reports.empty()
                              && value.iterations == 0U
                              && value.totalEvaluationCount == 0U
                              && value.totalMoveCount == 0U
                              && value.solution.layoutSnapshot.placements.size()
                                     == 9U,
                          output,
                          "zero strike/inner-iteration limit must not start a worker round");
        }
        jaguar::Layout finalLayout(value.solution.layoutSnapshot.container);
        if (!Expect(finalLayout.Restore(value.solution.layoutSnapshot).HasValue(),
                    output,
                    "four-worker final layout restore failed"))
            return false;
        return Expect(listener.progress.size() == 2U
                          && listener.progress[0].iteration == 0U
                          && listener.progress[1].iteration == 1U
                          && listener.reports.empty()
                          && listener.result.has_value()
                          && value.iterations == 0U
                          && value.totalEvaluationCount == 0U
                          && value.totalMoveCount == 0U
                          && value.solution.layoutSnapshot.placements.empty()
                          && separator.Tracker().IsSeparated()
                          && finalLayout.IsFeasible()
                          && listener.result->success,
                      output,
                      "natural four-worker empty round must preserve valid state and stop semantics");
    };

    return run(0U, 2U, 1U, false)
        && run(2U, 0U, 1U, false)
        && run(1U, 1U, 4U, true);
}

bool TestActiveFourWorkerInvariant(std::ostream& output)
{
    std::ifstream inputFile;
    for (const char* path :
         {"SparrowCore/Tests/Fixtures/SeparatorRoundRustTrace.json",
          "../SparrowCore/Tests/Fixtures/SeparatorRoundRustTrace.json",
          "../JaguarRun/SparrowCore/Tests/Fixtures/"
          "SeparatorRoundRustTrace.json",
          "..\\SparrowCore\\Tests\\Fixtures\\SeparatorRoundRustTrace.json"})
    {
        inputFile.open(path);
        if (inputFile.is_open())
        {
            break;
        }
        inputFile.clear();
    }
    if (!Expect(inputFile.is_open(), output,
                "active four-worker separator input must be readable"))
    {
        return false;
    }
    const std::string json{std::istreambuf_iterator<char>(inputFile),
                           std::istreambuf_iterator<char>()};
    const auto instance =
        MakeSeparatorTraceImporter().ImportSPInstanceJson(json);
    if (!Expect(instance.HasValue(), output,
                "active four-worker separator import failed"))
    {
        return false;
    }
    auto problemResult = jaguar::SPProblem::Create(instance.Value());
    if (!Expect(problemResult.HasValue(), output,
                "active four-worker separator problem creation failed"))
    {
        return false;
    }
    jaguar::SPProblem problem = std::move(problemResult.Value());
    if (!Expect(problem.ChangeStripWidth(9.0).HasValue(), output,
                "active four-worker strip resize failed"))
    {
        return false;
    }
    const auto initial =
        jaguar::Transform2d::FromRotationTranslation(0.0, {1.0, 1.0});
    for (std::size_t index = 0U; index < 2U; ++index)
    {
        if (!Expect(problem.PlaceItem({0U, initial}).HasValue(), output,
                    "active four-worker overlapping placement failed"))
        {
            return false;
        }
    }

    auto created = sparrow::Separator::Create(std::move(problem), 42U);
    if (!Expect(created.HasValue(), output,
                "active four-worker separator creation failed"))
    {
        return false;
    }
    sparrow::Separator separator = std::move(created.Value());
    if (!Expect(separator.Tracker().TotalLoss() > 0.0F, output,
                "active four-worker input must begin with a collision"))
    {
        return false;
    }

    sparrow::SeparatorConfig config;
    config.strikeLimit = 1U;
    config.noImprovementLimit = 1U;
    config.nContainerSamples = 1U;
    config.nFocussedSamples = 0U;
    config.nCoordinateDescents = 1U;
    config.nWorkers = 4U;
    sparrow::OptimizationTerminator terminator;
    SeparatorRoundRecordingListener listener;
    const auto result = separator.Run(config, terminator, &listener);
    if (!Expect(result.HasValue(), output,
                "active four-worker separator run failed"))
    {
        return false;
    }

    const auto& value = result.Value();
    jaguar::Layout finalLayout(value.solution.layoutSnapshot.container);
    if (!Expect(finalLayout.Restore(value.solution.layoutSnapshot).HasValue(),
                output, "active four-worker final layout restore failed"))
    {
        return false;
    }
    const float finalLoss = separator.Tracker().TotalLoss();
    return Expect(
        value.solution.layoutSnapshot.placements.size() == 2U &&
            listener.progress.size() >= 2U &&
            listener.progress.front().iteration == 0U &&
            std::all_of(listener.progress.begin(), listener.progress.end(),
                        [](const sparrow::SeparationProgress& progress)
                        {
                            return std::isfinite(progress.minimumLoss) &&
                                   progress.minimumLoss >= 0.0F;
                        }) &&
            listener.result.has_value() && value.totalEvaluationCount > 0U &&
            value.totalMoveCount > 0U && std::isfinite(finalLoss) &&
            finalLoss >= 0.0F &&
            listener.result->success == separator.Tracker().IsSeparated() &&
            listener.result->success == finalLayout.IsFeasible() &&
            listener.result->iterations == value.iterations &&
            listener.result->totalEvaluations == value.totalEvaluationCount &&
            listener.result->totalMoves == value.totalMoveCount,
        output,
        "active four-worker run must do worker work and return consistent "
        "state, tracker, and result invariants");
}

bool SameSeparatorSample(
    const jaguar::Transform2d& actualTransform,
    const sparrow::SampleEval& actualEvaluation,
    const jaguar::Transform2d& expectedTransform,
    const sparrow::SampleEval& expectedEvaluation) noexcept
{
    return actualEvaluation.kind == expectedEvaluation.kind
        && std::abs(actualTransform.rotationRad
                    - expectedTransform.rotationRad)
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

bool TestRustSeparatorSearchTrace(
    std::ostream& output,
    const char* inputFixtureName,
    const char* traceFixtureName,
    double stripWidth,
    std::size_t focussedBudget,
    std::size_t containerBudget,
    std::size_t coordinateDescentCapacity,
    jaguar::Point2d initialTranslation,
    bool requireExactTie)
{
    std::vector<sparrow::SeparatorSearchTraceEvent> expected;
    std::uint64_t expectedNextU64AfterSearch = 0U;
    if (!ReadRustSeparatorTrace(expected,
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
    if (!Expect(problem.ChangeStripWidth(stripWidth).HasValue(),
                output,
                "separator trace strip resize failed"))
    {
        return false;
    }
    const jaguar::Transform2d initialTransform =
        jaguar::Transform2d::FromRotationTranslation(
            0.0, initialTranslation);
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
    config.nContainerSamples = containerBudget;
    config.nFocussedSamples = focussedBudget;
    config.nCoordinateDescents = coordinateDescentCapacity;
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
    if (!Expect(rng.NextU64() == expectedNextU64AfterSearch,
                output,
                "Rust/C++ separator search post-call RNG state differs"))
    {
        output << std::hex << "expected next_u64=0x"
               << expectedNextU64AfterSearch << '\n';
        return false;
    }
    if (requireExactTie)
    {
        const auto hasExactRepeatedTie = [](const auto& trace) {
            for (std::size_t later = 0U; later < trace.size(); ++later)
            {
                const auto& rejected = trace[later];
                if (rejected.type
                        != sparrow::SeparatorSearchTraceEvent::Type::Report
                    || rejected.accepted
                    || rejected.evaluation.kind
                           == sparrow::SampleKind::SAMPLE_KIND_INVALID)
                {
                    continue;
                }
                for (std::size_t earlier = 0U; earlier < later; ++earlier)
                {
                    const auto& accepted = trace[earlier];
                    if (accepted.type
                            == sparrow::SeparatorSearchTraceEvent::Type::Report
                        && accepted.accepted
                        && accepted.evaluation.kind == rejected.evaluation.kind
                        && accepted.evaluation.loss == rejected.evaluation.loss
                        && accepted.transform.rotationRad
                               == rejected.transform.rotationRad
                        && accepted.transform.translation.x
                               == rejected.transform.translation.x
                        && accepted.transform.translation.y
                               == rejected.transform.translation.y)
                    {
                        return true;
                    }
                }
            }
            return false;
        };
        if (!Expect(hasExactRepeatedTie(expected)
                        && hasExactRepeatedTie(actual),
                    output,
                    "exact separator loss tie must reject the repeated candidate in Rust and C++"))
        {
            return false;
        }
        bool crossLanguageTieIsExact = false;
        for (std::size_t acceptedIndex = 0U;
             acceptedIndex < expected.size() && !crossLanguageTieIsExact;
             ++acceptedIndex)
        {
            const auto& accepted = expected[acceptedIndex];
            if (accepted.type
                    != sparrow::SeparatorSearchTraceEvent::Type::Report
                || !accepted.accepted)
            {
                continue;
            }
            for (std::size_t rejectedIndex = acceptedIndex + 1U;
                 rejectedIndex < expected.size();
                 ++rejectedIndex)
            {
                const auto& rejected = expected[rejectedIndex];
                if (rejected.type
                        != sparrow::SeparatorSearchTraceEvent::Type::Report
                    || rejected.accepted
                    || accepted.evaluation.kind != rejected.evaluation.kind
                    || accepted.evaluation.loss != rejected.evaluation.loss
                    || accepted.transform.rotationRad
                           != rejected.transform.rotationRad
                    || accepted.transform.translation.x
                           != rejected.transform.translation.x
                    || accepted.transform.translation.y
                           != rejected.transform.translation.y
                    || rejectedIndex >= actual.size())
                {
                    continue;
                }
                crossLanguageTieIsExact =
                    actual[acceptedIndex].evaluation.loss
                        == accepted.evaluation.loss
                    && actual[rejectedIndex].evaluation.loss
                        == rejected.evaluation.loss;
                if (crossLanguageTieIsExact)
                {
                    break;
                }
            }
        }
        if (!Expect(crossLanguageTieIsExact,
                    output,
                    "Rust/C++ repeated separator tie loss must match exactly"))
        {
            return false;
        }
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

bool TestSeparatorSamplerErrorPreservesState(std::ostream& output)
{
    const auto polygon = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}, {0.0, 1.0}});
    if (!Expect(polygon.HasValue(), output,
                "separator sampler-error polygon creation failed"))
    {
        return false;
    }
    jaguar::SurrogateConfig surrogateConfig{};
    surrogateConfig.poleLimits = {{{3U, 0.0}, {0U, 0.0}, {0U, 0.0}}};
    jaguar::OriginalShape original{
        polygon.Value(),
        jaguar::Transform2d::Identity(),
        jaguar::ShapeModifyMode::Inflate,
        {}};
    const auto invalidRotationItem = jaguar::Item::Create(
        0U,
        9800U,
        std::move(original),
        surrogateConfig,
        std::nullopt,
        {jaguar::RotationMode::Discrete,
         {std::numeric_limits<double>::quiet_NaN()}});
    if (!Expect(invalidRotationItem.HasValue(),
                output,
                "separator sampler-error item creation failed"))
    {
        return false;
    }
    const auto instance = jaguar::SPInstance::Create(
        {invalidRotationItem.Value()},
        {1U},
        4.0,
        jaguar::CDEConfig{},
        jaguar::ShapeModifyConfig{});
    if (!Expect(instance.HasValue(),
                output,
                "separator sampler-error instance creation failed"))
    {
        return false;
    }
    auto problemResult = jaguar::SPProblem::Create(instance.Value());
    if (!Expect(problemResult.HasValue(),
                output,
                "separator sampler-error problem creation failed"))
    {
        return false;
    }
    jaguar::SPProblem problem = std::move(problemResult.Value());
    if (!Expect(problem.ChangeStripWidth(8.0).HasValue(),
                output,
                "separator sampler-error strip resize failed"))
    {
        return false;
    }
    const auto moving = problem.PlaceItem({
        0U,
        jaguar::Transform2d::FromRotationTranslation(0.0, {1.0, 1.0})});
    if (!Expect(moving.HasValue(),
                output,
                "separator sampler-error reference placement failed"))
    {
        return false;
    }

    const jaguar::SPSolution initial = problem.Save();
    jaguar::Layout layout(initial.layoutSnapshot.container);
    if (!Expect(layout.Restore(initial.layoutSnapshot).HasValue(),
                output,
                "separator sampler-error layout restore failed"))
    {
        return false;
    }
    auto trackerResult = sparrow::CollisionTracker::Create(layout);
    if (!Expect(trackerResult.HasValue(),
                output,
                "separator sampler-error tracker creation failed"))
    {
        return false;
    }
    const auto before = layout.Save();
    const float beforeLoss = trackerResult.Value().TotalLoss();
    sparrow::SeparatorConfig config;
    config.nFocussedSamples = 1U;
    config.nContainerSamples = 1U;
    config.nCoordinateDescents = 1U;
    sparrow::SparrowRng rng(42U);
    std::vector<sparrow::SeparatorSearchTraceEvent> events;
    const auto result = sparrow::FindBestSeparatorMoveWithTrace(
        layout,
        trackerResult.Value(),
        moving.Value(),
        config,
        rng,
        [&events](const auto& event) { events.push_back(event); });
    const std::uint64_t nextU64AfterError = rng.NextU64();
    const jaguar::LayoutSnapshot after = layout.Save();
    const auto& beforeTransform = before.placements.front().placedItem.Transform();
    const auto& afterTransform = after.placements.front().placedItem.Transform();
    const bool unchanged = layout.PlacedItemCount() == 1U
        && beforeTransform.rotationRad == afterTransform.rotationRad
        && beforeTransform.translation.x == afterTransform.translation.x
        && beforeTransform.translation.y == afterTransform.translation.y
        && trackerResult.Value().TotalLoss() == beforeLoss;
    const bool traceMatches = events.size() == 2U
        && events[0].type == sparrow::SeparatorSearchTraceEvent::Type::Evaluation
        && events[0].phase
               == sparrow::SeparatorSearchTracePhase::ReferencePlacement
        && events[1].type == sparrow::SeparatorSearchTraceEvent::Type::Report
        && events[1].phase
               == sparrow::SeparatorSearchTracePhase::ReferencePlacement;
    sparrow::SparrowRng untouchedRng(42U);
    return Expect(!result.HasValue()
                      && result.GetError().code == jaguar::ErrorCode::InvalidInput
                      && unchanged && traceMatches
                      && nextU64AfterError == untouchedRng.NextU64(),
                  output,
                  "separator sampler error must propagate after reference events without mutating layout or RNG");
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

bool TestMultiworkerHazardFailureRetainsWorkerRngForRetry(
    std::ostream& output)
{
    const jaguar::ImporterConfig importerConfig =
        sparrow::MakeRustDefaultImporterConfig();
    const jaguar::Importer importer(importerConfig);
    const jaguar::ExternalItem externalItem{
        9802U,
        jaguar::RotationPolicy::Discrete({0.0}),
        jaguar::ExternalShape::Polygon({
            {0.0, 0.0}, {2.0, 0.0}, {2.0, 2.0}, {0.0, 2.0}}),
        std::nullopt};
    const auto item = importer.ImportItem(externalItem, 0U);
    if (!Expect(item.HasValue(), output,
                "multiworker failure item import failed"))
    {
        return false;
    }
    const jaguar::ExternalShape outerShape = jaguar::ExternalShape::Polygon({
        {0.0, 0.0}, {20.0, 0.0}, {20.0, 20.0}, {0.0, 20.0}});
    const auto plainContainer = importer.ImportContainer(
        {9800U, outerShape, {}});
    if (!Expect(plainContainer.HasValue(), output,
                "multiworker sampling container import failed"))
    {
        return false;
    }
    const auto samplerResult = sparrow::UniformRectSampler::Create(
        plainContainer.Value().CollisionShape().BoundingBox(),
        item.Value(),
        plainContainer.Value().CollisionShape().BoundingBox());
    if (!Expect(samplerResult.HasValue()
                    && samplerResult.Value().has_value(),
                output,
                "multiworker failure sampler must fit"))
    {
        return false;
    }

    sparrow::SparrowRng workerSeedSource(1U);
    std::vector<jaguar::Rect2d> firstCandidateBounds;
    firstCandidateBounds.reserve(2U);
    for (std::size_t workerIndex = 0U; workerIndex < 2U; ++workerIndex)
    {
        sparrow::SparrowRng workerRng(workerSeedSource.NextU64());
        const jaguar::Transform2d candidate =
            samplerResult.Value()->Sample(workerRng);
        const auto shape = item.Value().CollisionShape().TransformedRustF32(
            candidate);
        if (!Expect(shape.HasValue(), output,
                    "multiworker candidate transform failed"))
        {
            return false;
        }
        firstCandidateBounds.push_back(shape.Value().BoundingBox());
    }
    std::vector<jaguar::ExternalQualityZone> zones;
    zones.reserve(firstCandidateBounds.size());
    for (std::size_t index = 0U; index < firstCandidateBounds.size(); ++index)
    {
        const jaguar::Rect2d& bounds = firstCandidateBounds[index];
        zones.push_back({
            index + 1U,
            jaguar::ExternalShape::Rectangle(
                bounds.xMin, bounds.yMin, bounds.Width(), bounds.Height())});
    }
    const auto hazardContainer = importer.ImportContainer(
        {9801U, outerShape, std::move(zones)});
    if (!Expect(hazardContainer.HasValue(), output,
                "multiworker worker-specific hazard import failed"))
    {
        return false;
    }

    const auto strip = jaguar::Strip::Create(
        20.0, importerConfig.cdeConfig, importerConfig.shapeModifyConfig, 20.0);
    if (!Expect(strip.HasValue(), output,
                "multiworker base strip creation failed"))
    {
        return false;
    }
    const auto instanceResult = jaguar::SPInstance::Create(
        {item.Value()}, {1U}, strip.Value());
    if (!Expect(instanceResult.HasValue(), output,
                "multiworker failure instance creation failed"))
    {
        return false;
    }
    const jaguar::SPInstance instance = instanceResult.Value();
    auto problemResult = jaguar::SPProblem::Create(instance);
    if (!Expect(problemResult.HasValue(), output,
                "multiworker failure problem creation failed"))
    {
        return false;
    }
    jaguar::SPProblem problem = std::move(problemResult.Value());
    if (!Expect(problem.ChangeStripWidth(20.0).HasValue(),
                output,
                "multiworker failure strip resize failed"))
    {
        return false;
    }
    jaguar::Layout warmLayout(hazardContainer.Value());
    if (!Expect(warmLayout.PlaceItem(
                    instance.GetItem(0U),
                    jaguar::Transform2d::FromRotationTranslation(
                        0.0, {19.0, 1.0})).HasValue(),
                output,
                "multiworker overlapping warm placement failed"))
    {
        return false;
    }
    const jaguar::SPSolution baseSolution = problem.Save();
    const jaguar::SPSolution initial{baseSolution.strip, warmLayout.Save()};
    if (!Expect(problem.Restore(initial).HasValue(),
                output,
                "multiworker custom-container warm start restore failed"))
    {
        return false;
    }
    auto separatorResult = sparrow::Separator::Create(
        std::move(problem), 1U);
    if (!Expect(separatorResult.HasValue(), output,
                "multiworker failure separator creation failed"))
    {
        return false;
    }
    sparrow::Separator separator = std::move(separatorResult.Value());
    const float initialLoss = separator.Tracker().TotalLoss();
    const jaguar::SPSolution before = separator.SaveSolution();
    if (!Expect(initialLoss > 0.0F
                    && before.layoutSnapshot.placements.size() == 1U,
                output,
                "multiworker warm start must have a single exterior collision"))
    {
        return false;
    }

    sparrow::SeparatorConfig config;
    config.strikeLimit = 1U;
    config.noImprovementLimit = 1U;
    config.nContainerSamples = 1U;
    config.nFocussedSamples = 0U;
    config.nCoordinateDescents = 1U;
    config.nWorkers = 2U;
    sparrow::OptimizationTerminator terminator({});
    SeparatorRoundRecordingListener listener;
    bool propagated = false;
    try
    {
        (void)separator.Run(config, terminator, &listener);
    }
    catch (const std::logic_error& error)
    {
        propagated = std::string(error.what()) == "unsupported hazard entity";
    }

    const jaguar::SPSolution afterFailure = separator.SaveSolution();
    const auto& beforeTransform =
        before.layoutSnapshot.placements.front().placedItem.Transform();
    const auto& afterTransform =
        afterFailure.layoutSnapshot.placements.front().placedItem.Transform();
    const bool unchanged = beforeTransform.rotationRad
            == afterTransform.rotationRad
        && beforeTransform.translation == afterTransform.translation
        && separator.Tracker().TotalLoss() == initialLoss;
    const bool failureCallbacksMatch = listener.reports.empty()
        && listener.progress.size() == 1U
        && listener.progress.front().iteration == 0U
        && !listener.result.has_value();
    if (!Expect(propagated && unchanged && failureCallbacksMatch,
                output,
                "two worker QZone failures must escape after initial progress without changing the master or completing callbacks"))
    {
        return false;
    }

    bool retryCompleted = false;
    try
    {
        const auto retry = separator.Run(config, terminator, &listener);
        retryCompleted = retry.HasValue();
    }
    catch (const std::logic_error&)
    {
    }
    return Expect(retryCompleted && listener.result.has_value(),
                  output,
                  "a retry must use worker RNG states consumed before the propagated exception");
}

bool TestMultiworkerHazardStopsOptimizePhaseCallbacks(
    std::ostream& output)
{
    const jaguar::ImporterConfig importerConfig =
        sparrow::MakeRustDefaultImporterConfig();
    const jaguar::Importer importer(importerConfig);
    const jaguar::ExternalItem externalItem{
        9812U,
        jaguar::RotationPolicy::Discrete({0.0}),
        jaguar::ExternalShape::Polygon({
            {0.0, 0.0}, {2.0, 0.0}, {2.0, 2.0}, {0.0, 2.0}}),
        std::nullopt};
    const auto item = importer.ImportItem(externalItem, 0U);
    if (!Expect(item.HasValue(), output,
                "optimizer multiworker item import failed"))
    {
        return false;
    }
    const auto outerShape = [] {
        return jaguar::ExternalShape::Polygon({
            {0.0, 0.0}, {20.0, 0.0}, {20.0, 20.0}, {0.0, 20.0}});
    };
    const auto plainContainer = importer.ImportContainer(
        {9810U, outerShape(), {}});
    if (!Expect(plainContainer.HasValue(), output,
                "optimizer plain container import failed"))
    {
        return false;
    }
    const auto samplerResult = sparrow::UniformRectSampler::Create(
        plainContainer.Value().CollisionShape().BoundingBox(),
        item.Value(),
        plainContainer.Value().CollisionShape().BoundingBox());
    if (!Expect(samplerResult.HasValue()
                    && samplerResult.Value().has_value(),
                output,
                "optimizer multiworker sampler must fit"))
    {
        return false;
    }

    // The Rust optimizer skips the construction child stream for a warm
    // start, while the current C++ entry consumes it unconditionally. Include
    // both phase-seed streams so this fixture isolates propagation behavior.
    sparrow::SparrowRng rootRng(42U);
    const std::array<std::uint64_t, 2> phaseSeeds{{
        rootRng.NextU64(), rootRng.NextU64()}};
    std::vector<jaguar::Rect2d> firstCandidateBounds;
    firstCandidateBounds.reserve(4U);
    for (const std::uint64_t phaseSeed : phaseSeeds)
    {
        sparrow::SparrowRng separatorSeedSource(phaseSeed);
        for (std::size_t workerIndex = 0U; workerIndex < 2U; ++workerIndex)
        {
            sparrow::SparrowRng workerRng(separatorSeedSource.NextU64());
            const jaguar::Transform2d candidate =
                samplerResult.Value()->Sample(workerRng);
            const auto shape = item.Value().CollisionShape().TransformedRustF32(
                candidate);
            if (!Expect(shape.HasValue(), output,
                        "optimizer worker candidate transform failed"))
            {
                return false;
            }
            firstCandidateBounds.push_back(shape.Value().BoundingBox());
        }
    }

    std::vector<jaguar::ExternalQualityZone> zones;
    zones.reserve(firstCandidateBounds.size());
    for (std::size_t index = 0U; index < firstCandidateBounds.size(); ++index)
    {
        const jaguar::Rect2d& bounds = firstCandidateBounds[index];
        zones.push_back({
            index + 1U,
            jaguar::ExternalShape::Rectangle(
                bounds.xMin, bounds.yMin, bounds.Width(), bounds.Height())});
    }
    const auto hazardContainer = importer.ImportContainer(
        {9811U, outerShape(), std::move(zones)});
    if (!Expect(hazardContainer.HasValue(), output,
                "optimizer multiworker hazard container import failed"))
    {
        return false;
    }
    const auto strip = jaguar::Strip::Create(
        20.0, importerConfig.cdeConfig, importerConfig.shapeModifyConfig, 20.0);
    if (!Expect(strip.HasValue(), output,
                "optimizer warm-start strip creation failed"))
    {
        return false;
    }
    const auto instanceResult = jaguar::SPInstance::Create(
        {item.Value()}, {1U}, strip.Value());
    if (!Expect(instanceResult.HasValue(), output,
                "optimizer multiworker instance creation failed"))
    {
        return false;
    }
    jaguar::Layout warmLayout(hazardContainer.Value());
    if (!Expect(warmLayout.PlaceItem(
                    instanceResult.Value().GetItem(0U),
                    jaguar::Transform2d::FromRotationTranslation(
                        0.0, {19.0, 1.0})).HasValue(),
                output,
                "optimizer warm-start exterior placement failed"))
    {
        return false;
    }
    const jaguar::SPSolution warmStart{
        strip.Value(), warmLayout.Save()};
    if (!Expect(!warmLayout.IsFeasible(), output,
                "optimizer warm-start must contain an exterior collision"))
    {
        return false;
    }

    class PhaseRecordingListener final : public sparrow::OptimizationListener
    {
    public:
        void OnPhase(sparrow::OptimizationPhase phase) override
        {
            phases.push_back(phase);
            events.emplace_back("phase");
        }

        void OnSolution(
            const sparrow::OptimizationSolutionReport& report) override
        {
            reports.push_back(report.type);
            events.emplace_back("report");
        }

        void OnSeparationProgress(
            const sparrow::SeparationProgress& progress) override
        {
            progressIterations.push_back(progress.iteration);
            events.emplace_back("progress");
        }

        void OnSeparationResult(
            const sparrow::SeparationResult&) override
        {
            ++separationResults;
            events.emplace_back("separation_result");
        }

        std::vector<sparrow::OptimizationPhase> phases;
        std::vector<sparrow::OptimizationReportType> reports;
        std::vector<std::size_t> progressIterations;
        std::vector<std::string> events;
        std::size_t separationResults = 0U;
    } listener;

    sparrow::SparrowConfig config;
    config.seed = 42U;
    config.warmStart = warmStart;
    config.exploration.timeLimit = std::chrono::seconds{10};
    config.exploration.separator.strikeLimit = 1U;
    config.exploration.separator.noImprovementLimit = 1U;
    config.exploration.separator.nContainerSamples = 1U;
    config.exploration.separator.nFocussedSamples = 0U;
    config.exploration.separator.nCoordinateDescents = 1U;
    config.exploration.separator.nWorkers = 2U;

    bool propagated = false;
    try
    {
        const auto result = sparrow::Optimize(
            instanceResult.Value(), config, &listener);
        if (!result.HasValue())
        {
            output << "optimizer returned error: "
                   << result.GetError().message << '\n';
        }
    }
    catch (const std::logic_error& error)
    {
        propagated = std::string(error.what()) == "unsupported hazard entity";
    }
    return Expect(propagated
                      && listener.phases
                             == std::vector<sparrow::OptimizationPhase>{
                                 sparrow::OptimizationPhase::OPTIMIZATION_PHASE_EXPLORATION}
                      && listener.reports
                             == std::vector<sparrow::OptimizationReportType>{
                                 sparrow::OptimizationReportType::OPTIMIZATION_REPORT_EXPLORATION_FEASIBLE}
                      && listener.progressIterations
                             == std::vector<std::size_t>{0U}
                      && listener.events
                             == std::vector<std::string>{
                                 "phase", "report", "progress"}
                      && listener.separationResults == 0U,
                  output,
                  "multiworker panic must escape Optimize after only Exploration, initial report, and initial separation progress callbacks");
}
}

bool RunWorkerLossSelectorTests(std::ostream& output)
{
    return TestWorkerLossSelectorNonFiniteOrdering(output)
        && TestMixedNonFiniteWorkerResultCollection(output);
}

bool RunSeparatorTests(std::ostream& output)
{
    if (!RunWorkerLossSelectorTests(output))
    {
        return false;
    }
    if (!TestInitiallySeparatedRunCompletesEmptyWorkerRound(output))
    {
        return false;
    }
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
    if (!TestSeparatorThresholdAndIterationBoundaries(output))
    {
        return false;
    }
    if (!TestSeparatorZeroLimitsAndFourWorkerInvariant(output))
    {
        return false;
    }
    if (!TestActiveFourWorkerInvariant(output))
    {
        return false;
    }
    if (!TestRustSeparatorSearchTrace(
            output, "SeparatorRustTrace.json", "SeparatorSearchRustTrace.tsv",
            8.0, 8U, 16U, 2U, {1.0, 1.0}, false))
    {
        return false;
    }
    if (!TestRustSeparatorSearchTrace(
            output, "SeparatorRustTrace.json",
            "SeparatorSearchBudgetRustTrace.tsv", 8.0, 3U, 5U, 2U,
            {1.0, 1.0}, false))
    {
        return false;
    }
    if (!TestRustSeparatorSearchTrace(
            output, "SearchExactTie.json",
            "SeparatorSearchExactTieRustTrace.tsv", 2.0, 3U, 5U, 1U,
            {1.0, 1.0}, true))
    {
        return false;
    }
    if (!TestZeroSeparatorSampleBudgetUsesReferencePlacement(output))
    {
        return false;
    }
    if (!TestSeparatorSamplerErrorPreservesState(output))
    {
        return false;
    }
    if (!TestUnsupportedHazardPropagatesThroughWorkerSearch(output))
    {
        return false;
    }
    if (!TestMultiworkerHazardFailureRetainsWorkerRngForRetry(output))
    {
        return false;
    }
    if (!TestMultiworkerHazardStopsOptimizePhaseCallbacks(output))
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
