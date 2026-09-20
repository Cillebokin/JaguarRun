#pragma once

#include "JaguarCore/geometry/point.h"
#include "JaguarCore/geometry/rect.h"
#include "JaguarCore/export.h"
#include "JaguarCore/result.h"

namespace jaguar
{
struct Edge2d
{
    Point2d start{};
    Point2d end{};

    static JAGUARCORE_API Result<Edge2d> create(Point2d startPoint, Point2d endPoint);

    JAGUARCORE_API Rect2d boundingBox() const noexcept;
    JAGUARCORE_API double length() const noexcept;
    JAGUARCORE_API Point2d closestPoint(const Point2d& point) const noexcept;
    JAGUARCORE_API bool intersects(const Edge2d& other, double tolerance = 1.0e-9) const noexcept;
};
}
