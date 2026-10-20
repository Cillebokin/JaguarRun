#pragma once

#include "JaguarCore/Entities/Container.h"
#include "JaguarCore/Entities/Item.h"
#include "JaguarCore/Geometry/Point.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace jaguar
{
// 外部矩形
struct ExternalRectangle
{
    // 左边界坐标
    double xMin = 0.0;
    // 下边界坐标
    double yMin = 0.0;
    // 矩形宽度
    double width = 0.0;
    // 矩形高度
    double height = 0.0;
};

// 外部简单多边形
struct ExternalSimplePolygon
{
    // 多边形顶点
    std::vector<Point2d> points;
};

// 外部带孔多边形
struct ExternalPolygon
{
    // 多边形外轮廓
    std::vector<Point2d> outer;
    // 多边形内孔轮廓
    std::vector<std::vector<Point2d>> holes;
};

// 外部多多边形
struct ExternalMultiPolygon
{
    // 多个外部多边形
    std::vector<ExternalPolygon> polygons;
};

// 外部形状变体
struct ExternalShape
{
    // 支持的外部形状类型
    using Value = std::variant<ExternalRectangle,
                               ExternalSimplePolygon,
                               ExternalPolygon,
                               ExternalMultiPolygon>;

    // 当前外部形状值
    Value value{};

    // 创建外部矩形
    static JAGUARCORE_API ExternalShape Rectangle(double xMin,
                                                  double yMin,
                                                  double width,
                                                  double height);
    // 创建外部简单多边形
    static JAGUARCORE_API ExternalShape SimplePolygon(std::vector<Point2d> points);
    // 创建外部带孔多边形
    static JAGUARCORE_API ExternalShape Polygon(
        std::vector<Point2d> outer,
        std::vector<std::vector<Point2d>> holes = {});
    // 创建外部多多边形
    static JAGUARCORE_API ExternalShape MultiPolygon(
        std::vector<ExternalPolygon> polygons);
};

// 旋转策略类型
enum class RotationPolicyKind
{
    // 不允许旋转
    None,
    // 使用离散角度
    Discrete,
    // 按固定角度步长旋转
    Stepped,
    // 允许连续旋转
    Continuous,
};

// 外部物品的旋转策略
struct RotationPolicy
{
    // 旋转策略类型
    RotationPolicyKind kind = RotationPolicyKind::None;
    // 可用的旋转角度
    std::vector<double> anglesDegrees;
    // 旋转角度步长
    double stepDegrees = 0.0;

    // 创建不旋转策略
    static JAGUARCORE_API RotationPolicy None();
    // 创建离散角度策略
    static JAGUARCORE_API RotationPolicy Discrete(std::vector<double> anglesDegrees);
    // 创建固定步长策略
    static JAGUARCORE_API RotationPolicy Stepped(double stepDegrees);
    // 创建连续旋转策略
    static JAGUARCORE_API RotationPolicy Continuous();
};

// 外部物品
struct ExternalItem
{
    // 物品标识
    ItemId id = 0;
    // 旋转策略
    RotationPolicy rotation{};
    // 物品形状
    ExternalShape shape{};
    // 最低质量要求
    std::optional<std::size_t> minQuality;
};

// 外部质量区域
struct ExternalQualityZone
{
    // 区域质量等级
    std::size_t quality = 0;
    // 区域形状
    ExternalShape shape{};
};

// 外部容器
struct ExternalContainer
{
    // 容器标识
    ContainerId id = 0;
    // 容器形状
    ExternalShape shape{};
    // 容器质量区域
    std::vector<ExternalQualityZone> zones;
};

// 外部变换
struct ExternalTransformation
{
    // 旋转角度
    double rotationDegrees = 0.0;
    // 平移量
    Point2d translation{};
};

// 外部已放置物品
struct ExternalPlacedItem
{
    // 物品标识
    ItemId itemId = 0;
    // 物品变换
    ExternalTransformation transformation{};
};

// 外部布局
struct ExternalLayout
{
    // 容器标识
    ContainerId containerId = 0;
    // 已放置物品
    std::vector<ExternalPlacedItem> placedItems;
    // 布局密度
    double density = 0.0;
};

// 外部需求物品
struct ExternalDemandItem
{
    // 物品定义
    ExternalItem item;
    // 物品需求数量
    std::uint64_t demand = 0;
};

// 外部 SPP 实例
struct ExternalSPInstance
{
    // 实例名称
    std::string name;
    // 物品最小间距
    double minItemSeparation = 0.0;
    // 需求物品列表
    std::vector<ExternalDemandItem> items;
    // 条带高度
    double stripHeight = 0.0;
};

// 外部 SPP 解
struct ExternalSPSolution
{
    // 条带宽度
    double stripWidth = 0.0;
    // 布局结果
    ExternalLayout layout{};
    // 解的密度
    double density = 0.0;
    // 运行时间
    std::uint64_t runTimeSec = 0;
};

// Rust-compatible SPP output: instance fields at the root and solution nested
// under "solution".
struct ExternalSPOutput
{
    // External SPP instance
    ExternalSPInstance instance;
    // External SPP solution
    ExternalSPSolution solution;
};

// 外部装箱容器
struct ExternalBin
{
    // 容器定义
    ExternalContainer container;
    // 容器库存数量
    std::uint64_t stock = 0;
    // 容器使用成本
    std::uint64_t cost = 0;
};

// 外部 BPP 实例
struct ExternalBPInstance
{
    // 实例名称
    std::string name;
    // 物品最小间距
    double minItemSeparation = 0.0;
    // 需求物品列表
    std::vector<ExternalDemandItem> items;
    // 可用容器列表
    std::vector<ExternalBin> bins;
};

// 外部 BPP 解
struct ExternalBPSolution
{
    // 解的总成本
    std::uint64_t cost = 0;
    // 各容器布局
    std::vector<ExternalLayout> layouts;
    // 解的密度
    double density = 0.0;
    // 运行时间
    std::uint64_t runTimeSec = 0;
};

// 外部 MSP 条带配置
struct ExternalMSPStrip
{
    // 条带高度
    double height = 0.0;
    // 条带最大宽度
    double maxWidth = 0.0;
};

// 外部 MSP 实例
struct ExternalMSPInstance
{
    // 实例名称
    std::string name;
    // 物品最小间距
    double minItemSeparation = 0.0;
    // 需求物品列表
    std::vector<ExternalDemandItem> items;
    // 多条带配置
    ExternalMSPStrip strips{};
};

// 外部 MSP 解
struct ExternalMSPSolution
{
    // 各条带布局
    std::vector<ExternalLayout> layouts;
    // 输出容器列表
    std::vector<ExternalContainer> containers;
    // 解的密度
    double density = 0.0;
    // 运行时间
    std::uint64_t runTimeSec = 0;
};
}
