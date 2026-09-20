#pragma once

#include "JaguarCore/export.h"
#include "JaguarCore/geometry/simple_polygon.h"
#include "JaguarCore/geometry/shape_modification.h"
#include "JaguarCore/geometry/transformation.h"

namespace jaguar
{
struct OriginalShape
{
    SimplePolygon shape;
    Transform2d preTransform = Transform2d::identity();
    ShapeModifyMode modifyMode = ShapeModifyMode::Inflate;
    ShapeModifyConfig modifyConfig{};

    JAGUARCORE_API Result<SimplePolygon> convertToInternal() const;
};
}

