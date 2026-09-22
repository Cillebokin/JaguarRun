#pragma once

#include "SparrowCore/LbfConfig.h"
#include "SparrowCore/Optimizer/SppLbfOptimizer.h"
#include "SparrowCore/ResultAlias.h"

namespace sparrow
{
SPARROWCORE_API Result<SppLbfResult> ConstructSparrowInitialSolution(
    const jaguar::SPInstance& instance,
    const SparrowConstructorConfig& config);
}
