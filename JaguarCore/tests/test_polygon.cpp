#include "JaguarCore/geometry/convex_hull.h"
#include "JaguarCore/geometry/simple_polygon.h"

#include <cassert>
#include <cmath>

void runPolygonTests()
{
    const auto rectangle = jaguar::SimplePolygon::create({
        {0.0, 0.0}, {0.0, 2.0}, {2.0, 2.0}, {2.0, 0.0}});
    assert(rectangle.hasValue());
    assert(std::abs(rectangle.value().area() - 4.0) < 1.0e-12);
    assert(std::abs(rectangle.value().centroid().x - 1.0) < 1.0e-12);
    assert(std::abs(rectangle.value().centroid().y - 1.0) < 1.0e-12);
    assert(rectangle.value().contains({1.0, 1.0}));
    assert(!rectangle.value().contains({3.0, 1.0}));
    assert(rectangle.value().contains({0.0, 1.0}));
    assert(std::abs(rectangle.value().distanceTo({3.0, 1.0}) - 1.0) < 1.0e-12);

    const auto duplicate = jaguar::SimplePolygon::create({
        {0.0, 0.0}, {2.0, 0.0}, {2.0, 2.0}, {2.0, 2.0}, {0.0, 2.0}});
    assert(!duplicate.hasValue());
    assert(duplicate.error().code == jaguar::ErrorCode::InvalidPolygon);

    const auto bowTie = jaguar::SimplePolygon::create({
        {0.0, 0.0}, {2.0, 2.0}, {0.0, 2.0}, {2.0, 0.0}});
    assert(!bowTie.hasValue());
    assert(bowTie.error().code == jaguar::ErrorCode::SelfIntersection);

    const auto line = jaguar::SimplePolygon::create({
        {0.0, 0.0}, {1.0, 0.0}, {2.0, 0.0}});
    assert(!line.hasValue());
    assert(line.error().code == jaguar::ErrorCode::ZeroArea);

    const auto concave = jaguar::SimplePolygon::create({
        {0.0, 0.0}, {4.0, 0.0}, {4.0, 4.0}, {2.0, 2.0}, {0.0, 4.0}});
    assert(concave.hasValue());
    const auto crossingEdge = jaguar::Edge2d::create({-1.0, 2.0}, {5.0, 2.0});
    assert(crossingEdge.hasValue());
    assert(concave.value().intersects(crossingEdge.value()));

    const auto hull = jaguar::convexHull({
        {0.0, 0.0}, {2.0, 0.0}, {2.0, 2.0}, {0.0, 2.0}, {1.0, 1.0}});
    assert(hull.size() == 4);
}
