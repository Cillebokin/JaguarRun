#include "SparrowCore/Optimizer/SparrowOptimizer.h"

#include "SparrowWasm.h"

#include "JaguarCore/IO/Importer.h"
#include "JaguarCore/IO/JsonCodec.h"
#include "JaguarCore/IO/SvgExport.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <limits>
#include <optional>
#include <string>
#include <utility>

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#define SPARROW_WASM_KEEPALIVE EMSCRIPTEN_KEEPALIVE
#else
#define SPARROW_WASM_KEEPALIVE
#endif

#if defined(__EMSCRIPTEN__)
EM_JS(void, SparrowPostWasmEvent,
      (int kind,
       int phase,
       int report,
       unsigned int iteration,
       double stripWidth,
       double density,
       double collisionLoss,
       int feasible,
       double elapsedSeconds,
       double value,
       unsigned int count,
       double durationSeconds,
       const char* svg),
      {
          if (typeof postMessage !== "function")
              return;
          const event = {
              type: "sparrow-progress",
              kind,
              phase,
              report,
              iteration,
              stripWidth,
              density,
              collisionLoss,
              feasible: feasible !== 0,
              elapsedSeconds,
              value,
              count,
              durationSeconds
          };
          if (svg)
              event.svg = UTF8ToString(svg);
          postMessage(event);
      });
#endif

namespace
{
constexpr int kWasmEventPhase = 1;
constexpr int kWasmEventSolution = 2;
constexpr int kWasmEventSeparationProgress = 3;
constexpr int kWasmEventSeparationResult = 4;
constexpr int kWasmEventCompressionProgress = 5;

void PostWasmEvent(int kind,
                   sparrow::OptimizationPhase phase,
                   int report,
                   std::size_t iteration,
                   double stripWidth,
                   double density,
                   double collisionLoss,
                   bool feasible,
                   double elapsedSeconds,
                   double value = 0.0,
                   std::size_t count = 0U,
                   double durationSeconds = 0.0,
                   const char* svg = nullptr)
{
#if defined(__EMSCRIPTEN__)
    SparrowPostWasmEvent(
        kind,
        static_cast<int>(phase),
        report,
        static_cast<unsigned int>(iteration),
        stripWidth,
        density,
        collisionLoss,
        feasible ? 1 : 0,
        elapsedSeconds,
        value,
        static_cast<unsigned int>(count),
        durationSeconds,
        svg);
#else
    (void)kind;
    (void)phase;
    (void)report;
    (void)iteration;
    (void)stripWidth;
    (void)density;
    (void)collisionLoss;
    (void)feasible;
    (void)elapsedSeconds;
    (void)value;
    (void)count;
    (void)durationSeconds;
    (void)svg;
#endif
}

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

jaguar::Result<std::string> ExportSolutionSvg(
    const jaguar::SPSolution& solution,
    const std::string& title)
{
    jaguar::Layout layout(solution.layoutSnapshot.container);
    const auto restored = layout.Restore(solution.layoutSnapshot);
    if (!restored.HasValue())
    {
        return jaguar::Result<std::string>::FromError(restored.GetError());
    }
    jaguar::SvgExportOptions options;
    options.theme = jaguar::SvgLayoutTheme::Gray();
    options.title = title;
    options.drawSurrogates = false;
    options.highlightCollisions = true;
    options.includeAxes = false;
    return jaguar::ExportLayoutSvg(layout, options);
}

class WasmOptimizationListener final : public sparrow::OptimizationListener
{
public:
    WasmOptimizationListener()
        : m_Start(std::chrono::steady_clock::now())
    {
        PostWasmEvent(
            kWasmEventPhase,
            sparrow::OptimizationPhase::OPTIMIZATION_PHASE_CONSTRUCTION,
            0,
            0U,
            0.0,
            0.0,
            0.0,
            false,
            0.0);
    }

    void OnPhase(sparrow::OptimizationPhase phase) override
    {
        m_Phase = phase;
        PostWasmEvent(kWasmEventPhase, phase, 0, 0U, 0.0, 0.0, 0.0,
                      false, ElapsedSeconds());
    }

    void OnSolution(
        const sparrow::OptimizationSolutionReport& report) override
    {
        const auto now = std::chrono::steady_clock::now();
        const bool isFinal = report.type
            == sparrow::OptimizationReportType::OPTIMIZATION_REPORT_FINAL;
        if (!isFinal && m_LastSolution.has_value()
            && now - *m_LastSolution < std::chrono::milliseconds{100})
        {
            return;
        }
        m_LastSolution = now;

        std::string liveSvg;
        try
        {
            const auto svg = ExportSolutionSvg(
                report.solution, "SparrowCore Live Layout");
            if (svg.HasValue())
                liveSvg = svg.Value();
        }
        catch (...)
        {
            // Live rendering is an optional host update and must not abort Optimize.
        }
        PostWasmEvent(
            kWasmEventSolution,
            report.phase,
            static_cast<int>(report.type),
            report.iteration,
            report.stripWidth,
            report.density,
            report.collisionLoss,
            report.isFeasible,
            ElapsedSeconds(),
            0.0,
            0U,
            0.0,
            liveSvg.empty() ? nullptr : liveSvg.c_str());
    }

    void OnSeparationProgress(
        const sparrow::SeparationProgress& progress) override
    {
        if (!ShouldPublishProgress())
            return;
        PostWasmEvent(
            kWasmEventSeparationProgress,
            m_Phase,
            0,
            progress.iteration,
            progress.stripWidth,
            progress.density,
            progress.minimumLoss,
            false,
            ElapsedSeconds());
    }

    void OnSeparationResult(
        const sparrow::SeparationResult& result) override
    {
        PostWasmEvent(
            kWasmEventSeparationResult,
            m_Phase,
            result.success ? 1 : 0,
            result.iterations,
            0.0,
            0.0,
            0.0,
            false,
            ElapsedSeconds(),
            static_cast<double>(result.totalMoves),
            result.totalEvaluations,
            result.elapsedSeconds);
    }

    void OnCompressionProgress(double shrinkStep) override
    {
        if (!ShouldPublishProgress())
            return;
        PostWasmEvent(
            kWasmEventCompressionProgress,
            sparrow::OptimizationPhase::OPTIMIZATION_PHASE_COMPRESSION,
            0,
            0U,
            0.0,
            0.0,
            0.0,
            false,
            ElapsedSeconds(),
            shrinkStep);
    }

private:
    double ElapsedSeconds() const
    {
        return std::chrono::duration<double>(
            std::chrono::steady_clock::now() - m_Start).count();
    }

    bool ShouldPublishProgress()
    {
        const auto now = std::chrono::steady_clock::now();
        if (m_LastProgress.has_value()
            && now - *m_LastProgress < std::chrono::milliseconds{100})
        {
            return false;
        }
        m_LastProgress = now;
        return true;
    }

    std::chrono::steady_clock::time_point m_Start;
    std::optional<std::chrono::steady_clock::time_point> m_LastSolution;
    std::optional<std::chrono::steady_clock::time_point> m_LastProgress;
    sparrow::OptimizationPhase m_Phase =
        sparrow::OptimizationPhase::OPTIMIZATION_PHASE_CONSTRUCTION;
};

int CopyOwnedOutput(const std::string& json,
                    const std::string& svg,
                    int status,
                    char** outputBuffer,
                    std::size_t* outputJsonSize,
                    std::size_t* outputSvgSize)
{
    if (outputBuffer == nullptr || outputJsonSize == nullptr
        || outputSvgSize == nullptr)
    {
        return 3;
    }
    *outputBuffer = nullptr;
    *outputJsonSize = 0U;
    *outputSvgSize = 0U;

    const std::size_t maxSize = std::numeric_limits<std::size_t>::max();
    if (json.size() > maxSize - 2U
        || svg.size() > maxSize - json.size() - 2U)
    {
        return 3;
    }
    const std::size_t bufferSize = json.size() + 1U + svg.size() + 1U;
    auto* buffer = static_cast<char*>(std::malloc(bufferSize));
    if (buffer == nullptr)
        return 3;
    std::memcpy(buffer, json.data(), json.size());
    buffer[json.size()] = '\0';
    const std::size_t svgOffset = json.size() + 1U;
    std::memcpy(buffer + svgOffset, svg.data(), svg.size());
    buffer[svgOffset + svg.size()] = '\0';
    *outputBuffer = buffer;
    *outputJsonSize = json.size();
    *outputSvgSize = svg.size();
    return status;
}

jaguar::Result<OptimizedInput> OptimizeInput(
    const char* inputJson,
    std::size_t inputSize,
    sparrow::OptimizationListener* listener = nullptr)
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

    const auto optimized = sparrow::Optimize(
        instance.Value(), config, listener);
    if (!optimized.HasValue())
    {
        return jaguar::Result<OptimizedInput>::FromError(
            optimized.GetError());
    }
    return jaguar::Result<OptimizedInput>::FromValue(
        OptimizedInput{instance.Value(), optimized.Value().solution});
}
}

static int SparrowOptimizeJsonUnguarded(
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

static int SparrowOptimizeSvgUnguarded(
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
    options.theme = jaguar::SvgLayoutTheme::Gray();
    options.title = "SparrowCore Layout";
    options.drawSurrogates = false;
    options.highlightCollisions = true;
    options.includeAxes = false;
    const auto svg = jaguar::ExportLayoutSvg(layout, options);
    if (!svg.HasValue())
    {
        return CopyError("SVG export failed", outputSvg, outputCapacity, 2);
    }
    return CopyOutput(svg.Value(), outputSvg, outputCapacity);
}

// The C ABI must never let a C++ exception cross the boundary. Any exception
// raised by the optimizer, importer or exporter is reported as code 2 with a
// fixed message, so the JSON payload stays valid regardless of e.what().
extern "C" SPARROW_WASM_KEEPALIVE int SparrowOptimizeJson(
    const char* inputJson,
    std::size_t inputSize,
    char* outputJson,
    std::size_t outputCapacity)
{
    try
    {
        return SparrowOptimizeJsonUnguarded(
            inputJson, inputSize, outputJson, outputCapacity);
    }
    catch (const std::exception&)
    {
        return CopyError("internal error", outputJson, outputCapacity, 2);
    }
    catch (...)
    {
        return CopyError("internal error", outputJson, outputCapacity, 2);
    }
}

extern "C" SPARROW_WASM_KEEPALIVE int SparrowOptimizeSvg(
    const char* inputJson,
    std::size_t inputSize,
    char* outputSvg,
    std::size_t outputCapacity)
{
    try
    {
        return SparrowOptimizeSvgUnguarded(
            inputJson, inputSize, outputSvg, outputCapacity);
    }
    catch (const std::exception&)
    {
        return CopyError("internal error", outputSvg, outputCapacity, 2);
    }
    catch (...)
    {
        return CopyError("internal error", outputSvg, outputCapacity, 2);
    }
}

extern "C" SPARROW_WASM_KEEPALIVE int SparrowOptimizeWithSvg(
    const char* inputJson,
    std::size_t inputSize,
    char** outputBuffer,
    std::size_t* outputJsonSize,
    std::size_t* outputSvgSize)
{
    if (outputBuffer != nullptr)
        *outputBuffer = nullptr;
    if (outputJsonSize != nullptr)
        *outputJsonSize = 0U;
    if (outputSvgSize != nullptr)
        *outputSvgSize = 0U;
    if (outputBuffer == nullptr || outputJsonSize == nullptr
        || outputSvgSize == nullptr)
    {
        return 3;
    }

    try
    {
        if (inputJson == nullptr || inputSize == 0U)
        {
            return CopyOwnedOutput(
                "{\"error\":\"input is empty\"}", {}, 1,
                outputBuffer, outputJsonSize, outputSvgSize);
        }

        WasmOptimizationListener listener;
        const auto optimized = OptimizeInput(inputJson, inputSize, &listener);
        if (!optimized.HasValue())
        {
            return CopyOwnedOutput(
                "{\"error\":\"optimization failed\"}", {}, 2,
                outputBuffer, outputJsonSize, outputSvgSize);
        }

        const auto external = jaguar::ExternalSPSolutionFromSolution(
            optimized.Value().instance, optimized.Value().solution);
        if (!external.HasValue())
        {
            return CopyOwnedOutput(
                "{\"error\":\"solution conversion failed\"}", {}, 2,
                outputBuffer, outputJsonSize, outputSvgSize);
        }
        const auto json = jaguar::SerializeExternalSPSolutionJson(
            external.Value(), {false});
        if (!json.HasValue())
        {
            return CopyOwnedOutput(
                "{\"error\":\"solution serialization failed\"}", {}, 2,
                outputBuffer, outputJsonSize, outputSvgSize);
        }
        const auto svg = ExportSolutionSvg(
            optimized.Value().solution, "SparrowCore Layout");
        if (!svg.HasValue())
        {
            return CopyOwnedOutput(
                "{\"error\":\"SVG export failed\"}", {}, 2,
                outputBuffer, outputJsonSize, outputSvgSize);
        }
        return CopyOwnedOutput(
            json.Value(), svg.Value(), 0,
            outputBuffer, outputJsonSize, outputSvgSize);
    }
    catch (const std::exception&)
    {
        return CopyOwnedOutput(
            "{\"error\":\"internal error\"}", {}, 2,
            outputBuffer, outputJsonSize, outputSvgSize);
    }
    catch (...)
    {
        return CopyOwnedOutput(
            "{\"error\":\"internal error\"}", {}, 2,
            outputBuffer, outputJsonSize, outputSvgSize);
    }
}

extern "C" SPARROW_WASM_KEEPALIVE void SparrowFreeOutput(char* outputBuffer)
{
    std::free(outputBuffer);
}
