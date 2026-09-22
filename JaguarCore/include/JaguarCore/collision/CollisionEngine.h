#pragma once

#include "JaguarCore/Collision/HazardCollector.h"
#include "JaguarCore/Collision/HazardFilter.h"
#include "JaguarCore/Collision/Quadtree.h"
#include "JaguarCore/FailFast/Surrogate.h"

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

class CollisionEngine;

struct CollisionEngineDeleter
{
    JAGUARCORE_API void operator()(CollisionEngine* engine) const noexcept;
};

using CollisionEnginePtr = std::unique_ptr<CollisionEngine, CollisionEngineDeleter>;

struct CollisionSnapshot
{
    std::vector<Hazard> dynamicHazards;
};

// Const queries may run concurrently on one engine. RegisterHazard,
// DeregisterHazard, and Restore mutate engine state and must be externally
// serialized against all queries and other mutations. A parallel query may
// invoke its filter concurrently from multiple workers.
class CollisionEngine
{
public:
    JAGUARCORE_API CollisionEngine(Rect2d bounds, CDEConfig config);
    JAGUARCORE_API ~CollisionEngine();
    CollisionEngine(const CollisionEngine&) = delete;
    CollisionEngine& operator=(const CollisionEngine&) = delete;

    JAGUARCORE_API Result<HazardHandle> RegisterHazard(Hazard hazard);
    JAGUARCORE_API Result<Hazard> DeregisterHazard(HazardHandle handle);

    JAGUARCORE_API bool DetectPolygonCollision(const SimplePolygon& shape,
                                               const HazardFilter& filter) const;
    JAGUARCORE_API std::vector<std::uint8_t> DetectPolygonCollisions(
        const std::vector<SimplePolygon>& shapes,
        const HazardFilter& filter = NoFilter{}) const;
    JAGUARCORE_API Result<std::vector<std::uint8_t>> DetectPolygonCollisionsParallel(
        const std::vector<SimplePolygon>& shapes,
        const HazardFilter& filter = NoFilter{},
        const BatchCollisionOptions& options = {}) const;
    JAGUARCORE_API std::vector<HazardEntity> CollectPolygonCollisions(
        const SimplePolygon& shape) const;
    JAGUARCORE_API std::vector<HazardEntity> CollectPolygonCollisions(
        const SimplePolygon& shape, const HazardFilter& filter) const;
    JAGUARCORE_API bool CollectPolygonCollisionsUntil(
        const SimplePolygon& shape,
        HazardCollector& collector,
        const HazardFilter& filter,
        const CollisionStopCallback& stopAfterCollision) const;
    JAGUARCORE_API bool CollectPolygonCollisionsUntil(
        const SimplePolygon& shape,
        HazardCollector& collector,
        const CollisionStopCallback& stopAfterCollision) const;

    JAGUARCORE_API Result<bool> DetectSurrogateCollision(
        const Surrogate&, const Transform2d&, const HazardFilter&) const;
    JAGUARCORE_API Result<bool> CollectSurrogateCollisionsUntil(
        const Surrogate& surrogate,
        const Transform2d& transform,
        HazardCollector& collector,
        const HazardFilter& filter,
        const CollisionStopCallback& stopAfterCollision) const;
    JAGUARCORE_API Result<bool> CollectSurrogateCollisionsUntil(
        const Surrogate& surrogate,
        const Transform2d& transform,
        HazardCollector& collector,
        const CollisionStopCallback& stopAfterCollision) const;

    JAGUARCORE_API CollisionSnapshot Save() const;
    JAGUARCORE_API Result<void> Restore(const CollisionSnapshot& snapshot);

    JAGUARCORE_API const Rect2d& Bounds() const noexcept;

private:
    struct Slot
    {
        std::optional<Hazard> hazard;
        std::unique_ptr<PolygonSoA> shapeCache;
        std::uint32_t generation = 1;
    };

    const Hazard* Get(HazardHandle handle) const noexcept;
    Hazard* Get(HazardHandle handle) noexcept;
    const PolygonSoA* GetShapeCache(HazardHandle handle) const noexcept;
    HazardHandle AllocateHandle();
    bool ShapesCollide(const SimplePolygon& first, const SimplePolygon& second) const noexcept;
    bool ShapesCollide(const PolygonSoA& first, const PolygonSoA& second) const noexcept;
    bool HazardCollides(const SimplePolygon& shape, const Hazard& hazard) const noexcept;
    bool HazardCollides(const PolygonSoA& shape,
                        const Hazard& hazard,
                        const PolygonSoA& hazardShape) const noexcept;
    bool CircleCollides(const Circle2d& circle, const SimplePolygon& shape) const noexcept;
    bool EdgeCollides(const Edge2d& edge, const SimplePolygon& shape) const noexcept;
    bool CircleCollidesWithHazard(const Circle2d& circle,
                                  const Hazard& hazard) const noexcept;
    bool EdgeCollidesWithHazard(const Edge2d& edge,
                                 const Hazard& hazard) const noexcept;
    ThreadPool& GetThreadPool(std::size_t workerCount) const;

    Rect2d m_Bounds;
    CDEConfig m_Config;
    QuadTree m_QuadTree;
    std::vector<Slot> m_Slots;
    mutable std::mutex m_ThreadPoolMutex;
    mutable std::mutex m_ParallelQueryMutex;
    mutable std::unique_ptr<ThreadPool> m_ThreadPool;
};
}
