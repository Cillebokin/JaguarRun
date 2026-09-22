#include "JaguarCore/Geometry/ConvexHull.h"
#include "JaguarCore/Geometry/SimplePolygon.h"

#include <cassert>
#include <cmath>

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

    const auto hull = jaguar::ConvexHull({
        {0.0, 0.0}, {2.0, 0.0}, {2.0, 2.0}, {0.0, 2.0}, {1.0, 1.0}});
    assert(hull.size() == 4);
}
