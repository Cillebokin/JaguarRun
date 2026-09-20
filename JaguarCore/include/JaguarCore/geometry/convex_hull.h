#pragma once

#include "JaguarCore/export.h"
#include "JaguarCore/geometry/point.h"

#include <vector>

namespace jaguar
{
JAGUARCORE_API std::vector<Point2d> convexHull(std::vector<Point2d> points);
}
