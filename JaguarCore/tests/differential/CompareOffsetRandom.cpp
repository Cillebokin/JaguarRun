#include "JaguarCore/Geometry/Point.h"
#include "JaguarCore/Geometry/RustF32.h"
#include "JaguarCore/Geometry/ShapeModification.h"
#include "JaguarCore/Geometry/SimplePolygon.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace
{
struct XorShift64Star
{
    std::uint64_t state;

    std::uint64_t Next() noexcept
    {
        state ^= state >> 12U;
        state ^= state << 25U;
        state ^= state >> 27U;
        return state * 2685821657736338717ULL;
    }
};

std::string loadFixture()
{
    for (const std::string& path : {
             std::string("..\\JaguarCore\\Tests\\Fixtures\\RustReference\\")
                 + "offset_random_matrix.txt",
             std::string("JaguarCore\\Tests\\Fixtures\\RustReference\\")
                 + "offset_random_matrix.txt",
             std::string("..\\JaguarRun\\JaguarCore\\Tests\\Fixtures\\")
                 + "RustReference\\offset_random_matrix.txt"})
    {
        std::ifstream input(path, std::ios::binary);
        if (input)
            return std::string(std::istreambuf_iterator<char>(input),
                               std::istreambuf_iterator<char>{});
    }
    return {};
}

std::map<std::string, std::string> parseFixture(const std::string& contents)
{
    std::map<std::string, std::string> reference;
    std::size_t start = 0U;
    while (start < contents.size())
    {
        std::size_t end = contents.find('\n', start);
        if (end == std::string::npos)
            end = contents.size();
        std::string line = contents.substr(start, end - start);
        start = end + 1U;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const std::size_t equals = line.find('=');
        if (line.empty() || line.front() == '#' || equals == std::string::npos)
            continue;
        reference[line.substr(0U, equals)] = line.substr(equals + 1U);
    }
    return reference;
}

std::vector<double> numericValues(const std::string& value)
{
    std::vector<double> values;
    std::size_t start = 0U;
    while (start <= value.size())
    {
        std::size_t end = value.find(',', start);
        if (end == std::string::npos)
            end = value.size();
        values.push_back(std::atof(value.substr(start, end - start).c_str()));
        if (end == value.size())
            break;
        start = end + 1U;
    }
    return values;
}

// Census comparator: mirrors CompareGeometry's expectRustOffsetPolygon
// semantics (exact vertex count, AlmostEqualRustF32 area, cyclic coordinate
// match) and expectOffsetError semantics, but reports and counts instead of
// asserting so a full divergence census can be collected in one run.
bool polygonMatches(const std::string& value,
                    const jaguar::SimplePolygon& actual)
{
    const std::vector<double> expected = numericValues(value);
    const std::size_t expectedCount =
        expected.empty() ? 0U : static_cast<std::size_t>(expected.front());
    if (expectedCount != actual.VertexCount() ||
        expected.size() != 2U + expectedCount * 2U)
        return false;
    const float expectedArea = static_cast<float>(expected[1]);
    if (!jaguar::detail::AlmostEqualRustF32(
            expectedArea, static_cast<float>(actual.Area())))
        return false;
    for (std::size_t shift = 0U; shift < expectedCount; ++shift)
    {
        bool cycleMatches = true;
        for (std::size_t index = 0U; index < expectedCount; ++index)
        {
            const jaguar::Point2d& actualPoint =
                actual.Vertices()[(index + shift) % expectedCount];
            const float expectedX =
                static_cast<float>(expected[2U + index * 2U]);
            const float expectedY =
                static_cast<float>(expected[3U + index * 2U]);
            if (!jaguar::detail::AlmostEqualRustF32(
                    expectedX, static_cast<float>(actualPoint.x)) ||
                !jaguar::detail::AlmostEqualRustF32(
                    expectedY, static_cast<float>(actualPoint.y)))
            {
                cycleMatches = false;
                break;
            }
        }
        if (cycleMatches)
            return true;
    }
    return false;
}
}

// D8-A acceptance infrastructure: paired random offset matrix census.
// The generator mirrors RustOracle run_offset_random_matrix_oracle exactly
// (seed 0x4F464652414E444M, rectilinear monotone mountains, integer-derived
// f32-exact coordinates, RNG order: profile, {width,height} per point,
// scale, tx, ty, distance). Returns the number of divergent cases.
int runOffsetRandomMatrixCensus()
{
    const auto reference = parseFixture(loadFixture());
    if (reference.empty())
    {
        std::fprintf(stderr, "offset_random_matrix.txt fixture not found\n");
        return -1;
    }

    constexpr std::size_t N = 200U;
    constexpr float SCALES[4] = {1.0F, 10.0F, 100.0F, 1000.0F};
    constexpr float DISTANCES[4] = {0.05F, 0.25F, 1.0F, 2.5F};
    XorShift64Star rng{0x4F464652414E444DULL};

    std::size_t divergent = 0U;
    std::size_t compared = 0U;
    for (std::size_t index = 0U; index < N; ++index)
    {
        const std::size_t profile =
            3U + static_cast<std::size_t>(rng.Next() % 12U);
        std::uint64_t widths[16] = {};
        std::uint64_t heights[16] = {};
        for (std::size_t i = 0U; i < profile; ++i)
        {
            widths[i] = 1U + rng.Next() % 7U;
            heights[i] = 1U + rng.Next() % 9U;
        }
        const float scale = SCALES[rng.Next() % 4U];
        const float tx = static_cast<float>(rng.Next() % 100U);
        const float ty = static_cast<float>(rng.Next() % 100U);
        const float distance = DISTANCES[rng.Next() % 4U];

        float xs[16] = {};
        for (std::size_t i = 0U; i < profile; ++i)
        {
            const float previous = i == 0U ? 0.0F : xs[i - 1U];
            xs[i] = previous + static_cast<float>(widths[i]);
        }
        const float total = xs[profile - 1U] + 1.0F;
        std::vector<jaguar::Point2d> points;
        points.reserve(profile + 2U);
        const auto transform = [&](float x, float y) {
            return jaguar::Point2d{static_cast<double>(x * scale + tx),
                                   static_cast<double>(y * scale + ty)};
        };
        points.push_back(transform(0.0F, 0.0F));
        for (std::size_t i = 0U; i < profile; ++i)
        {
            points.push_back(
                transform(xs[i], static_cast<float>(heights[i])));
        }
        points.push_back(transform(total, 0.0F));

        const auto polygonKey = "offset.random." + std::to_string(index) + ".polygon";
        const auto polygon = jaguar::SimplePolygon::Create(points);
        if (!polygon.HasValue())
        {
            ++compared;
            const auto found = reference.find(polygonKey);
            if (found == reference.end() ||
                found->second !=
                    "error:" + polygon.GetError().message)
            {
                ++divergent;
                std::fprintf(stderr,
                             "DIVERGE %s: C++ polygon error '%s'\n",
                             polygonKey.c_str(),
                             polygon.GetError().message.c_str());
            }
            continue;
        }
        const auto foundPolygon = reference.find(polygonKey);
        if (foundPolygon != reference.end())
        {
            ++compared;
            ++divergent;
            std::fprintf(stderr,
                         "DIVERGE %s: fixture expects polygon error '%s', "
                         "C++ created %zu vertices\n",
                         polygonKey.c_str(),
                         foundPolygon->second.c_str(),
                         polygon.Value().VertexCount());
        }

        for (const auto& [suffix, mode] : {
                 std::pair<std::string, jaguar::ShapeModifyMode>{
                     "inflate", jaguar::ShapeModifyMode::Inflate},
                 std::pair<std::string, jaguar::ShapeModifyMode>{
                     "deflate", jaguar::ShapeModifyMode::Deflate}})
        {
            const std::string key =
                "offset.random." + std::to_string(index) + "." + suffix;
            const auto found = reference.find(key);
            if (found == reference.end())
            {
                ++divergent;
                std::fprintf(stderr, "DIVERGE %s: missing fixture key\n",
                             key.c_str());
                continue;
            }
            ++compared;
            const auto result = jaguar::OffsetShape(
                polygon.Value(), mode, static_cast<double>(distance));
            const std::string& expected = found->second;
            if (expected.rfind("error:", 0U) == 0U)
            {
                if (result.HasValue() ||
                    result.GetError().message != expected.substr(6U))
                {
                    ++divergent;
                    std::fprintf(stderr,
                                 "DIVERGE %s: fixture '%s', C++ %s\n",
                                 key.c_str(), expected.c_str(),
                                 result.HasValue()
                                     ? "succeeded"
                                     : result.GetError().message.c_str());
                }
                continue;
            }
            if (!result.HasValue())
            {
                ++divergent;
                std::fprintf(stderr,
                             "DIVERGE %s: fixture finite, C++ error '%s'\n",
                             key.c_str(),
                             result.GetError().message.c_str());
                continue;
            }
            if (!polygonMatches(expected, result.Value()))
            {
                ++divergent;
                std::fprintf(stderr,
                             "DIVERGE %s: geometry differs (C++ %zu "
                             "vertices, area %.9f)\n",
                             key.c_str(), result.Value().VertexCount(),
                             result.Value().Area());
            }
        }
    }
    std::printf(
        "offset random matrix census: %zu compared, %zu divergent\n",
        compared, divergent);
    return static_cast<int>(divergent);
}
