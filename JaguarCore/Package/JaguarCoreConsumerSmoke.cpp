#include "JaguarCore/Collision/CollisionEngine.h"
#include "JaguarCore/Geometry/SimplePolygon.h"
#include "JaguarCore/Version.h"

#include <cassert>
#include <cmath>
#include <limits>
#include <memory>
#include <string>

int main()
{
    assert(std::string(JaguarCoreVersion()) == "0.1.0-dev");
    assert(JaguarCoreApiVersion() == 2U);

    const auto polygon = jaguar::SimplePolygon::Create(
        {{0.0, 0.0}, {2.0, 0.0}, {0.0, 2.0}});
    assert(polygon.HasValue());

    const auto invalid = polygon.Value().Transformed(
        jaguar::Transform2d::FromRotationTranslation(
            std::numeric_limits<double>::quiet_NaN(), {0.0, 0.0}));
    assert(!invalid.HasValue());
    assert(invalid.GetError().code == jaguar::ErrorCode::InvalidInput);

    const auto bounds = jaguar::Rect2d::Create(0.0, 0.0, 10.0, 10.0);
    assert(bounds.HasValue());
    jaguar::CollisionEngine engine(bounds.Value(), jaguar::CDEConfig{});
    assert(!engine.DetectPolygonCollision(polygon.Value(), jaguar::NoFilter{}));
    return 0;
}
