#include "JaguarCore/Problems/Bpp.h"

#include <memory>
#include <utility>

namespace jaguar
{
BPInstance::BPInstance(std::vector<std::shared_ptr<const Item>> items,
                       std::vector<std::size_t> demands,
                       std::vector<Bin> bins)
    : m_Items(std::move(items)),
      m_Demands(std::move(demands)),
      m_Bins(std::move(bins))
{
}

JAGUARCORE_API Result<BPInstance> BPInstance::Create(
    std::vector<Item> items,
    std::vector<std::size_t> demands,
    std::vector<Bin> bins)
{
    if (items.size() != demands.size())
    {
        return Result<BPInstance>::FromError(
            {ErrorCode::InvalidInput, "BPP item and demand counts differ"});
    }

    std::vector<std::shared_ptr<const Item>> itemPointers;
    itemPointers.reserve(items.size());
    for (std::size_t i = 0; i < items.size(); ++i)
    {
        if (items[i].Index() != i)
        {
            return Result<BPInstance>::FromError(
                {ErrorCode::InvalidInput, "BPP item indices must be dense"});
        }
        for (std::size_t j = 0; j < i; ++j)
        {
            if (items[j].ExternalId() == items[i].ExternalId())
            {
                return Result<BPInstance>::FromError(
                    {ErrorCode::InvalidInput, "BPP item external IDs must be unique"});
            }
        }
        itemPointers.push_back(std::make_shared<const Item>(items[i]));
    }

    for (std::size_t i = 0; i < bins.size(); ++i)
    {
        if (bins[i].index != i)
        {
            return Result<BPInstance>::FromError(
                {ErrorCode::InvalidInput, "BPP bin indices must be dense"});
        }
    }

    return Result<BPInstance>::FromValue(
        BPInstance{std::move(itemPointers), std::move(demands), std::move(bins)});
}

JAGUARCORE_API std::size_t BPInstance::ItemCount() const noexcept
{
    return m_Items.size();
}

JAGUARCORE_API const Item& BPInstance::GetItem(ItemIndex index) const noexcept
{
    return *m_Items[index];
}

JAGUARCORE_API std::optional<ItemIndex>
BPInstance::ItemIndexFromExternalId(std::uint64_t externalId) const noexcept
{
    for (const auto& item : m_Items)
    {
        if (item->ExternalId() == externalId)
            return item->Index();
    }
    return std::nullopt;
}

JAGUARCORE_API std::size_t BPInstance::ItemDemand(ItemIndex index) const noexcept
{
    return m_Demands[index];
}

JAGUARCORE_API std::size_t BPInstance::BinCount() const noexcept
{
    return m_Bins.size();
}

JAGUARCORE_API const Bin& BPInstance::GetBin(std::size_t index) const noexcept
{
    return m_Bins[index];
}

JAGUARCORE_API std::size_t BPInstance::TotalDemand() const noexcept
{
    std::size_t result = 0;
    for (std::size_t demand : m_Demands)
        result += demand;
    return result;
}

JAGUARCORE_API double BPInstance::TotalItemArea() const noexcept
{
    float result = 0.0F;
    for (std::size_t i = 0; i < m_Items.size(); ++i)
    {
        result += static_cast<float>(m_Items[i]->Area()) *
                  static_cast<float>(m_Demands[i]);
    }
    return static_cast<double>(result);
}
}
