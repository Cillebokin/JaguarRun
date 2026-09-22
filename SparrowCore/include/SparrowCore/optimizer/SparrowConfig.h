#pragma once

#include "SparrowCore/LbfConfig.h"
#include "SparrowCore/Optimizer/OptimizationTerminator.h"
#include "SparrowCore/Optimizer/SeparatorWorker.h"
#include "SparrowCore/ResultAlias.h"

#include <cstddef>
#include <cstdint>

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
    std::size_t maxIterations = 50U;
    std::size_t maxConsecutiveFailures = 8U;
    double shrinkStep = 0.02;

    Result<void> Validate() const;
};

struct CompressionConfig
{
    SeparatorConfig separator;
    ShrinkDecayStrategy decayStrategy =
        ShrinkDecayStrategy::SHRINK_DECAY_TIME_BASED;
    std::size_t maxIterations = 100U;
    std::size_t maxConsecutiveFailures = 8U;
    double shrinkMin = 0.001;
    double shrinkMax = 0.02;
    double failureDecay = 0.5;

    Result<void> Validate() const;
};

struct SparrowConfig
{
    std::uint64_t seed = 0U;
    SparrowConstructorConfig constructor;
    ExplorationConfig exploration;
    CompressionConfig compression;
    OptimizationTerminatorConfig terminator;

    Result<void> Validate() const;
};
}
