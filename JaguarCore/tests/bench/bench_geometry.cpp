#include "JaguarCore/geometry/shape_modification.h"
#include "JaguarCore/geometry/simple_polygon.h"
#include "JaguarCore/geometry/transformation.h"

#include <cassert>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <vector>

namespace
{
using Clock = std::chrono::steady_clock;

jaguar::SimplePolygon makeRadialPolygon(std::size_t vertexCount)
{
    std::vector<jaguar::Point2d> points;
    points.reserve(vertexCount);
    const double pi = std::acos(-1.0);
    for (std::size_t i = 0; i < vertexCount; ++i)
    {
        const double angle = 2.0 * pi * static_cast<double>(i) /
                             static_cast<double>(vertexCount);
        const double radius = (i % 2 == 0) ? 100.0 : 72.0;
        points.push_back({radius * std::cos(angle), radius * std::sin(angle)});
    }
    const auto polygon = jaguar::SimplePolygon::create(std::move(points));
    assert(polygon.hasValue());
    return polygon.value();
}

double elapsedMilliseconds(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
}

int runGeometryBench()
{
    const jaguar::SimplePolygon polygon = makeRadialPolygon(128);
    constexpr std::size_t iterations = 20000;
    volatile double checksum = 0.0;

    auto start = Clock::now();
    for (std::size_t i = 0; i < iterations; ++i)
    {
        const auto edges = polygon.edges();
        checksum += polygon.area() + polygon.diameter();
        checksum += static_cast<double>(edges.size());
        checksum += polygon.contains({0.25, 0.5}) ? 1.0 : 0.0;
    }
    const double primitiveMilliseconds = elapsedMilliseconds(start);

    start = Clock::now();
    for (std::size_t i = 0; i < iterations; ++i)
    {
        const double angle = static_cast<double>(i % 360) * std::acos(-1.0) / 180.0;
        const auto transformed = polygon.transformed(
            jaguar::Transform2d::fromRotationTranslation(angle, {3.0, -2.0}));
        checksum += transformed.area();
    }
    const double transformMilliseconds = elapsedMilliseconds(start);

    const auto simplificationInput = jaguar::SimplePolygon::create({
        {0.0, 0.0}, {20.0, 0.0}, {20.0, 20.0}, {16.0, 20.0},
        {16.0, 19.9}, {14.0, 19.9}, {14.0, 20.0}, {0.0, 20.0}});
    assert(simplificationInput.hasValue());
    start = Clock::now();
    for (std::size_t i = 0; i < 500; ++i)
    {
        const auto simplified = jaguar::simplifyShape(
            simplificationInput.value(), jaguar::ShapeModifyMode::Inflate, 0.02);
        if (!simplified.hasValue())
            return 1;
        checksum += simplified.value().area();
    }
    const double simplifyMilliseconds = elapsedMilliseconds(start);

    std::cout << std::fixed << std::setprecision(3)
              << "geometry,primitive_ms," << primitiveMilliseconds << '\n'
              << "geometry,transform_ms," << transformMilliseconds << '\n'
              << "geometry,simplify_ms," << simplifyMilliseconds << '\n'
              << "geometry,checksum," << checksum << '\n';
    return std::isfinite(static_cast<double>(checksum)) ? 0 : 1;
}
