#include "SparrowCore/Optimizer/SparrowOptimizer.h"
#include "SparrowCore/Optimizer/OptimizationTerminator.h"
#include "SparrowCore/Version.h"

#include "OptimizationSignalHandler.h"

#include "JaguarCore/IO/Importer.h"
#include "JaguarCore/IO/JsonCodec.h"
#include "JaguarCore/IO/SvgExport.h"
#include "JaguarCore/Version.h"

#include <charconv>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <optional>
#include <string>

namespace
{
void PrintUsage()
{
    std::cerr << "usage: SparrowCli --input <instance.json> "
                 "--output <solution.json> [--svg <layout.svg>] "
                 "[--seed <u64>] [--samples <n>] [--time-ms <n>] "
                 "[--early-termination]\n";
}

bool ParseUnsigned(const std::string& text, std::uint64_t& value)
{
    if (text.empty())
    {
        return false;
    }
    std::uint64_t parsed = 0U;
    const auto parsedResult = std::from_chars(
        text.data(), text.data() + text.size(), parsed, 10);
    if (parsedResult.ec != std::errc{} || parsedResult.ptr != text.data() + text.size())
    {
        return false;
    }
    value = parsed;
    return true;
}

bool ReadFile(const std::string& path, std::string& content)
{
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open())
    {
        return false;
    }
    content.assign(std::istreambuf_iterator<char>(input),
                   std::istreambuf_iterator<char>());
    return true;
}
}

int main(int argc, char** argv)
{
    std::string inputPath;
    std::string outputPath;
    std::string svgPath;
    std::optional<std::uint64_t> seed;
    std::uint64_t samples = 1000U;
    std::uint64_t timeMs = 0U;
    bool earlyTermination = false;
    for (int index = 1; index < argc; ++index)
    {
        const std::string argument = argv[index];
        if (argument == "--early-termination")
        {
            earlyTermination = true;
            continue;
        }
        if ((argument == "--input" || argument == "--output"
             || argument == "--svg" || argument == "--seed"
             || argument == "--samples" || argument == "--time-ms")
            && index + 1 < argc)
        {
            const std::string value = argv[++index];
            if (argument == "--input")
            {
                inputPath = value;
            }
            else if (argument == "--output")
            {
                outputPath = value;
            }
            else if (argument == "--svg")
            {
                svgPath = value;
            }
            else if (argument == "--seed")
            {
                std::uint64_t parsedSeed = 0U;
                if (!ParseUnsigned(value, parsedSeed))
                {
                    std::cerr << "invalid seed\n";
                    return 2;
                }
                seed = parsedSeed;
            }
            else if (argument == "--samples")
            {
                if (!ParseUnsigned(value, samples) || samples == 0U)
                {
                    std::cerr << "invalid samples\n";
                    return 2;
                }
            }
            else if (!ParseUnsigned(value, timeMs))
            {
                std::cerr << "invalid time-ms\n";
                return 2;
            }
        }
        else
        {
            PrintUsage();
            return 2;
        }
    }
    if (inputPath.empty() || outputPath.empty())
    {
        PrintUsage();
        return 2;
    }

    std::string inputJson;
    if (!ReadFile(inputPath, inputJson))
    {
        std::cerr << "cannot read input: " << inputPath << '\n';
        return 3;
    }
    const jaguar::Importer importer(sparrow::MakeRustDefaultImporterConfig());
    const auto instance = importer.ImportSPInstanceJson(inputJson);
    if (!instance.HasValue())
    {
        std::cerr << "instance import failed: "
                  << instance.GetError().message << '\n';
        return 4;
    }

    sparrow::SparrowConfig config;
    config.seed = seed;
    config.constructor.nContainerSamples = static_cast<std::size_t>(samples);
    if (earlyTermination)
    {
        config.exploration.maxConsecutiveFailures = 10U;
        config.compression.decayStrategy =
            sparrow::ShrinkDecayStrategy::SHRINK_DECAY_FAILURE_BASED;
    }
    if (timeMs > static_cast<std::uint64_t>(
            std::numeric_limits<std::chrono::milliseconds::rep>::max()))
    {
        std::cerr << "time-ms is too large\n";
        return 2;
    }
    config.terminator.maxDuration = std::chrono::milliseconds{
        static_cast<std::chrono::milliseconds::rep>(timeMs)};

    const auto optimized = [&]() {
        sparrow::OptimizationTerminator terminator(config.terminator);
        terminator.Start();
        sparrow::cli::ScopedCtrlCHandler ctrlCHandler(terminator);
        if (!ctrlCHandler.IsInstalled())
        {
            return sparrow::Result<sparrow::SparrowOptimizationResult>::FromError(
                {jaguar::ErrorCode::InvalidInput,
                 "cannot install the Ctrl-C handler"});
        }
        return sparrow::Optimize(instance.Value(), config, terminator);
    }();
    if (!optimized.HasValue())
    {
        std::cerr << "optimization failed: "
                  << optimized.GetError().message << '\n';
        return 5;
    }
    if (seed.has_value())
    {
        std::cerr << "[MAIN] using seed: " << *seed << '\n';
    }
    else
    {
        std::cerr << "[MAIN] no seed provided, using: "
                  << optimized.Value().metrics.seed << '\n';
    }
    const auto external = jaguar::ExternalSPSolutionFromSolution(
        instance.Value(), optimized.Value().solution);
    if (!external.HasValue())
    {
        std::cerr << "solution conversion failed: "
                  << external.GetError().message << '\n';
        return 6;
    }
    const auto outputJson = jaguar::SerializeExternalSPSolutionJson(
        external.Value(), {true});
    if (!outputJson.HasValue())
    {
        std::cerr << "solution serialization failed: "
                  << outputJson.GetError().message << '\n';
        return 7;
    }
    std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
    if (!output.is_open())
    {
        std::cerr << "cannot write output: " << outputPath << '\n';
        return 8;
    }
    output << outputJson.Value();
    if (!output.good())
    {
        std::cerr << "output write failed\n";
        return 8;
    }

    if (!svgPath.empty())
    {
        jaguar::Layout layout(optimized.Value().solution.layoutSnapshot.container);
        const auto restored = layout.Restore(
            optimized.Value().solution.layoutSnapshot);
        if (!restored.HasValue())
        {
            std::cerr << "layout restore failed: "
                      << restored.GetError().message << '\n';
            return 9;
        }
        jaguar::SvgExportOptions svgOptions;
        svgOptions.title = "SparrowCore Layout";
        svgOptions.drawSurrogates = true;
        const auto svg = jaguar::ExportLayoutSvg(layout, svgOptions);
        if (!svg.HasValue())
        {
            std::cerr << "SVG export failed: "
                      << svg.GetError().message << '\n';
            return 9;
        }
        std::ofstream svgOutput(svgPath, std::ios::binary | std::ios::trunc);
        if (!svgOutput.is_open())
        {
            std::cerr << "cannot write SVG: " << svgPath << '\n';
            return 9;
        }
        svgOutput << svg.Value();
        if (!svgOutput.good())
        {
            std::cerr << "SVG write failed\n";
            return 9;
        }
    }
    std::cout << "SparrowCore " << sparrow::Version()
              << " / JaguarCore " << jaguar::Version()
              << ", width=" << optimized.Value().metrics.stripWidth
              << ", density=" << optimized.Value().metrics.density << '\n';
    return 0;
}
