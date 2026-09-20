#pragma once

#include "SparrowCore/export.h"
#include "SparrowCore/lbf_config.h"
#include "SparrowCore/lbf_metrics.h"
#include "SparrowCore/optimizer/lbf_search.h"
#include "SparrowCore/result_alias.h"

#include "JaguarCore/problems/bpp.h"

namespace sparrow
{
struct BppLbfResult
{
    jaguar::BPSolution solution;
    LbfMetrics metrics;
};

SPARROWCORE_API Result<BppLbfResult> solveBppLbf(
    const jaguar::BPInstance& instance,
    const LbfConfig& config);
}
