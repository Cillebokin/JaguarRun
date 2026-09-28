#pragma once

#include "SparrowCore/Export.h"
#include "SparrowCore/ResultAlias.h"

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
    PairMatrix() = default;

    static Result<PairMatrix> Create(std::size_t size);

    std::size_t Size() const noexcept;
    bool Contains(std::size_t first, std::size_t second) const noexcept;
    CollisionEntry& At(std::size_t first, std::size_t second) noexcept;
    const CollisionEntry& At(std::size_t first, std::size_t second) const noexcept;
    float TotalLoss() const noexcept;
    float TotalWeightedLoss() const noexcept;

private:
    PairMatrix(std::size_t size, std::size_t entryCount);

    std::size_t Index(std::size_t first, std::size_t second) const noexcept;

    std::size_t m_Size = 0U;
    std::vector<CollisionEntry> m_Entries;
};
}
