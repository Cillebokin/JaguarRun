#include "JaguarCore/entities/container.h"

#include <algorithm>
#include <memory>
#include <utility>

namespace jaguar
{
namespace
{
constexpr std::size_t kMaxQualities = 10;

Rect2d squareBounds(const Rect2d& box)
{
    const double side = std::max(box.width(), box.height());
    const double dx = (side - box.width()) * 0.5;
    const double dy = (side - box.height()) * 0.5;
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

JAGUARCORE_API Result<Container> Container::create(
    ContainerId id,
    OriginalShape originalOuter,
    std::vector<QualityZone> qualityZones,
    CDEConfig cdeConfig)
{
    auto collisionOuter = originalOuter.convertToInternal();
    if (!collisionOuter.hasValue())
        return Result<Container>::fromError(collisionOuter.error());

    std::vector<bool> qualitySeen(kMaxQualities, false);
    for (const QualityZone& zone : qualityZones)
    {
        if (zone.quality >= kMaxQualities || qualitySeen[zone.quality])
        {
            return Result<Container>::fromError(
                {ErrorCode::InvalidInput,
                 "container quality zones must have unique qualities below 10"});
        }
        if (zone.shapes.empty())
        {
            return Result<Container>::fromError(
                {ErrorCode::InvalidInput, "container quality zone is empty"});
        }
        qualitySeen[zone.quality] = true;
    }

    auto data = std::make_shared<Data>();
    data->id = id;
    data->originalOuter = std::make_shared<const OriginalShape>(
        std::move(originalOuter));
    data->collisionOuter = std::make_shared<const SimplePolygon>(
        std::move(collisionOuter.value()));
    data->qualityZones = std::move(qualityZones);
    data->cdeConfig = cdeConfig;
    data->bounds = squareBounds(data->collisionOuter->boundingBox());

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

    return Result<Container>::fromValue(Container{std::move(data)});
}

JAGUARCORE_API ContainerId Container::id() const noexcept
{
    return m_Data->id;
}

JAGUARCORE_API double Container::area() const noexcept
{
    return m_Data->originalOuter->shape.area();
}

JAGUARCORE_API const OriginalShape& Container::originalShape() const noexcept
{
    return *m_Data->originalOuter;
}

JAGUARCORE_API const SimplePolygon& Container::collisionShape() const noexcept
{
    return *m_Data->collisionOuter;
}

JAGUARCORE_API const Rect2d& Container::bounds() const noexcept
{
    return m_Data->bounds;
}

JAGUARCORE_API bool Container::sameBase(const Container& other) const noexcept
{
    return m_Data == other.m_Data;
}

JAGUARCORE_API std::unique_ptr<CollisionEngine> Container::createCollisionEngine() const
{
    auto engine = std::make_unique<CollisionEngine>(m_Data->bounds, m_Data->cdeConfig);
    for (const Hazard& hazard : m_Data->staticHazards)
    {
        if (!engine->registerHazard(hazard).hasValue())
            return nullptr;
    }
    return engine;
}
}
