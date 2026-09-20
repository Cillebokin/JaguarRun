#pragma once

#include "JaguarCore/export.h"
#include "JaguarCore/fail_fast/surrogate.h"
#include "JaguarCore/geometry/original_shape.h"
#include "JaguarCore/result.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace jaguar
{
using ItemId = std::uint64_t;
using ItemIndex = std::size_t;

enum class RotationMode
{
    None,
    Discrete,
    Continuous,
};

struct RotationRange
{
    RotationMode mode = RotationMode::None;
    std::vector<double> radians;
};

class Item
{
public:
    static JAGUARCORE_API Result<Item> create(
        ItemIndex index,
        ItemId externalId,
        OriginalShape originalShape,
        SurrogateConfig surrogateConfig,
        std::optional<std::size_t> minQuality = std::nullopt,
        RotationRange allowedRotation = {});

    JAGUARCORE_API ItemIndex index() const noexcept;
    JAGUARCORE_API ItemId externalId() const noexcept;
    JAGUARCORE_API const OriginalShape& originalShape() const noexcept;
    JAGUARCORE_API const SimplePolygon& collisionShape() const noexcept;
    JAGUARCORE_API const SurrogateConfig& surrogateConfig() const noexcept;
    JAGUARCORE_API std::optional<std::size_t> minQuality() const noexcept;
    JAGUARCORE_API const RotationRange& allowedRotation() const noexcept;
    JAGUARCORE_API double area() const noexcept;

private:
    Item(ItemIndex index,
         ItemId externalId,
         OriginalShape originalShape,
         SimplePolygon collisionShape,
         SurrogateConfig surrogateConfig,
         std::optional<std::size_t> minQuality,
         RotationRange allowedRotation);

    ItemIndex m_Index = 0;
    ItemId m_ExternalId = 0;
    OriginalShape m_OriginalShape;
    SimplePolygon m_CollisionShape;
    SurrogateConfig m_SurrogateConfig{};
    std::optional<std::size_t> m_MinQuality;
    RotationRange m_AllowedRotation;
};
}
