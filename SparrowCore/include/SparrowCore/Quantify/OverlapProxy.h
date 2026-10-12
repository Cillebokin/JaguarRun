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
    float epsilon) noexcept;

// Quantifies an already colliding pair using Sparrow's polygon loss formula.
// The low-level overload above intentionally remains available for callers
// that need the raw pole proxy.
SPARROWCORE_API float CalculatePolygonCollisionLoss(
    const jaguar::SimplePolygon& first,
    const jaguar::SimplePolygon& second) noexcept;

SPARROWCORE_API float CalculateContainerPenalty(
    const jaguar::SimplePolygon& shape,
    const jaguar::Rect2d& containerBounds);
}
