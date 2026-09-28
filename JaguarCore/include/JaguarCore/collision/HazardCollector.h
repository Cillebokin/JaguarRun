#pragma once

#include "JaguarCore/Collision/Hazard.h"

#include <utility>

namespace jaguar
{
// 碰撞障碍物收集器
class HazardCollector
{
public:
    // 判断是否包含指定句柄
    JAGUARCORE_API bool Contains(const HazardHandle& handle) const noexcept;
    // 判断是否包含指定实体
    JAGUARCORE_API bool ContainsEntity(const HazardEntity& entity) const noexcept;
    // 清空收集结果
    JAGUARCORE_API void Clear() noexcept;
    // 添加碰撞结果
    JAGUARCORE_API void Insert(HazardHandle handle, HazardEntity entity);
    // 移除指定句柄
    JAGUARCORE_API void Remove(HazardHandle handle) noexcept;
    // 移除指定实体
    JAGUARCORE_API bool RemoveEntity(const HazardEntity& entity) noexcept;
    // 获取结果数量
    JAGUARCORE_API std::size_t Size() const noexcept;
    // 判断结果是否为空
    JAGUARCORE_API bool Empty() const noexcept;
    // 获取所有收集结果
    JAGUARCORE_API const std::vector<std::pair<HazardHandle, HazardEntity>>& Entries() const noexcept;

private:
    std::vector<std::pair<HazardHandle, HazardEntity>> m_Entries; // 收集结果
};
}
