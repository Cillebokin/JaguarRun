#pragma once

#include "JaguarCore/Geometry/Point.h"
#include "JaguarCore/Geometry/RustF32.h"
#include "JaguarCore/Export.h"
#include "JaguarCore/Result.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <utility>
#include <vector>

namespace jaguar
{
struct Edge2d;

// 矩形之间的相对关系
enum class RectRelation
{
    Disjoint,     // 不相交
    Intersecting, // 相交
    Enclosed,     // 被另一个矩形包含
    Surrounding,  // 包含另一个矩形
};

enum class GeoPosition
{
    Interior,
    Exterior,
};

using SeparationDistance2d = std::pair<GeoPosition, double>;

// 二维轴对齐矩形
struct Rect2d
{
    double xMin = 0.0; // 最小 X 坐标
    double yMin = 0.0; // 最小 Y 坐标
    double xMax = 0.0; // 最大 X 坐标
    double yMax = 0.0; // 最大 Y 坐标

    // 创建有效矩形
    inline static Result<Rect2d> Create(
        double xMin, double yMin, double xMax, double yMax)
    {
        const float xMinF32 = static_cast<float>(xMin);
        const float yMinF32 = static_cast<float>(yMin);
        const float xMaxF32 = static_cast<float>(xMax);
        const float yMaxF32 = static_cast<float>(yMax);
        if (!(xMinF32 < xMaxF32 && yMinF32 < yMaxF32))
        {
            return Result<Rect2d>::FromError({ErrorCode::InvalidInput, "invalid rectangle"});
        }
        return Result<Rect2d>::FromValue(
            Rect2d{static_cast<double>(xMinF32),
                  static_cast<double>(yMinF32),
                  static_cast<double>(xMaxF32),
                  static_cast<double>(yMaxF32)});
    }

    inline static Result<Rect2d> FromDiagonalCornersRustF32(
        const Point2d& first,
        const Point2d& second)
    {
        const float xMinF32 = detail::MinRustF32(
            static_cast<float>(first.x), static_cast<float>(second.x));
        const float yMinF32 = detail::MinRustF32(
            static_cast<float>(first.y), static_cast<float>(second.y));
        const float xMaxF32 = detail::MaxRustF32(
            static_cast<float>(first.x), static_cast<float>(second.x));
        const float yMaxF32 = detail::MaxRustF32(
            static_cast<float>(first.y), static_cast<float>(second.y));
        return Create(xMinF32, yMinF32, xMaxF32, yMaxF32);
    }

    // 获取宽度
    inline double Width() const noexcept { return xMax - xMin; }
    // 获取高度
    inline double Height() const noexcept { return yMax - yMin; }
    // 计算面积
    inline double Area() const noexcept { return Width() * Height(); }

    inline double WidthRustF32() const noexcept
    {
        return static_cast<double>(static_cast<float>(xMax) -
                                   static_cast<float>(xMin));
    }

    inline double HeightRustF32() const noexcept
    {
        return static_cast<double>(static_cast<float>(yMax) -
                                   static_cast<float>(yMin));
    }

    inline double AreaRustF32() const noexcept
    {
        return static_cast<double>(
            (static_cast<float>(xMax) - static_cast<float>(xMin)) *
            (static_cast<float>(yMax) - static_cast<float>(yMin)));
    }

    inline double DiameterRustF32() const noexcept
    {
        const float width = static_cast<float>(xMax) - static_cast<float>(xMin);
        const float height = static_cast<float>(yMax) - static_cast<float>(yMin);
        return static_cast<double>(std::sqrt(width * width + height * height));
    }

    inline Rect2d InflateToSquareRustF32() const noexcept
    {
        const float minX = static_cast<float>(xMin);
        const float minY = static_cast<float>(yMin);
        const float maxX = static_cast<float>(xMax);
        const float maxY = static_cast<float>(yMax);
        const float width = maxX - minX;
        const float height = maxY - minY;
        float dx = 0.0F;
        float dy = 0.0F;
        if (height < width)
            dy = (width - height) / 2.0F;
        else if (width < height)
            dx = (height - width) / 2.0F;
        return Rect2d{static_cast<double>(minX - dx),
                      static_cast<double>(minY - dy),
                      static_cast<double>(maxX + dx),
                      static_cast<double>(maxY + dy)};
    }

    inline std::optional<Rect2d> ResizeByRustF32(
        double dx,
        double dy) const noexcept
    {
        const float resizeX = static_cast<float>(dx);
        const float resizeY = static_cast<float>(dy);
        const float minX = static_cast<float>(xMin) - resizeX;
        const float minY = static_cast<float>(yMin) - resizeY;
        const float maxX = static_cast<float>(xMax) + resizeX;
        const float maxY = static_cast<float>(yMax) + resizeY;
        if (minX < maxX && minY < maxY)
            return Rect2d{static_cast<double>(minX), static_cast<double>(minY),
                          static_cast<double>(maxX), static_cast<double>(maxY)};
        return std::nullopt;
    }

    inline Result<Rect2d> ScaledRustF32(double factor) const
    {
        const float scale = static_cast<float>(factor) - 1.0F;
        const float dx = (static_cast<float>(xMax) - static_cast<float>(xMin)) *
                         scale / 2.0F;
        const float dy = (static_cast<float>(yMax) - static_cast<float>(yMin)) *
                         scale / 2.0F;
        const auto result = ResizeByRustF32(dx, dy);
        if (!result.has_value())
            return Result<Rect2d>::FromError(
                {ErrorCode::InvalidInput, "scaled rectangle is invalid"});
        return Result<Rect2d>::FromValue(*result);
    }

    inline std::array<Point2d, 4> CornersRustF32() const noexcept
    {
        const double minX = static_cast<double>(static_cast<float>(xMin));
        const double minY = static_cast<double>(static_cast<float>(yMin));
        const double maxX = static_cast<double>(static_cast<float>(xMax));
        const double maxY = static_cast<double>(static_cast<float>(yMax));
        return {Point2d{maxX, maxY}, Point2d{minX, maxY},
                Point2d{minX, minY}, Point2d{maxX, minY}};
    }

    JAGUARCORE_API std::vector<Edge2d> EdgesRustF32() const;
    JAGUARCORE_API std::vector<Edge2d> SidesRustF32() const;

    inline static std::optional<Rect2d> IntersectionRustF32(
        const Rect2d& first,
        const Rect2d& second) noexcept
    {
        const float minX = detail::MaxRustF32(
            static_cast<float>(first.xMin), static_cast<float>(second.xMin));
        const float minY = detail::MaxRustF32(
            static_cast<float>(first.yMin), static_cast<float>(second.yMin));
        const float maxX = detail::MinRustF32(
            static_cast<float>(first.xMax), static_cast<float>(second.xMax));
        const float maxY = detail::MinRustF32(
            static_cast<float>(first.yMax), static_cast<float>(second.yMax));
        if (minX < maxX && minY < maxY)
            return Rect2d{static_cast<double>(minX), static_cast<double>(minY),
                          static_cast<double>(maxX), static_cast<double>(maxY)};
        return std::nullopt;
    }

    inline static Rect2d BoundingRectRustF32(
        const Rect2d& first,
        const Rect2d& second) noexcept
    {
        return Rect2d{
            static_cast<double>(detail::MinRustF32(
                static_cast<float>(first.xMin), static_cast<float>(second.xMin))),
            static_cast<double>(detail::MinRustF32(
                static_cast<float>(first.yMin), static_cast<float>(second.yMin))),
            static_cast<double>(detail::MaxRustF32(
                static_cast<float>(first.xMax), static_cast<float>(second.xMax))),
            static_cast<double>(detail::MaxRustF32(
                static_cast<float>(first.yMax), static_cast<float>(second.yMax)))};
    }

    inline double SquaredDistanceToRustF32(const Point2d& point) const noexcept
    {
        const float x = static_cast<float>(point.x);
        const float y = static_cast<float>(point.y);
        const float minX = static_cast<float>(xMin);
        const float minY = static_cast<float>(yMin);
        const float maxX = static_cast<float>(xMax);
        const float maxY = static_cast<float>(yMax);
        float distance = 0.0F;
        if (x < minX)
        {
            const float dx = x - minX;
            distance += dx * dx;
        }
        else if (x > maxX)
        {
            const float dx = x - maxX;
            distance += dx * dx;
        }
        if (y < minY)
        {
            const float dy = y - minY;
            distance += dy * dy;
        }
        else if (y > maxY)
        {
            const float dy = y - maxY;
            distance += dy * dy;
        }
        return static_cast<double>(std::abs(distance));
    }

    inline double DistanceToRustF32(const Point2d& point) const noexcept
    {
        return static_cast<double>(std::sqrt(static_cast<float>(
            SquaredDistanceToRustF32(point))));
    }

    inline SeparationDistance2d SquaredSeparationDistanceRustF32(
        const Point2d& point) const noexcept
    {
        if (!ContainsRustF32(point))
            return {GeoPosition::Exterior, SquaredDistanceToRustF32(point)};

        const float x = static_cast<float>(point.x);
        const float y = static_cast<float>(point.y);
        const float minDistance = detail::MinRustF32(
            detail::MinRustF32(std::abs(x - static_cast<float>(xMin)),
                               std::abs(x - static_cast<float>(xMax))),
            detail::MinRustF32(std::abs(y - static_cast<float>(yMin)),
                               std::abs(y - static_cast<float>(yMax))));
        return {GeoPosition::Interior,
                static_cast<double>(minDistance * minDistance)};
    }

    inline SeparationDistance2d SeparationDistanceRustF32(
        const Point2d& point) const noexcept
    {
        const SeparationDistance2d separation =
            SquaredSeparationDistanceRustF32(point);
        return {separation.first,
                static_cast<double>(std::sqrt(static_cast<float>(separation.second)))};
    }

    // 按偏移量扩展矩形
    inline Rect2d Inflated(double dx, double dy) const noexcept
    {
        return Rect2d{xMin - dx, yMin - dy, xMax + dx, yMax + dy};
    }

    // 将矩形分为四个子矩形
    inline std::array<Rect2d, 4> Quadrants() const noexcept
    {
        const double midX = (xMin + xMax) * 0.5;
        const double midY = (yMin + yMax) * 0.5;
        return {Rect2d{xMin, yMin, midX, midY}, Rect2d{midX, yMin, xMax, midY},
                Rect2d{midX, midY, xMax, yMax}, Rect2d{xMin, midY, midX, yMax}};
    }

    inline Point2d CentroidRustF32() const noexcept
    {
        const float minX = static_cast<float>(xMin);
        const float minY = static_cast<float>(yMin);
        const float maxX = static_cast<float>(xMax);
        const float maxY = static_cast<float>(yMax);
        const float midX = static_cast<float>(
            (static_cast<double>(minX) + static_cast<double>(maxX)) * 0.5);
        const float midY = static_cast<float>(
            (static_cast<double>(minY) + static_cast<double>(maxY)) * 0.5);
        return Point2d{static_cast<double>(midX), static_cast<double>(midY)};
    }

    // Rust-compatible f32 quadrant split, ordered [top-right, top-left,
    // bottom-left, bottom-right] like jagua-rs::Rect::quadrants.
    inline std::array<Rect2d, 4> QuadrantsRustF32() const noexcept
    {
        const float minX = static_cast<float>(xMin);
        const float minY = static_cast<float>(yMin);
        const float maxX = static_cast<float>(xMax);
        const float maxY = static_cast<float>(yMax);
        const Point2d centroid = CentroidRustF32();
        const float midX = static_cast<float>(centroid.x);
        const float midY = static_cast<float>(centroid.y);
        return {Rect2d{static_cast<double>(midX), static_cast<double>(midY),
                        static_cast<double>(maxX), static_cast<double>(maxY)},
                Rect2d{static_cast<double>(minX), static_cast<double>(midY),
                        static_cast<double>(midX), static_cast<double>(maxY)},
                Rect2d{static_cast<double>(minX), static_cast<double>(minY),
                        static_cast<double>(midX), static_cast<double>(midY)},
                Rect2d{static_cast<double>(midX), static_cast<double>(minY),
                        static_cast<double>(maxX), static_cast<double>(midY)}};
    }

    // 判断是否包含点
    inline bool Contains(const Point2d& point) const noexcept
    {
        return point.x >= xMin && point.x <= xMax && point.y >= yMin && point.y <= yMax;
    }

    inline bool ContainsRustF32(const Point2d& point) const noexcept
    {
        const float pointX = static_cast<float>(point.x);
        const float pointY = static_cast<float>(point.y);
        return pointX >= static_cast<float>(xMin) &&
               pointX <= static_cast<float>(xMax) &&
               pointY >= static_cast<float>(yMin) &&
               pointY <= static_cast<float>(yMax);
    }

    inline bool AlmostContainsRustF32(const Point2d& point) const noexcept
    {
        const float pointX = static_cast<float>(point.x);
        const float pointY = static_cast<float>(point.y);
        return detail::LessEqualRustF32(static_cast<float>(xMin), pointX) &&
               detail::LessEqualRustF32(pointX, static_cast<float>(xMax)) &&
               detail::LessEqualRustF32(static_cast<float>(yMin), pointY) &&
               detail::LessEqualRustF32(pointY, static_cast<float>(yMax));
    }

    // 判断是否与另一个矩形相交
    inline bool Intersects(const Rect2d& other) const noexcept
    {
        return std::max(xMin, other.xMin) <= std::min(xMax, other.xMax) &&
               std::max(yMin, other.yMin) <= std::min(yMax, other.yMax);
    }

    inline bool IntersectsRustF32(const Rect2d& other) const noexcept
    {
        const float firstXMin = static_cast<float>(xMin);
        const float firstYMin = static_cast<float>(yMin);
        const float firstXMax = static_cast<float>(xMax);
        const float firstYMax = static_cast<float>(yMax);
        const float secondXMin = static_cast<float>(other.xMin);
        const float secondYMin = static_cast<float>(other.yMin);
        const float secondXMax = static_cast<float>(other.xMax);
        const float secondYMax = static_cast<float>(other.yMax);
        return detail::MaxRustF32(firstXMin, secondXMin) <=
                   detail::MinRustF32(firstXMax, secondXMax) &&
               detail::MaxRustF32(firstYMin, secondYMin) <=
                   detail::MinRustF32(firstYMax, secondYMax);
    }

    // 获取与另一个矩形的相对关系
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

    inline RectRelation RelationToRustF32(const Rect2d& other) const noexcept
    {
        const float firstXMin = static_cast<float>(xMin);
        const float firstYMin = static_cast<float>(yMin);
        const float firstXMax = static_cast<float>(xMax);
        const float firstYMax = static_cast<float>(yMax);
        const float secondXMin = static_cast<float>(other.xMin);
        const float secondYMin = static_cast<float>(other.yMin);
        const float secondXMax = static_cast<float>(other.xMax);
        const float secondYMax = static_cast<float>(other.yMax);
        if (detail::MaxRustF32(firstXMin, secondXMin) >
                detail::MinRustF32(firstXMax, secondXMax) ||
            detail::MaxRustF32(firstYMin, secondYMin) >
                detail::MinRustF32(firstYMax, secondYMax))
        {
            return RectRelation::Disjoint;
        }
        if (firstXMin <= secondXMin && firstYMin <= secondYMin &&
            firstXMax >= secondXMax && firstYMax >= secondYMax)
        {
            return RectRelation::Surrounding;
        }
        if (firstXMin >= secondXMin && firstYMin >= secondYMin &&
            firstXMax <= secondXMax && firstYMax <= secondYMax)
        {
            return RectRelation::Enclosed;
        }
        return RectRelation::Intersecting;
    }

    inline RectRelation AlmostRelationToRustF32(const Rect2d& other) const noexcept
    {
        const float firstXMin = static_cast<float>(xMin);
        const float firstYMin = static_cast<float>(yMin);
        const float firstXMax = static_cast<float>(xMax);
        const float firstYMax = static_cast<float>(yMax);
        const float secondXMin = static_cast<float>(other.xMin);
        const float secondYMin = static_cast<float>(other.yMin);
        const float secondXMax = static_cast<float>(other.xMax);
        const float secondYMax = static_cast<float>(other.yMax);
        if (!detail::LessEqualRustF32(
                detail::MaxRustF32(firstXMin, secondXMin),
                detail::MinRustF32(firstXMax, secondXMax)) ||
            !detail::LessEqualRustF32(
                detail::MaxRustF32(firstYMin, secondYMin),
                detail::MinRustF32(firstYMax, secondYMax)))
        {
            return RectRelation::Disjoint;
        }
        if (detail::LessEqualRustF32(firstXMin, secondXMin) &&
            detail::LessEqualRustF32(firstYMin, secondYMin) &&
            detail::LessEqualRustF32(secondXMax, firstXMax) &&
            detail::LessEqualRustF32(secondYMax, firstYMax))
        {
            return RectRelation::Surrounding;
        }
        if (detail::LessEqualRustF32(secondXMin, firstXMin) &&
            detail::LessEqualRustF32(secondYMin, firstYMin) &&
            detail::LessEqualRustF32(firstXMax, secondXMax) &&
            detail::LessEqualRustF32(firstYMax, secondYMax))
        {
            return RectRelation::Enclosed;
        }
        return RectRelation::Intersecting;
    }

    // 计算与另一个矩形的交集
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
