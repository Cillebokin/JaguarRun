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
struct CompressionResult
{
    jaguar::SPSolution solution;
    LbfMetrics metrics;
    OptimizationHistory history;
};

SPARROWCORE_API Result<CompressionResult> Compress(
    const jaguar::SPInstance& instance,
    const jaguar::SPSolution& initialSolution,
    const CompressionConfig& config,
    std::uint64_t seed,
    OptimizationTerminator& terminator,
    OptimizationListener* listener = nullptr,
    OptimizationObserver* observer = nullptr);

SPARROWCORE_API Result<CompressionResult> Compress(
    const jaguar::SPInstance& instance,
    const jaguar::SPSolution& separatorState,
    const jaguar::SPSolution& initialSolution,
    const CompressionConfig& config,
    std::uint64_t seed,
    OptimizationTerminator& terminator,
    OptimizationListener* listener = nullptr,
    OptimizationObserver* observer = nullptr);
}
