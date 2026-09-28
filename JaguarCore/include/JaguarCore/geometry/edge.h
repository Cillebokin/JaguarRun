#pragma once

#include "JaguarCore/Geometry/Point.h"
#include "JaguarCore/Geometry/Rect.h"
#include "JaguarCore/Export.h"
#include "JaguarCore/NumericPolicy.h"
#include "JaguarCore/Result.h"

#include <optional>

namespace jaguar
{
struct Transform2d;

// 二维线段
struct Edge2d
{
    Point2d start{}; // 起点
    Point2d end{};   // 终点

    // 创建线段
    static JAGUARCORE_API Result<Edge2d> Create(Point2d startPoint, Point2d endPoint);

    // 获取包围盒
    JAGUARCORE_API Rect2d BoundingBox() const noexcept;
    // 计算长度
    JAGUARCORE_API double Length() const noexcept;
    JAGUARCORE_API double LengthRustF32() const noexcept;
    JAGUARCORE_API double XMinRustF32() const noexcept;
    JAGUARCORE_API double YMinRustF32() const noexcept;
    JAGUARCORE_API double XMaxRustF32() const noexcept;
    JAGUARCORE_API double YMaxRustF32() const noexcept;
    JAGUARCORE_API Point2d CentroidRustF32() const noexcept;
    JAGUARCORE_API Edge2d ExtendedAtFrontRustF32(double distance) const noexcept;
    JAGUARCORE_API Edge2d ExtendedAtBackRustF32(double distance) const noexcept;
    JAGUARCORE_API Edge2d ScaledRustF32(double factor) const noexcept;
    JAGUARCORE_API Edge2d ReversedRustF32() const noexcept;
    JAGUARCORE_API void TransformRustF32(const Transform2d& transform) noexcept;
    // 获取线段上距离给定点最近的点
    JAGUARCORE_API Point2d ClosestPoint(const Point2d& point) const noexcept;
    // 使用 jagua-rs 的 f32 运算规则获取最近点
    JAGUARCORE_API Point2d ClosestPointRustF32(
        const Point2d& point) const noexcept;
    // 使用 jagua-rs 的 f32 运算规则计算点到线段的距离
    JAGUARCORE_API double SquaredDistanceToRustF32(
        const Point2d& point) const noexcept;
    JAGUARCORE_API double DistanceToRustF32(const Point2d& point) const noexcept;
    // 判断两条线段是否相交
    JAGUARCORE_API bool Intersects(
        const Edge2d& other,
        double tolerance = NumericPolicy::DefaultEqualityTolerance()) const noexcept;
    // 使用 jagua-rs Edge::collides_with 的 f32 运算规则
    JAGUARCORE_API bool IntersectsRustF32(const Edge2d& other) const noexcept;
    // 返回 jagua-rs Edge::collides_at 同规则计算出的有限线段交点
    JAGUARCORE_API std::optional<Point2d> IntersectionPointRustF32(
        const Edge2d& other) const noexcept;
    // 使用 jagua-rs Rect::collides_with(Edge) 的 f32 运算规则
    JAGUARCORE_API bool IntersectsRustF32(const Rect2d& rectangle) const noexcept;
};
}
