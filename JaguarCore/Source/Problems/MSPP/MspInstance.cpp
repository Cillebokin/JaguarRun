#include "JaguarCore/Problems/MSpp.h"

#include <limits>
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

JAGUARCORE_API Result<MSPInstance> MSPInstance::Create(
    std::vector<Item> items,
    std::vector<std::size_t> demands,
    double maxWidth,
    double fixedHeight,
    CDEConfig cdeConfig,
    ShapeModifyConfig shapeModifyConfig)
{
    if (items.empty() || items.size() != demands.size())
    {
        return Result<MSPInstance>::FromError(
            {ErrorCode::InvalidInput, "MSPP instance has invalid items"});
    }

    std::size_t totalDemand = 0;
    std::vector<std::shared_ptr<const Item>> itemPointers;
    itemPointers.reserve(items.size());
    for (std::size_t i = 0; i < items.size(); ++i)
    {
        if (items[i].Index() != i)
        {
            return Result<MSPInstance>::FromError(
                {ErrorCode::InvalidInput, "MSPP item indices must be dense"});
        }
        for (std::size_t j = 0; j < i; ++j)
        {
            if (items[j].ExternalId() == items[i].ExternalId())
            {
                return Result<MSPInstance>::FromError(
                    {ErrorCode::InvalidInput, "MSPP item external IDs must be unique"});
            }
        }
        if (demands[i] > std::numeric_limits<std::size_t>::max() - totalDemand)
        {
            return Result<MSPInstance>::FromError(
                {ErrorCode::InvalidInput, "MSPP demand total overflows"});
        }
        totalDemand += demands[i];
        itemPointers.push_back(std::make_shared<const Item>(items[i]));
    }
    if (totalDemand == 0)
    {
        return Result<MSPInstance>::FromError(
            {ErrorCode::InvalidInput, "MSPP instance must have positive demand"});
    }

    const auto strip = MultiStrip::Create(
        maxWidth, fixedHeight, cdeConfig, shapeModifyConfig, maxWidth);
    if (!strip.HasValue())
        return Result<MSPInstance>::FromError(strip.GetError());
    return Result<MSPInstance>::FromValue(
        MSPInstance{std::move(itemPointers), std::move(demands), strip.Value()});
}

JAGUARCORE_API std::size_t MSPInstance::ItemCount() const noexcept
{
    return m_Items.size();
}

JAGUARCORE_API const Item& MSPInstance::GetItem(ItemIndex index) const noexcept
{
    return *m_Items[index];
}

JAGUARCORE_API std::size_t MSPInstance::ItemDemand(ItemIndex index) const noexcept
{
    return m_Demands[index];
}

JAGUARCORE_API const MultiStrip& MSPInstance::BaseStrip() const noexcept
{
    return m_BaseStrip;
}
}
