#include "SparrowCore/Optimizer/SparrowOptimizer.h"
#include "SparrowCore/Version.h"

#include "JaguarCore/IO/Importer.h"

#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
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
    const jaguar::Importer importer;
    const auto instance = importer.ImportSPInstanceJson(inputJson);
    if (!instance.HasValue())
    {
        std::cerr << "benchmark fixture import failed\n";
        return 4;
    }
    sparrow::SparrowConfig config;
    config.seed = 42U;
    config.constructor.seed = 42U;
    config.constructor.nContainerSamples = 128U;
    config.constructor.nFocussedSamples = 32U;
    config.exploration.maxIterations = 2U;
    config.exploration.maxConsecutiveFailures = 2U;
    config.compression.maxIterations = 2U;
    config.compression.maxConsecutiveFailures = 2U;
    const auto start = std::chrono::steady_clock::now();
    const auto result = sparrow::Optimize(instance.Value(), config);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    if (!result.HasValue())
    {
        std::cerr << "benchmark optimization failed: "
                  << result.GetError().message << '\n';
        return 5;
    }
    std::cout << "sparrowcore," << sparrow::Version()
              << ",lbf_spp_small," << result.Value().metrics.seed
              << "," << result.Value().metrics.sampleCount
              << "," << result.Value().metrics.placedItemCount
              << "," << result.Value().metrics.remainingDemand
              << "," << result.Value().metrics.stripWidth
              << "," << result.Value().metrics.density
              << "," << elapsed.count() << '\n';
    return 0;
}
