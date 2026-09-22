#pragma once

#include "JaguarCore/Export.h"
#include "JaguarCore/Geometry/SimplePolygon.h"
#include "JaguarCore/Geometry/ShapeModification.h"
#include "JaguarCore/Geometry/Transformation.h"

namespace jaguar
{
struct OriginalShape
{
    SimplePolygon shape;
    Transform2d preTransform = Transform2d::Identity();
    ShapeModifyMode modifyMode = ShapeModifyMode::Inflate;
    ShapeModifyConfig modifyConfig{};

    JAGUARCORE_API Result<SimplePolygon> ConvertToInternal() const;
};
}

