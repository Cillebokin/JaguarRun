#pragma once

#include "JaguarCore/collision/hazard_collector.h"
#include "JaguarCore/collision/hazard_filter.h"
#include "JaguarCore/collision/quadtree.h"
#include "JaguarCore/fail_fast/surrogate.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace jaguar
{
struct BatchCollisionOptions
{
    std::size_t workerCount = 0;
    std::size_t grainSize = 32;
};

using CollisionStopCallback = std::function<bool(const HazardEntity&)>;

class ThreadPool;
class PolygonSoA;

struct CDEConfig
{
    std::uint8_t quadtreeDepth = 4;
    std::uint8_t cdThreshold = 32;
    SurrogateConfig itemSurrogateConfig{};
};

struct CollisionSnapshot
{
    std::vector<Hazard> dynamicHazards;
};

class CollisionEngine
{
public:
    JAGUARCORE_API CollisionEngine(Rect2d bounds, CDEConfig config);
    JAGUARCORE_API ~CollisionEngine();
    CollisionEngine(const CollisionEngine&) = delete;
    CollisionEngine& operator=(const CollisionEngine&) = delete;

    JAGUARCORE_API Result<HazardHandle> registerHazard(Hazard hazard);
    JAGUARCORE_API Result<Hazard> deregisterHazard(HazardHandle handle);

    JAGUARCORE_API bool detectPolygonCollision(const SimplePolygon& shape,
                                               const HazardFilter& filter) const;
    JAGUARCORE_API std::vector<std::uint8_t> detectPolygonCollisions(
        const std::vector<SimplePolygon>& shapes,
        const HazardFilter& filter = NoFilter{}) const;
    JAGUARCORE_API std::vector<std::uint8_t> detectPolygonCollisionsParallel(
        const std::vector<SimplePolygon>& shapes,
        const HazardFilter& filter = NoFilter{},
        const BatchCollisionOptions& options = {}) const;
    JAGUARCORE_API std::vector<HazardEntity> collectPolygonCollisions(
        const SimplePolygon& shape) const;
    JAGUARCORE_API std::vector<HazardEntity> collectPolygonCollisions(
        const SimplePolygon& shape, const HazardFilter& filter) const;
    JAGUARCORE_API bool collectPolygonCollisionsUntil(
        const SimplePolygon& shape,
        HazardCollector& collector,
        const HazardFilter& filter,
        const CollisionStopCallback& stopAfterCollision) const;
    JAGUARCORE_API bool collectPolygonCollisionsUntil(
        const SimplePolygon& shape,
        HazardCollector& collector,
        const CollisionStopCallback& stopAfterCollision) const;

    JAGUARCORE_API bool detectSurrogateCollision(const Surrogate& surrogate,
                                                 const Transform2d& transform,
                                                 const HazardFilter& filter) const;
    JAGUARCORE_API bool collectSurrogateCollisionsUntil(
        const Surrogate& surrogate,
        const Transform2d& transform,
        HazardCollector& collector,
        const HazardFilter& filter,
        const CollisionStopCallback& stopAfterCollision) const;
    JAGUARCORE_API bool collectSurrogateCollisionsUntil(
        const Surrogate& surrogate,
        const Transform2d& transform,
        HazardCollector& collector,
        const CollisionStopCallback& stopAfterCollision) const;

    JAGUARCORE_API CollisionSnapshot save() const;
    JAGUARCORE_API Result<void> restore(const CollisionSnapshot& snapshot);

    JAGUARCORE_API const Rect2d& bounds() const noexcept;

private:
    struct Slot
    {
        std::optional<Hazard> hazard;
        std::unique_ptr<PolygonSoA> shapeCache;
        std::uint32_t generation = 1;
    };

    const Hazard* get(HazardHandle handle) const noexcept;
    Hazard* get(HazardHandle handle) noexcept;
    const PolygonSoA* getShapeCache(HazardHandle handle) const noexcept;
    HazardHandle allocateHandle();
    bool shapesCollide(const SimplePolygon& first, const SimplePolygon& second) const noexcept;
    bool shapesCollide(const PolygonSoA& first, const PolygonSoA& second) const noexcept;
    bool hazardCollides(const SimplePolygon& shape, const Hazard& hazard) const noexcept;
    bool hazardCollides(const PolygonSoA& shape,
                        const Hazard& hazard,
                        const PolygonSoA& hazardShape) const noexcept;
    bool circleCollides(const Circle2d& circle, const SimplePolygon& shape) const noexcept;
    bool edgeCollides(const Edge2d& edge, const SimplePolygon& shape) const noexcept;
    bool circleCollidesWithHazard(const Circle2d& circle,
                                  const Hazard& hazard) const noexcept;
    bool edgeCollidesWithHazard(const Edge2d& edge,
                                 const Hazard& hazard) const noexcept;
    ThreadPool& getThreadPool(std::size_t workerCount) const;

    Rect2d m_Bounds;
    CDEConfig m_Config;
    QuadTree m_QuadTree;
    std::vector<Slot> m_Slots;
    mutable std::mutex m_ThreadPoolMutex;
    mutable std::mutex m_ParallelQueryMutex;
    mutable std::unique_ptr<ThreadPool> m_ThreadPool;
};
}
