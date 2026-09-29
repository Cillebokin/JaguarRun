#include "SparrowCore/Quantify/CollisionTracker.h"

#include "SparrowCore/Quantify/OverlapProxy.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
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

bool IsValidEntry(const sparrow::CollisionEntry& entry) noexcept
{
    return std::isfinite(entry.loss) && entry.loss >= 0.0F
        && std::isfinite(entry.weight) && entry.weight > 0.0F;
}

float SafeLoss(float value) noexcept
{
    return std::isfinite(value) && value > 0.0F ? value : 0.0F;
}

std::optional<std::size_t> FindCollisionHandle(
    const std::vector<jaguar::HazardHandle>& handles,
    jaguar::HazardHandle target) noexcept
{
    for (std::size_t index = 0U; index < handles.size(); ++index)
    {
        if (handles[index] == target)
        {
            return index;
        }
    }
    return std::nullopt;
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

Result<void> CollisionTracker::UpdateMovedItem(
    const jaguar::Layout& layout,
    jaguar::PlacedItemHandle previousHandle,
    jaguar::PlacedItemHandle newHandle)
{
    const jaguar::LayoutSnapshot snapshot = layout.Save();
    const jaguar::Rect2d bounds = snapshot.container.CollisionShape().BoundingBox();
    if (!IsFiniteBounds(bounds)
        || bounds.xMin != m_ContainerBounds.xMin
        || bounds.yMin != m_ContainerBounds.yMin
        || bounds.xMax != m_ContainerBounds.xMax
        || bounds.yMax != m_ContainerBounds.yMax
        || snapshot.placements.size() != m_Handles.size()
        || m_ContainerEntries.size() != m_Handles.size()
        || m_PairMatrix.Size() != m_Handles.size())
    {
        return InvalidTracker("moved item update does not match layout");
    }

    const std::optional<std::size_t> movingIndex =
        IndexForHandle(previousHandle);
    if (!movingIndex.has_value())
    {
        return InvalidTracker("previous moved item handle is not tracked");
    }

    const jaguar::LayoutPlacementSnapshot* movedPlacement = nullptr;
    for (const jaguar::LayoutPlacementSnapshot& placement : snapshot.placements)
    {
        if (placement.handle == newHandle)
        {
            movedPlacement = &placement;
            break;
        }
    }
    if (movedPlacement == nullptr)
    {
        return InvalidTracker("new moved item handle is not in the layout");
    }

    for (std::size_t index = 0U; index < m_Handles.size(); ++index)
    {
        const jaguar::PlacedItemHandle expectedHandle =
            index == *movingIndex ? newHandle : m_Handles[index];
        const jaguar::LayoutPlacementSnapshot* placement = nullptr;
        for (const jaguar::LayoutPlacementSnapshot& candidate
             : snapshot.placements)
        {
            if (candidate.handle == expectedHandle)
            {
                placement = &candidate;
                break;
            }
        }
        if (placement == nullptr)
        {
            return InvalidTracker("layout changed more than one tracked item");
        }
        if (index != *movingIndex
            && (placement->placedItem.GetItemIndex()
                    != m_PlacedItems[index].GetItemIndex()
                || placement->placedItem.Transform().rotationRad
                       != m_PlacedItems[index].Transform().rotationRad
                || placement->placedItem.Transform().translation
                       != m_PlacedItems[index].Transform().translation))
        {
            return InvalidTracker("layout changed more than one tracked item");
        }
    }

    CollisionTracker rebuilt;
    const Result<void> rebuildResult = rebuilt.BuildFromLayout(layout);
    if (!rebuildResult.HasValue())
    {
        return rebuildResult;
    }
    if (!rebuilt.IndexForHandle(newHandle).has_value()
        || *rebuilt.IndexForHandle(newHandle) != *movingIndex)
    {
        return InvalidTracker("rebuilt tracker moved item index does not match");
    }

    // Rust's register_item_move recomputes losses for the moved item while
    // preserving all GLS weights. Rebuilding the collision cache here keeps
    // the same observable semantics and avoids stale pair entries.
    for (std::size_t first = 0U; first < m_PairMatrix.Size(); ++first)
    {
        for (std::size_t second = first;
             second < m_PairMatrix.Size();
             ++second)
        {
            rebuilt.m_PairMatrix.At(first, second).weight =
                m_PairMatrix.At(first, second).weight;
        }
    }
    for (std::size_t index = 0U; index < m_ContainerEntries.size(); ++index)
    {
        rebuilt.m_ContainerEntries[index].weight =
            m_ContainerEntries[index].weight;
    }
    *this = std::move(rebuilt);
    return Result<void>::FromValue();
}

CollisionTrackerSnapshot CollisionTracker::Save() const
{
    return {m_Handles, m_PairMatrix, m_ContainerEntries, m_IsSeparated};
}

Result<void> CollisionTracker::Restore(
    const CollisionTrackerSnapshot& snapshot,
    const jaguar::Layout& layout)
{
    const jaguar::LayoutSnapshot layoutSnapshot = layout.Save();
    if (!IsFiniteBounds(layoutSnapshot.container.Bounds())
        || snapshot.handles.size() != layoutSnapshot.placements.size()
        || snapshot.pairMatrix.Size() != snapshot.handles.size()
        || snapshot.containerEntries.size() != snapshot.handles.size()
        || snapshot.isSeparated != layout.IsFeasible())
    {
        return InvalidTracker("collision tracker snapshot does not match layout");
    }
    for (std::size_t index = 0U; index < snapshot.handles.size(); ++index)
    {
        if (!(snapshot.handles[index] == layoutSnapshot.placements[index].handle))
        {
            return InvalidTracker("collision tracker snapshot handles do not match layout");
        }
        if (!IsValidEntry(snapshot.containerEntries[index]))
        {
            return InvalidTracker("collision tracker snapshot contains an invalid container entry");
        }
    }
    for (std::size_t first = 0U; first < snapshot.pairMatrix.Size(); ++first)
    {
        for (std::size_t second = first;
             second < snapshot.pairMatrix.Size();
             ++second)
        {
            if (!IsValidEntry(snapshot.pairMatrix.At(first, second)))
            {
                return InvalidTracker("collision tracker snapshot contains an invalid pair entry");
            }
        }
    }

    m_Handles = snapshot.handles;
    m_PlacedItems.clear();
    m_PlacedItems.reserve(layoutSnapshot.placements.size());
    for (const jaguar::LayoutPlacementSnapshot& placement
         : layoutSnapshot.placements)
    {
        m_PlacedItems.push_back(placement.placedItem);
    }
    m_PairMatrix = snapshot.pairMatrix;
    m_ContainerEntries = snapshot.containerEntries;
    m_ContainerBounds = layoutSnapshot.container.CollisionShape().BoundingBox();
    m_IsSeparated = snapshot.isSeparated;
    return Result<void>::FromValue();
}

Result<void> CollisionTracker::RestoreButKeepWeights(
    const CollisionTrackerSnapshot& snapshot,
    const jaguar::Layout& layout)
{
    if (m_PairMatrix.Size() != snapshot.pairMatrix.Size()
        || m_ContainerEntries.size() != snapshot.containerEntries.size())
    {
        return InvalidTracker(
            "collision tracker weight state does not match snapshot");
    }

    std::vector<float> pairWeights;
    pairWeights.reserve(m_PairMatrix.Size() * (m_PairMatrix.Size() + 1U) / 2U);
    for (std::size_t first = 0U; first < m_PairMatrix.Size(); ++first)
    {
        for (std::size_t second = first;
             second < m_PairMatrix.Size();
             ++second)
        {
            pairWeights.push_back(m_PairMatrix.At(first, second).weight);
        }
    }
    std::vector<float> containerWeights;
    containerWeights.reserve(m_ContainerEntries.size());
    for (const CollisionEntry& entry : m_ContainerEntries)
    {
        containerWeights.push_back(entry.weight);
    }

    const auto restored = Restore(snapshot, layout);
    if (!restored.HasValue())
    {
        return restored;
    }
    std::size_t pairWeightIndex = 0U;
    for (std::size_t first = 0U; first < m_PairMatrix.Size(); ++first)
    {
        for (std::size_t second = first;
             second < m_PairMatrix.Size();
             ++second)
        {
            m_PairMatrix.At(first, second).weight =
                pairWeights[pairWeightIndex++];
        }
    }
    for (std::size_t index = 0U; index < m_ContainerEntries.size(); ++index)
    {
        m_ContainerEntries[index].weight = containerWeights[index];
    }
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
    const auto transformed = item.CollisionShape().TransformedRustF32(transform);
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

Result<float> CollisionTracker::EvaluateCandidateLoss(
    const jaguar::Layout& layout,
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
            {jaguar::ErrorCode::InvalidInput,
             "candidate item has no surrogate"});
    }
    const auto transformed = item.CollisionShape().TransformedRustF32(transform);
    if (!transformed.HasValue())
    {
        return Result<float>::FromError(transformed.GetError());
    }

    auto engine = layout.GetContainer().CreateCollisionEngine();
    if (!engine)
    {
        return Result<float>::FromError(
            {jaguar::ErrorCode::CollisionEngineFailure,
             "candidate collision engine is unavailable"});
    }

    std::vector<jaguar::HazardHandle> collisionHandles;
    collisionHandles.reserve(m_PlacedItems.size());
    for (std::size_t itemSequence = 0U; itemSequence < m_PlacedItems.size();
         ++itemSequence)
    {
        const jaguar::PlacedItem& placedItem = m_PlacedItems[itemSequence];
        // jagua-rs 的 PlacedItem 实体含 pk（每次放置唯一）；跟踪引擎是
        // 私有实例，用布局句柄+变换保证实体唯一性，同 Layout::MakeHazard。
        const jaguar::Hazard hazard{
            {jaguar::HazardKind::PlacedItem, placedItem.GetItemIndex(), 0, 0,
             m_Handles[itemSequence], placedItem.Transform()},
            std::make_shared<const jaguar::SimplePolygon>(
                placedItem.CollisionShape()),
            true};
        const auto registered = engine->RegisterHazard(hazard);
        if (!registered.HasValue())
        {
            return Result<float>::FromError(registered.GetError());
        }
        collisionHandles.push_back(registered.Value());
    }

    jaguar::HazardFilter filter;
    const jaguar::HazardHandle ownHandle = collisionHandles[movingIndex];
    filter.irrelevantHandle = [ownHandle](
                                  jaguar::HazardHandle handle,
                                  const jaguar::HazardEntity&) {
        return handle == ownHandle;
    };
    jaguar::HazardCollector collector;
    const auto surrogateCollisions = engine->CollectSurrogateCollisionsUntil(
        *candidateSurrogate,
        transform,
        collector,
        filter,
        [](const jaguar::HazardEntity&) { return false; });
    if (!surrogateCollisions.HasValue())
    {
        return Result<float>::FromError(surrogateCollisions.GetError());
    }
    engine->CollectPolygonCollisionsUntil(
        transformed.Value(),
        collector,
        filter,
        [](const jaguar::HazardEntity&) { return false; });

    float result = 0.0F;
    for (const auto& entry : collector.Entries())
    {
        if (entry.second.kind == jaguar::HazardKind::PlacedItem)
        {
            const auto otherIndex = FindCollisionHandle(
                collisionHandles, entry.first);
            if (!otherIndex.has_value() || *otherIndex == movingIndex)
            {
                continue;
            }
            result += CalculatePolygonCollisionLoss(
                          transformed.Value(),
                          m_PlacedItems[*otherIndex].CollisionShape())
                * PairWeight(movingIndex, *otherIndex);
        }
        else if (entry.second.kind == jaguar::HazardKind::Exterior)
        {
            result += CalculateContainerPenalty(
                          transformed.Value(), m_ContainerBounds)
                * ContainerWeight(movingIndex);
        }
    }
    if (!std::isfinite(result))
    {
        return Result<float>::FromError(
            {jaguar::ErrorCode::InvalidInput,
             "candidate collision loss is not finite"});
    }
    return Result<float>::FromValue(std::max(result, 0.0F));
}

void CollisionTracker::UpdateWeights() noexcept
{
    float maximumLoss = 0.0F;
    const auto updateMaximum = [&maximumLoss](const CollisionEntry& entry) {
        if (std::isfinite(entry.loss) && entry.loss > maximumLoss)
        {
            maximumLoss = entry.loss;
        }
    };
    for (std::size_t first = 0U; first < m_PairMatrix.Size(); ++first)
    {
        for (std::size_t second = first;
             second < m_PairMatrix.Size();
             ++second)
        {
            updateMaximum(m_PairMatrix.At(first, second));
        }
    }
    for (const CollisionEntry& entry : m_ContainerEntries)
    {
        updateMaximum(entry);
    }

    const auto update = [maximumLoss](CollisionEntry& entry) {
        const float multiplier = entry.loss > 0.0F && maximumLoss > 0.0F
            ? 1.2F + 0.8F * (entry.loss / maximumLoss)
            : 0.95F;
        const float updated = std::max(entry.weight * multiplier, 1.0F);
        if (std::isfinite(updated) && updated > 0.0F)
        {
            entry.weight = std::min(updated, 1000000.0F);
        }
    };
    for (std::size_t first = 0U; first < m_PairMatrix.Size(); ++first)
    {
        for (std::size_t second = first;
             second < m_PairMatrix.Size();
             ++second)
        {
            update(m_PairMatrix.At(first, second));
        }
    }
    for (CollisionEntry& entry : m_ContainerEntries)
    {
        update(entry);
    }
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

    m_ContainerBounds = snapshot.container.CollisionShape().BoundingBox();
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
    const auto pairMatrix = PairMatrix::Create(m_PlacedItems.size());
    if (!pairMatrix.HasValue())
    {
        return Result<void>::FromError(pairMatrix.GetError());
    }
    m_PairMatrix = pairMatrix.Value();
    m_ContainerEntries.assign(m_PlacedItems.size(), CollisionEntry{});

    auto engine = snapshot.container.CreateCollisionEngine();
    if (!engine)
    {
        return InvalidTracker("collision tracker collision engine is unavailable");
    }
    std::vector<jaguar::HazardHandle> collisionHandles;
    collisionHandles.reserve(m_PlacedItems.size());
    for (std::size_t itemSequence = 0U; itemSequence < m_PlacedItems.size();
         ++itemSequence)
    {
        const jaguar::PlacedItem& placedItem = m_PlacedItems[itemSequence];
        // jagua-rs 的 PlacedItem 实体含 pk（每次放置唯一）；跟踪引擎是
        // 私有实例，用布局句柄+变换保证实体唯一性，同 Layout::MakeHazard。
        const jaguar::Hazard hazard{
            {jaguar::HazardKind::PlacedItem, placedItem.GetItemIndex(), 0, 0,
             m_Handles[itemSequence], placedItem.Transform()},
            std::make_shared<const jaguar::SimplePolygon>(
                placedItem.CollisionShape()),
            true};
        const auto registered = engine->RegisterHazard(hazard);
        if (!registered.HasValue())
        {
            return Result<void>::FromError(registered.GetError());
        }
        collisionHandles.push_back(registered.Value());
    }

    for (std::size_t first = 0U; first < m_PlacedItems.size(); ++first)
    {
        jaguar::HazardFilter filter;
        const jaguar::HazardHandle ownHandle = collisionHandles[first];
        filter.irrelevantHandle = [ownHandle](
                                      jaguar::HazardHandle handle,
                                      const jaguar::HazardEntity&) {
            return handle == ownHandle;
        };
        jaguar::HazardCollector collector;
        engine->CollectPolygonCollisionsUntil(
            m_PlacedItems[first].CollisionShape(),
            collector,
            filter,
            [](const jaguar::HazardEntity&) { return false; });

        for (const auto& entry : collector.Entries())
        {
            if (entry.second.kind == jaguar::HazardKind::PlacedItem)
            {
                const auto second = FindCollisionHandle(
                    collisionHandles, entry.first);
                if (!second.has_value() || *second == first)
                {
                    continue;
                }
                m_PairMatrix.At(first, *second).loss =
                    CalculatePolygonCollisionLoss(
                        m_PlacedItems[first].CollisionShape(),
                        m_PlacedItems[*second].CollisionShape());
            }
            else if (entry.second.kind == jaguar::HazardKind::Exterior)
            {
                m_ContainerEntries[first].loss = CalculateContainerPenalty(
                    m_PlacedItems[first].CollisionShape(), m_ContainerBounds);
            }
        }
    }
    return Result<void>::FromValue();
}
}
