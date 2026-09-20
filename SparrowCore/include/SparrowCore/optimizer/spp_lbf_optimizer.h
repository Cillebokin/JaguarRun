#pragma once

#include "SparrowCore/export.h"
#include "SparrowCore/lbf_config.h"
#include "SparrowCore/lbf_metrics.h"
#include "SparrowCore/optimizer/lbf_search.h"
#include "SparrowCore/result_alias.h"

#include "JaguarCore/problems/spp.h"

namespace sparrow
{
struct SppLbfResult
{
    jaguar::SPSolution solution;
    LbfMetrics metrics;
};

SPARROWCORE_API Result<SppLbfResult> solveSppLbf(
    const jaguar::SPInstance& instance,
    const LbfConfig& config);
}
