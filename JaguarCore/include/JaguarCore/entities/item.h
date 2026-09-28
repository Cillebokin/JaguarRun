#pragma once

#include "JaguarCore/Export.h"
#include "JaguarCore/FailFast/Surrogate.h"
#include "JaguarCore/Geometry/OriginalShape.h"
#include "JaguarCore/Result.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace jaguar
{
// 物品外部标识
using ItemId = std::uint64_t;
// 物品在实例中的索引
using ItemIndex = std::size_t;

// 允许的旋转模式
enum class RotationMode
{
    None,       // 不允许旋转
    Discrete,   // 离散角度旋转
    Continuous, // 连续旋转
};

// 物品旋转范围
struct RotationRange
{
    RotationMode mode = RotationMode::None; // 旋转模式
    std::vector<double> radians;            // 离散旋转角度，单位为弧度
};

// 排料物品实体
class Item
{
public:
    // 创建物品
    static JAGUARCORE_API Result<Item> Create(
        ItemIndex index,
        ItemId externalId,
        OriginalShape originalShape,
        SurrogateConfig surrogateConfig,
        std::optional<std::size_t> minQuality = std::nullopt,
        RotationRange allowedRotation = {});

    // 获取内部索引
    JAGUARCORE_API ItemIndex Index() const noexcept;
    // 获取外部标识
    JAGUARCORE_API ItemId ExternalId() const noexcept;
    // 获取原始形状
    JAGUARCORE_API const OriginalShape& GetOriginalShape() const noexcept;
    // 获取碰撞形状
    JAGUARCORE_API const SimplePolygon& CollisionShape() const noexcept;
    // 获取快速碰撞代理配置
    JAGUARCORE_API const SurrogateConfig& GetSurrogateConfig() const noexcept;
    // 获取最低质量要求
    JAGUARCORE_API std::optional<std::size_t> MinQuality() const noexcept;
    // 获取允许的旋转范围
    JAGUARCORE_API const RotationRange& AllowedRotation() const noexcept;
    // 获取物品面积
    JAGUARCORE_API double Area() const noexcept;

private:
    Item(ItemIndex index,
         ItemId externalId,
         OriginalShape originalShape,
         SimplePolygon collisionShape,
         SurrogateConfig surrogateConfig,
         std::optional<std::size_t> minQuality,
         RotationRange allowedRotation);

    ItemIndex m_Index = 0;                   // 内部索引
    ItemId m_ExternalId = 0;                 // 外部标识
    OriginalShape m_OriginalShape;           // 原始形状
    SimplePolygon m_CollisionShape;          // 碰撞形状
    SurrogateConfig m_SurrogateConfig{};     // 快速碰撞代理配置
    std::optional<std::size_t> m_MinQuality; // 最低质量要求
    RotationRange m_AllowedRotation;         // 允许的旋转范围
};
}
