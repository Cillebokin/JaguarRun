#include "JaguarCore/Geometry/Transformation.h"

#include <cmath>

namespace jaguar
{
JAGUARCORE_API Point2d Transform2d::Apply(const Point2d& point) const noexcept
{
    const double sinValue = std::sin(rotationRad);
    const double cosValue = std::cos(rotationRad);
    return Point2d{cosValue * point.x - sinValue * point.y + translation.x,
                   sinValue * point.x + cosValue * point.y + translation.y};
}

JAGUARCORE_API Transform2d Transform2d::Inverse() const noexcept
{
    const double inverseRotation = -rotationRad;
    const double sinValue = std::sin(inverseRotation);
    const double cosValue = std::cos(inverseRotation);
    const Point2d inverseTranslation{
        -(cosValue * translation.x - sinValue * translation.y),
        -(sinValue * translation.x + cosValue * translation.y)};
    return Transform2d{inverseRotation, inverseTranslation};
}

JAGUARCORE_API Transform2d Transform2d::FollowedBy(const Transform2d& next) const noexcept
{
    return Transform2d{rotationRad + next.rotationRad, next.Apply(translation)};
}
}
