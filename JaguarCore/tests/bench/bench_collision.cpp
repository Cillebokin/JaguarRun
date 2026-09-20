#include "JaguarCore/collision/collision_engine.h"
#include "JaguarCore/geometry/simple_polygon.h"
#include "../../src/collision/soa_geometry.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <memory>
#include <vector>

namespace
{
using Clock = std::chrono::steady_clock;

jaguar::SimplePolygon rectangle(double xMin,
                                double yMin,
                                double xMax,
                                double yMax)
{
    const auto shape = jaguar::SimplePolygon::create({
        {xMin, yMin}, {xMax, yMin}, {xMax, yMax}, {xMin, yMax}});
    assert(shape.hasValue());
    return shape.value();
}

bool brutePolygonCollision(const jaguar::SimplePolygon& first,
                           const jaguar::SimplePolygon& second)
{
    if (!first.boundingBox().intersects(second.boundingBox()))
        return false;
    for (const auto& firstEdge : first.edges())
        for (const auto& secondEdge : second.edges())
            if (firstEdge.intersects(secondEdge))
                return true;
    return first.contains(second.vertices().front()) ||
           second.contains(first.vertices().front());
}

bool outsideBounds(const jaguar::SimplePolygon& polygon,
                   const jaguar::Rect2d& bounds)
{
    for (const auto& point : polygon.vertices())
    {
        if (!bounds.contains(point))
            return true;
    }
    return false;
}

double elapsedMilliseconds(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

struct SoaBenchmarkResult
{
    double milliseconds = 0.0;
    std::size_t hits = 0;
};

SoaBenchmarkResult benchmarkSoaMode(const jaguar::PolygonSoA& polygon,
                                    const std::vector<jaguar::Edge2d>& edges,
                                    jaguar::SoaExecutionMode mode)
{
    constexpr std::size_t repeats = 20;
    std::size_t hits = 0;
    const auto start = Clock::now();
    for (std::size_t repeat = 0; repeat < repeats; ++repeat)
    {
        for (const auto& edge : edges)
        {
            if (polygon.intersectsBoundary(edge, 1.0e-9, mode))
                ++hits;
        }
    }
    return {elapsedMilliseconds(start), hits};
}
}

int runCollisionBench()
{
    const auto bounds = jaguar::Rect2d::create(0.0, 0.0, 100.0, 100.0);
    assert(bounds.hasValue());
    jaguar::CDEConfig config{};
    config.quadtreeDepth = 6;
    config.cdThreshold = 16;
    config.itemSurrogateConfig.poleLimits = {{{4, 0.0}, {0, 0.0}, {0, 0.0}}};
    config.itemSurrogateConfig.failFastPoleAreaRatio = 0.5;

    jaguar::CollisionEngine engine(bounds.value(), config);
    std::vector<std::shared_ptr<const jaguar::SimplePolygon>> hazards;
    for (std::size_t row = 0; row < 12; ++row)
    {
        for (std::size_t column = 0; column < 12; ++column)
        {
            const double x = 2.0 + static_cast<double>(column) * 8.0;
            const double y = 2.0 + static_cast<double>(row) * 8.0;
            auto shape = std::make_shared<const jaguar::SimplePolygon>(
                rectangle(x, y, x + 2.5, y + 2.5));
            hazards.push_back(shape);
            const auto registered = engine.registerHazard({
                {jaguar::HazardKind::PlacedItem, row * 12 + column, 0, 0},
                shape, true});
            assert(registered.hasValue());
        }
    }

    const jaguar::SimplePolygon queryBase = rectangle(0.0, 0.0, 2.0, 2.0);
    auto surrogateShape = queryBase;
    const auto generatedSurrogate =
        surrogateShape.generateSurrogate(config.itemSurrogateConfig);
    if (!generatedSurrogate.hasValue())
        return 1;
    std::vector<jaguar::Transform2d> transforms;
    std::vector<jaguar::SimplePolygon> queries;
    for (std::size_t i = 0; i < 2000; ++i)
    {
        const double x = static_cast<double>((i * 17) % 96);
        const double y = static_cast<double>((i * 29) % 96);
        const auto transform = jaguar::Transform2d::fromRotationTranslation(
            static_cast<double>(i % 4) * 0.5 * std::acos(-1.0), {x, y});
        transforms.push_back(transform);
        queries.push_back(queryBase.transformed(transform));
    }

    std::size_t bruteHits = 0;
    auto start = Clock::now();
    for (const auto& query : queries)
    {
        bool hit = false;
        if (outsideBounds(query, bounds.value()))
            hit = true;
        for (const auto& hazard : hazards)
        {
            if (!hit && brutePolygonCollision(query, *hazard))
            {
                hit = true;
                break;
            }
        }
        bruteHits += hit ? 1U : 0U;
    }
    const double bruteMilliseconds = elapsedMilliseconds(start);

    std::size_t quadtreeHits = 0;
    start = Clock::now();
    for (const auto& query : queries)
        quadtreeHits += engine.detectPolygonCollision(query, jaguar::NoFilter{}) ? 1U : 0U;
    const double quadtreeMilliseconds = elapsedMilliseconds(start);
    if (quadtreeHits != bruteHits)
    {
        std::cerr << "collision baseline mismatch: brute=" << bruteHits
                  << " quadtree=" << quadtreeHits << '\n';
        for (std::size_t i = 0; i < queries.size(); ++i)
        {
            const bool brute = [&] {
                if (outsideBounds(queries[i], bounds.value()))
                    return true;
                for (const auto& hazard : hazards)
                    if (brutePolygonCollision(queries[i], *hazard))
                        return true;
                return false;
            }();
            const bool indexed = engine.detectPolygonCollision(
                queries[i], jaguar::NoFilter{});
            if (brute != indexed)
            {
                std::cerr << "first mismatch index=" << i
                          << " brute=" << brute << " quadtree=" << indexed
                          << " bbox=[" << queries[i].boundingBox().xMin << ','
                          << queries[i].boundingBox().yMin << ','
                          << queries[i].boundingBox().xMax << ','
                          << queries[i].boundingBox().yMax << "]\n";
                break;
            }
        }
        return 1;
    }

    std::size_t surrogateHits = 0;
    start = Clock::now();
    for (const auto& transform : transforms)
    {
        surrogateHits += engine.detectSurrogateCollision(
                             *surrogateShape.surrogate(), transform, jaguar::NoFilter{})
                             ? 1U
                             : 0U;
    }
    const double surrogateMilliseconds = elapsedMilliseconds(start);
    if (surrogateHits > quadtreeHits)
    {
        std::cerr << "surrogate screening cannot exceed exact hit count: surrogate="
                  << surrogateHits << " exact=" << quadtreeHits << '\n';
        return 1;
    }

    std::size_t batchHits = 0;
    start = Clock::now();
    for (std::size_t repeat = 0; repeat < 20; ++repeat)
    {
        const auto batch = engine.detectPolygonCollisions(queries);
        batchHits += static_cast<std::size_t>(
            std::count(batch.begin(), batch.end(), static_cast<std::uint8_t>(1)));
    }
    const double batchMilliseconds = elapsedMilliseconds(start);
    assert(batchHits == quadtreeHits * 20);

    std::size_t parallelBatchHits = 0;
    start = Clock::now();
    jaguar::BatchCollisionOptions parallelOptions{};
    parallelOptions.workerCount = 4;
    parallelOptions.grainSize = 32;
    for (std::size_t repeat = 0; repeat < 20; ++repeat)
    {
        const auto batch = engine.detectPolygonCollisionsParallel(
            queries, jaguar::NoFilter{}, parallelOptions);
        parallelBatchHits += static_cast<std::size_t>(
            std::count(batch.begin(), batch.end(), static_cast<std::uint8_t>(1)));
    }
    const double parallelBatchMilliseconds = elapsedMilliseconds(start);
    assert(parallelBatchHits == quadtreeHits * 20);

    const auto soaProbePolygon = jaguar::SimplePolygon::create({
        {0.0, 0.0}, {10.0, 0.0}, {10.0, 10.0}, {0.0, 10.0}});
    assert(soaProbePolygon.hasValue());
    const jaguar::PolygonSoA soaProbe(soaProbePolygon.value());
    std::vector<jaguar::Edge2d> soaEdges;
    soaEdges.reserve(5000);
    for (std::size_t i = 0; i < 5000; ++i)
    {
        const double x = static_cast<double>((i * 13) % 24) - 7.0;
        const double y = static_cast<double>((i * 19) % 24) - 7.0;
        const auto candidate = jaguar::Edge2d::create(
            {x, y},
            {x + 13.0, y + static_cast<double>(static_cast<int>(i % 5) - 2)});
        assert(candidate.hasValue());
        soaEdges.push_back(candidate.value());
    }
    const auto soaScalar = benchmarkSoaMode(
        soaProbe, soaEdges, jaguar::SoaExecutionMode::Scalar);
    const auto soaAuto = benchmarkSoaMode(
        soaProbe, soaEdges, jaguar::SoaExecutionMode::Auto);
    const auto soaAvx2 = benchmarkSoaMode(
        soaProbe, soaEdges, jaguar::SoaExecutionMode::Avx2);
    assert(soaScalar.hits == soaAuto.hits);
    assert(soaScalar.hits == soaAvx2.hits);

    std::cout << std::fixed << std::setprecision(3)
              << "collision,brute_ms," << bruteMilliseconds << '\n'
              << "collision,quadtree_ms," << quadtreeMilliseconds << '\n'
              << "collision,surrogate_ms," << surrogateMilliseconds << '\n'
              << "collision,batch_ms," << batchMilliseconds << '\n'
              << "collision,parallel_batch_ms," << parallelBatchMilliseconds << '\n'
              << "collision,soa_scalar_ms," << soaScalar.milliseconds << '\n'
              << "collision,soa_auto_ms," << soaAuto.milliseconds << '\n'
              << "collision,soa_avx2_ms," << soaAvx2.milliseconds << '\n'
              << "collision,brute_hits," << bruteHits << '\n'
              << "collision,quadtree_hits," << quadtreeHits << '\n'
              << "collision,surrogate_hits," << surrogateHits << '\n'
              << "collision,soa_hits," << soaScalar.hits << '\n'
              << "collision,soa_avx2_supported," <<
                     (jaguar::avx2Supported() ? 1 : 0) << '\n';
    return 0;
}
