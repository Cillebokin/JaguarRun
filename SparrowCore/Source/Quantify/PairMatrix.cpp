#include "SparrowCore/Quantify/PairMatrix.h"

#include <algorithm>
#include <cmath>

namespace sparrow
{
float CollisionEntry::WeightedLoss() const noexcept
{
    if (!std::isfinite(loss) || !std::isfinite(weight)
        || loss <= 0.0F || weight <= 0.0F)
    {
        return 0.0F;
    }
    const float result = loss * weight;
    return std::isfinite(result) ? result : 0.0F;
}

PairMatrix::PairMatrix(std::size_t size)
    : m_Size(size),
      m_Entries(size * (size + 1U) / 2U)
{
}

std::size_t PairMatrix::Size() const noexcept
{
    return m_Size;
}

bool PairMatrix::Contains(std::size_t first, std::size_t second) const noexcept
{
    return first < m_Size && second < m_Size;
}

CollisionEntry& PairMatrix::At(std::size_t first, std::size_t second) noexcept
{
    return m_Entries[Index(first, second)];
}

const CollisionEntry& PairMatrix::At(std::size_t first,
                                     std::size_t second) const noexcept
{
    return m_Entries[Index(first, second)];
}

float PairMatrix::TotalLoss() const noexcept
{
    float result = 0.0F;
    for (const CollisionEntry& entry : m_Entries)
    {
        if (std::isfinite(entry.loss) && entry.loss > 0.0F)
        {
            result += entry.loss;
        }
    }
    return std::isfinite(result) ? result : 0.0F;
}

float PairMatrix::TotalWeightedLoss() const noexcept
{
    float result = 0.0F;
    for (const CollisionEntry& entry : m_Entries)
    {
        result += entry.WeightedLoss();
    }
    return std::isfinite(result) ? result : 0.0F;
}

std::size_t PairMatrix::Index(std::size_t first,
                              std::size_t second) const noexcept
{
    const std::size_t smaller = std::min(first, second);
    const std::size_t larger = std::max(first, second);
    return larger * (larger + 1U) / 2U + smaller;
}
}
