#pragma once

#include "JaguarCore/geometry/point.h"
#include "JaguarCore/geometry/rect.h"
#include "JaguarCore/export.h"
#include "JaguarCore/result.h"

namespace jaguar
{
struct Circle2d
{
    Point2d center{};
    double radius = 0.0;

    static JAGUARCORE_API Result<Circle2d> create(Point2d centerPoint, double radiusValue);

    JAGUARCORE_API double area() const noexcept;
    JAGUARCORE_API Rect2d boundingBox() const noexcept;
    JAGUARCORE_API bool contains(const Point2d& point) const noexcept;
};
}
