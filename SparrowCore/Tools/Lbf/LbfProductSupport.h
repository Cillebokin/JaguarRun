#pragma once

#include "JaguarCore/IO/Importer.h"
#include "JaguarCore/IO/SvgExport.h"
#include "SparrowCore/LbfConfig.h"
#include "SparrowCore/LbfMetrics.h"
#include "SparrowCore/ResultAlias.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace sparrow::lbf_entry
{
using DiagnosticCallback = std::function<void(const std::string&)>;

enum class ErrorTextFormat
{
    Native,
    RustWasm,
};

struct RuntimeConfig
{
    LbfConfig optimizer;
    jaguar::ImporterConfig importer;
    jaguar::SvgExportOptions svg;
    std::optional<std::uint64_t> configuredSeed;
    jaguar::SvgColor holeFill{0x2d, 0x2d, 0x2d};
    bool svgQuadtree = false;
    bool svgDrawCdShapes = false;
    bool svgHighlightCdShapes = true;
};

struct Artifact
{
    std::string outputJson;
    std::vector<std::pair<std::string, std::string>> svgs;
    std::uint64_t solveTimeMs = 0;
    LbfMetrics metrics;
};

RuntimeConfig RustDefaultRuntimeConfig();
std::uint64_t RandomSeed();
std::string SerializeRuntimeConfig(const RuntimeConfig& config);
std::string SerializeWasmResult(const Artifact& artifact);

Result<Artifact> RunSppLbf(const std::string& instanceJson,
                           const RuntimeConfig& config,
                           std::uint64_t effectiveSeed,
                           const std::string& svgFileName,
                           const DiagnosticCallback& diagnostics = {},
                           ErrorTextFormat errorTextFormat = ErrorTextFormat::Native);
Result<Artifact> RunBppLbf(const std::string& instanceJson,
                           const RuntimeConfig& config,
                           std::uint64_t effectiveSeed,
                           const std::string& svgFileStem,
                           const DiagnosticCallback& diagnostics = {},
                           ErrorTextFormat errorTextFormat = ErrorTextFormat::Native);
}
