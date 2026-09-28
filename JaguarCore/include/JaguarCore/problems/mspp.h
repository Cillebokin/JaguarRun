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
// 多条带配置
struct MultiStrip
{
    // 条带允许的最大宽度
    double maxWidth = 0.0;
    // 条带固定高度
    double fixedHeight = 0.0;
    // 条带当前宽度
    double width = 0.0;
    // 离散碰撞检测配置
    CDEConfig cdeConfig{};
    // 形状修改配置
    ShapeModifyConfig shapeModifyConfig{};

    // 创建多条带配置
    static JAGUARCORE_API Result<MultiStrip> Create(
        double maxWidth,
        double fixedHeight,
        CDEConfig cdeConfig,
        ShapeModifyConfig shapeModifyConfig,
        double width);
    // 修改条带宽度
    JAGUARCORE_API Result<void> SetWidth(double newWidth);
    // 根据条带配置创建容器
    JAGUARCORE_API Result<Container> CreateContainer(ContainerId id = 0) const;
};

// MSP 问题实例
class MSPInstance
{
public:
    // 创建 MSP 问题实例
    static JAGUARCORE_API Result<MSPInstance> Create(
        std::vector<Item> items,
        std::vector<std::size_t> demands,
        double maxWidth,
        double fixedHeight,
        CDEConfig cdeConfig,
        ShapeModifyConfig shapeModifyConfig);
    // 使用显式多条带基础配置创建 MSP 问题实例
    static JAGUARCORE_API Result<MSPInstance> Create(
        std::vector<Item> items,
        std::vector<std::size_t> demands,
        MultiStrip baseStrip);

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
    // 获取所有需求物品的总面积
    JAGUARCORE_API double TotalItemArea() const noexcept;
    // 获取基础条带配置
    JAGUARCORE_API const MultiStrip& BaseStrip() const noexcept;

private:
    MSPInstance(std::vector<std::shared_ptr<const Item>> items,
                std::vector<std::size_t> demands,
                MultiStrip baseStrip);

    // 问题中的物品
    std::vector<std::shared_ptr<const Item>> m_Items;
    // 各物品的需求数量
    std::vector<std::size_t> m_Demands;
    // 基础条带配置
    MultiStrip m_BaseStrip;
};

// MSP 物品放置请求
struct MSPPlacement
{
    // 目标布局索引
    std::size_t layoutIndex = 0;
    // 物品索引
    ItemIndex itemIndex = 0;
    // 物品变换
    Transform2d transform = Transform2d::Identity();
};

// MSP 已放置物品句柄
struct MSPPlacedHandle
{
    // 所属布局索引
    std::size_t layoutIndex = 0;
    // 已放置物品句柄
    PlacedItemHandle placedItemHandle{};
};

// MSP 布局快照
struct MSPLayoutSnapshot
{
    // 布局索引
    std::size_t layoutIndex = 0;
    // 布局使用的条带配置
    MultiStrip strip{};
    // 布局内容快照
    LayoutSnapshot layoutSnapshot;
};

// MSP 求解结果
struct MSPSolution
{
    // 各条带布局的快照
    std::vector<MSPLayoutSnapshot> layouts;

    // 计算解的装载密度
    JAGUARCORE_API double Density() const;
    // 计算条带总宽度
    JAGUARCORE_API double TotalStripWidth() const;
};

// MSP 求解状态
class MSPProblem
{
public:
    // 创建 MSP 求解状态
    static JAGUARCORE_API Result<MSPProblem> Create(MSPInstance instance);

    MSPProblem(const MSPProblem&) = delete;
    MSPProblem& operator=(const MSPProblem&) = delete;
    MSPProblem(MSPProblem&&) noexcept = default;
    MSPProblem& operator=(MSPProblem&&) noexcept = default;

    // 添加一个条带布局
    JAGUARCORE_API Result<std::size_t> AddLayout(MultiStrip strip);
    // 移除一个条带布局
    JAGUARCORE_API Result<void> RemoveLayout(std::size_t layoutIndex);
    // 放置一个物品
    JAGUARCORE_API Result<MSPPlacedHandle> PlaceItem(
        const MSPPlacement& placement);
    // 移除一个已放置物品
    JAGUARCORE_API Result<MSPPlacement> RemoveItem(
        const MSPPlacedHandle& handle);
    // 修改指定条带的宽度
    JAGUARCORE_API Result<void> ChangeStripWidth(std::size_t layoutIndex,
                                                 double width);
    // 将指定条带调整为适合当前布局的宽度
    JAGUARCORE_API Result<void> FitStrip(std::size_t layoutIndex);
    // 保存当前求解状态
    JAGUARCORE_API MSPSolution Save() const;
    // 恢复已保存的求解状态
    JAGUARCORE_API Result<bool> Restore(const MSPSolution& solution);

    // 获取指定物品的剩余需求
    JAGUARCORE_API std::size_t RemainingDemand(ItemIndex index) const noexcept;
    // 获取已放置物品数量
    JAGUARCORE_API std::size_t PlacedItemCount() const noexcept;
    // 获取条带布局数量
    JAGUARCORE_API std::size_t LayoutCount() const noexcept;
    // 获取指定条带的宽度
    JAGUARCORE_API double StripWidth(std::size_t layoutIndex) const noexcept;
    // 获取当前宽度小于最大宽度的条带布局索引
    JAGUARCORE_API std::vector<std::size_t> ExtendableStrips() const;
    // 获取条带总宽度
    JAGUARCORE_API double TotalStripWidth() const noexcept;
    // 计算当前装载密度
    JAGUARCORE_API double Density() const;
    // 获取问题实例
    JAGUARCORE_API const MSPInstance& Instance() const noexcept;

private:
    // MSP 布局槽位
    struct LayoutSlot
    {
        // 当前布局
        std::optional<Layout> layout;
        // 布局使用的条带配置
        MultiStrip strip{};
        // 对齐 Rust SlotMap 的代数，用于拒绝失效布局键
        std::uint32_t generation = 1;
    };

    MSPProblem(MSPInstance instance,
               std::vector<std::size_t> remainingDemand);
    // 获取可修改的布局槽位
    LayoutSlot* GetLayout(std::size_t layoutIndex) noexcept;
    // 获取只读布局槽位
    const LayoutSlot* GetLayout(std::size_t layoutIndex) const noexcept;
    // 分配或释放带代数的布局键
    std::size_t AllocateLayoutSlot(LayoutSlot slot);
    void ReleaseLayoutSlot(std::size_t layoutIndex);

    // MSP 问题实例
    MSPInstance m_Instance;
    // 当前布局槽位
    std::vector<LayoutSlot> m_Layouts;
    // 按 Rust SlotMap 的 LIFO 规则复用已释放槽位
    std::vector<std::size_t> m_FreeLayoutIndices;
    // 各物品的剩余需求
    std::vector<std::size_t> m_RemainingDemand;
};
}
