#pragma once

#include "JaguarCore/Entities/Container.h"
#include "JaguarCore/Entities/Item.h"
#include "JaguarCore/IO/ExternalRepr.h"
#include "JaguarCore/Problems/Bpp.h"
#include "JaguarCore/Problems/MSpp.h"
#include "JaguarCore/Problems/Spp.h"

#include <string>

namespace jaguar
{
// 外部数据导入配置
struct ImporterConfig
{
    // 离散碰撞检测配置
    CDEConfig cdeConfig{};
    // 形状修改配置
    ShapeModifyConfig shapeModifyConfig{};
    // 物品之间的最小间距
    double minItemSeparation = 0.0;
};

// 外部数据导入器
class Importer
{
public:
    // 使用指定配置创建导入器
    explicit JAGUARCORE_API Importer(ImporterConfig config = {});

    // 导入内部物品
    JAGUARCORE_API Result<Item> ImportItem(const ExternalItem& external,
                                            ItemIndex internalIndex) const;
    // 导入内部容器
    JAGUARCORE_API Result<Container> ImportContainer(
        const ExternalContainer& external) const;

    // 从 JSON 导入内部物品
    JAGUARCORE_API Result<Item> ImportItemJson(const std::string& json,
                                               ItemIndex internalIndex) const;
    // 从 JSON 导入内部容器
    JAGUARCORE_API Result<Container> ImportContainerJson(
        const std::string& json) const;

    // 导入 SPP 问题实例
    JAGUARCORE_API Result<SPInstance> ImportSPInstance(
        const ExternalSPInstance& external) const;
    // 导入 SPP 求解结果
    JAGUARCORE_API Result<SPSolution> ImportSPSolution(
        const SPInstance& instance, const ExternalSPSolution& external) const;
    // 从 JSON 导入 SPP 问题实例
    JAGUARCORE_API Result<SPInstance> ImportSPInstanceJson(
        const std::string& json) const;
    // 从 JSON 导入 SPP 求解结果
    JAGUARCORE_API Result<SPSolution> ImportSPSolutionJson(
        const SPInstance& instance, const std::string& json) const;

    // 导入 BPP 问题实例
    JAGUARCORE_API Result<BPInstance> ImportBPInstance(
        const ExternalBPInstance& external) const;
    // 导入 BPP 求解结果
    JAGUARCORE_API Result<BPSolution> ImportBPSolution(
        const BPInstance& instance, const ExternalBPSolution& external) const;
    // 从 JSON 导入 BPP 问题实例
    JAGUARCORE_API Result<BPInstance> ImportBPInstanceJson(
        const std::string& json) const;
    // 从 JSON 导入 BPP 求解结果
    JAGUARCORE_API Result<BPSolution> ImportBPSolutionJson(
        const BPInstance& instance, const std::string& json) const;

    // 导入 MSP 问题实例
    JAGUARCORE_API Result<MSPInstance> ImportMSPInstance(
        const ExternalMSPInstance& external) const;
    // 导入 MSP 求解结果
    JAGUARCORE_API Result<MSPSolution> ImportMSPSolution(
        const MSPInstance& instance, const ExternalMSPSolution& external) const;
    // 从 JSON 导入 MSP 问题实例
    JAGUARCORE_API Result<MSPInstance> ImportMSPInstanceJson(
        const std::string& json) const;
    // 从 JSON 导入 MSP 求解结果
    JAGUARCORE_API Result<MSPSolution> ImportMSPSolutionJson(
        const MSPInstance& instance, const std::string& json) const;

private:
    // 导入物品形状
    Result<SimplePolygon> ImportItemShape(const ExternalShape& shape) const;
    // 导入容器外轮廓
    Result<SimplePolygon> ImportContainerOuter(const ExternalShape& shape) const;
    // 导入质量区域形状
    Result<SimplePolygon> ImportZoneShape(const ExternalShape& shape) const;
    // 导入旋转策略
    Result<RotationRange> ImportRotation(const RotationPolicy& rotation) const;
    // 导入简单多边形
    Result<SimplePolygon> ImportSimplePolygon(const std::vector<Point2d>& points) const;
    // 获取形状修改配置
    ShapeModifyConfig ShapeConfig() const;

    // 导入器配置
    ImporterConfig m_Config;
};
}
