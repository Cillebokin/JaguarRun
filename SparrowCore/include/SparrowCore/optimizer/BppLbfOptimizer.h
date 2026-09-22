#pragma once

#include "SparrowCore/Export.h"
#include "SparrowCore/LbfConfig.h"
#include "SparrowCore/LbfMetrics.h"
#include "SparrowCore/Optimizer/LbfSearch.h"
#include "SparrowCore/ResultAlias.h"

#include "JaguarCore/Problems/Bpp.h"

namespace sparrow
{
struct BppLbfResult
{
    jaguar::BPSolution solution;
    LbfMetrics metrics;
};

SPARROWCORE_API Result<BppLbfResult> SolveBppLbf(
    const jaguar::BPInstance& instance,
    const LbfConfig& config);
}
