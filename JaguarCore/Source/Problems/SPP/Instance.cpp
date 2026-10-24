#include "JaguarCore/Problems/Spp.h"

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

JAGUARCORE_API Result<SPInstance> SPInstance::Create(
    std::vector<Item> items,
    std::vector<std::size_t> demands,
    double fixedHeight,
    CDEConfig cdeConfig,
    ShapeModifyConfig shapeModifyConfig,
    double minItemSeparation)
{
    if (items.empty() || items.size() != demands.size() ||
        !std::isfinite(fixedHeight) || fixedHeight <= 0.0)
    {
        return Result<SPInstance>::FromError(
            {ErrorCode::InvalidInput, "SPP instance has invalid items or height"});
    }

    float totalArea = 0.0F;
    bool hasPositiveDemand = false;
    std::vector<std::shared_ptr<const Item>> itemPointers;
    itemPointers.reserve(items.size());
    for (std::size_t i = 0; i < items.size(); ++i)
    {
        if (items[i].Index() != i)
        {
            return Result<SPInstance>::FromError(
                {ErrorCode::InvalidInput, "SPP item indices must be dense"});
        }
        for (std::size_t j = 0; j < i; ++j)
        {
            if (items[j].ExternalId() == items[i].ExternalId())
            {
                return Result<SPInstance>::FromError(
                    {ErrorCode::InvalidInput, "SPP item external IDs must be unique"});
            }
        }
        const float itemArea = static_cast<float>(items[i].Area());
        const float demandArea =
            itemArea * static_cast<float>(demands[i]);
        const float nextTotalArea = totalArea + demandArea;
        if (!std::isfinite(itemArea) || !std::isfinite(demandArea) ||
            !std::isfinite(nextTotalArea))
        {
            return Result<SPInstance>::FromError(
                {ErrorCode::InvalidInput, "SPP item area is non-finite"});
        }
        totalArea = nextTotalArea;
        hasPositiveDemand = hasPositiveDemand || demands[i] > 0;
        itemPointers.push_back(std::make_shared<const Item>(items[i]));
    }

    if (!hasPositiveDemand || !std::isfinite(totalArea) || totalArea <= 0.0F)
    {
        return Result<SPInstance>::FromError(
            {ErrorCode::InvalidInput, "SPP instance must have positive demand"});
    }

    const float rustFixedHeight = static_cast<float>(fixedHeight);
    // 对齐 jagua-rs import_spp_instance（spp/io/import.rs）：
    // usable_height = fixed_height - separation（f32），
    // width = separation + total_item_area / usable_height（f32）。
    // separation == 0 时与旧公式 totalArea / fixedHeight 位级一致。
    const float rustSeparation = static_cast<float>(minItemSeparation);
    const float usableHeight = rustFixedHeight - rustSeparation;
    if (!(usableHeight > 0.0F))
    {
        return Result<SPInstance>::FromError(
            {ErrorCode::InvalidInput, "separation leaves no usable strip height"});
    }
    const float stripWidth = rustSeparation + totalArea / usableHeight;
    const auto strip = Strip::Create(
        static_cast<double>(rustFixedHeight), cdeConfig, shapeModifyConfig,
        static_cast<double>(stripWidth));
    if (!strip.HasValue())
        return Result<SPInstance>::FromError(strip.GetError());

    return Result<SPInstance>::FromValue(
        SPInstance{std::move(itemPointers), std::move(demands), strip.Value()});
}

JAGUARCORE_API Result<SPInstance> SPInstance::Create(
    std::vector<Item> items,
    std::vector<std::size_t> demands,
    Strip baseStrip)
{
    if (items.size() != demands.size())
    {
        return Result<SPInstance>::FromError(
            {ErrorCode::InvalidInput, "SPP item and demand counts differ"});
    }

    const auto normalizedStrip = Strip::Create(
        baseStrip.fixedHeight, baseStrip.cdeConfig,
        baseStrip.shapeModifyConfig, baseStrip.width);
    if (!normalizedStrip.HasValue())
        return Result<SPInstance>::FromError(normalizedStrip.GetError());

    std::vector<std::shared_ptr<const Item>> itemPointers;
    itemPointers.reserve(items.size());
    for (std::size_t i = 0; i < items.size(); ++i)
    {
        if (items[i].Index() != i)
        {
            return Result<SPInstance>::FromError(
                {ErrorCode::InvalidInput, "SPP item indices must be dense"});
        }
        for (std::size_t j = 0; j < i; ++j)
        {
            if (items[j].ExternalId() == items[i].ExternalId())
            {
                return Result<SPInstance>::FromError(
                    {ErrorCode::InvalidInput,
                     "SPP item external IDs must be unique"});
            }
        }
        itemPointers.push_back(std::make_shared<const Item>(items[i]));
    }

    return Result<SPInstance>::FromValue(
        SPInstance{std::move(itemPointers), std::move(demands),
                   normalizedStrip.Value()});
}

JAGUARCORE_API std::size_t SPInstance::ItemCount() const noexcept
{
    return m_Items.size();
}

JAGUARCORE_API const Item& SPInstance::GetItem(ItemIndex index) const noexcept
{
    return *m_Items[index];
}

JAGUARCORE_API std::optional<ItemIndex>
SPInstance::ItemIndexFromExternalId(std::uint64_t externalId) const noexcept
{
    for (const auto& item : m_Items)
    {
        if (item->ExternalId() == externalId)
            return item->Index();
    }
    return std::nullopt;
}

JAGUARCORE_API std::size_t SPInstance::ItemDemand(ItemIndex index) const noexcept
{
    return m_Demands[index];
}

JAGUARCORE_API std::size_t SPInstance::TotalDemand() const noexcept
{
    std::size_t result = 0;
    for (std::size_t demand : m_Demands)
        result += demand;
    return result;
}

JAGUARCORE_API double SPInstance::TotalItemArea() const noexcept
{
    float result = 0.0F;
    for (std::size_t i = 0; i < m_Items.size(); ++i)
    {
        result += static_cast<float>(m_Items[i]->Area()) *
                  static_cast<float>(m_Demands[i]);
    }
    return static_cast<double>(result);
}

JAGUARCORE_API const Strip& SPInstance::BaseStrip() const noexcept
{
    return m_BaseStrip;
}
}
