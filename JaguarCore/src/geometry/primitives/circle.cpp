#include "JaguarCore/geometry/circle.h"

#include <cmath>

namespace jaguar
{
JAGUARCORE_API Result<Circle2d> Circle2d::create(Point2d centerPoint, double radiusValue)
{
    if (!centerPoint.isFinite() || !std::isfinite(radiusValue) || radiusValue <= 0.0)
    {
        return Result<Circle2d>::fromError({ErrorCode::InvalidInput, "invalid circle"});
    }
    return Result<Circle2d>::fromValue(Circle2d{centerPoint, radiusValue});
}

JAGUARCORE_API double Circle2d::area() const noexcept
{
    return std::acos(-1.0) * radius * radius;
}

JAGUARCORE_API Rect2d Circle2d::boundingBox() const noexcept
{
    return Rect2d{center.x - radius, center.y - radius, center.x + radius, center.y + radius};
}

JAGUARCORE_API bool Circle2d::contains(const Point2d& point) const noexcept
{
    return center.squaredDistanceTo(point) <= radius * radius;
}
}
