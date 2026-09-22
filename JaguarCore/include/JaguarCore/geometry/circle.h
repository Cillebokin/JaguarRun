#pragma once

#include "JaguarCore/Geometry/Point.h"
#include "JaguarCore/Geometry/Rect.h"
#include "JaguarCore/Export.h"
#include "JaguarCore/Result.h"

namespace jaguar
{
struct Circle2d
{
    Point2d center{};
    double radius = 0.0;

    static JAGUARCORE_API Result<Circle2d> Create(Point2d centerPoint, double radiusValue);

    JAGUARCORE_API double Area() const noexcept;
    JAGUARCORE_API Rect2d BoundingBox() const noexcept;
    JAGUARCORE_API bool Contains(const Point2d& point) const noexcept;
};
}
