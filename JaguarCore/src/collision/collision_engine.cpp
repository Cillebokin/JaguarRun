#include "JaguarCore/collision/collision_engine.h"

#include "thread_pool.h"
#include "soa_geometry.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <thread>

namespace jaguar
{
namespace
{
bool isOutside(const Rect2d& bounds, const Rect2d& shapeBounds) noexcept
{
    return bounds.relationTo(shapeBounds) == RectRelation::Disjoint ||
           bounds.relationTo(shapeBounds) == RectRelation::Intersecting ||
           bounds.relationTo(shapeBounds) == RectRelation::Enclosed;
}

bool pointNearSegment(const Point2d& point, const Edge2d& edge, double radius) noexcept
{
    return edge.closestPoint(point).squaredDistanceTo(point) <= radius * radius;
}

bool polygonInsideContainer(const SimplePolygon& polygon,
                            const SimplePolygon& container) noexcept
{
    for (const Point2d& point : polygon.vertices())
    {
        if (!container.contains(point))
            return false;
    }

    for (const Edge2d& polygonEdge : polygon.edges())
    {
        for (const Edge2d& containerEdge : container.edges())
        {
            if (polygonEdge.intersects(containerEdge))
                return false;
        }
    }
    return true;
}

bool circleInsideContainer(const Circle2d& circle,
                           const SimplePolygon& container) noexcept
{
    if (!container.contains(circle.center))
        return false;
    for (const Edge2d& edge : container.edges())
    {
        if (pointNearSegment(circle.center, edge, circle.radius))
            return false;
    }
    return true;
}
}

JAGUARCORE_API CollisionEngine::CollisionEngine(Rect2d bounds, CDEConfig config)
    : m_Bounds(bounds),
      m_Config(config),
      m_QuadTree(bounds, config.quadtreeDepth, config.cdThreshold)
{
}

JAGUARCORE_API CollisionEngine::~CollisionEngine() = default;

JAGUARCORE_API Result<HazardHandle> CollisionEngine::registerHazard(Hazard hazard)
{
    if (!hazard.shape)
    {
        return Result<HazardHandle>::fromError(
             {ErrorCode::InvalidInput, "hazard shape is null"});
    }
    if (hazard.dynamic && hazard.entity.kind != HazardKind::PlacedItem)
    {
        return Result<HazardHandle>::fromError(
            {ErrorCode::InvalidInput,
             "only placed item hazards may be dynamic"});
    }

    const HazardHandle handle = allocateHandle();
    m_Slots[handle.index].hazard = std::move(hazard);
    m_Slots[handle.index].shapeCache =
        std::make_unique<PolygonSoA>(*m_Slots[handle.index].hazard->shape);
    m_QuadTree.registerHazard(handle, *m_Slots[handle.index].hazard);
    return Result<HazardHandle>::fromValue(handle);
}

HazardHandle CollisionEngine::allocateHandle()
{
    for (std::size_t i = 0; i < m_Slots.size(); ++i)
    {
        if (!m_Slots[i].hazard.has_value())
        {
            ++m_Slots[i].generation;
            return HazardHandle{i, m_Slots[i].generation};
        }
    }
    m_Slots.push_back(Slot{});
    return HazardHandle{m_Slots.size() - 1, m_Slots.back().generation};
}

const Hazard* CollisionEngine::get(HazardHandle handle) const noexcept
{
    if (handle.index >= m_Slots.size())
        return nullptr;
    const Slot& slot = m_Slots[handle.index];
    if (slot.generation != handle.generation || !slot.hazard.has_value())
        return nullptr;
    return &slot.hazard.value();
}

Hazard* CollisionEngine::get(HazardHandle handle) noexcept
{
    if (handle.index >= m_Slots.size())
        return nullptr;
    Slot& slot = m_Slots[handle.index];
    if (slot.generation != handle.generation || !slot.hazard.has_value())
        return nullptr;
    return &slot.hazard.value();
}

const PolygonSoA* CollisionEngine::getShapeCache(HazardHandle handle) const noexcept
{
    if (handle.index >= m_Slots.size())
        return nullptr;
    const Slot& slot = m_Slots[handle.index];
    if (slot.generation != handle.generation || !slot.hazard.has_value())
        return nullptr;
    return slot.shapeCache.get();
}

JAGUARCORE_API Result<Hazard> CollisionEngine::deregisterHazard(HazardHandle handle)
{
    Hazard* hazard = get(handle);
    if (!hazard)
    {
        return Result<Hazard>::fromError(
            {ErrorCode::InvalidInput, "invalid hazard handle"});
    }

    Hazard result = std::move(*hazard);
    m_QuadTree.deregisterHazard(handle);
    m_Slots[handle.index].hazard.reset();
    m_Slots[handle.index].shapeCache.reset();
    ++m_Slots[handle.index].generation;
    return Result<Hazard>::fromValue(std::move(result));
}

bool CollisionEngine::shapesCollide(const SimplePolygon& first,
                                    const SimplePolygon& second) const noexcept
{
    if (!first.boundingBox().intersects(second.boundingBox()))
        return false;

    for (const Edge2d& firstEdge : first.edges())
    {
        for (const Edge2d& secondEdge : second.edges())
        {
            if (firstEdge.intersects(secondEdge))
                return true;
        }
    }

    return first.contains(second.vertices().front()) ||
           second.contains(first.vertices().front());
}

bool CollisionEngine::shapesCollide(const PolygonSoA& first,
                                    const PolygonSoA& second) const noexcept
{
    return first.intersects(second);
}

bool CollisionEngine::hazardCollides(const SimplePolygon& shape,
                                     const Hazard& hazard) const noexcept
{
    if (hazard.entity.kind == HazardKind::Exterior)
        return !polygonInsideContainer(shape, *hazard.shape);
    return shapesCollide(shape, *hazard.shape);
}

bool CollisionEngine::hazardCollides(const PolygonSoA& shape,
                                     const Hazard& hazard,
                                     const PolygonSoA& hazardShape) const noexcept
{
    if (hazard.entity.kind == HazardKind::Exterior)
    {
        for (std::size_t i = 0; i < shape.vertexCount(); ++i)
        {
            if (!hazardShape.contains(shape.vertex(i)))
                return true;
        }
        for (std::size_t i = 0; i < shape.edgeCount(); ++i)
        {
            if (hazardShape.intersectsBoundary(shape.edge(i)))
                return true;
        }
        return false;
    }
    return shapesCollide(shape, hazardShape);
}

bool CollisionEngine::edgeCollides(const Edge2d& edge,
                                   const SimplePolygon& shape) const noexcept
{
    return shape.intersects(edge);
}

bool CollisionEngine::edgeCollidesWithHazard(const Edge2d& edge,
                                             const Hazard& hazard) const noexcept
{
    if (hazard.entity.kind != HazardKind::Exterior)
        return hazard.shape->intersects(edge);

    const SimplePolygon& container = *hazard.shape;
    if (!container.contains(edge.start) || !container.contains(edge.end))
        return true;
    for (const Edge2d& containerEdge : container.edges())
    {
        if (edge.intersects(containerEdge))
            return true;
    }
    return false;
}

bool CollisionEngine::circleCollidesWithHazard(const Circle2d& circle,
                                               const Hazard& hazard) const noexcept
{
    if (hazard.entity.kind != HazardKind::Exterior)
        return circleCollides(circle, *hazard.shape);
    return !circleInsideContainer(circle, *hazard.shape);
}

bool CollisionEngine::circleCollides(const Circle2d& circle,
                                     const SimplePolygon& shape) const noexcept
{
    if (shape.contains(circle.center))
        return true;
    for (const Edge2d& edge : shape.edges())
    {
        if (pointNearSegment(circle.center, edge, circle.radius))
            return true;
    }
    return false;
}

JAGUARCORE_API std::vector<HazardEntity> CollisionEngine::collectPolygonCollisions(
    const SimplePolygon& shape) const
{
    return collectPolygonCollisions(shape, NoFilter{});
}

JAGUARCORE_API std::vector<HazardEntity> CollisionEngine::collectPolygonCollisions(
    const SimplePolygon& shape,
    const HazardFilter& filter) const
{
    HazardCollector collector;
    collectPolygonCollisionsUntil(
        shape, collector, filter,
        [](const HazardEntity&) { return false; });

    std::vector<HazardEntity> result;
    result.reserve(collector.entries().size());
    for (const auto& entry : collector.entries())
        result.push_back(entry.second);

    std::sort(result.begin(), result.end(), [](const HazardEntity& lhs, const HazardEntity& rhs) {
        if (lhs.kind != rhs.kind)
            return lhs.kind < rhs.kind;
        if (lhs.itemIndex != rhs.itemIndex)
            return lhs.itemIndex < rhs.itemIndex;
        if (lhs.quality != rhs.quality)
            return lhs.quality < rhs.quality;
        return lhs.zoneIndex < rhs.zoneIndex;
    });
    return result;
}

JAGUARCORE_API bool CollisionEngine::collectPolygonCollisionsUntil(
    const SimplePolygon& shape,
    HazardCollector& collector,
    const HazardFilter& filter,
    const CollisionStopCallback& stopAfterCollision) const
{
    const HazardHandle exteriorHandle{
        std::numeric_limits<std::size_t>::max(),
        std::numeric_limits<std::uint32_t>::max()};
    const HazardEntity exteriorEntity{HazardKind::Exterior, 0, 0, 0};

    const auto report = [&](HazardHandle handle,
                            const HazardEntity& entity) {
        if (filter.isIrrelevant(handle, entity) || collector.contains(handle))
            return false;
        collector.insert(handle, entity);
        return stopAfterCollision && stopAfterCollision(entity);
    };

    std::optional<HazardHandle> registeredExterior;
    for (std::size_t i = 0; i < m_Slots.size(); ++i)
    {
        if (m_Slots[i].hazard.has_value() &&
            m_Slots[i].hazard->entity.kind == HazardKind::Exterior)
        {
            registeredExterior = HazardHandle{i, m_Slots[i].generation};
            break;
        }
    }
    if (isOutside(m_Bounds, shape.boundingBox()))
    {
        if (registeredExterior.has_value())
        {
            const Hazard* hazard = get(*registeredExterior);
            if (hazard && report(*registeredExterior, hazard->entity))
                return true;
        }
        else if (report(exteriorHandle, exteriorEntity))
        {
            return true;
        }
    }

    const PolygonSoA queryShape(shape);
    const auto candidates = m_QuadTree.queryVirtualRoot(shape.boundingBox());
    for (const HazardHandle handle : candidates)
    {
        const Hazard* hazard = get(handle);
        const PolygonSoA* hazardShape = getShapeCache(handle);
        if (!hazard || !hazard->shape ||
            filter.isIrrelevant(handle, hazard->entity) || collector.contains(handle))
            continue;

        const bool collides =
            hazardShape != nullptr ? hazardCollides(queryShape, *hazard, *hazardShape)
                                   : hazardCollides(shape, *hazard);
        if (collides && report(handle, hazard->entity))
            return true;
    }
    return false;
}

JAGUARCORE_API bool CollisionEngine::collectPolygonCollisionsUntil(
    const SimplePolygon& shape,
    HazardCollector& collector,
    const CollisionStopCallback& stopAfterCollision) const
{
    return collectPolygonCollisionsUntil(
        shape, collector, NoFilter{}, stopAfterCollision);
}

JAGUARCORE_API bool CollisionEngine::detectPolygonCollision(const SimplePolygon& shape,
                                                           const HazardFilter& filter) const
{
    return !collectPolygonCollisions(shape, filter).empty();
}

JAGUARCORE_API std::vector<std::uint8_t> CollisionEngine::detectPolygonCollisions(
    const std::vector<SimplePolygon>& shapes, const HazardFilter& filter) const
{
    std::vector<std::uint8_t> result;
    result.reserve(shapes.size());
    for (const SimplePolygon& shape : shapes)
    {
        result.push_back(detectPolygonCollision(shape, filter) ? 1U : 0U);
    }
    return result;
}

ThreadPool& CollisionEngine::getThreadPool(std::size_t workerCount) const
{
    const std::size_t normalizedWorkerCount =
        std::max<std::size_t>(1, workerCount == 0 ? std::thread::hardware_concurrency()
                                                  : workerCount);
    std::lock_guard<std::mutex> lock(m_ThreadPoolMutex);
    if (!m_ThreadPool)
        m_ThreadPool = std::make_unique<ThreadPool>(normalizedWorkerCount);
    return *m_ThreadPool;
}

JAGUARCORE_API std::vector<std::uint8_t> CollisionEngine::detectPolygonCollisionsParallel(
    const std::vector<SimplePolygon>& shapes,
    const HazardFilter& filter,
    const BatchCollisionOptions& options) const
{
    std::vector<std::uint8_t> result(shapes.size(), 0U);
    if (shapes.empty())
        return result;

    // ThreadPool keeps one synchronous task set per engine. Serialize batch
    // submissions on the same engine while still parallelizing all items in
    // each batch. Independent CollisionEngine instances remain concurrent.
    std::lock_guard<std::mutex> queryLock(m_ParallelQueryMutex);
    ThreadPool& pool = getThreadPool(options.workerCount);
    pool.parallelFor(shapes.size(), options.grainSize,
                     [this, &shapes, &filter, &result](std::size_t begin,
                                                       std::size_t end) {
                         for (std::size_t i = begin; i < end; ++i)
                             result[i] = detectPolygonCollision(shapes[i], filter) ? 1U : 0U;
                     });
    return result;
}

JAGUARCORE_API bool CollisionEngine::detectSurrogateCollision(const Surrogate& surrogate,
                                                              const Transform2d& transform,
                                                              const HazardFilter& filter) const
{
    const HazardHandle implicitExteriorHandle{
        std::numeric_limits<std::size_t>::max(),
        std::numeric_limits<std::uint32_t>::max()};
    const HazardEntity exteriorEntity{HazardKind::Exterior, 0, 0, 0};
    std::optional<HazardHandle> registeredExterior;
    for (std::size_t slotIndex = 0; slotIndex < m_Slots.size(); ++slotIndex)
    {
        if (m_Slots[slotIndex].hazard.has_value() &&
            m_Slots[slotIndex].hazard->entity.kind == HazardKind::Exterior)
        {
            registeredExterior =
                HazardHandle{slotIndex, m_Slots[slotIndex].generation};
            break;
        }
    }

    const auto exteriorIsRelevant = [&]() {
        const HazardHandle handle = registeredExterior.value_or(implicitExteriorHandle);
        const HazardEntity& entity = registeredExterior.has_value()
                                         ? get(*registeredExterior)->entity
                                         : exteriorEntity;
        return !filter.isIrrelevant(handle, entity);
    };

    for (std::size_t i = 0; i < surrogate.failFastPoleCount(); ++i)
    {
        Circle2d transformedPole = surrogate.poles()[i];
        transformedPole.center = transform.apply(transformedPole.center);
        if (!m_Bounds.contains(transformedPole.center) ||
            transformedPole.center.x - transformedPole.radius < m_Bounds.xMin ||
            transformedPole.center.x + transformedPole.radius > m_Bounds.xMax ||
            transformedPole.center.y - transformedPole.radius < m_Bounds.yMin ||
            transformedPole.center.y + transformedPole.radius > m_Bounds.yMax)
        {
            if (exteriorIsRelevant())
                return true;
            continue;
        }

        for (const HazardHandle handle :
             m_QuadTree.queryVirtualRoot(transformedPole.boundingBox()))
        {
            const Hazard* hazard = get(handle);
            if (hazard && hazard->shape &&
                !filter.isIrrelevant(handle, hazard->entity) &&
                circleCollidesWithHazard(transformedPole, *hazard))
                return true;
        }
    }

    for (const Edge2d& basePier : surrogate.piers())
    {
        const auto transformedStart = transform.apply(basePier.start);
        const auto transformedEnd = transform.apply(basePier.end);
        const auto transformed = Edge2d::create(transformedStart, transformedEnd);
        if (!transformed.hasValue())
            continue;
        if (isOutside(m_Bounds, transformed.value().boundingBox()) &&
            exteriorIsRelevant())
            return true;
        for (const HazardHandle handle :
             m_QuadTree.queryVirtualRoot(transformed.value().boundingBox()))
        {
            const Hazard* hazard = get(handle);
            if (hazard && hazard->shape &&
                !filter.isIrrelevant(handle, hazard->entity) &&
                edgeCollidesWithHazard(transformed.value(), *hazard))
                return true;
        }
    }
    return false;
}

JAGUARCORE_API bool CollisionEngine::collectSurrogateCollisionsUntil(
    const Surrogate& surrogate,
    const Transform2d& transform,
    HazardCollector& collector,
    const HazardFilter& filter,
    const CollisionStopCallback& stopAfterCollision) const
{
    const HazardHandle exteriorHandle{
        std::numeric_limits<std::size_t>::max(),
        std::numeric_limits<std::uint32_t>::max()};
    const HazardEntity exteriorEntity{HazardKind::Exterior, 0, 0, 0};
    std::optional<HazardHandle> registeredExterior;
    for (std::size_t slotIndex = 0; slotIndex < m_Slots.size(); ++slotIndex)
    {
        if (m_Slots[slotIndex].hazard.has_value() &&
            m_Slots[slotIndex].hazard->entity.kind == HazardKind::Exterior)
        {
            registeredExterior =
                HazardHandle{slotIndex, m_Slots[slotIndex].generation};
            break;
        }
    }

    const auto report = [&](HazardHandle handle,
                            const HazardEntity& entity) {
        if (filter.isIrrelevant(handle, entity) || collector.contains(handle))
            return false;
        collector.insert(handle, entity);
        return stopAfterCollision && stopAfterCollision(entity);
    };

    for (std::size_t i = 0; i < surrogate.failFastPoleCount(); ++i)
    {
        Circle2d transformedPole = surrogate.poles()[i];
        transformedPole.center = transform.apply(transformedPole.center);

        if (!m_Bounds.contains(transformedPole.center) ||
            transformedPole.center.x - transformedPole.radius < m_Bounds.xMin ||
            transformedPole.center.x + transformedPole.radius > m_Bounds.xMax ||
            transformedPole.center.y - transformedPole.radius < m_Bounds.yMin ||
            transformedPole.center.y + transformedPole.radius > m_Bounds.yMax)
        {
            if (registeredExterior.has_value())
            {
                const Hazard* hazard = get(*registeredExterior);
                if (hazard && report(*registeredExterior, hazard->entity))
                    return true;
            }
            else if (report(exteriorHandle, exteriorEntity))
            {
                return true;
            }
            continue;
        }

        for (const HazardHandle handle :
             m_QuadTree.queryVirtualRoot(transformedPole.boundingBox()))
        {
            const Hazard* hazard = get(handle);
            if (!hazard || !hazard->shape ||
                filter.isIrrelevant(handle, hazard->entity) || collector.contains(handle))
                continue;

            if (circleCollidesWithHazard(transformedPole, *hazard) &&
                report(handle, hazard->entity))
                return true;
        }
    }
    return false;
}

JAGUARCORE_API bool CollisionEngine::collectSurrogateCollisionsUntil(
    const Surrogate& surrogate,
    const Transform2d& transform,
    HazardCollector& collector,
    const CollisionStopCallback& stopAfterCollision) const
{
    return collectSurrogateCollisionsUntil(
        surrogate, transform, collector, NoFilter{}, stopAfterCollision);
}

JAGUARCORE_API CollisionSnapshot CollisionEngine::save() const
{
    CollisionSnapshot snapshot;
    for (const Slot& slot : m_Slots)
    {
        if (slot.hazard.has_value() && slot.hazard->dynamic)
            snapshot.dynamicHazards.push_back(*slot.hazard);
    }
    return snapshot;
}

JAGUARCORE_API Result<void> CollisionEngine::restore(const CollisionSnapshot& snapshot)
{
    for (const Hazard& hazard : snapshot.dynamicHazards)
    {
        if (!hazard.shape)
        {
            return Result<void>::fromError(
                {ErrorCode::InvalidInput,
                 "snapshot contains a dynamic hazard with a null shape"});
        }
        if (!hazard.dynamic)
        {
            return Result<void>::fromError(
                {ErrorCode::SnapshotMismatch,
                 "snapshot contains a non-dynamic hazard"});
        }
        if (hazard.entity.kind != HazardKind::PlacedItem)
        {
            return Result<void>::fromError(
                {ErrorCode::SnapshotMismatch,
                 "snapshot contains a non-placed-item dynamic hazard"});
        }
    }

    std::vector<Slot> rebuiltSlots(m_Slots.size());
    QuadTree rebuiltTree(m_Bounds, m_Config.quadtreeDepth, m_Config.cdThreshold);

    for (std::size_t i = 0; i < m_Slots.size(); ++i)
    {
        const Slot& current = m_Slots[i];
        Slot& rebuilt = rebuiltSlots[i];
        rebuilt.generation = current.generation;
        if (!current.hazard.has_value())
            continue;

        if (current.hazard->dynamic)
        {
            ++rebuilt.generation;
            continue;
        }

        rebuilt.hazard = *current.hazard;
        rebuilt.shapeCache = std::make_unique<PolygonSoA>(*rebuilt.hazard->shape);
        rebuiltTree.registerHazard(
            HazardHandle{i, rebuilt.generation}, *rebuilt.hazard);
    }

    for (const Hazard& hazard : snapshot.dynamicHazards)
    {
        HazardHandle handle{};
        bool foundSlot = false;
        for (std::size_t i = 0; i < rebuiltSlots.size(); ++i)
        {
            if (!rebuiltSlots[i].hazard.has_value())
            {
                ++rebuiltSlots[i].generation;
                handle = HazardHandle{i, rebuiltSlots[i].generation};
                foundSlot = true;
                break;
            }
        }
        if (!foundSlot)
        {
            rebuiltSlots.push_back(Slot{});
            handle = HazardHandle{rebuiltSlots.size() - 1,
                                  rebuiltSlots.back().generation};
        }

        Slot& rebuilt = rebuiltSlots[handle.index];
        rebuilt.hazard = hazard;
        rebuilt.shapeCache = std::make_unique<PolygonSoA>(*hazard.shape);
        rebuiltTree.registerHazard(handle, *rebuilt.hazard);
    }

    m_Slots = std::move(rebuiltSlots);
    m_QuadTree = std::move(rebuiltTree);
    return Result<void>::fromValue();
}

JAGUARCORE_API const Rect2d& CollisionEngine::bounds() const noexcept
{
    return m_Bounds;
}
}
