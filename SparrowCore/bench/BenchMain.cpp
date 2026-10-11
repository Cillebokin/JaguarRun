#include "SparrowCore/Optimizer/SparrowOptimizer.h"
#include "SparrowCore/Version.h"

#include "JaguarCore/Entities/Item.h"
#include "JaguarCore/IO/Importer.h"
#include "JaguarCore/Problems/Spp.h"

#include <chrono>
#include <cstring>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
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
}

int main(int argc, char** argv)
{
    if (argc != 2 || std::string(argv[1]) != "all")
    {
        std::cerr << "usage: SparrowCoreBench all\n";
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
