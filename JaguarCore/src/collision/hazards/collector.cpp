#include "JaguarCore/collision/hazard_collector.h"

#include <algorithm>

namespace jaguar
{
bool HazardCollector::contains(const HazardHandle& handle) const noexcept
{
    return std::any_of(m_Entries.begin(), m_Entries.end(),
                       [handle](const auto& entry) { return entry.first == handle; });
}

JAGUARCORE_API bool HazardCollector::containsEntity(
    const HazardEntity& entity) const noexcept
{
    return std::any_of(m_Entries.begin(), m_Entries.end(),
                       [entity](const auto& entry) { return entry.second == entity; });
}

void HazardCollector::clear() noexcept
{
    m_Entries.clear();
}

void HazardCollector::insert(HazardHandle handle, HazardEntity entity)
{
    const auto it = std::find_if(
        m_Entries.begin(), m_Entries.end(),
        [handle](const auto& entry) { return entry.first == handle; });
    if (it == m_Entries.end())
        m_Entries.emplace_back(handle, entity);
    else
        it->second = entity;
}

void HazardCollector::remove(HazardHandle handle) noexcept
{
    m_Entries.erase(std::remove_if(m_Entries.begin(), m_Entries.end(),
                                   [handle](const auto& entry) { return entry.first == handle; }),
                    m_Entries.end());
}

JAGUARCORE_API bool HazardCollector::removeEntity(
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

std::size_t HazardCollector::size() const noexcept
{
    return m_Entries.size();
}

bool HazardCollector::empty() const noexcept
{
    return m_Entries.empty();
}

const std::vector<std::pair<HazardHandle, HazardEntity>>& HazardCollector::entries() const noexcept
{
    return m_Entries;
}
}
