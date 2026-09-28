#include "JaguarCore/Entities/Item.h"

#include <utility>

namespace jaguar
{
namespace
{
bool rotationRangeRepresentable(const RotationRange& rotation) noexcept
{
    switch (rotation.mode)
    {
    case RotationMode::None:
    case RotationMode::Continuous:
        return rotation.radians.empty();
    case RotationMode::Discrete:
        return true;
    }
    return false;
}
}

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

JAGUARCORE_API Result<Item> Item::Create(
    ItemIndex index,
    ItemId externalId,
    OriginalShape originalShape,
    SurrogateConfig surrogateConfig,
    std::optional<std::size_t> minQuality,
    RotationRange allowedRotation)
{
    if (!rotationRangeRepresentable(allowedRotation))
    {
        return Result<Item>::FromError(
            {ErrorCode::InvalidInput, "item rotation range is invalid"});
    }
    for (double& angle : allowedRotation.radians)
        angle = static_cast<double>(static_cast<float>(angle));

    auto collisionShape = originalShape.ConvertToInternal();
    if (!collisionShape.HasValue())
        return Result<Item>::FromError(collisionShape.GetError());

    auto generated = collisionShape.Value().GenerateSurrogate(surrogateConfig);
    if (!generated.HasValue())
        return Result<Item>::FromError(generated.GetError());

    return Result<Item>::FromValue(Item{index, externalId,
                                       std::move(originalShape),
                                       std::move(collisionShape.Value()),
                                       surrogateConfig, minQuality,
                                       std::move(allowedRotation)});
}

JAGUARCORE_API ItemIndex Item::Index() const noexcept
{
    return m_Index;
}

JAGUARCORE_API ItemId Item::ExternalId() const noexcept
{
    return m_ExternalId;
}

JAGUARCORE_API const OriginalShape& Item::GetOriginalShape() const noexcept
{
    return m_OriginalShape;
}

JAGUARCORE_API const SimplePolygon& Item::CollisionShape() const noexcept
{
    return m_CollisionShape;
}

JAGUARCORE_API const SurrogateConfig& Item::GetSurrogateConfig() const noexcept
{
    return m_SurrogateConfig;
}

JAGUARCORE_API std::optional<std::size_t> Item::MinQuality() const noexcept
{
    return m_MinQuality;
}

JAGUARCORE_API const RotationRange& Item::AllowedRotation() const noexcept
{
    return m_AllowedRotation;
}

JAGUARCORE_API double Item::Area() const noexcept
{
    return m_OriginalShape.shape.Area();
}
}
