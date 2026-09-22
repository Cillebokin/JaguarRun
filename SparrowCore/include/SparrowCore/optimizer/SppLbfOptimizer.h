#pragma once

#include "SparrowCore/Export.h"
#include "SparrowCore/LbfConfig.h"
#include "SparrowCore/LbfMetrics.h"
#include "SparrowCore/Optimizer/LbfSearch.h"
#include "SparrowCore/ResultAlias.h"

#include "JaguarCore/Problems/Spp.h"

namespace sparrow
{
struct SppLbfResult
{
    jaguar::SPSolution solution;
    LbfMetrics metrics;
};

SPARROWCORE_API Result<SppLbfResult> SolveSppLbf(
    const jaguar::SPInstance& instance,
    const LbfConfig& config);
}
