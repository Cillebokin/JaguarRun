#include "JaguarCore/Geometry/circle.h"
#include "JaguarCore/Geometry/Edge.h"
#include "JaguarCore/Geometry/SimplePolygon.h"
#include "JaguarCore/Geometry/Transformation.h"
#include "JaguarCore/NumericPolicy.h"

#include <cassert>
#include <cmath>
#include <limits>

void runGeometryTests()
{
    const auto diagonal = jaguar::Edge2d::Create({0.0, 0.0}, {3.0, 4.0});
    assert(diagonal.HasValue());
    assert(std::abs(diagonal.Value().Length() - 5.0) < 1.0e-12);
    assert(diagonal.Value().BoundingBox().Contains({1.5, 2.0}));

    const auto crossingA = jaguar::Edge2d::Create({0.0, 0.0}, {10.0, 10.0});
    const auto crossingB = jaguar::Edge2d::Create({0.0, 10.0}, {10.0, 0.0});
    assert(crossingA.HasValue() && crossingB.HasValue());
    assert(crossingA.Value().Intersects(crossingB.Value()));

    const auto parallel = jaguar::Edge2d::Create({0.0, 1.0}, {10.0, 11.0});
    assert(parallel.HasValue());
    assert(!crossingA.Value().Intersects(parallel.Value()));

    const auto invalidEdge = jaguar::Edge2d::Create({1.0, 1.0}, {1.0, 1.0});
    assert(!invalidEdge.HasValue());

    const auto circle = jaguar::Circle2d::Create({2.0, 3.0}, 5.0);
    assert(circle.HasValue());
    assert(circle.Value().Contains({2.0, 3.0}));
    assert(!circle.Value().Contains({8.0, 3.0}));

    const auto invalidCircle = jaguar::Circle2d::Create({0.0, 0.0}, 0.0);
    assert(!invalidCircle.HasValue());

    const jaguar::Transform2d transform =
        jaguar::Transform2d::FromRotationTranslation(0.5 * std::acos(-1.0), {10.0, 20.0});
    const jaguar::Point2d transformed = transform.Apply({1.0, 0.0});
    assert(std::abs(transformed.x - 10.0) < 1.0e-12);
    assert(std::abs(transformed.y - 21.0) < 1.0e-12);

    const jaguar::Point2d restored = transform.Inverse().Apply(transformed);
    assert(std::abs(restored.x - 1.0) < 1.0e-12);
    assert(std::abs(restored.y) < 1.0e-12);

    const auto polygon = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {2.0, 0.0}, {0.0, 2.0}});
    assert(polygon.HasValue());
    const auto invalidTransform = polygon.Value().Transformed(
        jaguar::Transform2d::FromRotationTranslation(
            std::numeric_limits<double>::quiet_NaN(),
            {0.0, std::numeric_limits<double>::infinity()}));
    assert(!invalidTransform.HasValue());
    assert(invalidTransform.GetError().code == jaguar::ErrorCode::InvalidInput);

    const jaguar::NumericPolicy strictPolicy{
        1.0e-9, 1.0e-7, 1.0, 1.0e-3};
    const auto strictPolygon = jaguar::SimplePolygon::Create(
        {{0.0, 0.0}, {0.5, 0.0}, {0.0, 0.5}}, strictPolicy);
    assert(!strictPolygon.HasValue());
    assert(strictPolygon.GetError().code == jaguar::ErrorCode::ZeroArea);
}
