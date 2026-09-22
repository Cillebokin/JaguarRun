#pragma once

#include "JaguarCore/Geometry/Point.h"
#include "JaguarCore/Geometry/Rect.h"
#include "JaguarCore/Export.h"
#include "JaguarCore/NumericPolicy.h"
#include "JaguarCore/Result.h"

namespace jaguar
{
struct Edge2d
{
    Point2d start{};
    Point2d end{};

    static JAGUARCORE_API Result<Edge2d> Create(Point2d startPoint, Point2d endPoint);

    JAGUARCORE_API Rect2d BoundingBox() const noexcept;
    JAGUARCORE_API double Length() const noexcept;
    JAGUARCORE_API Point2d ClosestPoint(const Point2d& point) const noexcept;
    JAGUARCORE_API bool Intersects(
        const Edge2d& other,
        double tolerance = NumericPolicy::DefaultEqualityTolerance()) const noexcept;
};
}
