#pragma once

#include "JaguarCore/geometry/simple_polygon.h"
#include "JaguarCore/result.h"

#include <vector>

namespace jaguar::detail
{
Result<std::vector<SimplePolygon>> roundedStraightSkeletonOffsets(
    const SimplePolygon& shape, double signedDistance);
}
