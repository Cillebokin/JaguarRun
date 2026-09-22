#pragma once

#include "SparrowCore/Export.h"

#include "JaguarCore/FailFast/Surrogate.h"
#include "JaguarCore/Geometry/Rect.h"
#include "JaguarCore/Geometry/SimplePolygon.h"
#include "JaguarCore/Geometry/Transformation.h"

namespace sparrow
{
SPARROWCORE_API float CalculateOverlapProxy(
    const jaguar::Surrogate& first,
    const jaguar::Transform2d& firstTransform,
    const jaguar::Surrogate& second,
    const jaguar::Transform2d& secondTransform,
    float epsilon = 0.01F) noexcept;

SPARROWCORE_API float CalculateContainerPenalty(
    const jaguar::SimplePolygon& shape,
    const jaguar::Rect2d& containerBounds) noexcept;
}
