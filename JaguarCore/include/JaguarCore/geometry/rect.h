#pragma once

#include "JaguarCore/Geometry/Point.h"
#include "JaguarCore/Result.h"

#include <algorithm>
#include <array>
#include <optional>

namespace jaguar
{
enum class RectRelation
{
    Disjoint,
    Intersecting,
    Enclosed,
    Surrounding,
};

struct Rect2d
{
    double xMin = 0.0;
    double yMin = 0.0;
    double xMax = 0.0;
    double yMax = 0.0;

    inline static Result<Rect2d> Create(
        double xMin, double yMin, double xMax, double yMax)
    {
        if (!Point2d{xMin, yMin}.IsFinite() || !Point2d{xMax, yMax}.IsFinite() ||
            xMin >= xMax || yMin >= yMax)
        {
            return Result<Rect2d>::FromError({ErrorCode::InvalidInput, "invalid rectangle"});
        }
        return Result<Rect2d>::FromValue(Rect2d{xMin, yMin, xMax, yMax});
    }

    inline double Width() const noexcept { return xMax - xMin; }
    inline double Height() const noexcept { return yMax - yMin; }
    inline double Area() const noexcept { return Width() * Height(); }

    inline Rect2d Inflated(double dx, double dy) const noexcept
    {
        return Rect2d{xMin - dx, yMin - dy, xMax + dx, yMax + dy};
    }

    inline std::array<Rect2d, 4> Quadrants() const noexcept
    {
        const double midX = (xMin + xMax) * 0.5;
        const double midY = (yMin + yMax) * 0.5;
        return {Rect2d{xMin, yMin, midX, midY}, Rect2d{midX, yMin, xMax, midY},
                Rect2d{midX, midY, xMax, yMax}, Rect2d{xMin, midY, midX, yMax}};
    }

    inline bool Contains(const Point2d& point) const noexcept
    {
        return point.x >= xMin && point.x <= xMax && point.y >= yMin && point.y <= yMax;
    }

    inline bool Intersects(const Rect2d& other) const noexcept
    {
        return std::max(xMin, other.xMin) <= std::min(xMax, other.xMax) &&
               std::max(yMin, other.yMin) <= std::min(yMax, other.yMax);
    }

    inline RectRelation RelationTo(const Rect2d& other) const noexcept
    {
        if (!Intersects(other))
            return RectRelation::Disjoint;
        if (xMin <= other.xMin && yMin <= other.yMin && xMax >= other.xMax &&
            yMax >= other.yMax)
            return RectRelation::Surrounding;
        if (xMin >= other.xMin && yMin >= other.yMin && xMax <= other.xMax &&
            yMax <= other.yMax)
            return RectRelation::Enclosed;
        return RectRelation::Intersecting;
    }

    inline std::optional<Rect2d> Intersection(const Rect2d& other) const noexcept
    {
        const double newXMin = std::max(xMin, other.xMin);
        const double newYMin = std::max(yMin, other.yMin);
        const double newXMax = std::min(xMax, other.xMax);
        const double newYMax = std::min(yMax, other.yMax);
        if (newXMin >= newXMax || newYMin >= newYMax)
            return std::nullopt;
        return Rect2d{newXMin, newYMin, newXMax, newYMax};
    }
};
}
