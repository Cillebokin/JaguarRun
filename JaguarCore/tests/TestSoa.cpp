#include "../Source/Collision/SoaGeometry.h"
#include "JaguarCore/Geometry/Edge.h"
#include "JaguarCore/Geometry/SimplePolygon.h"

#include <cassert>
#include <cmath>
#include <random>

void runSoaTests()
{
    const auto rectangle = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {10.0, 0.0}, {10.0, 10.0}, {0.0, 10.0}});
    assert(rectangle.HasValue());
    const jaguar::PolygonSoA soa(rectangle.Value());
    assert(soa.VertexCount() == 4);
    assert(soa.edgeCount() == 4);
    assert(soa.BoundingBox().xMin == rectangle.Value().BoundingBox().xMin);
    assert(soa.BoundingBox().yMax == rectangle.Value().BoundingBox().yMax);
    assert(soa.Contains({5.0, 5.0}));
    assert(soa.Contains({0.0, 5.0}));
    assert(!soa.Contains({11.0, 5.0}));

    const auto crossing = jaguar::Edge2d::Create({-1.0, 5.0}, {11.0, 5.0});
    const auto disjoint = jaguar::Edge2d::Create({-1.0, -1.0}, {-1.0, 11.0});
    const auto collinear = jaguar::Edge2d::Create({2.0, 0.0}, {8.0, 0.0});
    assert(crossing.HasValue() && disjoint.HasValue() && collinear.HasValue());
    assert(soa.Intersects(crossing.Value()));
    assert(!soa.Intersects(disjoint.Value()));
    assert(soa.Intersects(collinear.Value()));

    const auto inner = jaguar::SimplePolygon::Create({
        {2.0, 2.0}, {4.0, 2.0}, {4.0, 4.0}, {2.0, 4.0}});
    const auto crossingPolygon = jaguar::SimplePolygon::Create({
        {-1.0, 4.0}, {4.0, 4.0}, {4.0, 6.0}, {-1.0, 6.0}});
    assert(inner.HasValue() && crossingPolygon.HasValue());
    const jaguar::PolygonSoA innerSoa(inner.Value());
    const jaguar::PolygonSoA crossingSoa(crossingPolygon.Value());
    assert(soa.Intersects(innerSoa));
    assert(soa.Intersects(crossingSoa));

    const auto disjointPolygon = jaguar::SimplePolygon::Create({
        {20.0, 20.0}, {22.0, 20.0}, {22.0, 22.0}, {20.0, 22.0}});
    assert(disjointPolygon.HasValue());
    assert(!soa.Intersects(jaguar::PolygonSoA(disjointPolygon.Value())));

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
        crossing.Value(), 1.0e-9, jaguar::SoaExecutionMode::Scalar));
    assert(soa.intersectsBoundary(
        crossing.Value(), 1.0e-9, jaguar::SoaExecutionMode::Auto));
    assert(soa.intersectsBoundary(
        crossing.Value(), 1.0e-9, jaguar::SoaExecutionMode::Avx2));

    std::mt19937 random(0x534F4132u);
    std::uniform_real_distribution<double> coordinate(-20.0, 20.0);
    for (std::size_t i = 0; i < 1000; ++i)
    {
        const auto candidate = jaguar::Edge2d::Create(
            {coordinate(random), coordinate(random)},
            {coordinate(random), coordinate(random)});
        if (!candidate.HasValue())
            continue;
        const bool scalar = soa.Intersects(
            candidate.Value(), 1.0e-9, jaguar::SoaExecutionMode::Scalar);
        const bool automatic = soa.Intersects(
            candidate.Value(), 1.0e-9, jaguar::SoaExecutionMode::Auto);
        const bool avx2 = soa.Intersects(
            candidate.Value(), 1.0e-9, jaguar::SoaExecutionMode::Avx2);
        assert(automatic == scalar);
        assert(avx2 == scalar);
    }

    assert(soa.Intersects(
        innerSoa, 1.0e-9, jaguar::SoaExecutionMode::Scalar));
    assert(soa.Intersects(
        innerSoa, 1.0e-9, jaguar::SoaExecutionMode::Auto));
    assert(soa.Intersects(
        innerSoa, 1.0e-9, jaguar::SoaExecutionMode::Avx2));
}
