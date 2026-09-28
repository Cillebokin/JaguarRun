#pragma once

#include "JaguarCore/Collision/Hazard.h"
#include "JaguarCore/Entities/Item.h"
#include "JaguarCore/Export.h"

#include <memory>

namespace jaguar
{
// 已放置物品句柄
using PlacedItemHandle = HazardHandle;

// 物品在布局中的放置实例
class PlacedItem
{
public:
    // 根据物品和变换创建放置实例
    static JAGUARCORE_API Result<PlacedItem> Create(const Item& item,
                                                   Transform2d transform);

    // 获取原始物品
    JAGUARCORE_API const Item& GetItem() const noexcept;
    // 获取物品索引
    JAGUARCORE_API ItemIndex GetItemIndex() const noexcept;
    // 获取放置变换
    JAGUARCORE_API const Transform2d& Transform() const noexcept;
    // 获取变换后的碰撞形状
    JAGUARCORE_API const SimplePolygon& CollisionShape() const noexcept;
    // 获取放置物品面积
    JAGUARCORE_API double Area() const noexcept;

private:
    PlacedItem(std::shared_ptr<const Item> item,
               Transform2d transform,
               SimplePolygon collisionShape);

    std::shared_ptr<const Item> m_Item;                // 原始物品
    Transform2d m_Transform = Transform2d::Identity(); // 放置变换
    SimplePolygon m_CollisionShape;                    // 变换后的碰撞形状
};
}
