#include "JaguarCore/Collision/HazardCollector.h"

#include <algorithm>

namespace jaguar
{
bool HazardCollector::Contains(const HazardHandle& handle) const noexcept
{
    return std::any_of(m_Entries.begin(), m_Entries.end(),
                       [handle](const auto& entry) { return entry.first == handle; });
}

JAGUARCORE_API bool HazardCollector::ContainsEntity(
    const HazardEntity& entity) const noexcept
{
    return std::any_of(m_Entries.begin(), m_Entries.end(),
                       [entity](const auto& entry) { return entry.second == entity; });
}

void HazardCollector::Clear() noexcept
{
    m_Entries.clear();
}

void HazardCollector::Insert(HazardHandle handle, HazardEntity entity)
{
    const auto it = std::find_if(
        m_Entries.begin(), m_Entries.end(),
        [handle](const auto& entry) { return entry.first == handle; });
    if (it == m_Entries.end())
        m_Entries.emplace_back(handle, entity);
    else
        it->second = entity;
}

void HazardCollector::Remove(HazardHandle handle) noexcept
{
    m_Entries.erase(std::remove_if(m_Entries.begin(), m_Entries.end(),
                                   [handle](const auto& entry) { return entry.first == handle; }),
                    m_Entries.end());
}

JAGUARCORE_API bool HazardCollector::RemoveEntity(
    const HazardEntity& entity) noexcept
{
    const auto it = std::find_if(
        m_Entries.begin(), m_Entries.end(),
        [entity](const auto& entry) { return entry.second == entity; });
    if (it == m_Entries.end())
        return false;
    m_Entries.erase(it);
    return true;
}

std::size_t HazardCollector::Size() const noexcept
{
    return m_Entries.size();
}

bool HazardCollector::Empty() const noexcept
{
    return m_Entries.empty();
}

const std::vector<std::pair<HazardHandle, HazardEntity>>& HazardCollector::Entries() const noexcept
{
    return m_Entries;
}
}
