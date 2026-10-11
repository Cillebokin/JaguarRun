#include "SparrowCore/Optimizer/SparrowOptimizer.h"

#include "SparrowWasm.h"

#include "JaguarCore/IO/Importer.h"
#include "JaguarCore/IO/JsonCodec.h"
#include "JaguarCore/IO/SvgExport.h"

#include <cstring>
#include <string>
#include <utility>

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#define SPARROW_WASM_KEEPALIVE EMSCRIPTEN_KEEPALIVE
#else
#define SPARROW_WASM_KEEPALIVE
#endif

namespace
{
int CopyOutput(const std::string& value,
               char* outputJson,
               std::size_t outputCapacity)
{
    if (outputJson == nullptr || outputCapacity <= value.size())
    {
        return 3;
    }
    std::memcpy(outputJson, value.data(), value.size());
    outputJson[value.size()] = '\0';
    return 0;
}

int CopyError(const char* message,
              char* outputJson,
              std::size_t outputCapacity,
              int code)
{
    const std::string value = std::string("{\"error\":\"")
        + message + "\"}";
    const int copied = CopyOutput(value, outputJson, outputCapacity);
    return copied == 0 ? code : copied;
}

struct OptimizedInput
{
    OptimizedInput(jaguar::SPInstance inputInstance,
                   jaguar::SPSolution optimizedSolution)
        : instance(std::move(inputInstance)),
          solution(std::move(optimizedSolution))
    {
    }

    jaguar::SPInstance instance;
    jaguar::SPSolution solution;
};

jaguar::Result<OptimizedInput> OptimizeInput(
    const char* inputJson,
    std::size_t inputSize)
{
    const jaguar::Importer importer(sparrow::MakeRustDefaultImporterConfig());
    const auto instance = importer.ImportSPInstanceJson(
        std::string(inputJson, inputSize));
    if (!instance.HasValue())
    {
        return jaguar::Result<OptimizedInput>::FromError(
            instance.GetError());
    }

    sparrow::SparrowConfig config;
    config.seed = 0U;
    config.constructor.nContainerSamples = 128U;
    config.constructor.nFocussedSamples = 0U;
    config.exploration.maxIterations = 2U;
    config.exploration.maxConsecutiveFailures = 3U;
    config.exploration.separator.nContainerSamples = 32U;
    config.exploration.separator.nFocussedSamples = 16U;
    // Keep the browser bridge bounded; the native Sparrow defaults remain
    // available through SparrowConfig and use the Rust no-improvement limit.
    config.exploration.separator.noImprovementLimit = 4U;
    config.compression.maxIterations = 2U;
    config.compression.maxConsecutiveFailures = 3U;
    config.compression.separator.nContainerSamples = 32U;
    config.compression.separator.nFocussedSamples = 16U;
    config.compression.separator.noImprovementLimit = 4U;
    config.terminator.maxIterations = 64U;

    const auto optimized = sparrow::Optimize(instance.Value(), config);
    if (!optimized.HasValue())
    {
        return jaguar::Result<OptimizedInput>::FromError(
            optimized.GetError());
    }
    return jaguar::Result<OptimizedInput>::FromValue(
        OptimizedInput{instance.Value(), optimized.Value().solution});
}
}

extern "C" SPARROW_WASM_KEEPALIVE int SparrowOptimizeJson(
    const char* inputJson,
    std::size_t inputSize,
    char* outputJson,
    std::size_t outputCapacity)
{
    if (inputJson == nullptr || inputSize == 0U)
    {
        return CopyError("input is empty", outputJson, outputCapacity, 1);
    }
    const auto optimized = OptimizeInput(inputJson, inputSize);
    if (!optimized.HasValue())
    {
        return CopyError("optimization failed", outputJson, outputCapacity, 2);
    }
    const auto external = jaguar::ExternalSPSolutionFromSolution(
        optimized.Value().instance, optimized.Value().solution);
    if (!external.HasValue())
    {
        return CopyError("solution conversion failed", outputJson, outputCapacity, 2);
    }
    const auto json = jaguar::SerializeExternalSPSolutionJson(
        external.Value(), {false});
    if (!json.HasValue())
    {
        return CopyError("solution serialization failed", outputJson, outputCapacity, 2);
    }
    return CopyOutput(json.Value(), outputJson, outputCapacity);
}

extern "C" SPARROW_WASM_KEEPALIVE int SparrowOptimizeSvg(
    const char* inputJson,
    std::size_t inputSize,
    char* outputSvg,
    std::size_t outputCapacity)
{
    if (inputJson == nullptr || inputSize == 0U)
    {
        return CopyError("input is empty", outputSvg, outputCapacity, 1);
    }
    const auto optimized = OptimizeInput(inputJson, inputSize);
    if (!optimized.HasValue())
    {
        return CopyError("optimization failed", outputSvg, outputCapacity, 2);
    }
    jaguar::Layout layout(optimized.Value().solution.layoutSnapshot.container);
    const auto restored = layout.Restore(
        optimized.Value().solution.layoutSnapshot);
    if (!restored.HasValue())
    {
        return CopyError("layout restore failed", outputSvg, outputCapacity, 2);
    }
    jaguar::SvgExportOptions options;
    options.title = "SparrowCore Layout";
    options.drawSurrogates = true;
    const auto svg = jaguar::ExportLayoutSvg(layout, options);
    if (!svg.HasValue())
    {
        return CopyError("SVG export failed", outputSvg, outputCapacity, 2);
    }
    return CopyOutput(svg.Value(), outputSvg, outputCapacity);
}
