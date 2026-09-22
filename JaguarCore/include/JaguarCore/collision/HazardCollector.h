#pragma once

#include "JaguarCore/Collision/Hazard.h"

#include <utility>

namespace jaguar
{
class HazardCollector
{
public:
    JAGUARCORE_API bool Contains(const HazardHandle& handle) const noexcept;
    JAGUARCORE_API bool ContainsEntity(const HazardEntity& entity) const noexcept;
    JAGUARCORE_API void Clear() noexcept;
    JAGUARCORE_API void Insert(HazardHandle handle, HazardEntity entity);
    JAGUARCORE_API void Remove(HazardHandle handle) noexcept;
    JAGUARCORE_API bool RemoveEntity(const HazardEntity& entity) noexcept;
    JAGUARCORE_API std::size_t Size() const noexcept;
    JAGUARCORE_API bool Empty() const noexcept;
    JAGUARCORE_API const std::vector<std::pair<HazardHandle, HazardEntity>>& Entries() const noexcept;

private:
    std::vector<std::pair<HazardHandle, HazardEntity>> m_Entries;
};
}
