#pragma once

#include "SparrowCore/Export.h"
#include "SparrowCore/LbfMetrics.h"
#include "SparrowCore/Optimizer/OptimizationListener.h"
#include "SparrowCore/Optimizer/SparrowConfig.h"
#include "SparrowCore/ResultAlias.h"

#include "JaguarCore/Problems/Spp.h"

namespace sparrow
{
struct SparrowOptimizationResult
{
    jaguar::SPSolution solution;
    LbfMetrics metrics;
};

SPARROWCORE_API Result<SparrowOptimizationResult> Optimize(
    const jaguar::SPInstance& instance,
    const SparrowConfig& config,
    OptimizationListener* listener = nullptr);
}
