#include "JaguarCore/Problems/MSpp.h"

#include <memory>
#include <utility>

namespace jaguar
{
namespace
{
Result<std::vector<std::shared_ptr<const Item>>> copyInstanceItems(
    const std::vector<Item>& items)
{
    std::vector<std::shared_ptr<const Item>> itemPointers;
    itemPointers.reserve(items.size());
    for (std::size_t i = 0; i < items.size(); ++i)
    {
        if (items[i].Index() != i)
        {
            return Result<std::vector<std::shared_ptr<const Item>>>::FromError(
                {ErrorCode::InvalidInput,
                 "MSPP item indices must be dense"});
        }
        for (std::size_t j = 0; j < i; ++j)
        {
            if (items[j].ExternalId() == items[i].ExternalId())
            {
                return Result<std::vector<std::shared_ptr<const Item>>>::FromError(
                    {ErrorCode::InvalidInput,
                     "MSPP item external IDs must be unique"});
            }
        }
        itemPointers.push_back(std::make_shared<const Item>(items[i]));
    }
    return Result<std::vector<std::shared_ptr<const Item>>>::FromValue(
        std::move(itemPointers));
}
}

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
    if (items.size() != demands.size())
    {
        return Result<MSPInstance>::FromError(
            {ErrorCode::InvalidInput, "MSPP item and demand counts differ"});
    }

    auto itemPointers = copyInstanceItems(items);
    if (!itemPointers.HasValue())
        return Result<MSPInstance>::FromError(itemPointers.GetError());

    const auto strip = MultiStrip::Create(
        maxWidth, fixedHeight, cdeConfig, shapeModifyConfig, maxWidth);
    if (!strip.HasValue())
        return Result<MSPInstance>::FromError(strip.GetError());
    return Result<MSPInstance>::FromValue(
        MSPInstance{std::move(itemPointers.Value()), std::move(demands),
                    strip.Value()});
}

JAGUARCORE_API Result<MSPInstance> MSPInstance::Create(
    std::vector<Item> items,
    std::vector<std::size_t> demands,
    MultiStrip baseStrip)
{
    if (items.size() != demands.size())
    {
        return Result<MSPInstance>::FromError(
            {ErrorCode::InvalidInput, "MSPP item and demand counts differ"});
    }

    const auto strip = MultiStrip::Create(
        baseStrip.maxWidth, baseStrip.fixedHeight, baseStrip.cdeConfig,
        baseStrip.shapeModifyConfig, baseStrip.width);
    if (!strip.HasValue())
        return Result<MSPInstance>::FromError(strip.GetError());

    auto itemPointers = copyInstanceItems(items);
    if (!itemPointers.HasValue())
        return Result<MSPInstance>::FromError(itemPointers.GetError());
    return Result<MSPInstance>::FromValue(
        MSPInstance{std::move(itemPointers.Value()), std::move(demands),
                    strip.Value()});
}

JAGUARCORE_API std::size_t MSPInstance::ItemCount() const noexcept
{
    return m_Items.size();
}

JAGUARCORE_API const Item& MSPInstance::GetItem(ItemIndex index) const noexcept
{
    return *m_Items[index];
}

JAGUARCORE_API std::optional<ItemIndex>
MSPInstance::ItemIndexFromExternalId(std::uint64_t externalId) const noexcept
{
    for (const auto& item : m_Items)
    {
        if (item->ExternalId() == externalId)
            return item->Index();
    }
    return std::nullopt;
}

JAGUARCORE_API std::size_t MSPInstance::ItemDemand(ItemIndex index) const noexcept
{
    return m_Demands[index];
}

JAGUARCORE_API std::size_t MSPInstance::TotalDemand() const noexcept
{
    std::size_t result = 0;
    for (std::size_t demand : m_Demands)
        result += demand;
    return result;
}

JAGUARCORE_API double MSPInstance::TotalItemArea() const noexcept
{
    float result = 0.0F;
    for (std::size_t i = 0; i < m_Items.size(); ++i)
    {
        result += static_cast<float>(m_Items[i]->Area()) *
                  static_cast<float>(m_Demands[i]);
    }
    return static_cast<double>(result);
}

JAGUARCORE_API const MultiStrip& MSPInstance::BaseStrip() const noexcept
{
    return m_BaseStrip;
}
}
