#pragma once

#include "JaguarCore/Export.h"
#include "JaguarCore/Geometry/Circle.h"
#include "JaguarCore/Result.h"

#include <vector>

namespace jaguar
{
class SimplePolygon;

// 计算与排除圆不相交、且位于多边形内部的最大圆
JAGUARCORE_API Result<Circle2d> ComputePointOfInaccessibility(
    const SimplePolygon& shape,
    const std::vector<Circle2d>& excluded = {});
}
