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
#include <exception>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

namespace
{
constexpr std::uint64_t kNanosecondsPerSecond = 1000000000ULL;

void PrintUsage(std::ostream& output)
{
    output << "usage: SparrowCli -i|--input <instance-or-output.json> [options]\n"
              "  default outputs: output/final_<instance-name>.json and .svg\n"
              "  --output <solution.json> and --svg <layout.svg> override those paths\n"
              "time options (choose one):\n"
              "  -t, --global-time <seconds>  Split total time 80% exploration / 20% compression\n"
              "  -e, --exploration <seconds> -c, --compression <seconds>\n"
              "  --time-ms <milliseconds>     C++ global hard-time-limit extension\n"
              "other options:\n"
              "  --svg <layout.svg>  -s, --rng-seed <u64> (alias: --seed <u64>)\n"
              "  --workers <n>  --samples <n>  -x, --early-termination  -h, --help\n"
              "With no time option, phase limits default to 480s exploration and 120s compression.\n";
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

bool EnsureParentDirectory(const std::string& filePath)
{
    const std::filesystem::path parent =
        std::filesystem::path(filePath).parent_path();
    if (parent.empty())
    {
        return true;
    }
    std::error_code error;
    std::filesystem::create_directories(parent, error);
    return !error;
}

bool SecondsToNanoseconds(std::uint64_t seconds,
                          std::chrono::nanoseconds& duration)
{
    const auto maxSeconds = static_cast<std::uint64_t>(
        std::numeric_limits<std::chrono::nanoseconds::rep>::max())
        / kNanosecondsPerSecond;
    if (seconds > maxSeconds)
    {
        return false;
    }
    duration = std::chrono::nanoseconds{
        static_cast<std::chrono::nanoseconds::rep>(
            seconds * kNanosecondsPerSecond)};
    return true;
}

std::chrono::nanoseconds ScaleNanoseconds(
    std::chrono::nanoseconds duration,
    std::uint64_t numerator,
    std::uint64_t denominator)
{
    const auto count = static_cast<std::uint64_t>(duration.count());
    const auto scaled = (count / denominator) * numerator
        + ((count % denominator) * numerator) / denominator;
    return std::chrono::nanoseconds{
        static_cast<std::chrono::nanoseconds::rep>(scaled)};
}
}

int RunCli(int argc, char** argv)
{
    const auto epoch = std::chrono::steady_clock::now();
    std::string inputPath;
    std::string outputPath;
    std::string svgPath;
    std::optional<std::uint64_t> seed;
    std::uint64_t samples = 1000U;
    std::uint64_t timeMs = 0U;
    std::optional<std::uint64_t> globalTimeSeconds;
    std::optional<std::uint64_t> explorationSeconds;
    std::optional<std::uint64_t> compressionSeconds;
    std::optional<std::uint64_t> workers;
    bool inputSpecified = false;
    bool outputSpecified = false;
    bool svgSpecified = false;
    bool seedSpecified = false;
    bool samplesSpecified = false;
    bool timeMsSpecified = false;
    bool globalTimeSpecified = false;
    bool explorationSpecified = false;
    bool compressionSpecified = false;
    bool workersSpecified = false;
    bool earlyTermination = false;
    for (int index = 1; index < argc; ++index)
    {
        const std::string argument = argv[index];
        if (argument == "-h" || argument == "--help")
        {
            PrintUsage(std::cout);
            return 0;
        }
        if (argument == "-x" || argument == "--early-termination")
        {
            if (earlyTermination)
            {
                std::cerr << "duplicate early-termination option\n";
                return 2;
            }
            earlyTermination = true;
            continue;
        }
        const bool inputOption = argument == "-i" || argument == "--input";
        const bool outputOption = argument == "--output";
        const bool svgOption = argument == "--svg";
        const bool seedOption = argument == "-s" || argument == "--rng-seed"
            || argument == "--seed";
        const bool samplesOption = argument == "--samples";
        const bool timeMsOption = argument == "--time-ms";
        const bool globalTimeOption = argument == "-t" || argument == "--global-time";
        const bool explorationOption = argument == "-e" || argument == "--exploration";
        const bool compressionOption = argument == "-c" || argument == "--compression";
        const bool workersOption = argument == "--workers";
        if (inputOption || outputOption || svgOption || seedOption || samplesOption
            || timeMsOption || globalTimeOption || explorationOption
            || compressionOption || workersOption)
        {
            if (index + 1 >= argc)
            {
                std::cerr << "missing value for option: " << argument << '\n';
                PrintUsage(std::cerr);
                return 2;
            }
            const std::string value = argv[++index];
            if (inputOption)
            {
                if (inputSpecified)
                {
                    std::cerr << "duplicate input option\n";
                    return 2;
                }
                inputSpecified = true;
                inputPath = value;
            }
            else if (outputOption)
            {
                if (outputSpecified)
                {
                    std::cerr << "duplicate output option\n";
                    return 2;
                }
                outputSpecified = true;
                outputPath = value;
            }
            else if (svgOption)
            {
                if (svgSpecified)
                {
                    std::cerr << "duplicate SVG option\n";
                    return 2;
                }
                svgSpecified = true;
                svgPath = value;
            }
            else if (seedOption)
            {
                if (seedSpecified)
                {
                    std::cerr << "duplicate seed option\n";
                    return 2;
                }
                seedSpecified = true;
                std::uint64_t parsedSeed = 0U;
                if (!ParseUnsigned(value, parsedSeed))
                {
                    std::cerr << "invalid seed\n";
                    return 2;
                }
                seed = parsedSeed;
            }
            else if (samplesOption)
            {
                if (samplesSpecified)
                {
                    std::cerr << "duplicate samples option\n";
                    return 2;
                }
                samplesSpecified = true;
                if (!ParseUnsigned(value, samples) || samples == 0U)
                {
                    std::cerr << "invalid samples\n";
                    return 2;
                }
            }
            else if (timeMsOption)
            {
                if (timeMsSpecified)
                {
                    std::cerr << "duplicate time-ms option\n";
                    return 2;
                }
                timeMsSpecified = true;
                if (!ParseUnsigned(value, timeMs))
                {
                    std::cerr << "invalid time-ms\n";
                    return 2;
                }
            }
            else if (globalTimeOption || explorationOption || compressionOption
                     || workersOption)
            {
                std::uint64_t parsedValue = 0U;
                if (!ParseUnsigned(value, parsedValue))
                {
                    std::cerr << "invalid numeric value for option: "
                              << argument << '\n';
                    return 2;
                }
                if (globalTimeOption)
                {
                    if (globalTimeSpecified)
                    {
                        std::cerr << "duplicate global-time option\n";
                        return 2;
                    }
                    globalTimeSpecified = true;
                    globalTimeSeconds = parsedValue;
                }
                else if (explorationOption)
                {
                    if (explorationSpecified)
                    {
                        std::cerr << "duplicate exploration option\n";
                        return 2;
                    }
                    explorationSpecified = true;
                    explorationSeconds = parsedValue;
                }
                else if (compressionOption)
                {
                    if (compressionSpecified)
                    {
                        std::cerr << "duplicate compression option\n";
                        return 2;
                    }
                    compressionSpecified = true;
                    compressionSeconds = parsedValue;
                }
                else
                {
                    if (workersSpecified || parsedValue == 0U
                        || parsedValue > static_cast<std::uint64_t>(
                            std::numeric_limits<std::size_t>::max()))
                    {
                        std::cerr << "invalid workers\n";
                        return 2;
                    }
                    workersSpecified = true;
                    workers = parsedValue;
                }
            }
        }
        else
        {
            std::cerr << "unknown option: " << argument << '\n';
            PrintUsage(std::cerr);
            return 2;
        }
    }
    if (!inputSpecified || inputPath.empty()
        || (outputSpecified && outputPath.empty())
        || (svgSpecified && svgPath.empty()))
    {
        PrintUsage(std::cerr);
        return 2;
    }
    if ((globalTimeSpecified && (explorationSpecified || compressionSpecified))
        || (explorationSpecified != compressionSpecified)
        || (timeMsSpecified
            && (globalTimeSpecified || explorationSpecified || compressionSpecified)))
    {
        std::cerr << "time options are mutually exclusive; -e and -c must be used together\n";
        return 2;
    }

    std::string inputJson;
    if (!ReadFile(inputPath, inputJson))
    {
        std::cerr << "cannot read input: " << inputPath << '\n';
        return 3;
    }
    const jaguar::Importer importer(sparrow::MakeRustDefaultImporterConfig());
    const auto externalOutput = jaguar::ParseExternalSPOutputJson(inputJson);
    const auto externalInstance = externalOutput.HasValue()
        ? jaguar::Result<jaguar::ExternalSPInstance>::FromValue(
              externalOutput.Value().instance)
        : jaguar::ParseExternalSPInstanceJson(inputJson);
    if (!externalInstance.HasValue())
    {
        std::cerr << "instance import failed: "
                  << externalInstance.GetError().message << '\n';
        return 4;
    }
    const auto instance = importer.ImportSPInstance(externalInstance.Value());
    if (!instance.HasValue())
    {
        std::cerr << "instance import failed: "
                  << instance.GetError().message << '\n';
        return 4;
    }

    const std::string instanceName = externalInstance.Value().name;
    if (outputPath.empty())
    {
        outputPath = (std::filesystem::path("output")
            / ("final_" + instanceName + ".json")).string();
    }
    if (svgPath.empty())
    {
        svgPath = (std::filesystem::path("output")
            / ("final_" + instanceName + ".svg")).string();
    }

    std::optional<jaguar::SPSolution> warmStart;
    if (externalOutput.HasValue())
    {
        const auto importedSolution = importer.ImportSPSolution(
            instance.Value(), externalOutput.Value().solution);
        if (!importedSolution.HasValue())
        {
            std::cerr << "warm-start import failed: "
                      << importedSolution.GetError().message << '\n';
            return 4;
        }
        warmStart = importedSolution.Value();
    }

    sparrow::SparrowConfig config;
    config.warmStart = std::move(warmStart);
    config.seed = seed;
    config.constructor.nContainerSamples = static_cast<std::size_t>(samples);
    if (workers.has_value())
    {
        config.exploration.separator.nWorkers = static_cast<std::size_t>(*workers);
        config.compression.separator.nWorkers = static_cast<std::size_t>(*workers);
    }
    if (globalTimeSeconds.has_value())
    {
        std::chrono::nanoseconds total;
        if (!SecondsToNanoseconds(*globalTimeSeconds, total))
        {
            std::cerr << "global-time is too large\n";
            return 2;
        }
        config.exploration.timeLimit = ScaleNanoseconds(total, 4U, 5U);
        config.compression.timeLimit = ScaleNanoseconds(total, 1U, 5U);
    }
    else if (explorationSeconds.has_value())
    {
        if (!SecondsToNanoseconds(*explorationSeconds, config.exploration.timeLimit)
            || !SecondsToNanoseconds(*compressionSeconds, config.compression.timeLimit))
        {
            std::cerr << "phase time limit is too large\n";
            return 2;
        }
    }
    else if (!timeMsSpecified)
    {
        std::chrono::nanoseconds total;
        if (!SecondsToNanoseconds(600U, total))
        {
            std::cerr << "default phase time limit is not representable\n";
            return 2;
        }
        config.exploration.timeLimit = ScaleNanoseconds(total, 4U, 5U);
        config.compression.timeLimit = ScaleNanoseconds(total, 1U, 5U);
    }
    if (earlyTermination)
    {
        config.exploration.maxConsecutiveFailures = 10U;
        config.compression.decayStrategy =
            sparrow::ShrinkDecayStrategy::SHRINK_DECAY_FAILURE_BASED;
    }
    const auto maxTimeMs = static_cast<std::uint64_t>(
        std::numeric_limits<std::chrono::nanoseconds::rep>::max())
        / 1000000ULL;
    if (timeMs > maxTimeMs)
    {
        std::cerr << "time-ms is too large\n";
        return 2;
    }
    if (timeMsSpecified)
    {
        config.terminator.maxDuration = std::chrono::nanoseconds{
            static_cast<std::chrono::nanoseconds::rep>(timeMs * 1000000ULL)};
    }

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
    const auto externalSolution = jaguar::ExternalSPSolutionFromSolution(
        instance.Value(), optimized.Value().solution,
        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - epoch).count()));
    if (!externalSolution.HasValue())
    {
        std::cerr << "solution conversion failed: "
                  << externalSolution.GetError().message << '\n';
        return 6;
    }
    const jaguar::ExternalSPOutput externalOutputResult{
        externalInstance.Value(), externalSolution.Value()};
    const auto outputJson = jaguar::SerializeExternalSPOutputJson(
        externalOutputResult, {true});
    if (!outputJson.HasValue())
    {
        std::cerr << "solution serialization failed: "
                  << outputJson.GetError().message << '\n';
        return 7;
    }
    if (!EnsureParentDirectory(outputPath))
    {
        std::cerr << "cannot create output directory: " << outputPath << '\n';
        return 8;
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
        svgOptions.theme = jaguar::SvgLayoutTheme::Gray();
        svgOptions.title = std::filesystem::path(svgPath).stem().string();
        svgOptions.drawSurrogates = false;
        svgOptions.highlightCollisions = true;
        svgOptions.includeAxes = false;
        const auto svg = jaguar::ExportLayoutSvg(layout, svgOptions);
        if (!svg.HasValue())
        {
            std::cerr << "SVG export failed: "
                      << svg.GetError().message << '\n';
            return 9;
        }
        if (!EnsureParentDirectory(svgPath))
        {
            std::cerr << "cannot create SVG output directory: " << svgPath << '\n';
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

// Exit code 10 is reserved for exceptions that escape the library layer. They
// are reported on stderr instead of terminating the process, so the exit code
// is always defined. Codes 2-9 keep their documented meanings.
int main(int argc, char** argv)
{
    try
    {
        return RunCli(argc, argv);
    }
    catch (const std::exception& error)
    {
        std::cerr << "internal error: " << error.what() << '\n';
        return 10;
    }
    catch (...)
    {
        std::cerr << "internal error: unknown exception\n";
        return 10;
    }
}
