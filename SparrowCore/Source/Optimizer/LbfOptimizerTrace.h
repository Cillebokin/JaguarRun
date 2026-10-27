#pragma once

#include "LbfSearchTrace.h"

#include "SparrowCore/Optimizer/BppLbfOptimizer.h"
#include "SparrowCore/Optimizer/SppLbfOptimizer.h"

#include <functional>
#include <optional>
#include <string>

namespace sparrow
{
using LbfDiagnosticCallback = std::function<void(const std::string&)>;

SPARROWCORE_API Result<std::optional<SearchResult>> SearchLbfWithDiagnostics(
    const jaguar::Layout& layout,
    const jaguar::Item& item,
    const LbfConfig& config,
    SparrowRng& rng,
    const jaguar::HazardFilter& filter,
    const LbfDiagnosticCallback& callback);

SPARROWCORE_API Result<SppLbfResult> SolveSppLbfWithDiagnostics(
    const jaguar::SPInstance& instance,
    const LbfConfig& config,
    const LbfDiagnosticCallback& callback);

SPARROWCORE_API Result<BppLbfResult> SolveBppLbfWithDiagnostics(
    const jaguar::BPInstance& instance,
    const LbfConfig& config,
    const LbfDiagnosticCallback& callback);
}
