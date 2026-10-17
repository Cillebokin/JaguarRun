#include "../Source/Collision/SoaGeometry.h"
#include "JaguarCore/Geometry/Edge.h"
#include "JaguarCore/Geometry/SimplePolygon.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <random>
#include <utility>
#include <vector>

namespace
{
std::uint64_t rawBits(double value) noexcept
{
    std::uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value), "unexpected double size");
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

void assertSameRawBits(double actual, double expected)
{
    assert(rawBits(actual) == rawBits(expected));
}

void assertSoaPreservesRawBits(const jaguar::SimplePolygon& polygon,
                               const jaguar::PolygonSoA& soa)
{
    assert(soa.VertexCount() == polygon.VertexCount());
    assert(soa.edgeCount() == polygon.VertexCount());

    const auto& vertices = polygon.Vertices();
    for (std::size_t i = 0; i < vertices.size(); ++i)
    {
        const jaguar::Point2d soaVertex = soa.vertex(i);
        assertSameRawBits(soaVertex.x, vertices[i].x);
        assertSameRawBits(soaVertex.y, vertices[i].y);

        const jaguar::Edge2d soaEdge = soa.edge(i);
        const jaguar::Point2d& expectedStart = vertices[i];
        const jaguar::Point2d& expectedEnd = vertices[(i + 1) % vertices.size()];
        assertSameRawBits(soaEdge.start.x, expectedStart.x);
        assertSameRawBits(soaEdge.start.y, expectedStart.y);
        assertSameRawBits(soaEdge.end.x, expectedEnd.x);
        assertSameRawBits(soaEdge.end.y, expectedEnd.y);
    }

    const jaguar::Rect2d& expectedBounds = polygon.BoundingBox();
    const jaguar::Rect2d& soaBounds = soa.BoundingBox();
    assertSameRawBits(soaBounds.xMin, expectedBounds.xMin);
    assertSameRawBits(soaBounds.yMin, expectedBounds.yMin);
    assertSameRawBits(soaBounds.xMax, expectedBounds.xMax);
    assertSameRawBits(soaBounds.yMax, expectedBounds.yMax);
}

void assertModeParity(const jaguar::PolygonSoA& polygon,
                      const jaguar::Edge2d& edge,
                      double tolerance)
{
    const bool scalarBoundary = polygon.intersectsBoundary(
        edge, tolerance, jaguar::SoaExecutionMode::Scalar);
    const bool automaticBoundary = polygon.intersectsBoundary(
        edge, tolerance, jaguar::SoaExecutionMode::Auto);
    const bool avx2Boundary = polygon.intersectsBoundary(
        edge, tolerance, jaguar::SoaExecutionMode::Avx2);
    assert(automaticBoundary == scalarBoundary);
    assert(avx2Boundary == scalarBoundary);

    const bool scalarIntersects = polygon.Intersects(
        edge, tolerance, jaguar::SoaExecutionMode::Scalar);
    const bool automaticIntersects = polygon.Intersects(
        edge, tolerance, jaguar::SoaExecutionMode::Auto);
    const bool avx2Intersects = polygon.Intersects(
        edge, tolerance, jaguar::SoaExecutionMode::Avx2);
    assert(automaticIntersects == scalarIntersects);
    assert(avx2Intersects == scalarIntersects);
}

jaguar::SimplePolygon makeRegularPolygon(std::size_t vertexCount)
{
    constexpr double pi = 3.14159265358979323846;
    std::vector<jaguar::Point2d> vertices;
    vertices.reserve(vertexCount);
    for (std::size_t i = 0; i < vertexCount; ++i)
    {
        const double angle = 2.0 * pi * static_cast<double>(i) /
                             static_cast<double>(vertexCount);
        vertices.push_back({10.0 * std::cos(angle), 10.0 * std::sin(angle)});
    }

    auto polygon = jaguar::SimplePolygon::Create(std::move(vertices));
    assert(polygon.HasValue());
    return polygon.Value();
}

void runSoaModeParityMatrix()
{
    constexpr double tolerances[] = {0.0, 1.0e-9, 1.0e-3};
    const auto insidePolygonResult = jaguar::SimplePolygon::Create({
        {-1.0, -1.0}, {1.0, -1.0}, {1.0, 1.0}, {-1.0, 1.0}});
    assert(insidePolygonResult.HasValue());
    const jaguar::PolygonSoA insidePolygon(insidePolygonResult.Value());

    for (std::size_t vertexCount = 3; vertexCount <= 11; ++vertexCount)
    {
        const jaguar::SimplePolygon source = makeRegularPolygon(vertexCount);
        const jaguar::PolygonSoA soa(source);
        assertSoaPreservesRawBits(source, soa);

        for (std::size_t edgeIndex = 0; edgeIndex < soa.edgeCount(); ++edgeIndex)
        {
            const jaguar::Edge2d boundary = soa.edge(edgeIndex);
            const jaguar::Point2d midpoint{
                (boundary.start.x + boundary.end.x) / 2.0,
                (boundary.start.y + boundary.end.y) / 2.0};
            const double dx = boundary.end.x - boundary.start.x;
            const double dy = boundary.end.y - boundary.start.y;
            const double length = std::sqrt(dx * dx + dy * dy);
            assert(length > 0.0);
            const jaguar::Point2d normal{-dy / length, dx / length};
            const auto crossing = jaguar::Edge2d::Create(
                {midpoint.x - 2.0 * normal.x, midpoint.y - 2.0 * normal.y},
                {midpoint.x + 2.0 * normal.x, midpoint.y + 2.0 * normal.y});
            assert(crossing.HasValue());

            for (double tolerance : tolerances)
            {
                assertModeParity(soa, crossing.Value(), tolerance);
                assert(soa.intersectsBoundary(
                    crossing.Value(), tolerance,
                    jaguar::SoaExecutionMode::Scalar));
                assert(soa.intersectsBoundary(
                    crossing.Value(), tolerance,
                    jaguar::SoaExecutionMode::Avx2));
            }

            constexpr double nearBoundaryOffset = 1.0e-3;
            const auto nearParallel = jaguar::Edge2d::Create(
                {boundary.start.x + nearBoundaryOffset * normal.x,
                 boundary.start.y + nearBoundaryOffset * normal.y},
                {boundary.end.x + nearBoundaryOffset * normal.x,
                 boundary.end.y + nearBoundaryOffset * normal.y});
            assert(nearParallel.HasValue());
            constexpr double nearTolerances[] = {0.5e-3, 1.0e-3, 2.0e-3};
            for (double tolerance : nearTolerances)
                assertModeParity(soa, nearParallel.Value(), tolerance);
        }

        const auto interiorEdge = jaguar::Edge2d::Create(
            {-0.125, -0.25}, {0.125, 0.25});
        assert(interiorEdge.HasValue());
        assert(!soa.intersectsBoundary(
            interiorEdge.Value(), 1.0e-9, jaguar::SoaExecutionMode::Scalar));
        assert(soa.Intersects(
            interiorEdge.Value(), 1.0e-9, jaguar::SoaExecutionMode::Scalar));
        assertModeParity(soa, interiorEdge.Value(), 1.0e-9);

        assert(insidePolygon.Intersects(
            soa, 1.0e-9, jaguar::SoaExecutionMode::Scalar));
        assert(insidePolygon.Intersects(
            soa, 1.0e-9, jaguar::SoaExecutionMode::Auto));
        assert(insidePolygon.Intersects(
            soa, 1.0e-9, jaguar::SoaExecutionMode::Avx2));

        std::mt19937 random(0x534F4132u + static_cast<std::uint32_t>(vertexCount));
        std::uniform_real_distribution<double> coordinate(-12.0, 12.0);
        for (std::size_t i = 0; i < 128; ++i)
        {
            const auto candidate = jaguar::Edge2d::Create(
                {coordinate(random), coordinate(random)},
                {coordinate(random), coordinate(random)});
            if (!candidate.HasValue())
                continue;
            for (double tolerance : tolerances)
                assertModeParity(soa, candidate.Value(), tolerance);
        }
    }

    const auto signedZeroPolygon = jaguar::SimplePolygon::Create({
        {-0.0, -0.0}, {4.0, -0.0}, {4.0, 3.0}, {-0.0, 3.0}});
    assert(signedZeroPolygon.HasValue());
    const jaguar::PolygonSoA signedZeroSoa(signedZeroPolygon.Value());
    assertSoaPreservesRawBits(signedZeroPolygon.Value(), signedZeroSoa);
}
}

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

#if !defined(JAGUARCORE_AVX2_COMPILED)
    assert(!jaguar::avx2Supported());
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

    runSoaModeParityMatrix();
}
