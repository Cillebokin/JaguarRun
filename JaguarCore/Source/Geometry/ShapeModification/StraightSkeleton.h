#pragma once

#include "JaguarCore/Geometry/SimplePolygon.h"
#include "JaguarCore/Result.h"

#include <vector>

namespace jaguar::detail
{
Result<SimplePolygon> createRustImportedOffsetPolygon(
    std::vector<Point2d> points);

Result<std::vector<SimplePolygon>> roundedStraightSkeletonOffsets(
    const SimplePolygon& shape, double signedDistance);
}
