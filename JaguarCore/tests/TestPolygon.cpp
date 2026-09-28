#include "JaguarCore/Geometry/ConvexHull.h"
#include "JaguarCore/Geometry/SimplePolygon.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

void runPolygonTests()
{
    const auto rectangle = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {0.0, 2.0}, {2.0, 2.0}, {2.0, 0.0}});
    assert(rectangle.HasValue());
    assert(std::abs(rectangle.Value().Area() - 4.0) < 1.0e-12);
    assert(std::abs(rectangle.Value().Centroid().x - 1.0) < 1.0e-12);
    assert(std::abs(rectangle.Value().Centroid().y - 1.0) < 1.0e-12);
    assert(rectangle.Value().Contains({1.0, 1.0}));
    assert(!rectangle.Value().Contains({3.0, 1.0}));
    assert(rectangle.Value().Contains({0.0, 1.0}));
    assert(std::abs(rectangle.Value().DistanceTo({3.0, 1.0}) - 1.0) < 1.0e-12);

    const auto nearZeroVertexSquare = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {2.0, 0.0}, {2.0, 2.0}, {0.0, 2.0}});
    assert(nearZeroVertexSquare.HasValue());
    const double nearZeroY = static_cast<double>(
        std::numeric_limits<float>::epsilon() * 0.25F);
    assert(!nearZeroVertexSquare.Value().ContainsRustF32({1.0, nearZeroY}));

    const auto tinyNonzeroSquare = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {0.0, 1.0e-5}, {1.0e-5, 1.0e-5}, {1.0e-5, 0.0}});
    assert(tinyNonzeroSquare.HasValue());
    assert(std::abs(tinyNonzeroSquare.Value().Area() - 1.0e-10) < 1.0e-15);

    const auto duplicate = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {2.0, 0.0}, {2.0, 2.0}, {2.0, 2.0}, {0.0, 2.0}});
    assert(!duplicate.HasValue());
    assert(duplicate.GetError().code == jaguar::ErrorCode::InvalidPolygon);

    const auto bowTie = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {2.0, 2.0}, {0.0, 2.0}, {2.0, 0.0}});
    assert(!bowTie.HasValue());
    assert(bowTie.GetError().code == jaguar::ErrorCode::SelfIntersection);

    const auto line = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {1.0, 0.0}, {2.0, 0.0}});
    assert(!line.HasValue());
    assert(line.GetError().code == jaguar::ErrorCode::ZeroArea);

    const auto concave = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {4.0, 0.0}, {4.0, 4.0}, {2.0, 2.0}, {0.0, 4.0}});
    assert(concave.HasValue());
    const auto crossingEdge = jaguar::Edge2d::Create({-1.0, 2.0}, {5.0, 2.0});
    assert(crossingEdge.HasValue());
    assert(concave.Value().Intersects(crossingEdge.Value()));

    const auto uShape = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {6.0, 0.0}, {6.0, 6.0}, {4.0, 6.0},
        {4.0, 2.0}, {2.0, 2.0}, {2.0, 6.0}, {0.0, 6.0}});
    assert(uShape.HasValue());
    assert(!uShape.Value().Contains(uShape.Value().Centroid()));
    const auto poi = uShape.Value().PointOfInaccessibility();
    assert(poi.radius > 0.0);
    assert(uShape.Value().ContainsRustF32(poi.center));
    assert(poi.center != uShape.Value().Centroid());

    const auto transformed = uShape.Value().TransformedRustF32(
        jaguar::Transform2d::FromRotationTranslation(0.1, {3.0, -2.0}));
    assert(transformed.HasValue());
    const float cosine = std::cos(0.1F);
    const float sine = std::sin(0.1F);
    const auto& first = uShape.Value().Vertices().front();
    const auto& firstTransformed = transformed.Value().Vertices().front();
    assert(firstTransformed.x == static_cast<double>(
        cosine * static_cast<float>(first.x)
        - sine * static_cast<float>(first.y) + 3.0F));
    assert(firstTransformed.y == static_cast<double>(
        sine * static_cast<float>(first.x)
        + cosine * static_cast<float>(first.y) - 2.0F));

    const auto metricShape = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {10.0, 0.0}, {10.0, 3.0}, {0.0, 3.0}});
    assert(metricShape.HasValue());
    const auto metricShapeTransform = metricShape.Value().TransformedRustF32(
        jaguar::Transform2d::FromRotationTranslation(
            0.37, {1'000'000.0, -2'000'000.0}));
    assert(metricShapeTransform.HasValue());
    assert(metricShapeTransform.Value().Area() == metricShape.Value().Area());
    assert(metricShapeTransform.Value().Diameter() == metricShape.Value().Diameter());
    const auto floatBits = [](double value) {
        const float normalized = static_cast<float>(value);
        std::uint32_t bits = 0;
        std::memcpy(&bits, &normalized, sizeof(bits));
        return bits;
    };
    const auto signedZeroShape = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {2.0, 0.0}, {2.0, 1.0}, {-0.0, 1.0}});
    assert(signedZeroShape.HasValue());
    assert(floatBits(signedZeroShape.Value().BoundingBox().xMin) == 0x80000000U);

    const auto translatedCollapseSource = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {1.0, 0.0}, {4.0, 1.0}, {0.0, 1.0}});
    assert(translatedCollapseSource.HasValue());
    const auto translatedCollapse = translatedCollapseSource.Value().TransformedRustF32(
        jaguar::Transform2d::FromRotationTranslation(0.0, {16'777'216.0, 0.0}));
    assert(translatedCollapse.HasValue());
    assert(translatedCollapse.Value().Vertices().size() == 4);
    assert(floatBits(translatedCollapse.Value().BoundingBox().xMin) == 0x4b800000U);
    assert(floatBits(translatedCollapse.Value().BoundingBox().xMax) == 0x4b800002U);
    assert(translatedCollapse.Value().Area() == translatedCollapseSource.Value().Area());
    assert(floatBits(translatedCollapse.Value().Diameter()) == 0x4083f07bU);
    assert(floatBits(translatedCollapse.Value().Centroid().x) == 0x4b4ccccdU);
    assert(floatBits(translatedCollapse.Value().Centroid().y) == 0x3f088889U);

    const auto degenerateBoundsSource = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}, {0.0, 1.0}});
    assert(degenerateBoundsSource.HasValue());
    const auto degenerateTransform = degenerateBoundsSource.Value().TransformedRustF32(
        jaguar::Transform2d::FromRotationTranslation(0.0, {16'777'216.0, 0.0}));
    assert(!degenerateTransform.HasValue());

    assert(floatBits(metricShapeTransform.Value().Centroid().x) == 0xcead93cdU);
    assert(floatBits(metricShapeTransform.Value().Centroid().y) == 0x4f2d9255U);

    const auto hull = jaguar::ConvexHull({
        {0.0, 0.0}, {2.0, 0.0}, {2.0, 2.0}, {0.0, 2.0}, {1.0, 1.0}});
    assert(hull.size() == 4);

    const auto tiedXHull = jaguar::ConvexHull({
        {0.0, 1.0}, {1.0, 0.0}, {0.0, 0.0}, {1.0, 1.0}});
    assert(tiedXHull.size() == 4);
    assert((tiedXHull[0] == jaguar::Point2d{0.0, 1.0}));
    assert((tiedXHull[1] == jaguar::Point2d{0.0, 0.0}));
    assert((tiedXHull[2] == jaguar::Point2d{1.0, 0.0}));
    assert((tiedXHull[3] == jaguar::Point2d{1.0, 1.0}));

    const auto singletonHull = jaguar::ConvexHull({{1.0, 2.0}});
    assert(singletonHull.empty());
}
