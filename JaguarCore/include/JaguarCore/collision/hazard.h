#pragma once

#include "JaguarCore/Export.h"
#include "JaguarCore/Geometry/SimplePolygon.h"
#include "JaguarCore/Geometry/Transformation.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace jaguar
{
// 障碍物句柄
struct HazardHandle
{
    std::size_t index = 0;        // 槽位索引
    std::uint32_t generation = 0; // 句柄代数

    // 判断两个句柄是否相等
    friend inline bool operator==(const HazardHandle& lhs,
                                  const HazardHandle& rhs) noexcept
    {
        return lhs.index == rhs.index && lhs.generation == rhs.generation;
    }
};

// 障碍物类型
enum class HazardKind
{
    PlacedItem,          // 已放置物品
    Exterior,            // 容器外部
    Hole,                // 容器孔洞
    InferiorQualityZone, // 低质量区域
};

// 障碍物所属实体
struct HazardEntity
{
    HazardKind kind = HazardKind::PlacedItem; // 障碍物类型
    std::size_t itemIndex = 0;                // 物品索引
    std::size_t quality = 0;                  // 质量等级
    std::size_t zoneIndex = 0;                // 区域索引
    HazardHandle placedItemHandle{};          // 已放置物品句柄
    Transform2d transform = Transform2d::Identity(); // 已放置物品变换

    // 判断两个实体是否相等
    friend inline bool operator==(const HazardEntity& lhs,
                                  const HazardEntity& rhs) noexcept
    {
        if (lhs.kind != rhs.kind)
            return false;
        switch (lhs.kind)
        {
        case HazardKind::PlacedItem:
            return lhs.itemIndex == rhs.itemIndex &&
                   lhs.placedItemHandle == rhs.placedItemHandle &&
                   static_cast<float>(lhs.transform.rotationRad) ==
                       static_cast<float>(rhs.transform.rotationRad) &&
                   static_cast<float>(lhs.transform.translation.x) ==
                       static_cast<float>(rhs.transform.translation.x) &&
                   static_cast<float>(lhs.transform.translation.y) ==
                       static_cast<float>(rhs.transform.translation.y);
        case HazardKind::Exterior:
            return true;
        case HazardKind::Hole:
            return lhs.zoneIndex == rhs.zoneIndex;
        case HazardKind::InferiorQualityZone:
            return lhs.quality == rhs.quality &&
                   lhs.zoneIndex == rhs.zoneIndex;
        }
        return false;
    }
};

// 碰撞检测用障碍物
struct Hazard
{
    HazardEntity entity;                        // 所属实体
    std::shared_ptr<const SimplePolygon> shape; // 碰撞形状
    bool dynamic = false;                       // 是否为动态障碍物
};
}
