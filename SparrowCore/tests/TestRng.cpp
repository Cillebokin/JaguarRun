#include "SparrowCore/LbfConfig.h"
#include "SparrowCore/LbfMetrics.h"
#include "SparrowCore/Random/SparrowRng.h"

#include "JaguarCore/Result.h"
#include "../Source/Optimizer/SeparatorRandom.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
bool Expect(bool condition, std::ostream& output, const char* message)
{
    if (condition)
    {
        return true;
    }

    output << "RNG TEST FAILURE: " << message << '\n';
    return false;
}

bool SameFloatBits(float left, float right) noexcept
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

bool LoadSeed42Reference(Seed42Reference& reference, std::ostream& output)
{
    const std::array<const char*, 4> paths = {
        "SparrowCore/Tests/Fixtures/LbfSeed42Reference.tsv",
        "../SparrowCore/Tests/Fixtures/LbfSeed42Reference.tsv",
        "../JaguarRun/SparrowCore/Tests/Fixtures/LbfSeed42Reference.tsv",
        "..\\SparrowCore\\Tests\\Fixtures\\LbfSeed42Reference.tsv",
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

bool TestSeed42Reference(std::ostream& output)
{
    Seed42Reference reference;
    if (!LoadSeed42Reference(reference, output))
    {
        return false;
    }

    sparrow::SparrowRng rawRng(42U);
    for (std::size_t index = 0; index < reference.raw.size(); ++index)
    {
        if (!Expect(rawRng.NextU64() == reference.raw[index],
                    output,
                    "seed=42 raw u64 reference mismatch"))
        {
            return false;
        }
    }

    sparrow::SparrowRng uniformRng(42U);
    for (std::size_t index = 0; index < reference.uniform.size(); ++index)
    {
        if (!Expect(SameFloatBits(uniformRng.Uniform01F32(), reference.uniform[index]),
                    output,
                    "seed=42 uniform f32 reference mismatch"))
        {
            return false;
        }
    }

    return true;
}

bool TestUniformIndexRustReference(std::ostream& output)
{
    std::ifstream input;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/SparrowUniformIndexRustReference.tsv",
             "../SparrowCore/Tests/Fixtures/SparrowUniformIndexRustReference.tsv",
             "../JaguarRun/SparrowCore/Tests/Fixtures/SparrowUniformIndexRustReference.tsv",
             "..\\SparrowCore\\Tests\\Fixtures\\SparrowUniformIndexRustReference.tsv"})
    {
        input.open(path);
        if (input.is_open())
        {
            break;
        }
        input.clear();
    }
    if (!Expect(input.is_open(),
                output,
                "Rust uniform-index reference fixture must be readable"))
    {
        return false;
    }

    std::uint64_t currentSeed = 0U;
    std::uint32_t currentBound = 0U;
    std::size_t nextOrdinal = 0U;
    std::size_t rowCount = 0U;
    bool groupStarted = false;
    sparrow::SparrowRng rng(0U);
    std::string line;
    while (std::getline(input, line))
    {
        if (line.empty() || line.front() == '#')
        {
            continue;
        }
        std::istringstream row(line);
        std::uint64_t seed = 0U;
        std::uint32_t bound = 0U;
        std::size_t ordinal = 0U;
        std::uint32_t expected = 0U;
        if (!(row >> seed >> bound >> ordinal >> expected) || bound == 0U)
        {
            return Expect(false,
                          output,
                          "Rust uniform-index reference row is malformed");
        }

        if (!groupStarted || seed != currentSeed || bound != currentBound)
        {
            if (groupStarted
                && !Expect(rowCount == 4U,
                           output,
                           "Rust uniform-index reference group is incomplete"))
            {
                return false;
            }
            currentSeed = seed;
            currentBound = bound;
            nextOrdinal = 0U;
            rowCount = 0U;
            rng = sparrow::SparrowRng(seed);
            groupStarted = true;
        }

        if (!Expect(ordinal == nextOrdinal,
                    output,
                    "Rust uniform-index reference ordinals must be contiguous"))
        {
            return false;
        }
        const std::uint32_t actual = rng.UniformIndex(bound);
        if (!Expect(actual == expected,
                    output,
                    "bounded integer output differs from Rust rand 0.10"))
        {
            output << "uniform index seed=" << seed << " bound=" << bound
                   << " ordinal=" << ordinal << " expected=" << expected
                   << " actual=" << actual << '\n';
            return false;
        }
        ++nextOrdinal;
        ++rowCount;
    }
    return Expect(groupStarted && rowCount == 4U,
                  output,
                  "Rust uniform-index reference fixture must end with a complete group");
}

bool TestSliceShuffleRustReference(std::ostream& output)
{
    std::ifstream input;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/SparrowSliceShuffleRustReference.tsv",
             "../SparrowCore/Tests/Fixtures/SparrowSliceShuffleRustReference.tsv",
             "../JaguarRun/SparrowCore/Tests/Fixtures/SparrowSliceShuffleRustReference.tsv",
             "..\\SparrowCore\\Tests\\Fixtures\\SparrowSliceShuffleRustReference.tsv"})
    {
        input.open(path);
        if (input.is_open())
        {
            break;
        }
        input.clear();
    }
    if (!Expect(input.is_open(),
                output,
                "Rust SliceRandom shuffle fixture must be readable"))
    {
        return false;
    }

    constexpr std::array<std::uint64_t, 4> seeds = {
        0U, 1U, 42U, std::numeric_limits<std::uint64_t>::max()};
    std::array<std::array<bool, 33>, seeds.size()> seen{};
    std::size_t rows = 0U;
    std::string line;
    while (std::getline(input, line))
    {
        if (line.empty() || line.front() == '#')
        {
            continue;
        }

        std::istringstream row(line);
        std::string record;
        std::uint64_t seed = 0U;
        std::size_t length = 0U;
        std::string permutation;
        std::array<std::string, 4> followingHex{};
        std::string extra;
        if (!(row >> record >> seed >> length >> permutation
                  >> followingHex[0] >> followingHex[1]
                  >> followingHex[2] >> followingHex[3])
            || (row >> extra)
            || record != "shuffle"
            || length > 32U)
        {
            return Expect(false,
                          output,
                          "Rust SliceRandom shuffle row is malformed");
        }

        const auto seedIt = std::find(seeds.begin(), seeds.end(), seed);
        if (!Expect(seedIt != seeds.end(),
                    output,
                    "Rust SliceRandom shuffle seed is unexpected"))
        {
            return false;
        }
        const std::size_t seedIndex = static_cast<std::size_t>(
            std::distance(seeds.begin(), seedIt));
        if (!Expect(!seen[seedIndex][length],
                    output,
                    "Rust SliceRandom shuffle row is duplicated"))
        {
            return false;
        }
        seen[seedIndex][length] = true;

        std::vector<std::size_t> expectedValues;
        if (length == 0U)
        {
            if (!Expect(permutation == "-",
                        output,
                        "empty Rust SliceRandom shuffle must use '-'"))
            {
                return false;
            }
        }
        else
        {
            std::istringstream values(permutation);
            std::string value;
            while (std::getline(values, value, ','))
            {
                try
                {
                    const auto parsed = std::stoull(value);
                    if (parsed >= length)
                    {
                        return Expect(false,
                                      output,
                                      "Rust SliceRandom permutation value is out of range");
                    }
                    expectedValues.push_back(static_cast<std::size_t>(parsed));
                }
                catch (const std::exception&)
                {
                    return Expect(false,
                                  output,
                                  "Rust SliceRandom permutation value is malformed");
                }
            }
            if (!Expect(expectedValues.size() == length,
                        output,
                        "Rust SliceRandom permutation length is incomplete"))
            {
                return false;
            }
        }

        std::vector<std::size_t> actualValues;
        actualValues.reserve(length);
        for (std::size_t value = 0U; value < length; ++value)
        {
            actualValues.push_back(value);
        }
        sparrow::SparrowRng rng(seed);
        sparrow::detail::ShuffleSliceRandom(actualValues, rng);
        if (!Expect(actualValues == expectedValues,
                    output,
                    "production SliceRandom shuffle permutation differs from Rust"))
        {
            output << "seed=" << seed << " length=" << length << '\n';
            return false;
        }

        for (std::size_t index = 0U; index < followingHex.size(); ++index)
        {
            std::uint64_t expected = 0U;
            try
            {
                expected = std::stoull(followingHex[index], nullptr, 16);
            }
            catch (const std::exception&)
            {
                return Expect(false,
                              output,
                              "Rust SliceRandom post-shuffle RNG value is malformed");
            }
            if (!Expect(rng.NextU64() == expected,
                        output,
                        "production SliceRandom shuffle post-state differs from Rust"))
            {
                output << "seed=" << seed << " length=" << length
                       << " following=" << index << '\n';
                return false;
            }
        }
        ++rows;
    }

    if (!Expect(rows == seeds.size() * 33U,
                output,
                "Rust SliceRandom shuffle fixture must cover every seed/length pair"))
    {
        return false;
    }
    for (const auto& seedRows : seen)
    {
        if (!Expect(std::all_of(seedRows.begin(), seedRows.end(), [](bool value) {
                        return value;
                    }),
                    output,
                    "Rust SliceRandom shuffle fixture is missing a length"))
        {
            return false;
        }
    }
    return true;
}

bool TestUniformIndexRejectsEmptyRangeWithoutConsumingRng(
    std::ostream& output)
{
    sparrow::SparrowRng actual(42U);
    sparrow::SparrowRng expected(42U);
    const auto initialState = actual.State();
    bool threwInvalidArgument = false;
    try
    {
        static_cast<void>(actual.UniformIndex(0U));
    }
    catch (const std::invalid_argument&)
    {
        threwInvalidArgument = true;
    }
    return Expect(threwInvalidArgument
                      && actual.State() == initialState
                      && actual.NextU64() == expected.NextU64(),
                  output,
                  "empty uniform-index range must fail before consuming RNG");
}

bool TestStateAndIntegerDeterminism(std::ostream& output)
{
    sparrow::SparrowRng first(42U);
    sparrow::SparrowRng second(42U);
    if (!Expect(first.State() == second.State(), output, "same seed state mismatch"))
    {
        return false;
    }

    for (std::size_t index = 0; index < 16; ++index)
    {
        if (!Expect(first.NextU64() == second.NextU64(),
                    output,
                    "same seed raw sequence mismatch"))
        {
            return false;
        }
    }

    sparrow::SparrowRng rawRng(42U);
    sparrow::SparrowRng u32Rng(42U);
    const std::uint64_t raw = rawRng.NextU64();
    if (!Expect(u32Rng.NextU32() == static_cast<std::uint32_t>(raw >> 32U),
                output,
                "nextU32 must expose the high 32 bits of nextU64"))
    {
        return false;
    }

    sparrow::SparrowRng zeroSeed(0U);
    sparrow::SparrowRng maximumSeed(std::numeric_limits<std::uint64_t>::max());
    if (!Expect(zeroSeed.State() != std::array<std::uint64_t, 4>{},
                output,
                "seed=0 must not create an all-zero state"))
    {
        return false;
    }
    if (!Expect(zeroSeed.State() != maximumSeed.State(),
                output,
                "zero and maximum seeds must create different states"))
    {
        return false;
    }

    bool anyRawDifference = false;
    for (std::size_t index = 0; index < 16; ++index)
    {
        if (zeroSeed.NextU64() != maximumSeed.NextU64())
        {
            anyRawDifference = true;
            break;
        }
    }
    return Expect(anyRawDifference,
                  output,
                  "different seeds must differ in their first 16 raw values");
}

bool TestNormalDeterminismAndConsumption(std::ostream& output)
{
    sparrow::SparrowRng first(42U);
    sparrow::SparrowRng second(42U);

    const auto initialState = first.State();
    const float firstNormal = first.Normal01F32();
    const auto stateAfterFirst = first.State();
    const float secondNormal = first.Normal01F32();
    const auto stateAfterSecond = first.State();

    if (!Expect(std::isfinite(firstNormal) && std::isfinite(secondNormal),
                output,
                "normal samples must be finite"))
    {
        return false;
    }
    if (!Expect(initialState != stateAfterFirst
                    && stateAfterFirst != stateAfterSecond,
                output,
                "each normal sample must consume the pinned Ziggurat RNG stream"))
    {
        return false;
    }

    if (!Expect(SameFloatBits(firstNormal, second.Normal01F32()),
                output,
                "same seed first normal sample mismatch"))
    {
        return false;
    }
    return Expect(SameFloatBits(secondNormal, second.Normal01F32()),
                  output,
                  "same seed second normal sample mismatch");
}

bool ExpectInvalidConfig(sparrow::LbfConfig config,
                         std::ostream& output,
                         const char* message)
{
    const sparrow::Result<void> result = config.Validate();
    return Expect(!result.HasValue() && result.GetError().code == jaguar::ErrorCode::InvalidInput,
                  output,
                  message);
}

bool TestConfigValidation(std::ostream& output)
{
    sparrow::LbfConfig defaults;
    if (!Expect(defaults.Validate().HasValue(), output, "default LBF config must be valid"))
    {
        return false;
    }

    sparrow::LbfConfig zeroSamples = defaults;
    zeroSamples.samplesPerItem = 0;
    if (!Expect(zeroSamples.Validate().HasValue(),
                output,
                "zero sample budget must represent a search with no sampling iterations"))
    {
        return false;
    }

    sparrow::LbfConfig negativeFraction = defaults;
    negativeFraction.localSearchFraction = -0.01;
    if (!Expect(negativeFraction.Validate().HasValue()
                    && negativeFraction.LocalSearchSampleBudget() == 0U
                    && negativeFraction.UniformSampleBudget()
                           == negativeFraction.samplesPerItem,
                output,
                "negative local search fraction must saturate to uniform-only sampling"))
    {
        return false;
    }

    sparrow::LbfConfig negativeOverflowFraction = defaults;
    negativeOverflowFraction.localSearchFraction =
        -std::numeric_limits<double>::infinity();
    if (!Expect(negativeOverflowFraction.Validate().HasValue()
                    && negativeOverflowFraction.LocalSearchSampleBudget() == 0U
                    && negativeOverflowFraction.UniformSampleBudget()
                           == negativeOverflowFraction.samplesPerItem,
                output,
                "negative infinite local fraction must saturate to uniform-only sampling"))
    {
        return false;
    }

    sparrow::LbfConfig excessiveFraction = defaults;
    excessiveFraction.localSearchFraction = 1.01;
    if (!ExpectInvalidConfig(excessiveFraction,
                             output,
                             "local search fraction above one must be invalid"))
    {
        return false;
    }

    sparrow::LbfConfig positiveOverflowFraction = defaults;
    positiveOverflowFraction.localSearchFraction =
        std::numeric_limits<double>::infinity();
    if (!ExpectInvalidConfig(positiveOverflowFraction,
                             output,
                             "positive infinite local fraction must remain invalid"))
    {
        return false;
    }

    sparrow::LbfConfig nonFinite = defaults;
    nonFinite.maxTranslationStddevRatio = std::numeric_limits<double>::infinity();
    if (!ExpectInvalidConfig(nonFinite,
                             output,
                             "infinite translation ratio must be invalid"))
    {
        return false;
    }

    sparrow::LbfConfig negativeRatio = defaults;
    negativeRatio.minRotationStddevRad = -1.0;
    if (!ExpectInvalidConfig(negativeRatio,
                             output,
                             "negative rotation ratio must be invalid"))
    {
        return false;
    }

    sparrow::LbfConfig invalidGrowth = defaults;
    invalidGrowth.widthGrowthRatio = 1.0;
    if (!ExpectInvalidConfig(invalidGrowth,
                             output,
                             "width growth ratio at one must be invalid"))
    {
        return false;
    }

    sparrow::LbfConfig invertedRange = defaults;
    invertedRange.minTranslationStddevRatio = 0.02;
    invertedRange.maxTranslationStddevRatio = 0.01;
    return ExpectInvalidConfig(invertedRange,
                               output,
                               "inverted translation range must be invalid");
}

bool TestMetricsValueType(std::ostream& output)
{
    sparrow::LbfMetrics metrics;
    metrics.seed = 42U;
    metrics.sampleCount = 64U;
    metrics.placedItemCount = 2U;
    metrics.remainingDemand = 0U;
    metrics.stripWidth = 4.276253;
    metrics.density = 0.46769914;
    metrics.feasible = true;

    return Expect(metrics.seed == 42U && metrics.sampleCount == 64U
                      && metrics.placedItemCount == 2U && metrics.remainingDemand == 0U
                      && metrics.stripWidth > 4.27 && metrics.density > 0.46
                      && metrics.feasible,
                  output,
                  "LBF metrics value type fields mismatch");
}
}

bool RunRngTests(std::ostream& output)
{
    return TestSeed42Reference(output)
        && TestUniformIndexRejectsEmptyRangeWithoutConsumingRng(output)
        && TestUniformIndexRustReference(output)
        && TestSliceShuffleRustReference(output)
        && TestStateAndIntegerDeterminism(output)
        && TestNormalDeterminismAndConsumption(output)
        && TestConfigValidation(output)
        && TestMetricsValueType(output);
}
