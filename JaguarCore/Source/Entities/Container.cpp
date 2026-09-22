#include "JaguarCore/Entities/Container.h"

#include <algorithm>
#include <memory>
#include <utility>

namespace jaguar
{
namespace
{
constexpr std::size_t MAX_QUALITIES = 10;

Rect2d squareBounds(const Rect2d& box)
{
    const double side = std::max(box.Width(), box.Height());
    const double dx = (side - box.Width()) * 0.5;
    const double dy = (side - box.Height()) * 0.5;
    return Rect2d{box.xMin - dx, box.yMin - dy,
                  box.xMin - dx + side, box.yMin - dy + side};
}
}

struct Container::Data
{
    ContainerId id = 0;
    std::shared_ptr<const OriginalShape> originalOuter;
    std::shared_ptr<const SimplePolygon> collisionOuter;
    std::vector<QualityZone> qualityZones;
    CDEConfig cdeConfig{};
    Rect2d bounds{};
    std::vector<Hazard> staticHazards;
};

Container::Container(std::shared_ptr<const Data> data)
    : m_Data(std::move(data))
{
}

JAGUARCORE_API Result<Container> Container::Create(
    ContainerId id,
    OriginalShape originalOuter,
    std::vector<QualityZone> qualityZones,
    CDEConfig cdeConfig)
{
    auto collisionOuter = originalOuter.ConvertToInternal();
    if (!collisionOuter.HasValue())
        return Result<Container>::FromError(collisionOuter.GetError());

    std::vector<bool> qualitySeen(MAX_QUALITIES, false);
    for (const QualityZone& zone : qualityZones)
    {
        if (zone.quality >= MAX_QUALITIES || qualitySeen[zone.quality])
        {
            return Result<Container>::FromError(
                {ErrorCode::InvalidInput,
                 "container quality zones must have unique qualities below 10"});
        }
        if (zone.shapes.empty())
        {
            return Result<Container>::FromError(
                {ErrorCode::InvalidInput, "container quality zone is empty"});
        }
        qualitySeen[zone.quality] = true;
    }

    auto data = std::make_shared<Data>();
    data->id = id;
    data->originalOuter = std::make_shared<const OriginalShape>(
        std::move(originalOuter));
    data->collisionOuter = std::make_shared<const SimplePolygon>(
        std::move(collisionOuter.Value()));
    data->qualityZones = std::move(qualityZones);
    data->cdeConfig = cdeConfig;
    data->bounds = squareBounds(data->collisionOuter->BoundingBox());

    data->staticHazards.push_back(Hazard{
        {HazardKind::Exterior, 0, 0, 0}, data->collisionOuter, false});

    for (const QualityZone& zone : data->qualityZones)
    {
        for (std::size_t shapeIndex = 0; shapeIndex < zone.shapes.size(); ++shapeIndex)
        {
            auto shape = std::make_shared<const SimplePolygon>(zone.shapes[shapeIndex]);
            const HazardEntity entity = zone.quality == 0
                                            ? HazardEntity{HazardKind::Hole, 0, 0,
                                                           shapeIndex}
                                            : HazardEntity{HazardKind::InferiorQualityZone,
                                                           0, zone.quality, shapeIndex};
            data->staticHazards.push_back(Hazard{entity, std::move(shape), false});
        }
    }

    return Result<Container>::FromValue(Container{std::move(data)});
}

JAGUARCORE_API ContainerId Container::Id() const noexcept
{
    return m_Data->id;
}

JAGUARCORE_API double Container::Area() const noexcept
{
    return m_Data->originalOuter->shape.Area();
}

JAGUARCORE_API const OriginalShape& Container::GetOriginalShape() const noexcept
{
    return *m_Data->originalOuter;
}

JAGUARCORE_API const SimplePolygon& Container::CollisionShape() const noexcept
{
    return *m_Data->collisionOuter;
}

JAGUARCORE_API const Rect2d& Container::Bounds() const noexcept
{
    return m_Data->bounds;
}

JAGUARCORE_API bool Container::SameBase(const Container& other) const noexcept
{
    return m_Data == other.m_Data;
}

JAGUARCORE_API CollisionEnginePtr Container::CreateCollisionEngine() const
{
    CollisionEnginePtr engine{
        new CollisionEngine(m_Data->bounds, m_Data->cdeConfig)};
    for (const Hazard& hazard : m_Data->staticHazards)
    {
        if (!engine->RegisterHazard(hazard).HasValue())
            return nullptr;
    }
    return engine;
}
}
