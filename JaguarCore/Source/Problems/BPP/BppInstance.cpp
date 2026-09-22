#include "JaguarCore/Problems/Bpp.h"

#include <limits>
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
    if (items.empty() || items.size() != demands.size() || bins.empty())
    {
        return Result<BPInstance>::FromError(
            {ErrorCode::InvalidInput, "BPP instance has invalid items or bins"});
    }

    std::size_t totalDemand = 0;
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
        if (demands[i] > std::numeric_limits<std::size_t>::max() - totalDemand)
        {
            return Result<BPInstance>::FromError(
                {ErrorCode::InvalidInput, "BPP demand total overflows"});
        }
        totalDemand += demands[i];
        itemPointers.push_back(std::make_shared<const Item>(items[i]));
    }

    for (std::size_t i = 0; i < bins.size(); ++i)
    {
        if (bins[i].index != i)
        {
            return Result<BPInstance>::FromError(
                {ErrorCode::InvalidInput, "BPP bin indices must be dense"});
        }
        for (std::size_t j = 0; j < i; ++j)
        {
            if (bins[j].externalId == bins[i].externalId)
            {
                return Result<BPInstance>::FromError(
                    {ErrorCode::InvalidInput, "BPP bin external IDs must be unique"});
            }
        }
    }

    if (totalDemand == 0)
    {
        return Result<BPInstance>::FromError(
            {ErrorCode::InvalidInput, "BPP instance must have positive demand"});
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
}
