#include "JaguarCore/collision/collision_engine.h"
#include "JaguarCore/geometry/edge.h"
#include "JaguarCore/geometry/simple_polygon.h"

#include <cassert>
#include <cmath>
#include <memory>

namespace
{
std::shared_ptr<const jaguar::SimplePolygon> rectangleShape(
    double xMin, double yMin, double xMax, double yMax)
{
    const auto polygon = jaguar::SimplePolygon::create({
        {xMin, yMin}, {xMax, yMin}, {xMax, yMax}, {xMin, yMax}});
    assert(polygon.hasValue());
    return std::make_shared<const jaguar::SimplePolygon>(polygon.value());
}

jaguar::SimplePolygon rectangle(double xMin,
                                double yMin,
                                double xMax,
                                double yMax)
{
    return *rectangleShape(xMin, yMin, xMax, yMax);
}
}

void runBoundaryDifferentialTests()
{
    const auto proper = jaguar::Edge2d::create({0.0, 0.0}, {4.0, 4.0});
    const auto tangent = jaguar::Edge2d::create({0.0, 4.0}, {4.0, 0.0});
    assert(proper.hasValue() && tangent.hasValue());
    assert(proper.value().intersects(tangent.value()));

    const auto endpointFirst = jaguar::Edge2d::create({0.0, 0.0}, {4.0, 0.0});
    const auto endpointSecond = jaguar::Edge2d::create({4.0, 0.0}, {4.0, 4.0});
    assert(endpointFirst.hasValue() && endpointSecond.hasValue());
    assert(endpointFirst.value().intersects(endpointSecond.value()));

    const auto collinearOverlapFirst = jaguar::Edge2d::create({0.0, 0.0}, {4.0, 0.0});
    const auto collinearOverlapSecond = jaguar::Edge2d::create({2.0, 0.0}, {6.0, 0.0});
    assert(collinearOverlapFirst.hasValue() && collinearOverlapSecond.hasValue());
    assert(!collinearOverlapFirst.value().intersects(collinearOverlapSecond.value()));

    const auto gap = jaguar::Edge2d::create({0.0, 0.0}, {4.0, 0.0});
    const auto smallGap = jaguar::Edge2d::create({4.0 + 5.0e-10, 0.0},
                                                   {8.0, 1.0});
    const auto largeGap = jaguar::Edge2d::create({4.0 + 5.0e-7, 0.0},
                                                   {8.0, 1.0});
    assert(gap.hasValue() && smallGap.hasValue() && largeGap.hasValue());
    assert(gap.value().intersects(smallGap.value()));
    assert(!gap.value().intersects(largeGap.value()));

    const auto bounds = jaguar::Rect2d::create(0.0, 0.0, 20.0, 20.0);
    assert(bounds.hasValue());
    jaguar::CollisionEngine engine(bounds.value(), jaguar::CDEConfig{});
    const auto hazard = engine.registerHazard({
        {jaguar::HazardKind::PlacedItem, 900, 0, 0},
        rectangleShape(4.0, 4.0, 6.0, 6.0), true});
    assert(hazard.hasValue());

    const auto cornerTouch = rectangle(6.0, 6.0, 8.0, 8.0);
    const auto smallPolygonGap = rectangle(6.0 + 5.0e-10, 6.0,
                                            8.0 + 5.0e-10, 8.0);
    const auto largePolygonGap = rectangle(6.0 + 5.0e-7, 6.0,
                                            8.0 + 5.0e-7, 8.0);
    assert(engine.detectPolygonCollision(cornerTouch, jaguar::NoFilter{}));
    assert(!engine.detectPolygonCollision(smallPolygonGap, jaguar::NoFilter{}));
    assert(!engine.detectPolygonCollision(largePolygonGap, jaguar::NoFilter{}));

    const auto interior = rectangle(4.0 + 5.0e-10, 4.0 + 5.0e-10,
                                    6.0 - 5.0e-10, 6.0 - 5.0e-10);
    assert(engine.detectPolygonCollision(interior, jaguar::NoFilter{}));
}
