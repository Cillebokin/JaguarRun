#include "JaguarCore/geometry/circle.h"
#include "JaguarCore/geometry/edge.h"
#include "JaguarCore/geometry/transformation.h"

#include <cassert>
#include <cmath>

void runGeometryTests()
{
    const auto diagonal = jaguar::Edge2d::create({0.0, 0.0}, {3.0, 4.0});
    assert(diagonal.hasValue());
    assert(std::abs(diagonal.value().length() - 5.0) < 1.0e-12);
    assert(diagonal.value().boundingBox().contains({1.5, 2.0}));

    const auto crossingA = jaguar::Edge2d::create({0.0, 0.0}, {10.0, 10.0});
    const auto crossingB = jaguar::Edge2d::create({0.0, 10.0}, {10.0, 0.0});
    assert(crossingA.hasValue() && crossingB.hasValue());
    assert(crossingA.value().intersects(crossingB.value()));

    const auto parallel = jaguar::Edge2d::create({0.0, 1.0}, {10.0, 11.0});
    assert(parallel.hasValue());
    assert(!crossingA.value().intersects(parallel.value()));

    const auto invalidEdge = jaguar::Edge2d::create({1.0, 1.0}, {1.0, 1.0});
    assert(!invalidEdge.hasValue());

    const auto circle = jaguar::Circle2d::create({2.0, 3.0}, 5.0);
    assert(circle.hasValue());
    assert(circle.value().contains({2.0, 3.0}));
    assert(!circle.value().contains({8.0, 3.0}));

    const auto invalidCircle = jaguar::Circle2d::create({0.0, 0.0}, 0.0);
    assert(!invalidCircle.hasValue());

    const jaguar::Transform2d transform =
        jaguar::Transform2d::fromRotationTranslation(0.5 * std::acos(-1.0), {10.0, 20.0});
    const jaguar::Point2d transformed = transform.apply({1.0, 0.0});
    assert(std::abs(transformed.x - 10.0) < 1.0e-12);
    assert(std::abs(transformed.y - 21.0) < 1.0e-12);

    const jaguar::Point2d restored = transform.inverse().apply(transformed);
    assert(std::abs(restored.x - 1.0) < 1.0e-12);
    assert(std::abs(restored.y) < 1.0e-12);
}
