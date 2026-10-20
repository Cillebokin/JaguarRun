#include "SparrowCore/Optimizer/SparrowOptimizer.h"
#include "SparrowCore/Version.h"

#include "JaguarCore/Entities/Item.h"
#include "JaguarCore/IO/Importer.h"
#include "JaguarCore/IO/JsonCodec.h"
#include "JaguarCore/IO/SvgExport.h"
#include "JaguarCore/Problems/Spp.h"
#include "JaguarCore/Version.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <numeric>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{
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

std::string FindFixture()
{
    const std::vector<std::string> paths = {
        "SparrowCore/Tests/Fixtures/LbfSppSmall.json",
        "../SparrowCore/Tests/Fixtures/LbfSppSmall.json",
        "../JaguarRun/SparrowCore/Tests/Fixtures/LbfSppSmall.json"};
    for (const std::string& path : paths)
    {
        std::string content;
        if (ReadFile(path, content))
        {
            return path;
        }
    }
    return {};
}

void HashBytes(std::uint64_t& hash,
               const void* data,
               std::size_t size) noexcept
{
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t index = 0U; index < size; ++index)
    {
        hash ^= static_cast<std::uint64_t>(bytes[index]);
        hash *= 1099511628211ULL;
    }
}

template <typename T>
void HashValue(std::uint64_t& hash, const T& value) noexcept
{
    HashBytes(hash, &value, sizeof(value));
}

std::uint64_t SolutionChecksum(const jaguar::SPSolution& solution) noexcept
{
    std::uint64_t hash = 1469598103934665603ULL;
    HashValue(hash, solution.strip.width);
    for (const jaguar::LayoutPlacementSnapshot& placement
         : solution.layoutSnapshot.placements)
    {
        HashValue(hash, placement.placedItem.GetItemIndex());
        HashValue(hash, placement.handle.index);
        HashValue(hash, placement.handle.generation);
        HashValue(hash, placement.placedItem.Transform().rotationRad);
        HashValue(hash, placement.placedItem.Transform().translation.x);
        HashValue(hash, placement.placedItem.Transform().translation.y);
    }
    return hash;
}

jaguar::Result<jaguar::SPInstance> MakeLargeInstance()
{
    std::vector<jaguar::Item> items;
    std::vector<std::size_t> demands;
    items.reserve(8U);
    demands.reserve(8U);
    for (std::size_t index = 0U; index < 8U; ++index)
    {
        const double width = 0.8 + static_cast<double>(index % 4U) * 0.2;
        const double height = 0.9 + static_cast<double>(index % 3U) * 0.15;
        const auto polygon = jaguar::SimplePolygon::Create({
            {0.0, 0.0}, {width, 0.0}, {width, height}, {0.0, height}});
        if (!polygon.HasValue())
        {
            return jaguar::Result<jaguar::SPInstance>::FromError(
                polygon.GetError());
        }
        jaguar::SurrogateConfig surrogateConfig{};
        surrogateConfig.poleLimits = {{{3U, 0.0}, {0U, 0.0}, {0U, 0.0}}};
        const auto item = jaguar::Item::Create(
            index,
            static_cast<jaguar::ItemId>(10000U + index),
            {polygon.Value(),
             jaguar::Transform2d::Identity(),
             jaguar::ShapeModifyMode::Inflate,
             {}},
            surrogateConfig);
        if (!item.HasValue())
        {
            return jaguar::Result<jaguar::SPInstance>::FromError(
                item.GetError());
        }
        items.push_back(item.Value());
        demands.push_back(2U + index % 2U);
    }
    return jaguar::SPInstance::Create(
        std::move(items),
        std::move(demands),
        8.0,
        jaguar::CDEConfig{},
        jaguar::ShapeModifyConfig{});
}

bool RunBenchmark(const std::string& name,
                  const jaguar::SPInstance& instance)
{
    sparrow::SparrowConfig config;
    config.seed = 42U;
    config.constructor.seed = 42U;
    const bool isLarge = name == "lbf_spp_large";
    config.constructor.nContainerSamples = isLarge ? 48U : 128U;
    config.constructor.nFocussedSamples = isLarge ? 12U : 32U;
    config.exploration.maxIterations = isLarge ? 1U : 2U;
    config.exploration.maxConsecutiveFailures = 3U;
    config.exploration.timeLimit = std::chrono::milliseconds{0};
    config.exploration.separator.strikeLimit = 2U;
    config.exploration.separator.noImprovementLimit = 6U;
    config.compression.maxIterations = isLarge ? 1U : 2U;
    config.compression.maxConsecutiveFailures = 3U;
    config.compression.timeLimit = std::chrono::milliseconds{0};
    config.compression.decayStrategy =
        sparrow::ShrinkDecayStrategy::SHRINK_DECAY_FAILURE_BASED;
    config.compression.separator.strikeLimit = 2U;
    config.compression.separator.noImprovementLimit = 6U;
    config.terminator.maxIterations = isLarge ? 512U : 256U;
    const auto start = std::chrono::steady_clock::now();
    const auto result = sparrow::Optimize(instance, config);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    if (!result.HasValue())
    {
        std::cerr << "benchmark optimization failed (" << name << "): "
                  << result.GetError().message << '\n';
        return false;
    }
    jaguar::Layout layout(result.Value().solution.layoutSnapshot.container);
    const auto restored = layout.Restore(result.Value().solution.layoutSnapshot);
    if (!restored.HasValue())
    {
        std::cerr << "benchmark layout restore failed (" << name << ")\n";
        return false;
    }
    const double validSampleRate = result.Value().metrics.feasible ? 1.0 : 0.0;
    const std::size_t exactCollisionLayout = layout.IsFeasible() ? 0U : 1U;
    const std::uint64_t checksum = SolutionChecksum(result.Value().solution);
    std::cout << "sparrowcore," << sparrow::Version()
              << "," << name << "," << result.Value().metrics.seed
              << "," << result.Value().metrics.sampleCount
              << "," << result.Value().metrics.placedItemCount
              << "," << result.Value().metrics.remainingDemand
              << "," << result.Value().metrics.stripWidth
              << "," << result.Value().metrics.density
              << "," << elapsed.count()
              << "," << validSampleRate
              << "," << result.Value().metrics.surrogateRejectCount
              << "," << result.Value().metrics.exactCollisionCount
              << "," << exactCollisionLayout
              << "," << checksum << '\n';
    return true;
}

bool ParseUnsigned(const std::string& text, std::uint64_t& value)
{
    if (text.empty())
    {
        return false;
    }
    std::uint64_t parsed = 0U;
    const auto result = std::from_chars(
        text.data(), text.data() + text.size(), parsed, 10);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
    {
        return false;
    }
    value = parsed;
    return true;
}

std::uint64_t GenerateRunSeed()
{
    std::random_device source;
    std::uniform_int_distribution<std::uint64_t> distribution;
    return distribution(source);
}

bool WriteLayoutSvg(const jaguar::SPSolution& solution,
                    const std::filesystem::path& path,
                    const std::string& title)
{
    jaguar::Layout layout(solution.layoutSnapshot.container);
    const auto restored = layout.Restore(solution.layoutSnapshot);
    if (!restored.HasValue())
    {
        std::cerr << "benchmark layout restore failed: "
                  << restored.GetError().message << '\n';
        return false;
    }
    jaguar::SvgExportOptions options;
    options.theme = jaguar::SvgLayoutTheme::Gray();
    options.title = title;
    options.drawSurrogates = false;
    options.highlightCollisions = true;
    options.includeAxes = false;
    const auto svg = jaguar::ExportLayoutSvg(layout, options);
    if (!svg.HasValue())
    {
        std::cerr << "benchmark SVG export failed: "
                  << svg.GetError().message << '\n';
        return false;
    }
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error)
    {
        std::cerr << "benchmark output directory creation failed: "
                  << error.message() << '\n';
        return false;
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.is_open())
    {
        std::cerr << "benchmark SVG cannot be written: " << path.string() << '\n';
        return false;
    }
    output << svg.Value();
    if (!output.good())
    {
        std::cerr << "benchmark SVG write failed: " << path.string() << '\n';
        return false;
    }
    return true;
}

float CalculatePercentile(const std::vector<float>& values, float percentile)
{
    std::vector<float> sorted = values;
    std::sort(sorted.begin(), sorted.end());
    const float rank = percentile * static_cast<float>(sorted.size() - 1U) + 1.0F;
    const auto lowerIndex = static_cast<std::size_t>(std::floor(rank)) - 1U;
    const auto upperIndex = static_cast<std::size_t>(std::ceil(rank)) - 1U;
    const float fraction = rank - std::floor(rank);
    return sorted[lowerIndex]
        + fraction * (sorted[upperIndex] - sorted[lowerIndex]);
}

float CalculateAverage(const std::vector<float>& values)
{
    return std::accumulate(values.begin(), values.end(), 0.0F)
        / static_cast<float>(values.size());
}

float CalculateStddev(const std::vector<float>& values)
{
    const float average = CalculateAverage(values);
    float sumSquares = 0.0F;
    for (const float value : values)
    {
        const float difference = value - average;
        sumSquares += difference * difference;
    }
    return std::sqrt(sumSquares / static_cast<float>(values.size()));
}

void PrintValues(const char* label, const std::vector<float>& values)
{
    std::cout << label << ": [";
    for (std::size_t index = 0U; index < values.size(); ++index)
    {
        if (index > 0U)
        {
            std::cout << ", ";
        }
        std::cout << values[index];
    }
    std::cout << "]\n";
}

struct StatisticalRun
{
    std::uint64_t seed = 0U;
    std::chrono::milliseconds elapsed{0};
    float width = 0.0F;
    float usage = 0.0F;
    double density = 0.0;
    std::optional<jaguar::SPSolution> solution;
};

struct StatisticalRunOutcome
{
    std::optional<StatisticalRun> run;
    std::string error;
};

bool RunStatisticalBenchmark(const std::string& inputPath,
                             std::uint64_t totalTimeSeconds,
                             std::size_t numberOfRuns)
{
    std::string inputJson;
    if (!ReadFile(inputPath, inputJson))
    {
        std::cerr << "benchmark input cannot be read: " << inputPath << '\n';
        return false;
    }
    const auto externalOutput = jaguar::ParseExternalSPOutputJson(inputJson);
    const auto externalInstance = externalOutput.HasValue()
        ? jaguar::Result<jaguar::ExternalSPInstance>::FromValue(
              externalOutput.Value().instance)
        : jaguar::ParseExternalSPInstanceJson(inputJson);
    if (!externalInstance.HasValue())
    {
        std::cerr << "benchmark input import failed: "
                  << externalInstance.GetError().message << '\n';
        return false;
    }
    const jaguar::Importer importer(sparrow::MakeRustDefaultImporterConfig());
    const auto instance = importer.ImportSPInstance(externalInstance.Value());
    if (!instance.HasValue())
    {
        std::cerr << "benchmark instance conversion failed: "
                  << instance.GetError().message << '\n';
        return false;
    }

    constexpr std::uint64_t nanosecondsPerSecond = 1000000000ULL;
    const auto maxSeconds = static_cast<std::uint64_t>(
        std::numeric_limits<std::chrono::nanoseconds::rep>::max())
        / nanosecondsPerSecond;
    if (totalTimeSeconds > maxSeconds)
    {
        std::cerr << "benchmark time limit is too large\n";
        return false;
    }
    const auto totalDuration = std::chrono::nanoseconds{
        static_cast<std::chrono::nanoseconds::rep>(
            totalTimeSeconds * nanosecondsPerSecond)};
    const auto exploreDuration = std::chrono::nanoseconds{
        totalDuration.count() / 5 * 4};
    const auto compressDuration = std::chrono::nanoseconds{
        totalDuration.count() / 5};

    sparrow::SparrowConfig baseConfig;
    baseConfig.exploration.timeLimit = exploreDuration;
    baseConfig.compression.timeLimit = compressDuration;
    const std::size_t workerCount = std::max<std::size_t>(
        1U, baseConfig.exploration.separator.nWorkers);
    const unsigned int reportedHardwareThreads = std::thread::hardware_concurrency();
    const std::size_t hardwareThreads = reportedHardwareThreads == 0U
        ? 1U : static_cast<std::size_t>(reportedHardwareThreads);
    const std::size_t runsPerBatch = std::min(
        numberOfRuns, std::max<std::size_t>(1U, hardwareThreads / workerCount));
    const std::size_t numberOfBatches =
        numberOfRuns / runsPerBatch + (numberOfRuns % runsPerBatch == 0U ? 0U : 1U);

    std::error_code directoryError;
    std::filesystem::create_directories("output", directoryError);
    if (directoryError)
    {
        std::cerr << "benchmark output directory creation failed: "
                  << directoryError.message() << '\n';
        return false;
    }
    std::cout << "[BENCH] SparrowCore " << sparrow::Version()
              << " / JaguarCore " << jaguar::Version() << '\n'
              << "[BENCH] starting " << numberOfRuns << " run(s) for "
              << externalInstance.Value().name << ", " << totalTimeSeconds
              << "s total split 80/20, batch size " << runsPerBatch
              << " (std::thread::hardware_concurrency)\n";

    std::vector<StatisticalRun> runs;
    runs.reserve(numberOfRuns);
    for (std::size_t batchStart = 0U; batchStart < numberOfRuns;)
    {
        // The pinned Rust Bench creates a full result slot vector for every
        // batch. Its final batch therefore runs the complete batch size even
        // when fewer requested runs remain.
        const std::size_t batchCount = runsPerBatch;
        std::cout << "[BENCH] batch " << (batchStart / runsPerBatch + 1U)
                  << '/' << numberOfBatches << '\n';
        std::vector<std::future<StatisticalRunOutcome>> futures;
        futures.reserve(batchCount);
        for (std::size_t offset = 0U; offset < batchCount; ++offset)
        {
            const std::uint64_t seed = GenerateRunSeed();
            sparrow::SparrowConfig runConfig = baseConfig;
            runConfig.seed = seed;
            futures.push_back(std::async(std::launch::async,
                [&instance, runConfig = std::move(runConfig), seed]() mutable {
                    const auto start = std::chrono::steady_clock::now();
                    const auto optimized = sparrow::Optimize(
                        instance.Value(), runConfig);
                    const auto elapsed = std::chrono::duration_cast<
                        std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - start);
                    if (!optimized.HasValue())
                    {
                        return StatisticalRunOutcome{
                            std::nullopt, optimized.GetError().message};
                    }
                    if (!std::isfinite(optimized.Value().metrics.stripWidth)
                        || !std::isfinite(optimized.Value().metrics.density))
                    {
                        return StatisticalRunOutcome{
                            std::nullopt,
                            "optimization returned non-finite width or density"};
                    }
                    StatisticalRun run;
                    run.seed = seed;
                    run.elapsed = elapsed;
                    run.width = static_cast<float>(
                        optimized.Value().metrics.stripWidth);
                    run.usage = static_cast<float>(
                        optimized.Value().metrics.density * 100.0);
                    run.density = optimized.Value().metrics.density;
                    run.solution.emplace(
                        std::move(optimized.Value().solution));
                    return StatisticalRunOutcome{std::move(run), {}};
                }));
        }
        for (std::size_t offset = 0U; offset < futures.size(); ++offset)
        {
            auto outcome = futures[offset].get();
            if (!outcome.run.has_value())
            {
                std::cerr << "benchmark optimization failed (run "
                          << (batchStart + offset) << "): " << outcome.error << '\n';
                return false;
            }
            auto run = std::move(*outcome.run);
            std::cout << "[BENCH] [id:" << (batchStart + offset)
                      << "] seed=" << run.seed << " width=" << run.width
                      << " usage=" << run.usage << " elapsed_ms="
                      << run.elapsed.count() << '\n';
            if (!WriteLayoutSvg(*run.solution,
                    std::filesystem::path("output")
                        / ("final_bench_" + std::to_string(batchStart + offset) + ".svg"),
                    "final_bench_" + std::to_string(batchStart + offset)))
            {
                return false;
            }
            runs.push_back(std::move(run));
        }
        batchStart += batchCount;
    }

    std::size_t bestIndex = 0U;
    for (std::size_t index = 1U; index < runs.size(); ++index)
    {
        if (runs[index].density >= runs[bestIndex].density)
        {
            bestIndex = index;
        }
    }
    const std::string instanceName = externalInstance.Value().name;
    const auto bestPath = std::filesystem::path("output")
        / ("final_best_" + instanceName + ".svg");
    if (!WriteLayoutSvg(*runs[bestIndex].solution, bestPath,
                        "final_best_" + instanceName))
    {
        return false;
    }

    std::vector<float> widths;
    std::vector<float> usages;
    widths.reserve(runs.size());
    usages.reserve(runs.size());
    for (const StatisticalRun& run : runs)
    {
        widths.push_back(run.width);
        usages.push_back(run.usage);
    }
    std::cout << "==== BENCH FINISHED ====\n"
              << "requested_runs: " << numberOfRuns << '\n'
              << "actual_runs: " << runs.size() << '\n';
    PrintValues("widths", widths);
    PrintValues("usages", usages);
    std::cout << std::fixed << std::setprecision(3)
              << "---- WIDTH STATS ----\n"
              << "worst:  " << *std::max_element(widths.begin(), widths.end()) << '\n'
              << "25%:    " << CalculatePercentile(widths, 0.75F) << '\n'
              << "med:    " << CalculatePercentile(widths, 0.5F) << '\n'
              << "75%:    " << CalculatePercentile(widths, 0.25F) << '\n'
              << "best:   " << *std::min_element(widths.begin(), widths.end()) << '\n'
              << "avg:    " << CalculateAverage(widths) << '\n'
              << "stddev: " << CalculateStddev(widths) << '\n'
              << "---- USAGE STATS ----\n"
              << "worst:  " << *std::min_element(usages.begin(), usages.end()) << '\n'
              << "25%:    " << CalculatePercentile(usages, 0.25F) << '\n'
              << "median: " << CalculatePercentile(usages, 0.5F) << '\n'
              << "75%:    " << CalculatePercentile(usages, 0.75F) << '\n'
              << "best:   " << *std::max_element(usages.begin(), usages.end()) << '\n'
              << "avg:    " << CalculateAverage(usages) << '\n'
              << "stddev: " << CalculateStddev(usages) << '\n'
              << "======================\n"
              << "[BENCH] best SVG: " << bestPath.string() << '\n';
    return true;
}
}

int main(int argc, char** argv)
{
    if (argc == 2 && (std::string(argv[1]) == "-h"
                      || std::string(argv[1]) == "--help"))
    {
        std::cout << "usage: SparrowCoreBench all\n"
                     "       SparrowCoreBench <input.json> <time-limit-seconds> <runs>\n";
        return 0;
    }
    if (argc == 4)
    {
        std::uint64_t timeLimitSeconds = 0U;
        std::uint64_t numberOfRuns = 0U;
        constexpr std::uint64_t nanosecondsPerSecond = 1000000000ULL;
        const auto maxTimeSeconds = static_cast<std::uint64_t>(
            std::numeric_limits<std::chrono::nanoseconds::rep>::max())
            / nanosecondsPerSecond;
        if (!ParseUnsigned(argv[2], timeLimitSeconds)
            || !ParseUnsigned(argv[3], numberOfRuns)
            || timeLimitSeconds > maxTimeSeconds
            || numberOfRuns == 0U
            || numberOfRuns > static_cast<std::uint64_t>(
                std::numeric_limits<std::size_t>::max()))
        {
            std::cerr << "invalid time limit or run count\n";
            return 2;
        }
        if (!RunStatisticalBenchmark(
                argv[1], timeLimitSeconds,
                static_cast<std::size_t>(numberOfRuns)))
        {
            return 3;
        }
        return 0;
    }
    if (argc != 2 || std::string(argv[1]) != "all")
    {
        std::cerr << "usage: SparrowCoreBench all\n"
                     "       SparrowCoreBench <input.json> <time-limit-seconds> <runs>\n";
        return 2;
    }
    const std::string fixture = FindFixture();
    if (fixture.empty())
    {
        std::cerr << "benchmark fixture cannot be found\n";
        return 3;
    }
    std::string inputJson;
    if (!ReadFile(fixture, inputJson))
    {
        return 3;
    }
    const jaguar::Importer importer(sparrow::MakeRustDefaultImporterConfig());
    const auto instance = importer.ImportSPInstanceJson(inputJson);
    if (!instance.HasValue())
    {
        std::cerr << "benchmark fixture import failed\n";
        return 4;
    }
    if (!RunBenchmark("lbf_spp_small", instance.Value()))
    {
        return 5;
    }
    const auto largeInstance = MakeLargeInstance();
    if (!largeInstance.HasValue())
    {
        std::cerr << "large benchmark fixture construction failed: "
                  << largeInstance.GetError().message << '\n';
        return 6;
    }
    return RunBenchmark("lbf_spp_large", largeInstance.Value()) ? 0 : 7;
}
