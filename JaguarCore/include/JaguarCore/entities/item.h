#pragma once

#include "JaguarCore/Export.h"
#include "JaguarCore/FailFast/Surrogate.h"
#include "JaguarCore/Geometry/OriginalShape.h"
#include "JaguarCore/Result.h"

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
    static JAGUARCORE_API Result<Item> Create(
        ItemIndex index,
        ItemId externalId,
        OriginalShape originalShape,
        SurrogateConfig surrogateConfig,
        std::optional<std::size_t> minQuality = std::nullopt,
        RotationRange allowedRotation = {});

    JAGUARCORE_API ItemIndex Index() const noexcept;
    JAGUARCORE_API ItemId ExternalId() const noexcept;
    JAGUARCORE_API const OriginalShape& GetOriginalShape() const noexcept;
    JAGUARCORE_API const SimplePolygon& CollisionShape() const noexcept;
    JAGUARCORE_API const SurrogateConfig& GetSurrogateConfig() const noexcept;
    JAGUARCORE_API std::optional<std::size_t> MinQuality() const noexcept;
    JAGUARCORE_API const RotationRange& AllowedRotation() const noexcept;
    JAGUARCORE_API double Area() const noexcept;

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
