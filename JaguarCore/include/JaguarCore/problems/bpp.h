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
// BPP 容器定义
struct Bin
{
    // 容器在实例中的索引
    std::size_t index = 0;
    // 外部系统中的容器标识
    ContainerId externalId = 0;
    // 容器几何与配置
    Container container;
    // 可使用的容器数量
    std::size_t stock = 0;
    // 启用该容器的成本
    std::uint64_t cost = 0;
};

// BPP 问题实例
class BPInstance
{
public:
    // 创建 BPP 问题实例
    static JAGUARCORE_API Result<BPInstance> Create(
        std::vector<Item> items,
        std::vector<std::size_t> demands,
        std::vector<Bin> bins);

    // 获取物品数量
    JAGUARCORE_API std::size_t ItemCount() const noexcept;
    // 获取指定物品
    JAGUARCORE_API const Item& GetItem(ItemIndex index) const noexcept;
    // 按外部标识查找物品索引
    JAGUARCORE_API std::optional<ItemIndex> ItemIndexFromExternalId(
        std::uint64_t externalId) const noexcept;
    // 获取指定物品的需求数量
    JAGUARCORE_API std::size_t ItemDemand(ItemIndex index) const noexcept;
    // 获取容器类型数量
    JAGUARCORE_API std::size_t BinCount() const noexcept;
    // 获取指定容器类型
    JAGUARCORE_API const Bin& GetBin(std::size_t index) const noexcept;
    // 获取物品总需求数量
    JAGUARCORE_API std::size_t TotalDemand() const noexcept;
    // 获取所有需求物品的总面积
    JAGUARCORE_API double TotalItemArea() const noexcept;

private:
    BPInstance(std::vector<std::shared_ptr<const Item>> items,
               std::vector<std::size_t> demands,
               std::vector<Bin> bins);

    // 问题中的物品
    std::vector<std::shared_ptr<const Item>> m_Items;
    // 各物品的需求数量
    std::vector<std::size_t> m_Demands;
    // 可用的容器类型
    std::vector<Bin> m_Bins;
};

// 放置目标布局的状态
enum class LayoutTargetKind
{
    // 放入已有布局
    Open,
    // 根据容器索引新开布局
    Closed,
};

// BPP 物品放置请求
struct BPPlacement
{
    // 目标布局的状态
    LayoutTargetKind targetKind = LayoutTargetKind::Closed;
    // 目标布局索引
    std::size_t layoutIndex = 0;
    // 目标容器索引
    std::size_t binIndex = 0;
    // 物品索引
    ItemIndex itemIndex = 0;
    // 物品变换
    Transform2d transform = Transform2d::Identity();
};

// BPP 已放置物品句柄
struct BPPlacedHandle
{
    // 所属布局索引
    std::size_t layoutIndex = 0;
    // 已放置物品句柄
    PlacedItemHandle placedItemHandle{};
};

// BPP 布局快照
struct BPLayoutSnapshot
{
    // 布局索引
    std::size_t layoutIndex = 0;
    // 容器索引
    std::size_t binIndex = 0;
    // 布局内容快照
    LayoutSnapshot layoutSnapshot;
};

// BPP 求解结果
struct BPSolution
{
    // 各容器布局的快照
    std::vector<BPLayoutSnapshot> layouts;

    // 计算解的装载密度
    JAGUARCORE_API double Density() const;
    // 计算解使用容器的总成本
    JAGUARCORE_API std::uint64_t Cost(const BPInstance& instance) const;
};

// BPP 求解状态
class BPProblem
{
public:
    // 创建 BPP 求解状态
    static JAGUARCORE_API Result<BPProblem> Create(BPInstance instance);

    BPProblem(const BPProblem&) = delete;
    BPProblem& operator=(const BPProblem&) = delete;
    BPProblem(BPProblem&&) noexcept = default;
    BPProblem& operator=(BPProblem&&) noexcept = default;

    // 放置一个物品
    JAGUARCORE_API Result<BPPlacedHandle> PlaceItem(
        const BPPlacement& placement);
    // 移除一个已放置物品
    JAGUARCORE_API Result<BPPlacement> RemoveItem(const BPPlacedHandle& handle);
    // 移除一个容器布局并返还其中物品需求与容器库存
    JAGUARCORE_API Result<void> RemoveLayout(std::size_t layoutIndex);
    // 保存当前求解状态
    JAGUARCORE_API BPSolution Save() const;
    // 恢复已保存的求解状态
    JAGUARCORE_API Result<bool> Restore(const BPSolution& solution);

    // 获取指定物品的剩余需求
    JAGUARCORE_API std::size_t RemainingDemand(ItemIndex index) const noexcept;
    // 获取指定容器的剩余库存
    JAGUARCORE_API std::size_t RemainingBinStock(std::size_t index) const noexcept;
    // 获取已放置物品数量
    JAGUARCORE_API std::size_t PlacedItemCount() const noexcept;
    // 获取开放布局数量
    JAGUARCORE_API std::size_t OpenLayoutCount() const noexcept;
    // 计算当前装载密度
    JAGUARCORE_API double Density() const;
    // 获取当前容器总成本
    JAGUARCORE_API std::uint64_t BinCost() const noexcept;
    // 获取问题实例
    JAGUARCORE_API const BPInstance& Instance() const noexcept;

private:
    // BPP 布局槽位
    struct LayoutSlot
    {
        // 当前布局
        std::optional<Layout> layout;
        // 所用容器索引
        std::size_t binIndex = 0;
        // 对齐 Rust SlotMap 的代数，用于拒绝失效布局键
        std::uint32_t generation = 1;
    };

    BPProblem(BPInstance instance,
              std::vector<std::size_t> remainingDemand,
              std::vector<std::size_t> remainingBinStock);
    // 为指定容器创建布局
    Result<std::size_t> OpenLayout(std::size_t binIndex);
    // 获取可修改的布局槽位
    LayoutSlot* GetLayout(std::size_t layoutIndex) noexcept;
    // 获取只读布局槽位
    const LayoutSlot* GetLayout(std::size_t layoutIndex) const noexcept;
    // 分配或释放带代数的布局键
    std::size_t AllocateLayoutSlot(LayoutSlot slot);
    void ReleaseLayoutSlot(std::size_t layoutIndex);

    // BPP 问题实例
    BPInstance m_Instance;
    // 当前布局槽位
    std::vector<LayoutSlot> m_Layouts;
    // 按 Rust SlotMap 的 LIFO 规则复用已释放槽位
    std::vector<std::size_t> m_FreeLayoutIndices;
    // 各物品的剩余需求
    std::vector<std::size_t> m_RemainingDemand;
    // 各容器的剩余库存
    std::vector<std::size_t> m_RemainingBinStock;
};
}
