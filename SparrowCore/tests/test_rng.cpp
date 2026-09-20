#include "SparrowCore/lbf_config.h"
#include "SparrowCore/lbf_metrics.h"
#include "SparrowCore/random/sparrow_rng.h"

#include "JaguarCore/result.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <ostream>
#include <sstream>
#include <string>

namespace
{
bool expect(bool condition, std::ostream& output, const char* message)
{
    if (condition)
    {
        return true;
    }

    output << "RNG TEST FAILURE: " << message << '\n';
    return false;
}

bool sameFloatBits(float left, float right) noexcept
{
    std::uint32_t leftBits = 0;
    std::uint32_t rightBits = 0;
    std::memcpy(&leftBits, &left, sizeof(leftBits));
    std::memcpy(&rightBits, &right, sizeof(rightBits));
    return leftBits == rightBits;
}

struct Seed42Reference
{
    std::array<std::uint64_t, 4> raw{};
    std::array<float, 4> uniform{};
    std::array<bool, 4> rawPresent{};
    std::array<bool, 4> uniformPresent{};
};

bool loadSeed42Reference(Seed42Reference& reference, std::ostream& output)
{
    const std::array<const char*, 3> paths = {
        "SparrowCore/tests/fixtures/lbf_seed42_reference.tsv",
        "../SparrowCore/tests/fixtures/lbf_seed42_reference.tsv",
        "..\\SparrowCore\\tests\\fixtures\\lbf_seed42_reference.tsv",
    };

    std::ifstream input;
    for (const char* path : paths)
    {
        input.open(path);
        if (input.is_open())
        {
            break;
        }
        input.clear();
    }
    if (!input.is_open())
    {
        output << "RNG TEST FAILURE: seed=42 reference fixture cannot be opened\n";
        return false;
    }

    std::string line;
    while (std::getline(input, line))
    {
        if (line.empty() || line[0] == '#')
        {
            continue;
        }

        std::istringstream row(line);
        std::uint64_t seed = 0;
        std::string primitive;
        std::size_t ordinal = 0;
        std::string decimalValue;
        std::string hexValue;
        if (!(row >> seed >> primitive >> ordinal >> decimalValue >> hexValue)
            || seed != 42U
            || ordinal >= 4U)
        {
            output << "RNG TEST FAILURE: malformed seed=42 reference row\n";
            return false;
        }

        if (primitive == "raw_u64")
        {
            std::istringstream value(decimalValue);
            if (!(value >> reference.raw[ordinal]))
            {
                output << "RNG TEST FAILURE: malformed raw seed=42 reference value\n";
                return false;
            }
            reference.rawPresent[ordinal] = true;
        }
        else if (primitive == "uniform_f32")
        {
            std::istringstream value(decimalValue);
            if (!(value >> reference.uniform[ordinal]))
            {
                output << "RNG TEST FAILURE: malformed uniform seed=42 reference value\n";
                return false;
            }
            reference.uniformPresent[ordinal] = true;
        }
        else
        {
            output << "RNG TEST FAILURE: unknown seed=42 reference primitive\n";
            return false;
        }
    }

    for (std::size_t index = 0; index < 4U; ++index)
    {
        if (!reference.rawPresent[index] || !reference.uniformPresent[index])
        {
            output << "RNG TEST FAILURE: incomplete seed=42 reference fixture\n";
            return false;
        }
    }
    return true;
}

bool testSeed42Reference(std::ostream& output)
{
    Seed42Reference reference;
    if (!loadSeed42Reference(reference, output))
    {
        return false;
    }

    sparrow::SparrowRng rawRng(42U);
    for (std::size_t index = 0; index < reference.raw.size(); ++index)
    {
        if (!expect(rawRng.nextU64() == reference.raw[index],
                    output,
                    "seed=42 raw u64 reference mismatch"))
        {
            return false;
        }
    }

    sparrow::SparrowRng uniformRng(42U);
    for (std::size_t index = 0; index < reference.uniform.size(); ++index)
    {
        if (!expect(sameFloatBits(uniformRng.uniform01F32(), reference.uniform[index]),
                    output,
                    "seed=42 uniform f32 reference mismatch"))
        {
            return false;
        }
    }

    return true;
}

bool testStateAndIntegerDeterminism(std::ostream& output)
{
    sparrow::SparrowRng first(42U);
    sparrow::SparrowRng second(42U);
    if (!expect(first.state() == second.state(), output, "same seed state mismatch"))
    {
        return false;
    }

    for (std::size_t index = 0; index < 16; ++index)
    {
        if (!expect(first.nextU64() == second.nextU64(),
                    output,
                    "same seed raw sequence mismatch"))
        {
            return false;
        }
    }

    sparrow::SparrowRng rawRng(42U);
    sparrow::SparrowRng u32Rng(42U);
    const std::uint64_t raw = rawRng.nextU64();
    if (!expect(u32Rng.nextU32() == static_cast<std::uint32_t>(raw >> 32U),
                output,
                "nextU32 must expose the high 32 bits of nextU64"))
    {
        return false;
    }

    sparrow::SparrowRng zeroSeed(0U);
    sparrow::SparrowRng maximumSeed(std::numeric_limits<std::uint64_t>::max());
    if (!expect(zeroSeed.state() != std::array<std::uint64_t, 4>{},
                output,
                "seed=0 must not create an all-zero state"))
    {
        return false;
    }
    if (!expect(zeroSeed.state() != maximumSeed.state(),
                output,
                "zero and maximum seeds must create different states"))
    {
        return false;
    }

    bool anyRawDifference = false;
    for (std::size_t index = 0; index < 16; ++index)
    {
        if (zeroSeed.nextU64() != maximumSeed.nextU64())
        {
            anyRawDifference = true;
            break;
        }
    }
    return expect(anyRawDifference,
                  output,
                  "different seeds must differ in their first 16 raw values");
}

bool testNormalCacheAndDeterminism(std::ostream& output)
{
    sparrow::SparrowRng first(42U);
    sparrow::SparrowRng second(42U);

    const float firstNormal = first.normal01F32();
    const auto stateAfterFirst = first.state();
    const float secondNormal = first.normal01F32();
    const auto stateAfterSecond = first.state();

    if (!expect(std::isfinite(firstNormal) && std::isfinite(secondNormal),
                output,
                "normal samples must be finite"))
    {
        return false;
    }
    if (!expect(stateAfterFirst == stateAfterSecond,
                output,
                "normal spare sample must not consume another raw state"))
    {
        return false;
    }

    if (!expect(sameFloatBits(firstNormal, second.normal01F32()),
                output,
                "same seed first normal sample mismatch"))
    {
        return false;
    }
    return expect(sameFloatBits(secondNormal, second.normal01F32()),
                  output,
                  "same seed cached normal sample mismatch");
}

bool expectInvalidConfig(sparrow::LbfConfig config,
                         std::ostream& output,
                         const char* message)
{
    const sparrow::Result<void> result = config.validate();
    return expect(!result.hasValue() && result.error().code == jaguar::ErrorCode::InvalidInput,
                  output,
                  message);
}

bool testConfigValidation(std::ostream& output)
{
    sparrow::LbfConfig defaults;
    if (!expect(defaults.validate().hasValue(), output, "default LBF config must be valid"))
    {
        return false;
    }

    sparrow::LbfConfig zeroSamples = defaults;
    zeroSamples.samplesPerItem = 0;
    if (!expectInvalidConfig(zeroSamples, output, "zero sample budget must be invalid"))
    {
        return false;
    }

    sparrow::LbfConfig negativeFraction = defaults;
    negativeFraction.localSearchFraction = -0.01;
    if (!expectInvalidConfig(negativeFraction,
                             output,
                             "negative local search fraction must be invalid"))
    {
        return false;
    }

    sparrow::LbfConfig excessiveFraction = defaults;
    excessiveFraction.localSearchFraction = 1.01;
    if (!expectInvalidConfig(excessiveFraction,
                             output,
                             "local search fraction above one must be invalid"))
    {
        return false;
    }

    sparrow::LbfConfig nonFinite = defaults;
    nonFinite.maxTranslationStddevRatio = std::numeric_limits<double>::infinity();
    if (!expectInvalidConfig(nonFinite,
                             output,
                             "infinite translation ratio must be invalid"))
    {
        return false;
    }

    sparrow::LbfConfig negativeRatio = defaults;
    negativeRatio.minRotationStddevRad = -1.0;
    if (!expectInvalidConfig(negativeRatio,
                             output,
                             "negative rotation ratio must be invalid"))
    {
        return false;
    }

    sparrow::LbfConfig invalidGrowth = defaults;
    invalidGrowth.widthGrowthRatio = 1.0;
    if (!expectInvalidConfig(invalidGrowth,
                             output,
                             "width growth ratio at one must be invalid"))
    {
        return false;
    }

    sparrow::LbfConfig invertedRange = defaults;
    invertedRange.minTranslationStddevRatio = 0.02;
    invertedRange.maxTranslationStddevRatio = 0.01;
    return expectInvalidConfig(invertedRange,
                               output,
                               "inverted translation range must be invalid");
}

bool testMetricsValueType(std::ostream& output)
{
    sparrow::LbfMetrics metrics;
    metrics.seed = 42U;
    metrics.sampleCount = 64U;
    metrics.placedItemCount = 2U;
    metrics.remainingDemand = 0U;
    metrics.stripWidth = 4.276253;
    metrics.density = 0.46769914;
    metrics.feasible = true;

    return expect(metrics.seed == 42U && metrics.sampleCount == 64U
                      && metrics.placedItemCount == 2U && metrics.remainingDemand == 0U
                      && metrics.stripWidth > 4.27 && metrics.density > 0.46
                      && metrics.feasible,
                  output,
                  "LBF metrics value type fields mismatch");
}
}

bool runRngTests(std::ostream& output)
{
    return testSeed42Reference(output)
        && testStateAndIntegerDeterminism(output)
        && testNormalCacheAndDeterminism(output)
        && testConfigValidation(output)
        && testMetricsValueType(output);
}
