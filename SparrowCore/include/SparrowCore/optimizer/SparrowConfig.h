#pragma once

#include "SparrowCore/LbfConfig.h"
#include "SparrowCore/Optimizer/OptimizationTerminator.h"
#include "SparrowCore/Optimizer/SeparatorWorker.h"
#include "SparrowCore/ResultAlias.h"

#include "JaguarCore/Problems/Spp.h"
#include "JaguarCore/IO/Importer.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

namespace sparrow
{
enum class ShrinkDecayStrategy
{
    SHRINK_DECAY_TIME_BASED,
    SHRINK_DECAY_FAILURE_BASED,
};

struct ExplorationConfig
{
    SeparatorConfig separator;
    // Zero means unlimited, matching the Rust phase loop which is bounded by
    // its terminator rather than a second iteration counter.
    std::size_t maxIterations = 0U;
    std::optional<std::size_t> maxConsecutiveFailures = std::nullopt;
    double shrinkStep = 0.001;
    // Zero keeps the complete failed-solution pool, as in sparrow.
    std::size_t infeasiblePoolSize = 0U;
    std::size_t disruptionCount = 1U;
    std::size_t containedRelocationCount = 1U;
    double solutionPoolDistributionStddev = 0.25;
    double largeItemChAreaCutoffPercentile = 0.75;
    std::chrono::nanoseconds timeLimit{std::chrono::minutes{9}};

    Result<void> Validate() const;
};

struct CompressionConfig
{
    SeparatorConfig separator = [] {
        SeparatorConfig value;
        value.strikeLimit = 5U;
        value.noImprovementLimit = 100U;
        value.nContainerSamples = 50U;
        value.nFocussedSamples = 25U;
        value.nCoordinateDescents = 3U;
        value.nWorkers = 3U;
        return value;
    }();
    ShrinkDecayStrategy decayStrategy =
        ShrinkDecayStrategy::SHRINK_DECAY_TIME_BASED;
    std::size_t maxIterations = 0U;
    std::size_t maxConsecutiveFailures = 0U;
    double shrinkMin = 0.00001;
    double shrinkMax = 0.0005;
    double failureDecay = 0.9;
    std::chrono::nanoseconds timeLimit{std::chrono::seconds{60}};

    Result<void> Validate() const;
};

struct SparrowConfig
{
    std::optional<std::uint64_t> seed = std::nullopt;
    // Like Rust, a restorable warm start may be incomplete or geometrically
    // infeasible; its actual feasibility is reflected in LbfMetrics.
    std::optional<jaguar::SPSolution> warmStart;
    SparrowConstructorConfig constructor;
    ExplorationConfig exploration;
    CompressionConfig compression;
    OptimizationTerminatorConfig terminator;

    Result<void> Validate() const;
};

// Builds the JaguarCore import settings used by Rust DEFAULT_SPARROW_CONFIG.
// Use this before importing an instance when Rust-compatible geometry and
// collision surrogates are required.
inline jaguar::ImporterConfig MakeRustDefaultImporterConfig()
{
    jaguar::ImporterConfig config;
    config.cdeConfig.quadtreeDepth = 4U;
    config.cdeConfig.cdThreshold = 64U;
    config.cdeConfig.itemSurrogateConfig.poleLimits = {
        {{64U, 0.0}, {16U, 0.8}, {8U, 0.9}}};
    config.cdeConfig.itemSurrogateConfig.failFastPoleAreaRatio = 0.5;
    config.cdeConfig.itemSurrogateConfig.failFastPiers = 0U;
    config.shapeModifyConfig.simplifyTolerance = 0.001;
    config.shapeModifyConfig.narrowConcavityCutoff =
        std::pair<double, double>{0.01, 0.01};
    return config;
}
}
