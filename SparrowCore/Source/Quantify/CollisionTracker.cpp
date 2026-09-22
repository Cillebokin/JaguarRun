#include "SparrowCore/Quantify/CollisionTracker.h"

#include "SparrowCore/Quantify/OverlapProxy.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace
{
sparrow::Result<void> InvalidTracker(const char* message)
{
    return sparrow::Result<void>::FromError(
        {jaguar::ErrorCode::InvalidInput, message});
}

bool IsFiniteBounds(const jaguar::Rect2d& bounds) noexcept
{
    return std::isfinite(bounds.xMin) && std::isfinite(bounds.yMin)
        && std::isfinite(bounds.xMax) && std::isfinite(bounds.yMax)
        && bounds.xMin < bounds.xMax && bounds.yMin < bounds.yMax;
}

float SafeLoss(float value) noexcept
{
    return std::isfinite(value) && value > 0.0F ? value : 0.0F;
}
}

namespace sparrow
{
Result<CollisionTracker> CollisionTracker::Create(const jaguar::Layout& layout)
{
    CollisionTracker tracker;
    const Result<void> rebuilt = tracker.BuildFromLayout(layout);
    if (!rebuilt.HasValue())
    {
        return Result<CollisionTracker>::FromError(rebuilt.GetError());
    }
    return Result<CollisionTracker>::FromValue(std::move(tracker));
}

Result<void> CollisionTracker::Rebuild(const jaguar::Layout& layout)
{
    return BuildFromLayout(layout);
}

CollisionTrackerSnapshot CollisionTracker::Save() const
{
    return {m_Handles, m_PairMatrix, m_ContainerEntries, m_IsSeparated};
}

Result<void> CollisionTracker::Restore(
    const CollisionTrackerSnapshot& snapshot,
    const jaguar::Layout& layout)
{
    const Result<void> rebuilt = BuildFromLayout(layout);
    if (!rebuilt.HasValue())
    {
        return rebuilt;
    }
    const bool handlesMatch = snapshot.handles.size() == m_Handles.size()
        && std::equal(snapshot.handles.begin(),
                      snapshot.handles.end(),
                      m_Handles.begin(),
                      [](const jaguar::PlacedItemHandle& left,
                         const jaguar::PlacedItemHandle& right) {
                          return left == right;
                      });
    if (!handlesMatch
        || snapshot.pairMatrix.Size() != m_PairMatrix.Size()
        || snapshot.containerEntries.size() != m_ContainerEntries.size())
    {
        return InvalidTracker("collision tracker snapshot does not match layout");
    }
    for (std::size_t first = 0U; first < m_PairMatrix.Size(); ++first)
    {
        for (std::size_t second = first; second < m_PairMatrix.Size(); ++second)
        {
            m_PairMatrix.At(first, second) = snapshot.pairMatrix.At(first, second);
        }
    }
    m_ContainerEntries = snapshot.containerEntries;
    m_IsSeparated = snapshot.isSeparated;
    return Result<void>::FromValue();
}

std::size_t CollisionTracker::ItemCount() const noexcept
{
    return m_PlacedItems.size();
}

std::optional<std::size_t> CollisionTracker::IndexForHandle(
    jaguar::PlacedItemHandle handle) const noexcept
{
    for (std::size_t index = 0U; index < m_Handles.size(); ++index)
    {
        if (m_Handles[index] == handle)
        {
            return index;
        }
    }
    return std::nullopt;
}

float CollisionTracker::PairLoss(std::size_t first,
                                 std::size_t second) const noexcept
{
    if (!m_PairMatrix.Contains(first, second))
    {
        return 0.0F;
    }
    return SafeLoss(m_PairMatrix.At(first, second).loss);
}

float CollisionTracker::PairWeight(std::size_t first,
                                   std::size_t second) const noexcept
{
    if (!m_PairMatrix.Contains(first, second))
    {
        return 0.0F;
    }
    const float weight = m_PairMatrix.At(first, second).weight;
    return std::isfinite(weight) && weight > 0.0F ? weight : 0.0F;
}

float CollisionTracker::ContainerLoss(std::size_t index) const noexcept
{
    if (index >= m_ContainerEntries.size())
    {
        return 0.0F;
    }
    return SafeLoss(m_ContainerEntries[index].loss);
}

float CollisionTracker::ContainerWeight(std::size_t index) const noexcept
{
    if (index >= m_ContainerEntries.size())
    {
        return 0.0F;
    }
    const float weight = m_ContainerEntries[index].weight;
    return std::isfinite(weight) && weight > 0.0F ? weight : 0.0F;
}

float CollisionTracker::ItemLoss(std::size_t index) const noexcept
{
    if (index >= ItemCount())
    {
        return 0.0F;
    }
    float result = ContainerLoss(index);
    for (std::size_t other = 0U; other < ItemCount(); ++other)
    {
        if (other != index)
        {
            result += PairLoss(index, other);
        }
    }
    return std::isfinite(result) ? result : 0.0F;
}

float CollisionTracker::WeightedItemLoss(std::size_t index) const noexcept
{
    if (index >= ItemCount())
    {
        return 0.0F;
    }
    float result = ContainerLoss(index) * ContainerWeight(index);
    for (std::size_t other = 0U; other < ItemCount(); ++other)
    {
        if (other != index)
        {
            result += PairLoss(index, other) * PairWeight(index, other);
        }
    }
    return std::isfinite(result) ? result : 0.0F;
}

float CollisionTracker::TotalLoss() const noexcept
{
    float result = m_PairMatrix.TotalLoss();
    for (const CollisionEntry& entry : m_ContainerEntries)
    {
        result += SafeLoss(entry.loss);
    }
    return std::isfinite(result) ? result : 0.0F;
}

float CollisionTracker::TotalWeightedLoss() const noexcept
{
    float result = m_PairMatrix.TotalWeightedLoss();
    for (const CollisionEntry& entry : m_ContainerEntries)
    {
        result += entry.WeightedLoss();
    }
    return std::isfinite(result) ? result : 0.0F;
}

bool CollisionTracker::IsSeparated() const noexcept
{
    return m_IsSeparated;
}

Result<float> CollisionTracker::EvaluateCandidateLoss(
    const jaguar::Item& item,
    const jaguar::Transform2d& transform,
    std::size_t movingIndex) const
{
    if (movingIndex >= ItemCount() || !transform.IsFinite())
    {
        return Result<float>::FromError(
            {jaguar::ErrorCode::InvalidInput, "candidate transform is invalid"});
    }
    const jaguar::Surrogate* candidateSurrogate =
        item.CollisionShape().GetSurrogate();
    if (candidateSurrogate == nullptr)
    {
        return Result<float>::FromError(
            {jaguar::ErrorCode::InvalidInput, "candidate item has no surrogate"});
    }
    const auto transformed = item.CollisionShape().Transformed(transform);
    if (!transformed.HasValue())
    {
        return Result<float>::FromError(transformed.GetError());
    }

    float result = CalculateContainerPenalty(
        transformed.Value(), m_ContainerBounds) * ContainerWeight(movingIndex);
    for (std::size_t index = 0U; index < ItemCount(); ++index)
    {
        if (index == movingIndex)
        {
            continue;
        }
        const jaguar::Surrogate* otherSurrogate =
            m_PlacedItems[index].GetItem().CollisionShape().GetSurrogate();
        if (otherSurrogate == nullptr)
        {
            continue;
        }
        const float loss = CalculateOverlapProxy(
            *candidateSurrogate,
            transform,
            *otherSurrogate,
            m_PlacedItems[index].Transform());
        result += loss * PairWeight(movingIndex, index);
    }
    if (!std::isfinite(result))
    {
        return Result<float>::FromError(
            {jaguar::ErrorCode::InvalidInput,
             "candidate collision loss is not finite"});
    }
    return Result<float>::FromValue(std::max(result, 0.0F));
}

void CollisionTracker::UpdateWeights(float collisionMultiplier,
                                      float clearDecay) noexcept
{
    if (!std::isfinite(collisionMultiplier) || collisionMultiplier < 1.0F
        || !std::isfinite(clearDecay) || clearDecay <= 0.0F
        || clearDecay > 1.0F)
    {
        return;
    }
    const auto update = [collisionMultiplier, clearDecay](CollisionEntry& entry) {
        const float factor = entry.loss > 0.0F
            ? collisionMultiplier
            : clearDecay;
        const float updated = entry.weight * factor;
        if (std::isfinite(updated) && updated > 0.0F)
        {
            entry.weight = std::min(updated, 1000000.0F);
        }
    };
    for (std::size_t first = 0U; first < m_PairMatrix.Size(); ++first)
    {
        for (std::size_t second = first; second < m_PairMatrix.Size(); ++second)
        {
            update(m_PairMatrix.At(first, second));
        }
    }
    for (CollisionEntry& entry : m_ContainerEntries)
    {
        update(entry);
    }
}

Result<void> CollisionTracker::BuildFromLayout(const jaguar::Layout& layout)
{
    const jaguar::LayoutSnapshot snapshot = layout.Save();
    if (!IsFiniteBounds(snapshot.container.Bounds()))
    {
        return InvalidTracker("collision tracker container bounds are invalid");
    }

    m_ContainerBounds = snapshot.container.Bounds();
    m_IsSeparated = layout.IsFeasible();
    m_Handles.clear();
    m_PlacedItems.clear();
    m_Handles.reserve(snapshot.placements.size());
    m_PlacedItems.reserve(snapshot.placements.size());
    for (const jaguar::LayoutPlacementSnapshot& placement : snapshot.placements)
    {
        m_Handles.push_back(placement.handle);
        m_PlacedItems.push_back(placement.placedItem);
    }
    m_PairMatrix = PairMatrix(m_PlacedItems.size());
    m_ContainerEntries.assign(m_PlacedItems.size(), CollisionEntry{});

    for (std::size_t first = 0U; first < m_PlacedItems.size(); ++first)
    {
        const jaguar::Surrogate* firstSurrogate =
            m_PlacedItems[first].GetItem().CollisionShape().GetSurrogate();
        m_ContainerEntries[first].loss = CalculateContainerPenalty(
            m_PlacedItems[first].CollisionShape(), m_ContainerBounds);
        if (firstSurrogate == nullptr)
        {
            continue;
        }
        for (std::size_t second = first + 1U;
             second < m_PlacedItems.size();
             ++second)
        {
            const jaguar::Surrogate* secondSurrogate =
                m_PlacedItems[second].GetItem().CollisionShape().GetSurrogate();
            if (secondSurrogate == nullptr)
            {
                continue;
            }
            m_PairMatrix.At(first, second).loss = CalculateOverlapProxy(
                *firstSurrogate,
                m_PlacedItems[first].Transform(),
                *secondSurrogate,
                m_PlacedItems[second].Transform());
        }
    }
    return Result<void>::FromValue();
}
}
