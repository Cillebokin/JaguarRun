#include "SparrowCore/Optimizer/SparrowOptimizer.h"
#include "SparrowCore/Version.h"

#include "JaguarCore/IO/Importer.h"
#include "JaguarCore/IO/JsonCodec.h"
#include "JaguarCore/Version.h"

#include <charconv>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace
{
void PrintUsage()
{
    std::cerr << "usage: SparrowCli --input <instance.json> "
                 "--output <solution.json> [--seed <u64>] [--samples <n>]\n";
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
    std::uint64_t seed = 0U;
    std::uint64_t samples = 1000U;
    for (int index = 1; index < argc; ++index)
    {
        const std::string argument = argv[index];
        if ((argument == "--input" || argument == "--output"
             || argument == "--seed" || argument == "--samples")
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
            else if (argument == "--seed")
            {
                if (!ParseUnsigned(value, seed))
                {
                    std::cerr << "invalid seed\n";
                    return 2;
                }
            }
            else if (!ParseUnsigned(value, samples) || samples == 0U)
            {
                std::cerr << "invalid samples\n";
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
    const jaguar::Importer importer;
    const auto instance = importer.ImportSPInstanceJson(inputJson);
    if (!instance.HasValue())
    {
        std::cerr << "instance import failed: "
                  << instance.GetError().message << '\n';
        return 4;
    }

    sparrow::SparrowConfig config;
    config.seed = seed;
    config.constructor.seed = seed;
    config.constructor.nContainerSamples = static_cast<std::size_t>(samples);
    config.constructor.nFocussedSamples = static_cast<std::size_t>(samples / 4U);
    config.exploration.maxIterations = 16U;
    config.exploration.maxConsecutiveFailures = 4U;
    config.exploration.separator.nContainerSamples = 64U;
    config.exploration.separator.nFocussedSamples = 32U;
    config.compression.maxIterations = 32U;
    config.compression.maxConsecutiveFailures = 4U;
    config.compression.separator.nContainerSamples = 64U;
    config.compression.separator.nFocussedSamples = 32U;
    config.terminator.maxIterations = 1000U;

    const auto optimized = sparrow::Optimize(instance.Value(), config);
    if (!optimized.HasValue())
    {
        std::cerr << "optimization failed: "
                  << optimized.GetError().message << '\n';
        return 5;
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
    std::cout << "SparrowCore " << sparrow::Version()
              << " / JaguarCore " << jaguar::Version()
              << ", width=" << optimized.Value().metrics.stripWidth
              << ", density=" << optimized.Value().metrics.density << '\n';
    return 0;
}
