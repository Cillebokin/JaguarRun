#include "JaguarCore/Collision/CollisionEngine.h"

#include "ThreadPool.h"
#include "SoaGeometry.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <thread>
#include <utility>

namespace jaguar
{
namespace
{
bool isOutside(const Rect2d& bounds, const Rect2d& shapeBounds) noexcept
{
    return bounds.RelationTo(shapeBounds) == RectRelation::Disjoint ||
           bounds.RelationTo(shapeBounds) == RectRelation::Intersecting ||
           bounds.RelationTo(shapeBounds) == RectRelation::Enclosed;
}

bool pointNearSegment(const Point2d& point, const Edge2d& edge, double radius) noexcept
{
    return edge.ClosestPoint(point).SquaredDistanceTo(point) <= radius * radius;
}

bool polygonInsideContainer(const SimplePolygon& polygon,
                            const SimplePolygon& container) noexcept
{
    for (const Point2d& point : polygon.Vertices())
    {
        if (!container.Contains(point))
            return false;
    }

    for (const Edge2d& polygonEdge : polygon.Edges())
    {
        for (const Edge2d& containerEdge : container.Edges())
        {
            if (polygonEdge.Intersects(containerEdge))
                return false;
        }
    }
    return true;
}

bool circleInsideContainer(const Circle2d& circle,
                           const SimplePolygon& container) noexcept
{
    if (!container.Contains(circle.center))
        return false;
    for (const Edge2d& edge : container.Edges())
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

JAGUARCORE_API void CollisionEngineDeleter::operator()(CollisionEngine* engine) const noexcept
{
    delete engine;
}

JAGUARCORE_API Result<HazardHandle> CollisionEngine::RegisterHazard(Hazard hazard)
{
    if (!hazard.shape)
    {
        return Result<HazardHandle>::FromError(
             {ErrorCode::InvalidInput, "hazard shape is null"});
    }
    if (hazard.dynamic && hazard.entity.kind != HazardKind::PlacedItem)
    {
        return Result<HazardHandle>::FromError(
            {ErrorCode::InvalidInput,
             "only placed item hazards may be dynamic"});
    }

    const HazardHandle handle = AllocateHandle();
    m_Slots[handle.index].hazard = std::move(hazard);
    m_Slots[handle.index].shapeCache =
        std::make_unique<PolygonSoA>(*m_Slots[handle.index].hazard->shape);
    m_QuadTree.RegisterHazard(handle, *m_Slots[handle.index].hazard);
    return Result<HazardHandle>::FromValue(handle);
}

HazardHandle CollisionEngine::AllocateHandle()
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

const Hazard* CollisionEngine::Get(HazardHandle handle) const noexcept
{
    if (handle.index >= m_Slots.size())
        return nullptr;
    const Slot& slot = m_Slots[handle.index];
    if (slot.generation != handle.generation || !slot.hazard.has_value())
        return nullptr;
    return &slot.hazard.value();
}

Hazard* CollisionEngine::Get(HazardHandle handle) noexcept
{
    if (handle.index >= m_Slots.size())
        return nullptr;
    Slot& slot = m_Slots[handle.index];
    if (slot.generation != handle.generation || !slot.hazard.has_value())
        return nullptr;
    return &slot.hazard.value();
}

const PolygonSoA* CollisionEngine::GetShapeCache(HazardHandle handle) const noexcept
{
    if (handle.index >= m_Slots.size())
        return nullptr;
    const Slot& slot = m_Slots[handle.index];
    if (slot.generation != handle.generation || !slot.hazard.has_value())
        return nullptr;
    return slot.shapeCache.get();
}

JAGUARCORE_API Result<Hazard> CollisionEngine::DeregisterHazard(HazardHandle handle)
{
    Hazard* hazard = Get(handle);
    if (!hazard)
    {
        return Result<Hazard>::FromError(
            {ErrorCode::InvalidInput, "invalid hazard handle"});
    }

    Hazard result = std::move(*hazard);
    m_QuadTree.DeregisterHazard(handle);
    m_Slots[handle.index].hazard.reset();
    m_Slots[handle.index].shapeCache.reset();
    ++m_Slots[handle.index].generation;
    return Result<Hazard>::FromValue(std::move(result));
}

bool CollisionEngine::ShapesCollide(const SimplePolygon& first,
                                    const SimplePolygon& second) const noexcept
{
    if (!first.BoundingBox().Intersects(second.BoundingBox()))
        return false;

    for (const Edge2d& firstEdge : first.Edges())
    {
        for (const Edge2d& secondEdge : second.Edges())
        {
            if (firstEdge.Intersects(secondEdge))
                return true;
        }
    }

    return first.Contains(second.Vertices().front()) ||
           second.Contains(first.Vertices().front());
}

bool CollisionEngine::ShapesCollide(const PolygonSoA& first,
                                    const PolygonSoA& second) const noexcept
{
    return first.Intersects(second);
}

bool CollisionEngine::HazardCollides(const SimplePolygon& shape,
                                     const Hazard& hazard) const noexcept
{
    if (hazard.entity.kind == HazardKind::Exterior)
        return !polygonInsideContainer(shape, *hazard.shape);
    return ShapesCollide(shape, *hazard.shape);
}

bool CollisionEngine::HazardCollides(const PolygonSoA& shape,
                                     const Hazard& hazard,
                                     const PolygonSoA& hazardShape) const noexcept
{
    if (hazard.entity.kind == HazardKind::Exterior)
    {
        for (std::size_t i = 0; i < shape.VertexCount(); ++i)
        {
            if (!hazardShape.Contains(shape.vertex(i)))
                return true;
        }
        for (std::size_t i = 0; i < shape.edgeCount(); ++i)
        {
            if (hazardShape.intersectsBoundary(shape.edge(i)))
                return true;
        }
        return false;
    }
    return ShapesCollide(shape, hazardShape);
}

bool CollisionEngine::EdgeCollides(const Edge2d& edge,
                                   const SimplePolygon& shape) const noexcept
{
    return shape.Intersects(edge);
}

bool CollisionEngine::EdgeCollidesWithHazard(const Edge2d& edge,
                                             const Hazard& hazard) const noexcept
{
    if (hazard.entity.kind != HazardKind::Exterior)
        return hazard.shape->Intersects(edge);

    const SimplePolygon& container = *hazard.shape;
    if (!container.Contains(edge.start) || !container.Contains(edge.end))
        return true;
    for (const Edge2d& containerEdge : container.Edges())
    {
        if (edge.Intersects(containerEdge))
            return true;
    }
    return false;
}

bool CollisionEngine::CircleCollidesWithHazard(const Circle2d& circle,
                                               const Hazard& hazard) const noexcept
{
    if (hazard.entity.kind != HazardKind::Exterior)
        return CircleCollides(circle, *hazard.shape);
    return !circleInsideContainer(circle, *hazard.shape);
}

bool CollisionEngine::CircleCollides(const Circle2d& circle,
                                     const SimplePolygon& shape) const noexcept
{
    if (shape.Contains(circle.center))
        return true;
    for (const Edge2d& edge : shape.Edges())
    {
        if (pointNearSegment(circle.center, edge, circle.radius))
            return true;
    }
    return false;
}

JAGUARCORE_API std::vector<HazardEntity> CollisionEngine::CollectPolygonCollisions(
    const SimplePolygon& shape) const
{
    return CollectPolygonCollisions(shape, NoFilter{});
}

JAGUARCORE_API std::vector<HazardEntity> CollisionEngine::CollectPolygonCollisions(
    const SimplePolygon& shape,
    const HazardFilter& filter) const
{
    HazardCollector collector;
    CollectPolygonCollisionsUntil(
        shape, collector, filter,
        [](const HazardEntity&) { return false; });

    std::vector<HazardEntity> result;
    result.reserve(collector.Entries().size());
    for (const auto& entry : collector.Entries())
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

JAGUARCORE_API bool CollisionEngine::CollectPolygonCollisionsUntil(
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
        if (filter.IsIrrelevant(handle, entity) || collector.Contains(handle))
            return false;
        collector.Insert(handle, entity);
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
    if (isOutside(m_Bounds, shape.BoundingBox()))
    {
        if (registeredExterior.has_value())
        {
            const Hazard* hazard = Get(*registeredExterior);
            if (hazard && report(*registeredExterior, hazard->entity))
                return true;
        }
        else if (report(exteriorHandle, exteriorEntity))
        {
            return true;
        }
    }

    const PolygonSoA queryShape(shape);
    const auto candidates = m_QuadTree.QueryVirtualRoot(shape.BoundingBox());
    for (const HazardHandle handle : candidates)
    {
        const Hazard* hazard = Get(handle);
        const PolygonSoA* hazardShape = GetShapeCache(handle);
        if (!hazard || !hazard->shape ||
            filter.IsIrrelevant(handle, hazard->entity) || collector.Contains(handle))
            continue;

        const bool collides =
            hazardShape != nullptr ? HazardCollides(queryShape, *hazard, *hazardShape)
                                   : HazardCollides(shape, *hazard);
        if (collides && report(handle, hazard->entity))
            return true;
    }
    return false;
}

JAGUARCORE_API bool CollisionEngine::CollectPolygonCollisionsUntil(
    const SimplePolygon& shape,
    HazardCollector& collector,
    const CollisionStopCallback& stopAfterCollision) const
{
    return CollectPolygonCollisionsUntil(
        shape, collector, NoFilter{}, stopAfterCollision);
}

JAGUARCORE_API bool CollisionEngine::DetectPolygonCollision(const SimplePolygon& shape,
                                                           const HazardFilter& filter) const
{
    return !CollectPolygonCollisions(shape, filter).empty();
}

JAGUARCORE_API std::vector<std::uint8_t> CollisionEngine::DetectPolygonCollisions(
    const std::vector<SimplePolygon>& shapes, const HazardFilter& filter) const
{
    std::vector<std::uint8_t> result;
    result.reserve(shapes.size());
    for (const SimplePolygon& shape : shapes)
    {
        result.push_back(DetectPolygonCollision(shape, filter) ? 1U : 0U);
    }
    return result;
}

ThreadPool& CollisionEngine::GetThreadPool(std::size_t workerCount) const
{
    const std::size_t normalizedWorkerCount =
        std::max<std::size_t>(1, workerCount == 0 ? std::thread::hardware_concurrency()
                                                  : workerCount);
    std::lock_guard<std::mutex> lock(m_ThreadPoolMutex);
    if (!m_ThreadPool)
        m_ThreadPool = std::make_unique<ThreadPool>(normalizedWorkerCount);
    return *m_ThreadPool;
}

JAGUARCORE_API Result<std::vector<std::uint8_t>> CollisionEngine::DetectPolygonCollisionsParallel(
    const std::vector<SimplePolygon>& shapes,
    const HazardFilter& filter,
    const BatchCollisionOptions& options) const
{
    if (options.grainSize == 0)
    {
        return Result<std::vector<std::uint8_t>>::FromError(
            {ErrorCode::InvalidInput, "parallel collision grain size must be greater than zero"});
    }

    std::vector<std::uint8_t> result(shapes.size(), 0U);
    if (shapes.empty())
        return Result<std::vector<std::uint8_t>>::FromValue(std::move(result));

    // ThreadPool keeps one synchronous task set per engine. Serialize batch
    // submissions on the same engine while still parallelizing all items in
    // each batch. Independent CollisionEngine instances remain concurrent.
    std::lock_guard<std::mutex> queryLock(m_ParallelQueryMutex);
    ThreadPool& pool = GetThreadPool(options.workerCount);
    const Result<void> parallelResult = pool.ParallelFor(
        shapes.size(), options.grainSize,
        [this, &shapes, &filter, &result](std::size_t begin,
                                          std::size_t end) -> Result<void> {
            for (std::size_t i = begin; i < end; ++i)
                result[i] = DetectPolygonCollision(shapes[i], filter) ? 1U : 0U;
            return Result<void>::FromValue();
        });
    if (!parallelResult.HasValue())
        return Result<std::vector<std::uint8_t>>::FromError(parallelResult.GetError());
    return Result<std::vector<std::uint8_t>>::FromValue(std::move(result));
}

JAGUARCORE_API Result<bool> CollisionEngine::DetectSurrogateCollision(
    const Surrogate& surrogate,
    const Transform2d& transform,
    const HazardFilter& filter) const
{
    if (!transform.IsFinite())
    {
        return Result<bool>::FromError(
            {ErrorCode::InvalidInput, "transform contains a non-finite value"});
    }

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
                                         ? Get(*registeredExterior)->entity
                                         : exteriorEntity;
        return !filter.IsIrrelevant(handle, entity);
    };

    for (std::size_t i = 0; i < surrogate.FailFastPoleCount(); ++i)
    {
        Circle2d transformedPole = surrogate.Poles()[i];
        transformedPole.center = transform.Apply(transformedPole.center);
        if (!m_Bounds.Contains(transformedPole.center) ||
            transformedPole.center.x - transformedPole.radius < m_Bounds.xMin ||
            transformedPole.center.x + transformedPole.radius > m_Bounds.xMax ||
            transformedPole.center.y - transformedPole.radius < m_Bounds.yMin ||
            transformedPole.center.y + transformedPole.radius > m_Bounds.yMax)
        {
            if (exteriorIsRelevant())
                return Result<bool>::FromValue(true);
            continue;
        }

        for (const HazardHandle handle :
             m_QuadTree.QueryVirtualRoot(transformedPole.BoundingBox()))
        {
            const Hazard* hazard = Get(handle);
            if (hazard && hazard->shape &&
                !filter.IsIrrelevant(handle, hazard->entity) &&
                CircleCollidesWithHazard(transformedPole, *hazard))
                 return Result<bool>::FromValue(true);
        }
    }

    for (const Edge2d& basePier : surrogate.Piers())
    {
        const auto transformedStart = transform.Apply(basePier.start);
        const auto transformedEnd = transform.Apply(basePier.end);
        const auto transformed = Edge2d::Create(transformedStart, transformedEnd);
        if (!transformed.HasValue())
            continue;
        if (isOutside(m_Bounds, transformed.Value().BoundingBox()) &&
            exteriorIsRelevant())
            return Result<bool>::FromValue(true);
        for (const HazardHandle handle :
             m_QuadTree.QueryVirtualRoot(transformed.Value().BoundingBox()))
        {
            const Hazard* hazard = Get(handle);
            if (hazard && hazard->shape &&
                !filter.IsIrrelevant(handle, hazard->entity) &&
                EdgeCollidesWithHazard(transformed.Value(), *hazard))
                return Result<bool>::FromValue(true);
        }
    }
    return Result<bool>::FromValue(false);
}

JAGUARCORE_API Result<bool> CollisionEngine::CollectSurrogateCollisionsUntil(
    const Surrogate& surrogate,
    const Transform2d& transform,
    HazardCollector& collector,
    const HazardFilter& filter,
    const CollisionStopCallback& stopAfterCollision) const
{
    if (!transform.IsFinite())
    {
        return Result<bool>::FromError(
            {ErrorCode::InvalidInput, "transform contains a non-finite value"});
    }

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
        if (filter.IsIrrelevant(handle, entity) || collector.Contains(handle))
            return false;
        collector.Insert(handle, entity);
        return stopAfterCollision && stopAfterCollision(entity);
    };

    for (std::size_t i = 0; i < surrogate.FailFastPoleCount(); ++i)
    {
        Circle2d transformedPole = surrogate.Poles()[i];
        transformedPole.center = transform.Apply(transformedPole.center);

        if (!m_Bounds.Contains(transformedPole.center) ||
            transformedPole.center.x - transformedPole.radius < m_Bounds.xMin ||
            transformedPole.center.x + transformedPole.radius > m_Bounds.xMax ||
            transformedPole.center.y - transformedPole.radius < m_Bounds.yMin ||
            transformedPole.center.y + transformedPole.radius > m_Bounds.yMax)
        {
            if (registeredExterior.has_value())
            {
                const Hazard* hazard = Get(*registeredExterior);
                if (hazard && report(*registeredExterior, hazard->entity))
                    return Result<bool>::FromValue(true);
            }
            else if (report(exteriorHandle, exteriorEntity))
            {
                return Result<bool>::FromValue(true);
            }
            continue;
        }

        for (const HazardHandle handle :
             m_QuadTree.QueryVirtualRoot(transformedPole.BoundingBox()))
        {
            const Hazard* hazard = Get(handle);
            if (!hazard || !hazard->shape ||
                filter.IsIrrelevant(handle, hazard->entity) || collector.Contains(handle))
                continue;

            if (CircleCollidesWithHazard(transformedPole, *hazard) &&
                report(handle, hazard->entity))
                return Result<bool>::FromValue(true);
        }
    }
    return Result<bool>::FromValue(false);
}

JAGUARCORE_API Result<bool> CollisionEngine::CollectSurrogateCollisionsUntil(
    const Surrogate& surrogate,
    const Transform2d& transform,
    HazardCollector& collector,
    const CollisionStopCallback& stopAfterCollision) const
{
    return CollectSurrogateCollisionsUntil(
        surrogate, transform, collector, NoFilter{}, stopAfterCollision);
}

JAGUARCORE_API CollisionSnapshot CollisionEngine::Save() const
{
    CollisionSnapshot snapshot;
    for (const Slot& slot : m_Slots)
    {
        if (slot.hazard.has_value() && slot.hazard->dynamic)
            snapshot.dynamicHazards.push_back(*slot.hazard);
    }
    return snapshot;
}

JAGUARCORE_API Result<void> CollisionEngine::Restore(const CollisionSnapshot& snapshot)
{
    for (const Hazard& hazard : snapshot.dynamicHazards)
    {
        if (!hazard.shape)
        {
            return Result<void>::FromError(
                {ErrorCode::InvalidInput,
                 "snapshot contains a dynamic hazard with a null shape"});
        }
        if (!hazard.dynamic)
        {
            return Result<void>::FromError(
                {ErrorCode::SnapshotMismatch,
                 "snapshot contains a non-dynamic hazard"});
        }
        if (hazard.entity.kind != HazardKind::PlacedItem)
        {
            return Result<void>::FromError(
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
        rebuiltTree.RegisterHazard(
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
        rebuiltTree.RegisterHazard(handle, *rebuilt.hazard);
    }

    m_Slots = std::move(rebuiltSlots);
    m_QuadTree = std::move(rebuiltTree);
    return Result<void>::FromValue();
}

JAGUARCORE_API const Rect2d& CollisionEngine::Bounds() const noexcept
{
    return m_Bounds;
}
}
