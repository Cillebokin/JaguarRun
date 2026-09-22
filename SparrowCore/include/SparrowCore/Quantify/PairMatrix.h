#pragma once

#include "SparrowCore/Export.h"

#include <cstddef>
#include <vector>

namespace sparrow
{
struct SPARROWCORE_API CollisionEntry
{
    float loss = 0.0F;
    float weight = 1.0F;

    float WeightedLoss() const noexcept;
};

class SPARROWCORE_API PairMatrix
{
public:
    explicit PairMatrix(std::size_t size = 0U);

    std::size_t Size() const noexcept;
    bool Contains(std::size_t first, std::size_t second) const noexcept;
    CollisionEntry& At(std::size_t first, std::size_t second) noexcept;
    const CollisionEntry& At(std::size_t first, std::size_t second) const noexcept;
    float TotalLoss() const noexcept;
    float TotalWeightedLoss() const noexcept;

private:
    std::size_t Index(std::size_t first, std::size_t second) const noexcept;

    std::size_t m_Size = 0U;
    std::vector<CollisionEntry> m_Entries;
};
}
