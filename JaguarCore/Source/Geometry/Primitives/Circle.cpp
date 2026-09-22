#include "JaguarCore/Geometry/Circle.h"

#include <cmath>

namespace jaguar
{
JAGUARCORE_API Result<Circle2d> Circle2d::Create(Point2d centerPoint, double radiusValue)
{
    if (!centerPoint.IsFinite() || !std::isfinite(radiusValue) || radiusValue <= 0.0)
    {
        return Result<Circle2d>::FromError({ErrorCode::InvalidInput, "invalid circle"});
    }
    return Result<Circle2d>::FromValue(Circle2d{centerPoint, radiusValue});
}

JAGUARCORE_API double Circle2d::Area() const noexcept
{
    return std::acos(-1.0) * radius * radius;
}

JAGUARCORE_API Rect2d Circle2d::BoundingBox() const noexcept
{
    return Rect2d{center.x - radius, center.y - radius, center.x + radius, center.y + radius};
}

JAGUARCORE_API bool Circle2d::Contains(const Point2d& point) const noexcept
{
    return center.SquaredDistanceTo(point) <= radius * radius;
}
}
