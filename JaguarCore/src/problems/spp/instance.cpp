#include "JaguarCore/problems/spp.h"

#include <cmath>
#include <memory>
#include <utility>

namespace jaguar
{
SPInstance::SPInstance(std::vector<std::shared_ptr<const Item>> items,
                       std::vector<std::size_t> demands,
                       Strip baseStrip)
    : m_Items(std::move(items)),
      m_Demands(std::move(demands)),
      m_BaseStrip(baseStrip)
{
}

JAGUARCORE_API Result<SPInstance> SPInstance::create(
    std::vector<Item> items,
    std::vector<std::size_t> demands,
    double fixedHeight,
    CDEConfig cdeConfig,
    ShapeModifyConfig shapeModifyConfig)
{
    if (items.empty() || items.size() != demands.size() ||
        !std::isfinite(fixedHeight) || fixedHeight <= 0.0)
    {
        return Result<SPInstance>::fromError(
            {ErrorCode::InvalidInput, "SPP instance has invalid items or height"});
    }

    double totalArea = 0.0;
    std::size_t totalDemand = 0;
    std::vector<std::shared_ptr<const Item>> itemPointers;
    itemPointers.reserve(items.size());
    for (std::size_t i = 0; i < items.size(); ++i)
    {
        if (items[i].index() != i)
        {
            return Result<SPInstance>::fromError(
                {ErrorCode::InvalidInput, "SPP item indices must be dense"});
        }
        for (std::size_t j = 0; j < i; ++j)
        {
            if (items[j].externalId() == items[i].externalId())
            {
                return Result<SPInstance>::fromError(
                    {ErrorCode::InvalidInput, "SPP item external IDs must be unique"});
            }
        }
        totalArea += items[i].area() * static_cast<double>(demands[i]);
        totalDemand += demands[i];
        itemPointers.push_back(std::make_shared<const Item>(items[i]));
    }

    if (totalDemand == 0 || !std::isfinite(totalArea) || totalArea <= 0.0)
    {
        return Result<SPInstance>::fromError(
            {ErrorCode::InvalidInput, "SPP instance must have positive demand"});
    }

    const auto strip = Strip::create(
        fixedHeight, cdeConfig, shapeModifyConfig, totalArea / fixedHeight);
    if (!strip.hasValue())
        return Result<SPInstance>::fromError(strip.error());

    return Result<SPInstance>::fromValue(
        SPInstance{std::move(itemPointers), std::move(demands), strip.value()});
}

JAGUARCORE_API std::size_t SPInstance::itemCount() const noexcept
{
    return m_Items.size();
}

JAGUARCORE_API const Item& SPInstance::item(ItemIndex index) const noexcept
{
    return *m_Items[index];
}

JAGUARCORE_API std::size_t SPInstance::itemDemand(ItemIndex index) const noexcept
{
    return m_Demands[index];
}

JAGUARCORE_API std::size_t SPInstance::totalDemand() const noexcept
{
    std::size_t result = 0;
    for (std::size_t demand : m_Demands)
        result += demand;
    return result;
}

JAGUARCORE_API double SPInstance::totalItemArea() const noexcept
{
    double result = 0.0;
    for (std::size_t i = 0; i < m_Items.size(); ++i)
        result += m_Items[i]->area() * static_cast<double>(m_Demands[i]);
    return result;
}

JAGUARCORE_API const Strip& SPInstance::baseStrip() const noexcept
{
    return m_BaseStrip;
}
}
