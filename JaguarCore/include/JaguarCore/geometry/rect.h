#pragma once

#include "JaguarCore/geometry/point.h"
#include "JaguarCore/result.h"

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

    static Result<Rect2d> create(double xMin, double yMin, double xMax, double yMax)
    {
        if (!Point2d{xMin, yMin}.isFinite() || !Point2d{xMax, yMax}.isFinite() ||
            xMin >= xMax || yMin >= yMax)
        {
            return Result<Rect2d>::fromError({ErrorCode::InvalidInput, "invalid rectangle"});
        }
        return Result<Rect2d>::fromValue(Rect2d{xMin, yMin, xMax, yMax});
    }

    double width() const noexcept { return xMax - xMin; }
    double height() const noexcept { return yMax - yMin; }
    double area() const noexcept { return width() * height(); }

    Rect2d inflated(double dx, double dy) const noexcept
    {
        return Rect2d{xMin - dx, yMin - dy, xMax + dx, yMax + dy};
    }

    std::array<Rect2d, 4> quadrants() const noexcept
    {
        const double midX = (xMin + xMax) * 0.5;
        const double midY = (yMin + yMax) * 0.5;
        return {Rect2d{xMin, yMin, midX, midY}, Rect2d{midX, yMin, xMax, midY},
                Rect2d{midX, midY, xMax, yMax}, Rect2d{xMin, midY, midX, yMax}};
    }

    bool contains(const Point2d& point) const noexcept
    {
        return point.x >= xMin && point.x <= xMax && point.y >= yMin && point.y <= yMax;
    }

    bool intersects(const Rect2d& other) const noexcept
    {
        return std::max(xMin, other.xMin) <= std::min(xMax, other.xMax) &&
               std::max(yMin, other.yMin) <= std::min(yMax, other.yMax);
    }

    RectRelation relationTo(const Rect2d& other) const noexcept
    {
        if (!intersects(other))
            return RectRelation::Disjoint;
        if (xMin <= other.xMin && yMin <= other.yMin && xMax >= other.xMax &&
            yMax >= other.yMax)
            return RectRelation::Surrounding;
        if (xMin >= other.xMin && yMin >= other.yMin && xMax <= other.xMax &&
            yMax <= other.yMax)
            return RectRelation::Enclosed;
        return RectRelation::Intersecting;
    }

    std::optional<Rect2d> intersection(const Rect2d& other) const noexcept
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
