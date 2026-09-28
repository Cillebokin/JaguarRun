#include "JaguarCore/Collision/CollisionEngine.h"

#include "ThreadPool.h"
#include "SoaGeometry.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <thread>
#include <utility>

namespace jaguar
{
namespace
{
Rect2d normalizeBoundsRustF32(const Rect2d& bounds) noexcept
{
    return Rect2d{static_cast<double>(static_cast<float>(bounds.xMin)),
                  static_cast<double>(static_cast<float>(bounds.yMin)),
                  static_cast<double>(static_cast<float>(bounds.xMax)),
                  static_cast<double>(static_cast<float>(bounds.yMax))};
}

bool isOutside(const Rect2d& bounds, const Rect2d& shapeBounds) noexcept
{
    return bounds.RelationTo(shapeBounds) == RectRelation::Disjoint ||
           bounds.RelationTo(shapeBounds) == RectRelation::Intersecting ||
           bounds.RelationTo(shapeBounds) == RectRelation::Enclosed;
}

bool polygonInsideContainer(const SimplePolygon& polygon,
                            const SimplePolygon& container) noexcept
{
    for (const Point2d& point : polygon.Vertices())
    {
        if (!container.ContainsRustF32(point))
            return false;
    }

    for (const Edge2d& polygonEdge : polygon.Edges())
    {
        for (const Edge2d& containerEdge : container.Edges())
        {
            if (polygonEdge.IntersectsRustF32(containerEdge))
                return false;
        }
    }
    return true;
}

bool pointNearSegment(const Point2d& point, const Edge2d& edge, double radius) noexcept
{
    const float squaredDistance = static_cast<float>(
        edge.SquaredDistanceToRustF32(point));
    const float radiusF32 = static_cast<float>(radius);
    return squaredDistance <= radiusF32 * radiusF32;
}

}

JAGUARCORE_API CollisionEngine::CollisionEngine(Rect2d bounds, CDEConfig config)
    : m_Bounds(normalizeBoundsRustF32(bounds)),
      m_Config(config),
      m_QuadTree(m_Bounds, config.quadtreeDepth, config.cdThreshold),
      m_Slots(1)
{
    m_Slots.front().generation = 0;
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
    const auto duplicateEntity = std::find_if(
        m_Slots.begin(), m_Slots.end(), [&hazard](const Slot& slot) {
            return slot.hazard.has_value() &&
                   slot.hazard->entity == hazard.entity;
        });
    if (duplicateEntity != m_Slots.end())
    {
        return Result<HazardHandle>::FromError(
            {ErrorCode::InvalidInput,
             "hazard entity is already registered"});
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
    if (!m_FreeSlotIndices.empty())
    {
        const std::size_t slotIndex = m_FreeSlotIndices.back();
        m_FreeSlotIndices.pop_back();
        Slot& slot = m_Slots[slotIndex];
        slot.generation |= 1U;
        return HazardHandle{slotIndex, slot.generation};
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

std::optional<HazardHandle> CollisionEngine::FindHazardHandle(
    const HazardEntity& entity) const noexcept
{
    for (std::size_t slotIndex = 0; slotIndex < m_Slots.size(); ++slotIndex)
    {
        const Slot& slot = m_Slots[slotIndex];
        if (slot.hazard.has_value() && slot.hazard->entity == entity)
            return HazardHandle{slotIndex, slot.generation};
    }
    return std::nullopt;
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
    m_FreeSlotIndices.push_back(handle.index);
    return Result<Hazard>::FromValue(std::move(result));
}

bool CollisionEngine::ShapesCollide(const SimplePolygon& first,
                                    const SimplePolygon& second) const noexcept
{
    if (first.BoundingBox().RelationToRustF32(second.BoundingBox())
        == RectRelation::Disjoint)
        return false;

    for (const Edge2d& firstEdge : first.Edges())
    {
        for (const Edge2d& secondEdge : second.Edges())
        {
            if (firstEdge.IntersectsRustF32(secondEdge))
                return true;
        }
    }

    const RectRelation relation = first.BoundingBox().AlmostRelationToRustF32(
        second.BoundingBox());
    if (relation == RectRelation::Surrounding)
        return first.ContainsRustF32(
            second.PointOfInaccessibility().center);
    if (relation == RectRelation::Enclosed)
        return second.ContainsRustF32(
            first.PointOfInaccessibility().center);
    return false;
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
    {
        if (m_Config.quadtreeDepth > 0U)
            return !polygonInsideContainer(shape, *hazard.shape);

        const SimplePolygon& exteriorShape = *hazard.shape;
        const RectRelation relation =
            exteriorShape.BoundingBox().AlmostRelationToRustF32(
                shape.BoundingBox());
        bool contained = false;
        if (relation == RectRelation::Surrounding)
        {
            contained = exteriorShape.ContainsRustF32(
                shape.PointOfInaccessibility().center);
        }
        else if (relation == RectRelation::Enclosed)
        {
            contained = shape.ContainsRustF32(
                exteriorShape.PointOfInaccessibility().center);
        }
        return !contained;
    }
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
    return shape.IntersectsRustF32(edge);
}

bool CollisionEngine::EdgeCollidesWithHazard(const Edge2d& edge,
                                             const Hazard& hazard) const noexcept
{
    if (hazard.entity.kind != HazardKind::Exterior)
        return hazard.shape->IntersectsRustF32(edge);

    for (const Edge2d& boundaryEdge : hazard.shape->Edges())
    {
        if (edge.IntersectsRustF32(boundaryEdge))
            return true;
    }
    return false;
}

bool CollisionEngine::CircleCollidesWithHazard(const Circle2d& circle,
                                               const Hazard& hazard) const noexcept
{
    if (hazard.entity.kind != HazardKind::Exterior)
        return CircleCollides(circle, *hazard.shape);

    for (const Edge2d& boundaryEdge : hazard.shape->Edges())
    {
        if (pointNearSegment(circle.center, boundaryEdge, circle.radius))
            return true;
    }
    return false;
}

bool CollisionEngine::CircleCollides(const Circle2d& circle,
                                     const SimplePolygon& shape) const noexcept
{
    if (shape.ContainsRustF32(circle.center))
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

    const auto candidates = m_QuadTree.QueryVirtualRoot(shape.BoundingBox());
    for (const HazardHandle handle : candidates)
    {
        const Hazard* hazard = Get(handle);
        if (!hazard || !hazard->shape ||
            filter.IsIrrelevant(handle, hazard->entity) || collector.Contains(handle))
            continue;

        // The AVX2 cache uses a separate double-precision representation. The
        // Rust-compatible path must use POI containment and f32 predicates
        // consistently, so exact collection goes through the canonical shape
        // implementation.
        const bool collides = HazardCollides(shape, *hazard);
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
    if (isOutside(m_Bounds, shape.BoundingBox()))
        return true;
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

    for (std::size_t i = 0; i < surrogate.FailFastPoleCount(); ++i)
    {
        Circle2d transformedPole = surrogate.Poles()[i];
        transformedPole.center = transform.ApplyRustF32(transformedPole.center);

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
        const auto transformedStart = transform.ApplyRustF32(basePier.start);
        const auto transformedEnd = transform.ApplyRustF32(basePier.end);
        const auto transformed = Edge2d::Create(transformedStart, transformedEnd);
        if (!transformed.HasValue())
            continue;
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
        transformedPole.center = transform.ApplyRustF32(transformedPole.center);

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
    std::vector<HazardEntity> snapshotEntities;
    snapshotEntities.reserve(snapshot.dynamicHazards.size());
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

        const bool duplicatesStaticEntity = std::any_of(
            m_Slots.begin(), m_Slots.end(), [&hazard](const Slot& slot) {
                return slot.hazard.has_value() && !slot.hazard->dynamic &&
                       slot.hazard->entity == hazard.entity;
            });
        if (duplicatesStaticEntity ||
            std::find(snapshotEntities.begin(), snapshotEntities.end(),
                      hazard.entity) != snapshotEntities.end())
        {
            return Result<void>::FromError(
                {ErrorCode::SnapshotMismatch,
                 "snapshot contains duplicate hazard entities"});
        }
        snapshotEntities.push_back(hazard.entity);
    }

    std::vector<std::size_t> unmatchedCurrentDynamic;
    for (std::size_t currentIndex = 0; currentIndex < m_Slots.size(); ++currentIndex)
    {
        const Slot& current = m_Slots[currentIndex];
        if (current.hazard.has_value() && current.hazard->dynamic)
            unmatchedCurrentDynamic.push_back(currentIndex);
    }

    std::vector<std::optional<std::size_t>> matchedCurrentSlots(
        snapshot.dynamicHazards.size());
    std::vector<std::size_t> hazardsToAdd;
    for (std::size_t snapshotIndex = 0;
         snapshotIndex < snapshot.dynamicHazards.size(); ++snapshotIndex)
    {
        const Hazard& target = snapshot.dynamicHazards[snapshotIndex];
        const auto match = std::find_if(
            unmatchedCurrentDynamic.begin(), unmatchedCurrentDynamic.end(),
            [this, &target](std::size_t currentIndex) {
                const Slot& current = m_Slots[currentIndex];
                return current.hazard.has_value() &&
                       current.hazard->entity == target.entity;
            });
        if (match == unmatchedCurrentDynamic.end())
        {
            hazardsToAdd.push_back(snapshotIndex);
            continue;
        }

        const std::size_t currentIndex = *match;
        matchedCurrentSlots[snapshotIndex] = currentIndex;
        *match = unmatchedCurrentDynamic.back();
        unmatchedCurrentDynamic.pop_back();
    }

    std::vector<Slot> rebuiltSlots(m_Slots.size());
    std::vector<std::size_t> rebuiltFreeSlotIndices = m_FreeSlotIndices;
    QuadTree rebuiltTree(m_Bounds, m_Config.quadtreeDepth, m_Config.cdThreshold);

    for (std::size_t i = 0; i < m_Slots.size(); ++i)
    {
        const Slot& current = m_Slots[i];
        Slot& rebuilt = rebuiltSlots[i];
        rebuilt.generation = current.generation;
        if (!current.hazard.has_value() || current.hazard->dynamic)
            continue;

        rebuilt.hazard = *current.hazard;
        rebuilt.shapeCache = std::make_unique<PolygonSoA>(*rebuilt.hazard->shape);
        rebuiltTree.RegisterHazard(
            HazardHandle{i, rebuilt.generation}, *rebuilt.hazard);
    }

    for (std::size_t snapshotIndex = 0;
         snapshotIndex < matchedCurrentSlots.size(); ++snapshotIndex)
    {
        if (!matchedCurrentSlots[snapshotIndex].has_value())
            continue;
        const std::size_t currentIndex = matchedCurrentSlots[snapshotIndex].value();
        const Slot& current = m_Slots[currentIndex];
        const HazardHandle handle{currentIndex, current.generation};
        Slot& rebuilt = rebuiltSlots[handle.index];
        rebuilt.hazard = current.hazard.value();
        rebuilt.shapeCache = std::make_unique<PolygonSoA>(*rebuilt.hazard->shape);
        rebuiltTree.RegisterHazard(handle, *rebuilt.hazard);
    }

    for (const std::size_t currentIndex : unmatchedCurrentDynamic)
    {
        rebuiltSlots[currentIndex].generation =
            m_Slots[currentIndex].generation + 1U;
        rebuiltFreeSlotIndices.push_back(currentIndex);
    }

    for (const std::size_t snapshotIndex : hazardsToAdd)
    {
        std::size_t slotIndex = 0;
        if (!rebuiltFreeSlotIndices.empty())
        {
            slotIndex = rebuiltFreeSlotIndices.back();
            rebuiltFreeSlotIndices.pop_back();
            rebuiltSlots[slotIndex].generation |= 1U;
        }
        else
        {
            slotIndex = rebuiltSlots.size();
            rebuiltSlots.push_back(Slot{});
        }

        Slot& rebuilt = rebuiltSlots[slotIndex];
        const Hazard& hazard = snapshot.dynamicHazards[snapshotIndex];
        rebuilt.hazard = hazard;
        rebuilt.shapeCache = std::make_unique<PolygonSoA>(*hazard.shape);
        rebuiltTree.RegisterHazard(
            HazardHandle{slotIndex, rebuilt.generation}, *rebuilt.hazard);
    }

    m_Slots = std::move(rebuiltSlots);
    m_FreeSlotIndices = std::move(rebuiltFreeSlotIndices);
    m_QuadTree = std::move(rebuiltTree);
    return Result<void>::FromValue();
}

JAGUARCORE_API const Rect2d& CollisionEngine::Bounds() const noexcept
{
    return m_Bounds;
}
}
