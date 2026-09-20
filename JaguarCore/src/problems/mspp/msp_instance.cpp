#include "JaguarCore/problems/mspp.h"

#include <memory>
#include <utility>

namespace jaguar
{
MSPInstance::MSPInstance(std::vector<std::shared_ptr<const Item>> items,
                         std::vector<std::size_t> demands,
                         MultiStrip baseStrip)
    : m_Items(std::move(items)),
      m_Demands(std::move(demands)),
      m_BaseStrip(baseStrip)
{
}

JAGUARCORE_API Result<MSPInstance> MSPInstance::create(
    std::vector<Item> items,
    std::vector<std::size_t> demands,
    double maxWidth,
    double fixedHeight,
    CDEConfig cdeConfig,
    ShapeModifyConfig shapeModifyConfig)
{
    if (items.empty() || items.size() != demands.size())
    {
        return Result<MSPInstance>::fromError(
            {ErrorCode::InvalidInput, "MSPP instance has invalid items"});
    }

    std::size_t totalDemand = 0;
    std::vector<std::shared_ptr<const Item>> itemPointers;
    itemPointers.reserve(items.size());
    for (std::size_t i = 0; i < items.size(); ++i)
    {
        if (items[i].index() != i)
        {
            return Result<MSPInstance>::fromError(
                {ErrorCode::InvalidInput, "MSPP item indices must be dense"});
        }
        for (std::size_t j = 0; j < i; ++j)
        {
            if (items[j].externalId() == items[i].externalId())
            {
                return Result<MSPInstance>::fromError(
                    {ErrorCode::InvalidInput, "MSPP item external IDs must be unique"});
            }
        }
        totalDemand += demands[i];
        itemPointers.push_back(std::make_shared<const Item>(items[i]));
    }
    if (totalDemand == 0)
    {
        return Result<MSPInstance>::fromError(
            {ErrorCode::InvalidInput, "MSPP instance must have positive demand"});
    }

    const auto strip = MultiStrip::create(
        maxWidth, fixedHeight, cdeConfig, shapeModifyConfig, maxWidth);
    if (!strip.hasValue())
        return Result<MSPInstance>::fromError(strip.error());
    return Result<MSPInstance>::fromValue(
        MSPInstance{std::move(itemPointers), std::move(demands), strip.value()});
}

JAGUARCORE_API std::size_t MSPInstance::itemCount() const noexcept
{
    return m_Items.size();
}

JAGUARCORE_API const Item& MSPInstance::item(ItemIndex index) const noexcept
{
    return *m_Items[index];
}

JAGUARCORE_API std::size_t MSPInstance::itemDemand(ItemIndex index) const noexcept
{
    return m_Demands[index];
}

JAGUARCORE_API const MultiStrip& MSPInstance::baseStrip() const noexcept
{
    return m_BaseStrip;
}
}
