#pragma once

#include "JaguarCore/Geometry/SimplePolygon.h"
#include "JaguarCore/Result.h"

#include <vector>

namespace jaguar::detail
{
Result<std::vector<SimplePolygon>> roundedStraightSkeletonOffsets(
    const SimplePolygon& shape, double signedDistance);
}
