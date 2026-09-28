#include "JaguarCore/Collision/CollisionEngine.h"
#include "JaguarCore/Geometry/Edge.h"
#include "JaguarCore/Geometry/SimplePolygon.h"

#include <cassert>
#include <cmath>
#include <memory>

namespace
{
std::shared_ptr<const jaguar::SimplePolygon> rectangleShape(
    double xMin, double yMin, double xMax, double yMax)
{
    const auto polygon = jaguar::SimplePolygon::Create({
        {xMin, yMin}, {xMax, yMin}, {xMax, yMax}, {xMin, yMax}});
    assert(polygon.HasValue());
    return std::make_shared<const jaguar::SimplePolygon>(polygon.Value());
}

jaguar::SimplePolygon Rectangle(double xMin,
                                double yMin,
                                double xMax,
                                double yMax)
{
    return *rectangleShape(xMin, yMin, xMax, yMax);
}
}

void runBoundaryDifferentialTests()
{
    const auto rootBounds = jaguar::Rect2d::Create(0.0, 0.0, 10.0, 10.0);
    assert(rootBounds.HasValue());
    jaguar::CollisionEngine rootBoundedEngine(rootBounds.Value(), jaguar::CDEConfig{});
    const auto rootExterior = rootBoundedEngine.RegisterHazard({
        {jaguar::HazardKind::Exterior, 0, 0, 0},
        rectangleShape(0.0, 0.0, 10.0, 10.0), false});
    assert(rootExterior.HasValue());
    const auto partiallyOutOfBounds = jaguar::SimplePolygon::Create({
        {-1.0, 1.0}, {1.0, 1.0}, {1.0, 2.0}, {-1.0, 2.0}});
    assert(partiallyOutOfBounds.HasValue());
    jaguar::HazardFilter ignoreExterior{};
    ignoreExterior.irrelevant = [](const jaguar::HazardEntity& entity) {
        return entity.kind == jaguar::HazardKind::Exterior;
    };
    // Rust returns collision immediately when a polygon extends beyond the CDE root,
    // before consulting the hazard filter.
    assert(rootBoundedEngine.DetectPolygonCollision(
        partiallyOutOfBounds.Value(), ignoreExterior));

    const auto proper = jaguar::Edge2d::Create({0.0, 0.0}, {4.0, 4.0});
    const auto tangent = jaguar::Edge2d::Create({0.0, 4.0}, {4.0, 0.0});
    assert(proper.HasValue() && tangent.HasValue());
    assert(proper.Value().Intersects(tangent.Value()));

    const auto endpointFirst = jaguar::Edge2d::Create({0.0, 0.0}, {4.0, 0.0});
    const auto endpointSecond = jaguar::Edge2d::Create({4.0, 0.0}, {4.0, 4.0});
    assert(endpointFirst.HasValue() && endpointSecond.HasValue());
    assert(endpointFirst.Value().Intersects(endpointSecond.Value()));

    const auto collinearOverlapFirst = jaguar::Edge2d::Create({0.0, 0.0}, {4.0, 0.0});
    const auto collinearOverlapSecond = jaguar::Edge2d::Create({2.0, 0.0}, {6.0, 0.0});
    assert(collinearOverlapFirst.HasValue() && collinearOverlapSecond.HasValue());
    assert(!collinearOverlapFirst.Value().Intersects(collinearOverlapSecond.Value()));

    const auto gap = jaguar::Edge2d::Create({0.0, 0.0}, {4.0, 0.0});
    const auto smallGap = jaguar::Edge2d::Create({4.0 + 5.0e-10, 0.0},
                                                   {8.0, 1.0});
    const auto largeGap = jaguar::Edge2d::Create({4.0 + 5.0e-7, 0.0},
                                                   {8.0, 1.0});
    assert(gap.HasValue() && smallGap.HasValue() && largeGap.HasValue());
    assert(gap.Value().Intersects(smallGap.Value()));
    assert(!gap.Value().Intersects(largeGap.Value()));

    const auto bounds = jaguar::Rect2d::Create(0.0, 0.0, 20.0, 20.0);
    assert(bounds.HasValue());
    jaguar::CollisionEngine engine(bounds.Value(), jaguar::CDEConfig{});
    const auto hazard = engine.RegisterHazard({
        {jaguar::HazardKind::PlacedItem, 900, 0, 0},
        rectangleShape(4.0, 4.0, 6.0, 6.0), true});
    assert(hazard.HasValue());

    const auto cornerTouch = Rectangle(6.0, 6.0, 8.0, 8.0);
    const auto smallPolygonGap = Rectangle(6.0 + 5.0e-10, 6.0,
                                            8.0 + 5.0e-10, 8.0);
    const auto largePolygonGap = Rectangle(6.0 + 5.0e-7, 6.0,
                                            8.0 + 5.0e-7, 8.0);
    assert(engine.DetectPolygonCollision(cornerTouch, jaguar::NoFilter{}));
    // The 5e-10 gap rounds to the hazard boundary in Rust's f32 geometry.
    assert(engine.DetectPolygonCollision(smallPolygonGap, jaguar::NoFilter{}));
    assert(!engine.DetectPolygonCollision(largePolygonGap, jaguar::NoFilter{}));

    const auto interior = Rectangle(4.0 + 5.0e-10, 4.0 + 5.0e-10,
                                    6.0 - 5.0e-10, 6.0 - 5.0e-10);
    assert(engine.DetectPolygonCollision(interior, jaguar::NoFilter{}));
}
