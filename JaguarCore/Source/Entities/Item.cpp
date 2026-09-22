#include "JaguarCore/Entities/Item.h"

#include <cmath>
#include <utility>

namespace jaguar
{
namespace
{
constexpr std::size_t MAX_ROTATION_ANGLES = 65536;

bool rotationRangeValid(const RotationRange& rotation) noexcept
{
    if (rotation.radians.size() > MAX_ROTATION_ANGLES)
        return false;
    if (rotation.mode == RotationMode::None ||
        rotation.mode == RotationMode::Continuous)
    {
        if (!rotation.radians.empty())
            return false;
    }
    else if (rotation.mode == RotationMode::Discrete)
    {
        if (rotation.radians.empty())
            return false;
    }
    else
    {
        return false;
    }

    for (double angle : rotation.radians)
    {
        if (!std::isfinite(angle))
            return false;
    }
    return true;
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
    if (!rotationRangeValid(allowedRotation))
    {
        return Result<Item>::FromError(
            {ErrorCode::InvalidInput, "item rotation range is invalid"});
    }

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
