#pragma once

#include "SparrowCore/Export.h"
#include "SparrowCore/Quantify/PairMatrix.h"
#include "SparrowCore/ResultAlias.h"

#include "JaguarCore/Entities/Layout.h"
#include "JaguarCore/Entities/PlacedItem.h"
#include "JaguarCore/Entities/Item.h"
#include "JaguarCore/Geometry/Transformation.h"

#include <cstddef>
#include <optional>
#include <vector>

namespace sparrow
{
struct CollisionTrackerSnapshot
{
    std::vector<jaguar::PlacedItemHandle> handles;
    PairMatrix pairMatrix;
    std::vector<CollisionEntry> containerEntries;
    bool isSeparated = false;
};

class SPARROWCORE_API CollisionTracker
{
public:
    static Result<CollisionTracker> Create(const jaguar::Layout& layout);

    Result<void> Rebuild(const jaguar::Layout& layout);
    CollisionTrackerSnapshot Save() const;
    Result<void> Restore(const CollisionTrackerSnapshot& snapshot,
                         const jaguar::Layout& layout);

    std::size_t ItemCount() const noexcept;
    std::optional<std::size_t> IndexForHandle(
        jaguar::PlacedItemHandle handle) const noexcept;
    float PairLoss(std::size_t first, std::size_t second) const noexcept;
    float PairWeight(std::size_t first, std::size_t second) const noexcept;
    float ContainerLoss(std::size_t index) const noexcept;
    float ContainerWeight(std::size_t index) const noexcept;
    float ItemLoss(std::size_t index) const noexcept;
    float WeightedItemLoss(std::size_t index) const noexcept;
    float TotalLoss() const noexcept;
    float TotalWeightedLoss() const noexcept;
    bool IsSeparated() const noexcept;
    Result<float> EvaluateCandidateLoss(
        const jaguar::Item& item,
        const jaguar::Transform2d& transform,
        std::size_t movingIndex) const;
    void UpdateWeights(float collisionMultiplier = 1.2F,
                       float clearDecay = 0.95F) noexcept;

private:
    Result<void> BuildFromLayout(const jaguar::Layout& layout);

    std::vector<jaguar::PlacedItemHandle> m_Handles;
    std::vector<jaguar::PlacedItem> m_PlacedItems;
    PairMatrix m_PairMatrix;
    std::vector<CollisionEntry> m_ContainerEntries;
    jaguar::Rect2d m_ContainerBounds{};
    bool m_IsSeparated = false;
};
}
