#include "JaguarCore/Collision/CollisionEngine.h"
#include "JaguarCore/Geometry/PointOfInaccessibility.h"
#include "JaguarCore/Geometry/RustF32.h"

#include <cassert>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace
{
using ReferenceMap = std::map<std::string, std::string>;

std::string loadReference(const char* fileName)
{
    for (const std::string& path : {
             std::string("..\\JaguarCore\\Tests\\Fixtures\\RustReference\\") + fileName,
             std::string("JaguarCore\\Tests\\Fixtures\\RustReference\\") + fileName,
             std::string("..\\JaguarRun\\JaguarCore\\Tests\\Fixtures\\RustReference\\")
                 + fileName})
    {
        std::ifstream input(path, std::ios::binary);
        if (input)
            return std::string(std::istreambuf_iterator<char>(input),
                               std::istreambuf_iterator<char>());
    }
    return {};
}

ReferenceMap loadSurrogateReference()
{
#ifdef JAGUARCORE_TEST_RELEASE
    const std::string contents = loadReference("surrogate_b3_release.txt");
#else
    const std::string contents = loadReference("surrogate_b3_debug.txt");
#endif
    assert(!contents.empty());

    ReferenceMap reference;
    std::istringstream input(contents);
    for (std::string line; std::getline(input, line);)
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const std::size_t separator = line.find('=');
        if (line.empty() || separator == std::string::npos || line[0] == '#')
            continue;
        reference.emplace(line.substr(0, separator), line.substr(separator + 1));
    }
    assert(!reference.empty());
    return reference;
}

const std::string& referenceValue(const ReferenceMap& reference,
                                  const std::string& key)
{
    const auto found = reference.find(key);
    if (found == reference.end())
        std::fprintf(stderr, "B3 Rust reference is missing key: %s\n", key.c_str());
    assert(found != reference.end());
    return found->second;
}

std::vector<double> numericValues(const std::string& text)
{
    std::vector<double> values;
    for (std::size_t index = 0; index < text.size();)
    {
        const char current = text[index];
        std::size_t previous = index;
        while (previous > 0 &&
               std::isspace(static_cast<unsigned char>(text[previous - 1])))
            --previous;
        const bool valueBoundary =
            previous == 0 || text[previous - 1] == '=' ||
            text[previous - 1] == ':' || text[previous - 1] == ',' ||
            text[previous - 1] == ';' || text[previous - 1] == '[';
        const bool startsNumber =
            valueBoundary &&
            (std::isdigit(static_cast<unsigned char>(current)) || current == '.' ||
             ((current == '-' || current == '+') && index + 1 < text.size() &&
              (std::isdigit(static_cast<unsigned char>(text[index + 1])) ||
               text[index + 1] == '.')));
        if (!startsNumber)
        {
            ++index;
            continue;
        }

        char* end = nullptr;
        const double value = std::strtod(text.c_str() + index, &end);
        if (end == text.c_str() + index)
        {
            ++index;
            continue;
        }
        values.push_back(value);
        index = static_cast<std::size_t>(end - text.c_str());
    }
    return values;
}

void expectRustNumbers(const ReferenceMap& reference,
                       const std::string& key,
                       const std::vector<double>& actual)
{
    const std::vector<double> expected = numericValues(referenceValue(reference, key));
    if (expected.size() != actual.size())
    {
        std::fprintf(stderr,
                     "B3 reference %s field count: Rust %zu, C++ %zu\n",
                     key.c_str(), expected.size(), actual.size());
    }
    assert(expected.size() == actual.size());
    for (std::size_t index = 0; index < expected.size(); ++index)
    {
        const float expectedValue = static_cast<float>(expected[index]);
        const float actualValue = static_cast<float>(actual[index]);
        const bool matches =
            jaguar::detail::AlmostEqualRustF32(expectedValue, actualValue);
        if (!matches)
        {
            std::fprintf(stderr,
                         "B3 reference %s[%zu]: Rust %.9g, C++ %.9g\n",
                         key.c_str(), index, expectedValue, actualValue);
        }
        assert(matches);
    }
}

void expectRustExactNumbers(const ReferenceMap& reference,
                           const std::string& key,
                           const std::vector<double>& actual)
{
    const std::vector<double> expected = numericValues(referenceValue(reference, key));
    if (expected != actual)
        std::fprintf(stderr, "B3 reference %s differs\n", key.c_str());
    assert(expected == actual);
}

void expectRustText(const ReferenceMap& reference,
                    const std::string& key,
                    const std::string& actual)
{
    const std::string& expected = referenceValue(reference, key);
    if (expected != actual)
    {
        std::fprintf(stderr,
                     "B3 reference %s: Rust %s, C++ %s\n",
                     key.c_str(), expected.c_str(), actual.c_str());
    }
    assert(expected == actual);
}

float referenceFloat(const ReferenceMap& reference, const std::string& key)
{
    const std::vector<double> values = numericValues(referenceValue(reference, key));
    assert(values.size() == 1);
    return static_cast<float>(values.front());
}

void compareSurrogate(const ReferenceMap& reference,
                      const char* name,
                      const std::vector<jaguar::Point2d>& vertices)
{
    const auto shape = jaguar::SimplePolygon::Create(vertices);
    assert(shape.HasValue());

    jaguar::SurrogateConfig config{};
    config.poleLimits = {{{8, 0.0}, {4, 0.75}, {2, 0.9}}};
    config.failFastPoleAreaRatio = 0.5;
    config.failFastPiers = 2;
    const auto result = jaguar::Surrogate::Create(shape.Value(), config);
    assert(result.HasValue());
    const jaguar::Surrogate& surrogate = result.Value();
    const std::string prefix = std::string("b3.") + name;

    expectRustExactNumbers(reference, prefix + ".poles.count",
                           {static_cast<double>(surrogate.Poles().size())});
    expectRustExactNumbers(reference, prefix + ".fail_fast_poles.count",
                           {static_cast<double>(surrogate.FailFastPoleCount())});
    expectRustExactNumbers(reference, prefix + ".piers.count",
                           {static_cast<double>(surrogate.Piers().size())});
    expectRustNumbers(reference, prefix + ".convex_hull_area",
                      {surrogate.ConvexHullArea()});

    std::vector<double> hullIndices;
    hullIndices.reserve(surrogate.ConvexHullIndices().size());
    for (const std::size_t index : surrogate.ConvexHullIndices())
        hullIndices.push_back(static_cast<double>(index));
    expectRustExactNumbers(reference, prefix + ".convex_hull_indices", hullIndices);

    for (std::size_t index = 0; index < surrogate.Poles().size(); ++index)
    {
        const jaguar::Circle2d& pole = surrogate.Poles()[index];
        std::ostringstream key;
        key << prefix << ".pole[" << index << ']';
        expectRustNumbers(reference, key.str(),
                          {pole.center.x, pole.center.y, pole.radius});
    }

    for (std::size_t index = 0; index < surrogate.Piers().size(); ++index)
    {
        const jaguar::Edge2d& pier = surrogate.Piers()[index];
        std::ostringstream key;
        key << prefix << ".pier[" << index << ']';
        expectRustNumbers(reference, key.str(),
                          {pier.start.x, pier.start.y,
                           pier.end.x, pier.end.y, pier.Length()});
    }
}

void compareSurrogateCollision(const ReferenceMap& reference)
{
    const auto bounds = jaguar::Rect2d::Create(0.0, 0.0, 10.0, 10.0);
    assert(bounds.HasValue());
    const auto notch = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {10.0, 0.0}, {10.0, 10.0}, {6.0, 10.0},
        {6.0, 4.0}, {4.0, 4.0}, {4.0, 10.0}, {0.0, 10.0}});
    const auto smallShape = jaguar::SimplePolygon::Create({
        {4.95, 7.95}, {5.05, 7.95}, {5.05, 8.05}, {4.95, 8.05}});
    assert(notch.HasValue() && smallShape.HasValue());

    jaguar::SurrogateConfig surrogateConfig{};
    surrogateConfig.poleLimits = {{{1, 0.0}, {0, 0.0}, {0, 0.0}}};
    surrogateConfig.failFastPoleAreaRatio = 1.0;
    const auto surrogate = jaguar::Surrogate::Create(smallShape.Value(), surrogateConfig);
    assert(surrogate.HasValue());
    const auto transform = jaguar::Transform2d::FromRotationTranslation(0.0, {0.0, 0.0});

    for (const auto& [name, threshold] :
         {std::pair<const char*, std::uint8_t>{"high", 8}, {"low", 0}})
    {
        jaguar::CDEConfig config{};
        config.quadtreeDepth = 4;
        config.cdThreshold = threshold;
        jaguar::CollisionEngine engine(bounds.Value(), config);
        const auto hazard = engine.RegisterHazard({
            {jaguar::HazardKind::Exterior},
            std::make_shared<const jaguar::SimplePolygon>(notch.Value()), false});
        assert(hazard.HasValue());

        const auto detected = engine.DetectSurrogateCollision(
            surrogate.Value(), transform, jaguar::NoFilter{});
        assert(detected.HasValue());
        expectRustText(reference,
                       std::string("collision.notch.detect.") + name,
                       detected.Value() ? "true" : "false");

        jaguar::HazardCollector collector;
        const auto stopped = engine.CollectSurrogateCollisionsUntil(
            surrogate.Value(), transform, collector, jaguar::NoFilter{},
            [](const jaguar::HazardEntity&) { return false; });
        assert(stopped.HasValue());
        std::ostringstream collected;
        collected << "stopped:" << (stopped.Value() ? "true" : "false")
                  << ",count:" << collector.Size();
        expectRustText(reference,
                       std::string("collision.notch.collect.") + name,
                       collected.str());
    }

    const auto largeBounds = jaguar::Rect2d::Create(0.0, 0.0, 40.0, 40.0);
    const auto largeNotch = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {40.0, 0.0}, {40.0, 40.0}, {30.0, 40.0},
        {30.0, 10.0}, {10.0, 10.0}, {10.0, 40.0}, {0.0, 40.0}});
    assert(largeBounds.HasValue() && largeNotch.HasValue());

    jaguar::SurrogateConfig pierOnlyConfig{};
    pierOnlyConfig.poleLimits = {{{1, 0.0}, {0, 0.0}, {0, 0.0}}};
    pierOnlyConfig.failFastPiers = 1;
    const auto pierOnly = jaguar::Surrogate::Create(notch.Value(), pierOnlyConfig);
    assert(pierOnly.HasValue());
    expectRustExactNumbers(reference, "collision.notch.pier_only_count",
                           {static_cast<double>(pierOnly.Value().Piers().size())});

    jaguar::CDEConfig collisionConfig{};
    collisionConfig.quadtreeDepth = 4;
    collisionConfig.cdThreshold = 8;
    jaguar::CollisionEngine largeEngine(largeBounds.Value(), collisionConfig);
    const auto largeHazard = largeEngine.RegisterHazard({
        {jaguar::HazardKind::Exterior},
        std::make_shared<const jaguar::SimplePolygon>(largeNotch.Value()), false});
    assert(largeHazard.HasValue());
    const auto pierDetected = largeEngine.DetectSurrogateCollision(
        pierOnly.Value(),
        jaguar::Transform2d::FromRotationTranslation(0.0, {15.0, 15.0}),
        jaguar::NoFilter{});
    assert(pierDetected.HasValue());
    expectRustText(reference, "collision.notch.detect_pier_only",
                   pierDetected.Value() ? "true" : "false");
}

void compareSurrogateBoundariesAndFailures(const ReferenceMap& reference)
{
    const auto square = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}, {0.0, 1.0}});
    assert(square.HasValue());

    jaguar::SurrogateConfig baselineConfig{};
    baselineConfig.poleLimits = {{{3, 0.0}, {100, 2.0}, {100, 2.0}}};
    const auto baseline = jaguar::Surrogate::Create(square.Value(), baselineConfig);
    assert(baseline.HasValue());
    assert(baseline.Value().Poles().size() >= 3);
    expectRustExactNumbers(reference, "b3.boundary.baseline_poles",
                           {static_cast<double>(baseline.Value().Poles().size())});

    float coverageAfterTwo = 0.0F;
    for (std::size_t index = 0; index < 2; ++index)
        coverageAfterTwo += static_cast<float>(
            baseline.Value().Poles()[index].AreaRustF32());
    coverageAfterTwo /= static_cast<float>(square.Value().Area());
    expectRustNumbers(reference, "b3.boundary.coverage_after_two",
                      {coverageAfterTwo});

    const std::vector<std::pair<const char*, const char*>> poleThresholdCases{
        {"below", "b3.pole_threshold.below_value"},
        {"equal", "b3.pole_threshold.equal_value"},
        {"above", "b3.pole_threshold.above_value"}};
    for (const auto& [name, valueKey] : poleThresholdCases)
    {
        jaguar::SurrogateConfig config{};
        config.poleLimits = {{{2, referenceFloat(reference, valueKey)},
                              {100, 2.0}, {100, 2.0}}};
        const auto surrogate = jaguar::Surrogate::Create(square.Value(), config);
        assert(surrogate.HasValue());
        expectRustExactNumbers(
            reference, std::string("b3.pole_threshold.") + name + "_poles",
            {static_cast<double>(surrogate.Value().Poles().size())});
    }

    const std::vector<std::pair<const char*, const char*>> failFastRatioCases{
        {"below", "b3.fail_fast_ratio.below_value"},
        {"equal", "b3.fail_fast_ratio.equal_value"},
        {"above", "b3.fail_fast_ratio.above_value"}};
    for (const auto& [name, valueKey] : failFastRatioCases)
    {
        jaguar::SurrogateConfig config = baselineConfig;
        config.failFastPoleAreaRatio = referenceFloat(reference, valueKey);
        const auto surrogate = jaguar::Surrogate::Create(square.Value(), config);
        assert(surrogate.HasValue());
        expectRustExactNumbers(
            reference, std::string("b3.fail_fast_ratio.") + name + "_poles",
            {static_cast<double>(surrogate.Value().FailFastPoleCount())});
    }

    const float nextAfterOne = std::nextafter(1.0F, 2.0F);
    const float smallestPositive = std::numeric_limits<float>::denorm_min();
    const std::vector<std::pair<const char*, float>> invalidRatios{
        {"negative", -smallestPositive}, {"above_one", nextAfterOne},
        {"nan", std::numeric_limits<float>::quiet_NaN()},
        {"positive_inf", std::numeric_limits<float>::infinity()},
        {"negative_inf", -std::numeric_limits<float>::infinity()}};
    for (const auto& [name, ratio] : invalidRatios)
    {
        jaguar::SurrogateConfig config = baselineConfig;
        config.failFastPoleAreaRatio = static_cast<double>(ratio);
        const auto surrogate = jaguar::Surrogate::Create(square.Value(), config);
        expectRustText(reference, std::string("b3.ratio.") + name,
                       surrogate.HasValue() ? "ok" : "err");
        assert(!surrogate.HasValue());
        assert(surrogate.GetError().code == jaguar::ErrorCode::InvalidInput);
    }

    for (const auto& [name, ratio] :
         {std::pair<const char*, float>{"zero", 0.0F}, {"one", 1.0F}})
    {
        jaguar::SurrogateConfig config = baselineConfig;
        config.failFastPoleAreaRatio = ratio;
        const auto surrogate = jaguar::Surrogate::Create(square.Value(), config);
        assert(surrogate.HasValue());
        std::ostringstream outcome;
        outcome << "ok:" << surrogate.Value().FailFastPoleCount();
        expectRustText(reference, std::string("b3.ratio.") + name,
                       outcome.str());
    }

    const auto offsetSquare = jaguar::SimplePolygon::Create({
        {100.0, 100.0}, {101.0, 100.0}, {101.0, 101.0}, {100.0, 101.0}});
    assert(offsetSquare.HasValue());
    jaguar::SurrogateConfig pierFailureConfig{};
    pierFailureConfig.poleLimits = {{{1, 0.0}, {0, 0.0}, {0, 0.0}}};
    pierFailureConfig.failFastPiers = 1;
    const auto pierFailure = jaguar::Surrogate::Create(
        offsetSquare.Value(), pierFailureConfig);
    expectRustText(reference, "b3.pier_generation_failure", "err:no ray found");
    assert(!pierFailure.HasValue());
    assert(pierFailure.GetError().code == jaguar::ErrorCode::Infeasible);

    const auto coveringPole = jaguar::Circle2d::Create({0.5, 0.5}, 100.0);
    assert(coveringPole.HasValue());
    const auto poleFailure = jaguar::ComputePointOfInaccessibility(
        square.Value(), {coveringPole.Value()});
    expectRustText(reference, "b3.pole_generation_failure", "err:no pole found");
    assert(!poleFailure.HasValue());
    assert(poleFailure.GetError().code == jaguar::ErrorCode::InvalidPolygon);
}
}

void runSurrogateDifferentialTests()
{
    const ReferenceMap reference = loadSurrogateReference();
    compareSurrogate(reference, "u", {
        {0.0, 0.0}, {10.0, 0.0}, {10.0, 10.0}, {6.0, 10.0},
        {6.0, 4.0}, {4.0, 4.0}, {4.0, 10.0}, {0.0, 10.0}});
    compareSurrogate(reference, "l", {
        {0.0, 0.0}, {8.0, 0.0}, {8.0, 3.0},
        {3.0, 3.0}, {3.0, 8.0}, {0.0, 8.0}});
    compareSurrogate(reference, "c", {
        {0.0, 0.0}, {10.0, 0.0}, {10.0, 2.0}, {3.0, 2.0},
        {3.0, 8.0}, {10.0, 8.0}, {10.0, 10.0}, {0.0, 10.0}});
    compareSurrogateCollision(reference);
    compareSurrogateBoundariesAndFailures(reference);
}
