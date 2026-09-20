#include "../src/collision/soa_geometry.h"
#include "JaguarCore/geometry/edge.h"
#include "JaguarCore/geometry/simple_polygon.h"

#include <cassert>
#include <cmath>
#include <random>

void runSoaTests()
{
    const auto rectangle = jaguar::SimplePolygon::create({
        {0.0, 0.0}, {10.0, 0.0}, {10.0, 10.0}, {0.0, 10.0}});
    assert(rectangle.hasValue());
    const jaguar::PolygonSoA soa(rectangle.value());
    assert(soa.vertexCount() == 4);
    assert(soa.edgeCount() == 4);
    assert(soa.boundingBox().xMin == rectangle.value().boundingBox().xMin);
    assert(soa.boundingBox().yMax == rectangle.value().boundingBox().yMax);
    assert(soa.contains({5.0, 5.0}));
    assert(soa.contains({0.0, 5.0}));
    assert(!soa.contains({11.0, 5.0}));

    const auto crossing = jaguar::Edge2d::create({-1.0, 5.0}, {11.0, 5.0});
    const auto disjoint = jaguar::Edge2d::create({-1.0, -1.0}, {-1.0, 11.0});
    const auto collinear = jaguar::Edge2d::create({2.0, 0.0}, {8.0, 0.0});
    assert(crossing.hasValue() && disjoint.hasValue() && collinear.hasValue());
    assert(soa.intersects(crossing.value()));
    assert(!soa.intersects(disjoint.value()));
    assert(soa.intersects(collinear.value()));

    const auto inner = jaguar::SimplePolygon::create({
        {2.0, 2.0}, {4.0, 2.0}, {4.0, 4.0}, {2.0, 4.0}});
    const auto crossingPolygon = jaguar::SimplePolygon::create({
        {-1.0, 4.0}, {4.0, 4.0}, {4.0, 6.0}, {-1.0, 6.0}});
    assert(inner.hasValue() && crossingPolygon.hasValue());
    const jaguar::PolygonSoA innerSoa(inner.value());
    const jaguar::PolygonSoA crossingSoa(crossingPolygon.value());
    assert(soa.intersects(innerSoa));
    assert(soa.intersects(crossingSoa));

    const auto disjointPolygon = jaguar::SimplePolygon::create({
        {20.0, 20.0}, {22.0, 20.0}, {22.0, 22.0}, {20.0, 22.0}});
    assert(disjointPolygon.hasValue());
    assert(!soa.intersects(jaguar::PolygonSoA(disjointPolygon.value())));

    const bool runtimeAvx2Supported = jaguar::avx2Supported();
    assert(runtimeAvx2Supported == jaguar::avx2Supported());
#if defined(JAGUARCORE_AVX2_COMPILED)
    // The AVX2 object is present, but execution still depends on the runtime
    // CPU/OS feature check; mode equivalence below covers either outcome.
    (void)runtimeAvx2Supported;
#else
    assert(!runtimeAvx2Supported);
#endif
    assert(soa.intersectsBoundary(
        crossing.value(), 1.0e-9, jaguar::SoaExecutionMode::Scalar));
    assert(soa.intersectsBoundary(
        crossing.value(), 1.0e-9, jaguar::SoaExecutionMode::Auto));
    assert(soa.intersectsBoundary(
        crossing.value(), 1.0e-9, jaguar::SoaExecutionMode::Avx2));

    std::mt19937 random(0x534F4132u);
    std::uniform_real_distribution<double> coordinate(-20.0, 20.0);
    for (std::size_t i = 0; i < 1000; ++i)
    {
        const auto candidate = jaguar::Edge2d::create(
            {coordinate(random), coordinate(random)},
            {coordinate(random), coordinate(random)});
        if (!candidate.hasValue())
            continue;
        const bool scalar = soa.intersects(
            candidate.value(), 1.0e-9, jaguar::SoaExecutionMode::Scalar);
        const bool automatic = soa.intersects(
            candidate.value(), 1.0e-9, jaguar::SoaExecutionMode::Auto);
        const bool avx2 = soa.intersects(
            candidate.value(), 1.0e-9, jaguar::SoaExecutionMode::Avx2);
        assert(automatic == scalar);
        assert(avx2 == scalar);
    }

    assert(soa.intersects(
        innerSoa, 1.0e-9, jaguar::SoaExecutionMode::Scalar));
    assert(soa.intersects(
        innerSoa, 1.0e-9, jaguar::SoaExecutionMode::Auto));
    assert(soa.intersects(
        innerSoa, 1.0e-9, jaguar::SoaExecutionMode::Avx2));
}
