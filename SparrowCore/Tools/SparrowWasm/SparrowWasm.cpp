#include "SparrowCore/Optimizer/SparrowOptimizer.h"

#include "JaguarCore/IO/Importer.h"
#include "JaguarCore/IO/JsonCodec.h"

#include <cstring>
#include <string>

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
    const jaguar::Importer importer;
    const auto instance = importer.ImportSPInstanceJson(
        std::string(inputJson, inputSize));
    if (!instance.HasValue())
    {
        return CopyError("instance import failed", outputJson, outputCapacity, 1);
    }

    sparrow::SparrowConfig config;
    config.seed = 0U;
    config.constructor.seed = 0U;
    config.constructor.nContainerSamples = 128U;
    config.constructor.nFocussedSamples = 32U;
    config.exploration.maxIterations = 8U;
    config.exploration.maxConsecutiveFailures = 3U;
    config.exploration.separator.nContainerSamples = 32U;
    config.exploration.separator.nFocussedSamples = 16U;
    config.compression.maxIterations = 8U;
    config.compression.maxConsecutiveFailures = 3U;
    config.compression.separator.nContainerSamples = 32U;
    config.compression.separator.nFocussedSamples = 16U;
    config.terminator.maxIterations = 256U;

    const auto optimized = sparrow::Optimize(instance.Value(), config);
    if (!optimized.HasValue())
    {
        return CopyError("optimization failed", outputJson, outputCapacity, 2);
    }
    const auto external = jaguar::ExternalSPSolutionFromSolution(
        instance.Value(), optimized.Value().solution);
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
