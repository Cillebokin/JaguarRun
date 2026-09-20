#pragma once

#include "JaguarCore/collision/hazard.h"

#include <utility>

namespace jaguar
{
class HazardCollector
{
public:
    JAGUARCORE_API bool contains(const HazardHandle& handle) const noexcept;
    JAGUARCORE_API bool containsEntity(const HazardEntity& entity) const noexcept;
    JAGUARCORE_API void clear() noexcept;
    JAGUARCORE_API void insert(HazardHandle handle, HazardEntity entity);
    JAGUARCORE_API void remove(HazardHandle handle) noexcept;
    JAGUARCORE_API bool removeEntity(const HazardEntity& entity) noexcept;
    JAGUARCORE_API std::size_t size() const noexcept;
    JAGUARCORE_API bool empty() const noexcept;
    JAGUARCORE_API const std::vector<std::pair<HazardHandle, HazardEntity>>& entries() const noexcept;

private:
    std::vector<std::pair<HazardHandle, HazardEntity>> m_Entries;
};
}
