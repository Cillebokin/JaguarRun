#pragma once

#include "JaguarCore/geometry/point.h"
#include "JaguarCore/export.h"

namespace jaguar
{
struct Transform2d
{
    double rotationRad = 0.0;
    Point2d translation{};

    static Transform2d identity() noexcept { return {}; }

    static Transform2d fromRotationTranslation(double rotation, Point2d offset) noexcept
    {
        return Transform2d{rotation, offset};
    }

    JAGUARCORE_API Point2d apply(const Point2d& point) const noexcept;
    JAGUARCORE_API Transform2d inverse() const noexcept;
    JAGUARCORE_API Transform2d followedBy(const Transform2d& next) const noexcept;
};
}
