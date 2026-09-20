#include "JaguarCore/entities/item.h"

#include <utility>

namespace jaguar
{
Item::Item(ItemIndex index,
           ItemId externalId,
           OriginalShape originalShape,
           SimplePolygon collisionShape,
           SurrogateConfig surrogateConfig,
           std::optional<std::size_t> minQuality,
           RotationRange allowedRotation)
    : m_Index(index),
      m_ExternalId(externalId),
      m_OriginalShape(std::move(originalShape)),
      m_CollisionShape(std::move(collisionShape)),
      m_SurrogateConfig(surrogateConfig),
      m_MinQuality(minQuality),
      m_AllowedRotation(std::move(allowedRotation))
{
}

JAGUARCORE_API Result<Item> Item::create(
    ItemIndex index,
    ItemId externalId,
    OriginalShape originalShape,
    SurrogateConfig surrogateConfig,
    std::optional<std::size_t> minQuality,
    RotationRange allowedRotation)
{
    auto collisionShape = originalShape.convertToInternal();
    if (!collisionShape.hasValue())
        return Result<Item>::fromError(collisionShape.error());

    auto generated = collisionShape.value().generateSurrogate(surrogateConfig);
    if (!generated.hasValue())
        return Result<Item>::fromError(generated.error());

    return Result<Item>::fromValue(Item{index, externalId,
                                       std::move(originalShape),
                                       std::move(collisionShape.value()),
                                       surrogateConfig, minQuality,
                                       std::move(allowedRotation)});
}

JAGUARCORE_API ItemIndex Item::index() const noexcept
{
    return m_Index;
}

JAGUARCORE_API ItemId Item::externalId() const noexcept
{
    return m_ExternalId;
}

JAGUARCORE_API const OriginalShape& Item::originalShape() const noexcept
{
    return m_OriginalShape;
}

JAGUARCORE_API const SimplePolygon& Item::collisionShape() const noexcept
{
    return m_CollisionShape;
}

JAGUARCORE_API const SurrogateConfig& Item::surrogateConfig() const noexcept
{
    return m_SurrogateConfig;
}

JAGUARCORE_API std::optional<std::size_t> Item::minQuality() const noexcept
{
    return m_MinQuality;
}

JAGUARCORE_API const RotationRange& Item::allowedRotation() const noexcept
{
    return m_AllowedRotation;
}

JAGUARCORE_API double Item::area() const noexcept
{
    return m_OriginalShape.shape.area();
}
}
