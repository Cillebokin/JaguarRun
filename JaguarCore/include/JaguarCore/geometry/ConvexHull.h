#pragma once

#include "JaguarCore/Export.h"
#include "JaguarCore/Geometry/Point.h"

#include <vector>

namespace jaguar
{
// 计算点集的凸包
JAGUARCORE_API std::vector<Point2d> ConvexHull(std::vector<Point2d> points);
}
