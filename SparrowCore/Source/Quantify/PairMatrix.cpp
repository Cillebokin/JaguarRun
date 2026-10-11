#include "SparrowCore/Quantify/PairMatrix.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace sparrow
{
float CollisionEntry::WeightedLoss() const noexcept
{
    return weight * loss;
}

PairMatrix::PairMatrix(std::size_t size, std::size_t entryCount)
    : m_Size(size),
      m_Entries(entryCount)
{
}

Result<PairMatrix> PairMatrix::Create(std::size_t size)
{
    if (size == 0U)
    {
        return Result<PairMatrix>::FromValue(PairMatrix{});
    }

#ifndef NDEBUG
    if (size == std::numeric_limits<std::size_t>::max()
        || size > std::numeric_limits<std::size_t>::max() / (size + 1U))
    {
        throw std::overflow_error("pair matrix length calculation overflowed");
    }
    const std::size_t entryCount = size * (size + 1U) / 2U;
#else
    const std::size_t entryCount = size * (size + 1U) / 2U;
#endif
    return Result<PairMatrix>::FromValue(
        PairMatrix(size, entryCount));
}

std::size_t PairMatrix::Size() const noexcept
{
    return m_Size;
}

bool PairMatrix::Contains(std::size_t first, std::size_t second) const noexcept
{
    return first < m_Size && second < m_Size;
}

CollisionEntry& PairMatrix::At(std::size_t first, std::size_t second)
{
    return m_Entries.at(Index(first, second));
}

const CollisionEntry& PairMatrix::At(std::size_t first,
                                     std::size_t second) const
{
    return m_Entries.at(Index(first, second));
}

float PairMatrix::TotalLoss() const noexcept
{
    float result = 0.0F;
    for (const CollisionEntry& entry : m_Entries)
    {
        result += entry.loss;
    }
    return result;
}

float PairMatrix::TotalWeightedLoss() const noexcept
{
    float result = 0.0F;
    for (const CollisionEntry& entry : m_Entries)
    {
        result += entry.weight * entry.loss;
    }
    return result;
}

std::size_t PairMatrix::Index(std::size_t first,
                              std::size_t second) const
{
    if (!Contains(first, second))
    {
#ifndef NDEBUG
        throw std::out_of_range("PairMatrix index is outside its logical size");
#else
        const std::size_t smaller = std::min(first, second);
        const std::size_t larger = std::max(first, second);
        return smaller * m_Size + larger
            - (smaller * (smaller + 1U)) / 2U;
#endif
    }

    const std::size_t smaller = std::min(first, second);
    const std::size_t larger = std::max(first, second);
    const std::size_t half = smaller / 2U;
    std::size_t rowStart = smaller * (m_Size - half);
    if (smaller % 2U == 0U)
    {
        rowStart += half;
    }
    return rowStart + larger - smaller;
}
}
