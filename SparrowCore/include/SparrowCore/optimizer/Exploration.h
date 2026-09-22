#pragma once

#include "SparrowCore/LbfMetrics.h"
#include "SparrowCore/Optimizer/OptimizationListener.h"
#include "SparrowCore/Optimizer/OptimizationTerminator.h"
#include "SparrowCore/Optimizer/SparrowConfig.h"
#include "SparrowCore/ResultAlias.h"

#include "JaguarCore/Problems/Spp.h"

#include <cstdint>

namespace sparrow
{
struct ExplorationResult
{
    jaguar::SPSolution solution;
    LbfMetrics metrics;
};

SPARROWCORE_API Result<ExplorationResult> Explore(
    const jaguar::SPInstance& instance,
    const jaguar::SPSolution& initialSolution,
    const ExplorationConfig& config,
    std::uint64_t seed,
    OptimizationTerminator& terminator,
    OptimizationListener* listener = nullptr);
}
