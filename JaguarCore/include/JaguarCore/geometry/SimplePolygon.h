#pragma once

#include "JaguarCore/Export.h"
#include "JaguarCore/Geometry/Circle.h"
#include "JaguarCore/Geometry/Edge.h"
#include "JaguarCore/Geometry/Rect.h"
#include "JaguarCore/Geometry/Transformation.h"
#include "JaguarCore/NumericPolicy.h"
#include "JaguarCore/Result.h"

#include <memory>
#include <vector>

namespace jaguar
{
struct SurrogateConfig;
class Surrogate;

// 简单多边形
class SimplePolygon
{
public:
    // 创建并校验多边形
    static JAGUARCORE_API Result<SimplePolygon> Create(
        std::vector<Point2d> points, NumericPolicy policy = {});

    // 获取顶点列表
    JAGUARCORE_API const std::vector<Point2d>& Vertices() const noexcept;

    // 获取包围盒
    JAGUARCORE_API const Rect2d& BoundingBox() const noexcept;

    // 获取质心
    JAGUARCORE_API Point2d Centroid() const noexcept;

    // 计算面积
    JAGUARCORE_API double Area() const noexcept;

    // 计算直径
    JAGUARCORE_API double Diameter() const noexcept;

    // 获取顶点数量
    JAGUARCORE_API std::size_t VertexCount() const noexcept;

    // 获取边列表
    JAGUARCORE_API std::vector<Edge2d> Edges() const;

    // 应用几何变换
    JAGUARCORE_API Result<SimplePolygon> Transformed(const Transform2d& transform) const;
    // 以 jagua-rs 的 f32 规则应用几何变换
    JAGUARCORE_API Result<SimplePolygon> TransformedRustF32(
        const Transform2d& transform) const;

    // 获取多边形的极点（Pole of Inaccessibility）
    JAGUARCORE_API const Circle2d& PointOfInaccessibility() const noexcept;

    // 生成快速碰撞代理
    JAGUARCORE_API Result<void> GenerateSurrogate(const SurrogateConfig& config);

    // 获取快速碰撞代理
    JAGUARCORE_API const Surrogate* GetSurrogate() const noexcept;

    // 判断点是否在多边形内
    JAGUARCORE_API bool Contains(
        const Point2d& point,
        double tolerance = NumericPolicy::DefaultEqualityTolerance()) const noexcept;
    // 使用 jagua-rs 的 f32 射线算法判断点是否在多边形内
    JAGUARCORE_API bool ContainsRustF32(const Point2d& point) const noexcept;

    // 判断是否与线段相交
    JAGUARCORE_API bool Intersects(
        const Edge2d& edge,
        double tolerance = NumericPolicy::DefaultEqualityTolerance()) const noexcept;
    // 使用 jagua-rs 的 f32 线段相交规则
    JAGUARCORE_API bool IntersectsRustF32(const Edge2d& edge) const noexcept;

    // 计算到点的距离
    JAGUARCORE_API double DistanceTo(const Point2d& point) const noexcept;
    // 使用 jagua-rs 的 f32 距离规则
    JAGUARCORE_API double DistanceToRustF32(const Point2d& point) const noexcept;
    JAGUARCORE_API double SquaredDistanceToRustF32(
        const Point2d& point) const noexcept;
    JAGUARCORE_API SeparationDistance2d SeparationDistanceRustF32(
        const Point2d& point) const noexcept;
    JAGUARCORE_API SeparationDistance2d SquaredSeparationDistanceRustF32(
        const Point2d& point) const noexcept;

private:
    explicit SimplePolygon(std::vector<Point2d> points,
                           Rect2d boundingBox,
                           Point2d centroid,
                           double area,
                           double diameter);

    std::vector<Point2d> m_Vertices;              // 顶点列表
    Rect2d m_BoundingBox;                         // 包围盒
    Point2d m_Centroid;                           // 质心
    double m_Area = 0.0;                          // 面积
    double m_Diameter = 0.0;                      // 直径
    Circle2d m_PointOfInaccessibility{};          // 极点
    std::shared_ptr<const Surrogate> m_Surrogate; // 快速碰撞代理
};
}
