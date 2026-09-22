#pragma once

#include "JaguarCore/Geometry/Point.h"
#include "JaguarCore/Export.h"

#include <cmath>

namespace jaguar
{
struct Transform2d
{
    double rotationRad = 0.0;
    Point2d translation{};

    inline static Transform2d Identity() noexcept { return {}; }

    inline static Transform2d FromRotationTranslation(
        double rotation, Point2d offset) noexcept
    {
        return Transform2d{rotation, offset};
    }

    inline bool IsFinite() const noexcept
    {
        return std::isfinite(rotationRad) && translation.IsFinite();
    }

    JAGUARCORE_API Point2d Apply(const Point2d& point) const noexcept;
    JAGUARCORE_API Transform2d Inverse() const noexcept;
    JAGUARCORE_API Transform2d FollowedBy(const Transform2d& next) const noexcept;
};
}
