#pragma once

#include "JaguarCore/Collision/CollisionEngine.h"
#include "JaguarCore/Entities/Container.h"
#include "JaguarCore/Entities/PlacedItem.h"
#include "JaguarCore/Export.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace jaguar
{
// 已放置物品的快照
struct LayoutPlacementSnapshot
{
    PlacedItemHandle handle{}; // 已放置物品句柄
    PlacedItem placedItem;     // 已放置物品
};

// 布局状态快照
struct LayoutSnapshot
{
    Container container;                             // 容器
    std::vector<LayoutPlacementSnapshot> placements; // 已放置物品
    CollisionSnapshot cdeSnapshot;                   // 碰撞引擎快照
    std::vector<std::uint32_t> slotGenerations;      // 全部槽位代数
    std::vector<std::size_t> freeSlotIndices;        // 按分配栈顺序保存的空闲槽

    // 按已放置物句柄槽位顺序累计原始物品面积
    JAGUARCORE_API float PlacedItemArea() const;
};

// 容器中的物品布局
class Layout
{
public:
    // 使用容器创建布局
    explicit JAGUARCORE_API Layout(Container container);
    Layout(const Layout&) = delete;
    Layout& operator=(const Layout&) = delete;
    Layout(Layout&&) noexcept = default;
    Layout& operator=(Layout&&) noexcept = default;

    // 放置物品
    JAGUARCORE_API Result<PlacedItemHandle> PlaceItem(const Item& item,
                                                       Transform2d transform);
    // 移除物品
    JAGUARCORE_API Result<PlacedItem> RemoveItem(PlacedItemHandle handle);
    // 替换容器
    JAGUARCORE_API Result<void> SwapContainer(Container container);
    // 判断布局是否可行
    JAGUARCORE_API bool IsFeasible() const;
    // 计算已放置物品的面积总和
    JAGUARCORE_API float PlacedItemArea() const noexcept;
    // 计算布局密度
    JAGUARCORE_API double Density() const;
    // 保存布局快照
    JAGUARCORE_API LayoutSnapshot Save() const;
    // 从快照恢复布局
    JAGUARCORE_API Result<void> Restore(const LayoutSnapshot& snapshot);

    // 获取当前容器
    JAGUARCORE_API const Container& GetContainer() const noexcept;
    // 获取已放置物品数量
    JAGUARCORE_API std::size_t PlacedItemCount() const noexcept;
    // 按首次出现顺序获取布局中的不同物品类型
    JAGUARCORE_API std::vector<std::reference_wrapper<const Item>> Items() const;
    // 获取最右侧碰撞位置
    JAGUARCORE_API std::optional<double> RightmostCollisionX() const noexcept;

private:
    struct Slot
    {
        std::optional<PlacedItem> placedItem; // 已放置物品
        HazardHandle cdeHandle{};             // 碰撞引擎句柄
        std::uint32_t generation = 1;         // 句柄代数
    };

    Slot* Get(PlacedItemHandle handle) noexcept;
    const Slot* Get(PlacedItemHandle handle) const noexcept;
    PlacedItemHandle AllocateHandle();
    static Hazard MakeHazard(const PlacedItem& placedItem,
                             PlacedItemHandle handle);

    Container m_Container;     // 当前容器
    CollisionEnginePtr m_Cde;  // 碰撞引擎
    std::vector<Slot> m_Slots; // 物品槽位
    std::vector<std::size_t> m_FreeSlotIndices; // 按释放顺序复用物品槽
};
}
