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
// jagua-rs qt_traits 的象限分类辅助（供 QTNode 查询移植使用）。
// 定义在 quadtree.cpp 的匿名命名空间中，这里通过引擎的 friend 访问节点。
namespace quadtree_query
{
std::array<bool, 4> edgeCollidesWithQuadrants(
    const Edge2d& edge, const Rect2d& nodeBounds,
    const std::array<Rect2d, 4>& quadrants) noexcept;
std::array<bool, 4> circleCollidesWithQuadrants(
    const Circle2d& circle, const std::array<Rect2d, 4>& quadrants) noexcept;
std::array<std::size_t, 4> circleQuadrantOrder(
    const Circle2d& circle, const std::array<Rect2d, 4>& quadrants) noexcept;
}

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


bool pointNearSegment(const Point2d& point, const Edge2d& edge, double radius) noexcept
{
    const float squaredDistance = static_cast<float>(
        edge.SquaredDistanceToRustF32(point));
    const float radiusF32 = static_cast<float>(radius);
    return squaredDistance <= radiusF32 * radiusF32;
}

// jagua-rs detect_containment_collision 的移植：
// bbox 近似关系判定 + POI 单点包含 + scope 解释
bool DetectContainmentCollision(const SimplePolygon& shape,
                                const SimplePolygon& hazardShape,
                                const HazardEntity& entity) noexcept
{
    const RectRelation relation =
        hazardShape.BoundingBox().AlmostRelationToRustF32(shape.BoundingBox());
    bool contained = false;
    if (relation == RectRelation::Surrounding)
        contained = hazardShape.ContainsRustF32(shape.PointOfInaccessibility().center);
    else if (relation == RectRelation::Enclosed)
        contained = shape.ContainsRustF32(hazardShape.PointOfInaccessibility().center);

    const bool interiorScope = entity.kind != HazardKind::Exterior;
    return interiorScope == contained;
}

// ---- jagua-rs QTNode 查询的移植（binary detect 与 collect）----
// 这些谓词在 CollisionEngine 的成员函数中使用（它是 QuadTree 的 friend）。

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

bool CollisionEngine::EdgeCollides(const Edge2d& edge,
                                   const SimplePolygon& shape) const noexcept
{
    return shape.IntersectsRustF32(edge);
}

// jagua-rs QTNode::collides<T> for Edge 的移植（短路 detect）
bool CollisionEngine::NodeCollidesWithEdge(
    const QuadTree::Node& node,
    const Edge2d& edge,
    const HazardFilter& filter) const noexcept
{
    // 节点内最强 relevant hazard（Entire > Partial > None；列表已按降序排列）
    const QuadTree::NodeHazard* strongest = nullptr;
    for (const QuadTree::NodeHazard& candidate : node.hazards)
    {
        if (!filter.IsIrrelevant(candidate.handle, candidate.entity))
        {
            strongest = &candidate;
            break;
        }
    }
    if (!strongest)
        return false;
    if (strongest->presence == HazardPresence::None)
        return false;
    if (strongest->presence == HazardPresence::Entire)
        return true;

    if (node.children.front())
    {
        const auto quadrants = node.bounds.QuadrantsRustF32();
        const auto colliding = quadtree_query::edgeCollidesWithQuadrants(
            edge, node.bounds, quadrants);
        for (std::size_t idx = 0; idx < 4; ++idx)
        {
            if (colliding[idx] &&
                NodeCollidesWithEdge(*node.children[idx], edge, filter))
                return true;
        }
        return false;
    }

    // jagua-rs QTHazPartial::collides_with：ff_bbox 预筛 + 边子集判交
    for (const QuadTree::NodeHazard& hazard : node.hazards)
    {
        if (filter.IsIrrelevant(hazard.handle, hazard.entity))
            continue;
        if (hazard.presence == HazardPresence::Partial &&
            edge.IntersectsRustF32(hazard.partial->ffBbox) &&
            std::any_of(hazard.partial->edges.begin(), hazard.partial->edges.end(),
                       [&edge](const Edge2d& candidate) {
                           return candidate.IntersectsRustF32(edge);
                       }))
            return true;
    }
    return false;
}

// jagua-rs QTNode::collides<T> for Circle 的移植
bool CollisionEngine::NodeCollidesWithCircle(
    const QuadTree::Node& node,
    const Circle2d& circle,
    const HazardFilter& filter) const noexcept
{
    const QuadTree::NodeHazard* strongest = nullptr;
    for (const QuadTree::NodeHazard& candidate : node.hazards)
    {
        if (!filter.IsIrrelevant(candidate.handle, candidate.entity))
        {
            strongest = &candidate;
            break;
        }
    }
    if (!strongest)
        return false;
    if (strongest->presence == HazardPresence::None)
        return false;
    if (strongest->presence == HazardPresence::Entire)
        return true;

    if (node.children.front())
    {
        const auto quadrants = node.bounds.QuadrantsRustF32();
        const auto colliding =
            quadtree_query::circleCollidesWithQuadrants(circle, quadrants);
        const auto order =
            quadtree_query::circleQuadrantOrder(circle, quadrants);
        for (const std::size_t idx : order)
        {
            if (colliding[idx] &&
                NodeCollidesWithCircle(*node.children[idx], circle, filter))
                return true;
        }
        return false;
    }

    for (const QuadTree::NodeHazard& hazard : node.hazards)
    {
        if (filter.IsIrrelevant(hazard.handle, hazard.entity))
            continue;
        if (hazard.presence == HazardPresence::Partial &&
            circle.IntersectsRustF32(hazard.partial->ffBbox) &&
            std::any_of(hazard.partial->edges.begin(), hazard.partial->edges.end(),
                       [&circle](const Edge2d& candidate) {
                           return circle.IntersectsRustF32(candidate);
                       }))
            return true;
    }
    return false;
}

// jagua-rs QTNode::collect_collisions_until for Edge 的移植
bool CollisionEngine::CollectEdgeCollisionsUntil(
    const QuadTree::Node& node,
    const Edge2d& edge,
    HazardCollector& collector,
    const HazardFilter& filter,
    const CollisionStopCallback& stopAfterCollision,
    const std::function<bool(HazardHandle, const HazardEntity&)>& report) const
{
    const bool performCdNow = node.activeEdges <= m_Config.cdThreshold;
    if (node.children.front() && !performCdNow)
    {
        const auto quadrants = node.bounds.QuadrantsRustF32();
        const auto colliding = quadtree_query::edgeCollidesWithQuadrants(
            edge, node.bounds, quadrants);
        for (std::size_t idx = 0; idx < 4; ++idx)
        {
            if (colliding[idx] &&
                CollectEdgeCollisionsUntil(*node.children[idx], edge, collector,
                                           filter, stopAfterCollision, report))
                return true;
        }
        return false;
    }

    for (const QuadTree::NodeHazard& hazard : node.hazards)
    {
        if (collector.Contains(hazard.handle))
            continue;
        bool collides = false;
        if (hazard.presence == HazardPresence::Entire)
        {
            collides = true;
        }
        else if (hazard.presence == HazardPresence::Partial)
        {
            collides = edge.IntersectsRustF32(hazard.partial->ffBbox) &&
                      std::any_of(hazard.partial->edges.begin(),
                                  hazard.partial->edges.end(),
                                  [&edge](const Edge2d& candidate) {
                                      return candidate.IntersectsRustF32(edge);
                                  });
        }
        if (collides && report(hazard.handle, hazard.entity))
            return true;
    }
    return false;
}

// jagua-rs QTNode::collect_collisions_until for Circle 的移植
bool CollisionEngine::CollectCircleCollisionsUntil(
    const QuadTree::Node& node,
    const Circle2d& circle,
    HazardCollector& collector,
    const HazardFilter& filter,
    const CollisionStopCallback& stopAfterCollision,
    const std::function<bool(HazardHandle, const HazardEntity&)>& report) const
{
    const bool performCdNow = node.activeEdges <= m_Config.cdThreshold;
    if (node.children.front() && !performCdNow)
    {
        const auto quadrants = node.bounds.QuadrantsRustF32();
        const auto colliding =
            quadtree_query::circleCollidesWithQuadrants(circle, quadrants);
        // jagua-rs collect 路径固定 0..3 顺序（quadrant_order 仅用于
        // collides 路径，qt_node.rs:183-187）。
        for (std::size_t idx = 0; idx < 4; ++idx)
        {
            if (colliding[idx] &&
                CollectCircleCollisionsUntil(*node.children[idx], circle, collector,
                                             filter, stopAfterCollision, report))
                return true;
        }
        return false;
    }

    for (const QuadTree::NodeHazard& hazard : node.hazards)
    {
        if (collector.Contains(hazard.handle))
            continue;
        bool collides = false;
        if (hazard.presence == HazardPresence::Entire)
        {
            collides = true;
        }
        else if (hazard.presence == HazardPresence::Partial)
        {
            collides = circle.IntersectsRustF32(hazard.partial->ffBbox) &&
                      std::any_of(hazard.partial->edges.begin(),
                                  hazard.partial->edges.end(),
                                  [&circle](const Edge2d& candidate) {
                                      return circle.IntersectsRustF32(candidate);
                                  });
        }
        if (collides && report(hazard.handle, hazard.entity))
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
    // jagua-rs collect_poly_collisions_until 的移植：先注入 exterior，
    // 再按形状边在四叉树内收集，最后对 Partial hazard 做包含检测。
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

    const auto& virtualRoot = m_QuadTree.VirtualRootFor(
        m_QuadTree.m_Root, shape.BoundingBox());

    // 形状边在树内的碰撞收集
    for (const Edge2d& edge : shape.Edges())
    {
        if (CollectEdgeCollisionsUntil(
                virtualRoot, edge, collector, filter, stopAfterCollision, report))
            return true;
    }

    // 对虚拟根内 Partial hazard 做包含检测（jagua-rs detect_containment_collision）
    for (const auto& nodeHazard : virtualRoot.hazards)
    {
        if (nodeHazard.presence != HazardPresence::Partial ||
            collector.Contains(nodeHazard.handle))
            continue;
        const Hazard* hazard = Get(nodeHazard.handle);
        if (!hazard || !hazard->shape ||
            filter.IsIrrelevant(nodeHazard.handle, hazard->entity))
            continue;
        if (DetectContainmentCollision(shape, *hazard->shape, hazard->entity) &&
            report(nodeHazard.handle, hazard->entity))
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
    // jagua-rs detect_poly_collision 的移植：短路边遍历 + 包含检测
    if (isOutside(m_Bounds, shape.BoundingBox()))
        return true;

    const auto& virtualRoot = m_QuadTree.VirtualRootFor(
        m_QuadTree.m_Root, shape.BoundingBox());

    // 边相交快速路径
    for (const Edge2d& edge : shape.Edges())
    {
        if (NodeCollidesWithEdge(virtualRoot, edge, filter))
            return true;
    }

    // 包含检测（Partial hazard）
    for (const auto& nodeHazard : virtualRoot.hazards)
    {
        if (nodeHazard.presence != HazardPresence::Partial)
            continue;
        if (filter.IsIrrelevant(nodeHazard.handle, nodeHazard.entity))
            continue;
        const Hazard* hazard = Get(nodeHazard.handle);
        if (!hazard || !hazard->shape)
            continue;
        if (DetectContainmentCollision(shape, *hazard->shape, hazard->entity))
            return true;
    }
    return false;
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

    // jagua-rs detect_surrogate_collision：ff pole/pier 逐个在四叉树内查询
    for (std::size_t i = 0; i < surrogate.FailFastPoleCount(); ++i)
    {
        Circle2d transformedPole = surrogate.Poles()[i];
        transformedPole.center = transform.ApplyRustF32(transformedPole.center);

        if (NodeCollidesWithCircle(m_QuadTree.m_Root, transformedPole, filter))
            return Result<bool>::FromValue(true);
    }

    for (const Edge2d& basePier : surrogate.Piers())
    {
        const auto transformedStart = transform.ApplyRustF32(basePier.start);
        const auto transformedEnd = transform.ApplyRustF32(basePier.end);
        const auto transformed = Edge2d::Create(transformedStart, transformedEnd);
        if (!transformed.HasValue())
            continue;
        if (NodeCollidesWithEdge(m_QuadTree.m_Root, transformed.Value(), filter))
            return Result<bool>::FromValue(true);
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

    // jagua-rs collect_surrogate_collisions_until：ff pole/pier 在树内收集
    for (std::size_t i = 0; i < surrogate.FailFastPoleCount(); ++i)
    {
        Circle2d transformedPole = surrogate.Poles()[i];
        transformedPole.center = transform.ApplyRustF32(transformedPole.center);

        if (CollectCircleCollisionsUntil(
                m_QuadTree.m_Root, transformedPole, collector, filter,
                stopAfterCollision, report))
            return Result<bool>::FromValue(true);
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
