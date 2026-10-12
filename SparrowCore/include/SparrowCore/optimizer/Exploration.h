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
    jaguar::SPSolution separatorState;
    LbfMetrics metrics;
    OptimizationHistory history;
};

// Any snapshot restorable against the instance may enter Explore, including a
// partial or geometrically infeasible layout. The initial report kind mirrors
// Rust's unconditional Exploration Feasible report; its isFeasible field and
// the returned metrics describe the actual layout and remaining demand.
SPARROWCORE_API Result<ExplorationResult> Explore(
    const jaguar::SPInstance& instance,
    const jaguar::SPSolution& initialSolution,
    const ExplorationConfig& config,
    std::uint64_t seed,
    OptimizationTerminator& terminator,
    OptimizationListener* listener = nullptr,
    OptimizationObserver* observer = nullptr);
}
