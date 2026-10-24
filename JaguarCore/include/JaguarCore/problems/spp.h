#pragma once

#include "JaguarCore/Entities/Layout.h"
#include "JaguarCore/Export.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace jaguar
{
// 单条带配置
struct Strip
{
    // 条带固定高度
    double fixedHeight = 0.0;
    // 条带当前宽度
    double width = 0.0;
    // 离散碰撞检测配置
    CDEConfig cdeConfig{};
    // 形状修改配置
    ShapeModifyConfig shapeModifyConfig{};

    // 创建单条带配置
    static JAGUARCORE_API Result<Strip> Create(
        double fixedHeight,
        CDEConfig cdeConfig,
        ShapeModifyConfig shapeModifyConfig,
        double width);
    // 修改条带宽度
    JAGUARCORE_API Result<void> SetWidth(double newWidth);
    // 根据条带配置创建容器
    JAGUARCORE_API Result<Container> CreateContainer(ContainerId id = 0) const;
};

// SPP 问题实例
class SPInstance
{
public:
    // 创建 SPP 问题实例
    // minItemSeparation 参与 Rust import_spp_instance 的初始条宽公式：
    // separation + total_item_area / (fixed_height - separation)
    static JAGUARCORE_API Result<SPInstance> Create(
        std::vector<Item> items,
        std::vector<std::size_t> demands,
        double fixedHeight,
        CDEConfig cdeConfig,
        ShapeModifyConfig shapeModifyConfig,
        double minItemSeparation = 0.0);
    // 使用显式基础条带创建 SPP 问题实例
    static JAGUARCORE_API Result<SPInstance> Create(
        std::vector<Item> items,
        std::vector<std::size_t> demands,
        Strip baseStrip);

    // 获取物品数量
    JAGUARCORE_API std::size_t ItemCount() const noexcept;
    // 获取指定物品
    JAGUARCORE_API const Item& GetItem(ItemIndex index) const noexcept;
    // 按外部标识查找物品索引
    JAGUARCORE_API std::optional<ItemIndex> ItemIndexFromExternalId(
        std::uint64_t externalId) const noexcept;
    // 获取指定物品的需求数量
    JAGUARCORE_API std::size_t ItemDemand(ItemIndex index) const noexcept;
    // 获取物品总需求数量
    JAGUARCORE_API std::size_t TotalDemand() const noexcept;
    // 获取物品总面积
    JAGUARCORE_API double TotalItemArea() const noexcept;
    // 获取基础条带配置
    JAGUARCORE_API const Strip& BaseStrip() const noexcept;

private:
    SPInstance(std::vector<std::shared_ptr<const Item>> items,
               std::vector<std::size_t> demands,
               Strip baseStrip);

    // 问题中的物品
    std::vector<std::shared_ptr<const Item>> m_Items;
    // 各物品的需求数量
    std::vector<std::size_t> m_Demands;
    // 基础条带配置
    Strip m_BaseStrip;
};

// SPP 物品放置请求
struct SPPlacement
{
    // 物品索引
    ItemIndex itemIndex = 0;
    // 物品变换
    Transform2d transform = Transform2d::Identity();
};

// SPP 求解结果
struct SPSolution
{
    // 求解结果使用的条带配置
    Strip strip;
    // 布局内容快照
    LayoutSnapshot layoutSnapshot;

    // 计算解的装载密度
    JAGUARCORE_API double Density() const;
    // 获取结果条带宽度
    JAGUARCORE_API double StripWidth() const noexcept;
};

// SPP 求解状态
class SPProblem
{
public:
    // 创建 SPP 求解状态
    static JAGUARCORE_API Result<SPProblem> Create(SPInstance instance);

    SPProblem(const SPProblem&) = delete;
    SPProblem& operator=(const SPProblem&) = delete;
    SPProblem(SPProblem&&) noexcept = default;
    SPProblem& operator=(SPProblem&&) noexcept = default;

    // 放置一个物品
    JAGUARCORE_API Result<PlacedItemHandle> PlaceItem(
        const SPPlacement& placement);
    // 移除一个已放置物品
    JAGUARCORE_API Result<SPPlacement> RemoveItem(PlacedItemHandle handle);
    // 修改条带宽度
    JAGUARCORE_API Result<void> ChangeStripWidth(double newWidth);
    // 将条带调整为适合当前布局的宽度
    JAGUARCORE_API Result<void> FitStrip();
    // 保存当前求解状态
    JAGUARCORE_API SPSolution Save() const;
    // 恢复已保存的求解状态
    JAGUARCORE_API Result<void> Restore(const SPSolution& solution);

    // 获取指定物品的剩余需求
    JAGUARCORE_API std::size_t RemainingDemand(ItemIndex index) const noexcept;
    // 获取已放置物品数量
    JAGUARCORE_API std::size_t PlacedItemCount() const noexcept;
    // 计算当前装载密度
    JAGUARCORE_API double Density() const;
    // 获取当前条带宽度
    JAGUARCORE_API double StripWidth() const noexcept;
    // 获取问题实例
    JAGUARCORE_API const SPInstance& Instance() const noexcept;

private:
    SPProblem(SPInstance instance, Strip strip, Layout layout);

    // SPP 问题实例
    SPInstance m_Instance;
    // 当前条带配置
    Strip m_Strip;
    // 当前布局
    Layout m_Layout;
    // 各物品的剩余需求
    std::vector<std::size_t> m_RemainingDemand;
};
}
