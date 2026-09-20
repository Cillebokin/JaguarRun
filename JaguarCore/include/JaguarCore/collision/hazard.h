#pragma once

#include "JaguarCore/export.h"
#include "JaguarCore/geometry/simple_polygon.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace jaguar
{
enum class HazardKind
{
    PlacedItem,
    Exterior,
    Hole,
    InferiorQualityZone,
};

struct HazardEntity
{
    HazardKind kind = HazardKind::PlacedItem;
    std::size_t itemIndex = 0;
    std::size_t quality = 0;
    std::size_t zoneIndex = 0;

    friend bool operator==(const HazardEntity& lhs, const HazardEntity& rhs) noexcept
    {
        return lhs.kind == rhs.kind && lhs.itemIndex == rhs.itemIndex &&
               lhs.quality == rhs.quality && lhs.zoneIndex == rhs.zoneIndex;
    }
};

struct HazardHandle
{
    std::size_t index = 0;
    std::uint32_t generation = 0;

    friend bool operator==(const HazardHandle& lhs, const HazardHandle& rhs) noexcept
    {
        return lhs.index == rhs.index && lhs.generation == rhs.generation;
    }
};

struct Hazard
{
    HazardEntity entity;
    std::shared_ptr<const SimplePolygon> shape;
    bool dynamic = false;
};
}
