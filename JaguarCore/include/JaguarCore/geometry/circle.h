#pragma once

#include "JaguarCore/Geometry/Point.h"
#include "JaguarCore/Geometry/Rect.h"
#include "JaguarCore/Export.h"
#include "JaguarCore/Result.h"

#include <vector>

namespace jaguar
{
struct Edge2d;
struct Transform2d;

// 二维圆
struct Circle2d
{
    Point2d center{};    // 圆心
    double radius = 0.0; // 半径

    // 创建圆
    static JAGUARCORE_API Result<Circle2d> Create(Point2d centerPoint, double radiusValue);
    // Smallest circle that contains all inputs; empty input is reported as InvalidInput.
    static JAGUARCORE_API Result<Circle2d> BoundingCircleRustF32(
        const std::vector<Circle2d>& circles);

    // 计算面积
    JAGUARCORE_API double Area() const noexcept;
    // 使用 jagua-rs Circle::area 的 f32 运算规则计算面积
    JAGUARCORE_API double AreaRustF32() const noexcept;
    // 获取包围盒
    JAGUARCORE_API Rect2d BoundingBox() const noexcept;
    // 使用 jagua-rs Circle::bbox 的 f32 运算规则生成包围盒
    JAGUARCORE_API Rect2d BoundingBoxRustF32() const noexcept;
    JAGUARCORE_API double DiameterRustF32() const noexcept;
    JAGUARCORE_API void TransformRustF32(const Transform2d& transform) noexcept;
    // 判断点是否在圆内
    JAGUARCORE_API bool Contains(const Point2d& point) const noexcept;
    // 使用 jagua-rs Circle::collides_with 的 f32 运算规则判断点
    JAGUARCORE_API bool ContainsRustF32(const Point2d& point) const noexcept;
    // 使用 jagua-rs Circle::collides_with 的 f32 运算规则判断圆
    JAGUARCORE_API bool IntersectsRustF32(const Circle2d& other) const noexcept;
    // 使用 jagua-rs Circle::collides_with 的 f32 运算规则判断线段
    JAGUARCORE_API bool IntersectsRustF32(const Edge2d& edge) const noexcept;
    // 使用 jagua-rs Circle::collides_with 的 f32 运算规则判断矩形
    JAGUARCORE_API bool IntersectsRustF32(const Rect2d& rectangle) const noexcept;
    JAGUARCORE_API double DistanceToRustF32(const Point2d& point) const noexcept;
    JAGUARCORE_API double DistanceToRustF32(const Circle2d& other) const noexcept;
    JAGUARCORE_API double DistanceToRustF32(const Edge2d& edge) const noexcept;
    JAGUARCORE_API double SquaredDistanceToRustF32(
        const Point2d& point) const noexcept;
    JAGUARCORE_API double SquaredDistanceToRustF32(
        const Circle2d& other) const noexcept;
    JAGUARCORE_API double SquaredDistanceToRustF32(
        const Edge2d& edge) const noexcept;
    JAGUARCORE_API SeparationDistance2d SeparationDistanceRustF32(
        const Point2d& point) const noexcept;
    JAGUARCORE_API SeparationDistance2d SeparationDistanceRustF32(
        const Circle2d& other) const noexcept;
    JAGUARCORE_API SeparationDistance2d SeparationDistanceRustF32(
        const Edge2d& edge) const noexcept;
    JAGUARCORE_API SeparationDistance2d SquaredSeparationDistanceRustF32(
        const Point2d& point) const noexcept;
    JAGUARCORE_API SeparationDistance2d SquaredSeparationDistanceRustF32(
        const Circle2d& other) const noexcept;
    JAGUARCORE_API SeparationDistance2d SquaredSeparationDistanceRustF32(
        const Edge2d& edge) const noexcept;
};
}
