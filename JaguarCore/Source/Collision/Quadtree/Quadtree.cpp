#include "JaguarCore/Collision/Quadtree.h"
#include "JaguarCore/Geometry/Circle.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <functional>
#include <limits>
#include <stdexcept>

namespace jaguar
{
namespace
{
// jagua-rs Rect::QUADRANT_NEIGHBOR_LAYOUT
constexpr std::array<std::array<std::size_t, 2>, 4> kQuadrantNeighborLayout{
    {{1, 3}, {0, 2}, {1, 3}, {0, 2}}};

// 判断多边形是否与矩形边/角接触（用于 constrict 的边归类）
// jagua-rs Rect::collides_with(&Edge)：bbox 预筛 + 端点包含 + 四边判交
bool rectCollidesWithEdge(const Rect2d& rect, const Edge2d& edge) noexcept
{
    return edge.IntersectsRustF32(rect);
}

bool rectContainsPolygonBBox(const Rect2d& rect,
                             const Rect2d& polygonBounds) noexcept
{
    return rect.RelationToRustF32(polygonBounds) == RectRelation::Surrounding;
}

// jagua-rs: CollisionHalf —— 半段相交方向
enum class CollisionHalf
{
    FirstHalf,
    Halfway,
    SecondHalf,
};

// jagua-rs qt_traits::edge_intersection_half 的移植
bool edgeIntersectionHalf(const Edge2d& first,
                          const Edge2d& second,
                          CollisionHalf& firstLocation,
                          CollisionHalf& secondLocation) noexcept
{
    const float x1 = static_cast<float>(first.start.x);
    const float y1 = static_cast<float>(first.start.y);
    const float x2 = static_cast<float>(first.end.x);
    const float y2 = static_cast<float>(first.end.y);
    const float x3 = static_cast<float>(second.start.x);
    const float y3 = static_cast<float>(second.start.y);
    const float x4 = static_cast<float>(second.end.x);
    const float y4 = static_cast<float>(second.end.y);

    const float tNom = (x2 - x4) * (y4 - y3) - (y2 - y4) * (x4 - x3);
    const float tDenom = (x2 - x1) * (y4 - y3) - (y2 - y1) * (x4 - x3);
    const float uNom = (x2 - x4) * (y2 - y1) - (y2 - y4) * (x2 - x1);
    const float uDenom = tDenom;
    if (tDenom == 0.0F || uDenom == 0.0F)
        return false;

    const float t = tNom / tDenom;
    const float u = uNom / uDenom;
    if ((t >= 0.0F && t <= 1.0F) && (u >= 0.0F && u <= 1.0F))
    {
        const auto classify = [](float value) {
            if (value > 0.5F)
                return CollisionHalf::FirstHalf;
            if (value < 0.5F)
                return CollisionHalf::SecondHalf;
            return CollisionHalf::Halfway;
        };
        firstLocation = classify(t);
        secondLocation = classify(u);
        return true;
    }
    return false;
}

// jagua-rs qt_traits::half_intersect：first/second 半段命中时置位对应象限
template <std::size_t N>
void halfIntersect(const Edge2d& probe,
                   const Edge2d& splitter,
                   std::array<std::optional<bool>*, N>& firstQuadrants,
                   std::array<std::optional<bool>*, N>& secondQuadrants) noexcept
{
    bool anyUndetermined = false;
    for (std::optional<bool>* flag : firstQuadrants)
        anyUndetermined = anyUndetermined || !flag->has_value();
    for (std::optional<bool>* flag : secondQuadrants)
        anyUndetermined = anyUndetermined || !flag->has_value();
    if (!anyUndetermined)
        return;

    CollisionHalf firstLocation;
    CollisionHalf secondLocation;
    if (!edgeIntersectionHalf(probe, splitter, firstLocation, secondLocation))
        return;
    if (secondLocation == CollisionHalf::FirstHalf ||
        secondLocation == CollisionHalf::Halfway)
    {
        for (std::optional<bool>* flag : firstQuadrants)
            *flag = true;
    }
    if (secondLocation == CollisionHalf::SecondHalf ||
        secondLocation == CollisionHalf::Halfway)
    {
        for (std::optional<bool>* flag : secondQuadrants)
            *flag = true;
    }
}
}  // 匿名命名空间结束

// jagua-rs QTQueryable for Edge：与四个象限相交的保守分类
// （供 CollisionEngine 的 QTNode 查询移植使用）
namespace quadtree_query
{
std::array<bool, 4> edgeCollidesWithQuadrants(
    const Edge2d& edge, const Rect2d& nodeBounds,
    const std::array<Rect2d, 4>& quadrants) noexcept
{
    const float eXMin = static_cast<float>(
        std::min(edge.start.x, edge.end.x));
    const float eXMax = static_cast<float>(
        std::max(edge.start.x, edge.end.x));
    const float eYMin = static_cast<float>(
        std::min(edge.start.y, edge.end.y));
    const float eYMax = static_cast<float>(
        std::max(edge.start.y, edge.end.y));

    std::array<std::optional<bool>, 4> classified{};
    for (std::size_t i = 0; i < 4; ++i)
    {
        const Rect2d& q = quadrants[i];
        const float xNoOverlap =
            std::max(eXMin, static_cast<float>(q.xMin)) >
            std::min(eXMax, static_cast<float>(q.xMax));
        const float yNoOverlap =
            std::max(eYMin, static_cast<float>(q.yMin)) >
            std::min(eYMax, static_cast<float>(q.yMax));
        if (xNoOverlap || yNoOverlap)
            classified[i] = false;
        else if (q.ContainsRustF32(edge.start) || q.ContainsRustF32(edge.end))
            classified[i] = true;
    }

    if (std::all_of(classified.begin(), classified.end(),
                    [](const std::optional<bool>& value) {
                        return value.has_value();
                    }))
    {
        return {classified[0].value(), classified[1].value(),
                classified[2].value(), classified[3].value()};
    }

    // 与父节点中线/边的半段相交推断
    // jagua-rs sides() 顺序（corners [TR,TL,BL,BR] 的相邻连线）：
    // [0]=top, [1]=left, [2]=bottom, [3]=right
    const std::vector<Edge2d> sides = nodeBounds.SidesRustF32();
    const Edge2d top = sides[0];
    const Edge2d left = sides[1];
    const Edge2d bottom = sides[2];
    const Edge2d right = sides[3];
    const Point2d centroid = nodeBounds.CentroidRustF32();
    const Edge2d hBisect{
        Point2d{nodeBounds.xMin, centroid.y},
        Point2d{nodeBounds.xMax, centroid.y}};
    const Edge2d vBisect{
        Point2d{centroid.x, nodeBounds.yMin},
        Point2d{centroid.x, nodeBounds.yMax}};

    std::array<std::optional<bool>*, 1> q1Left{&classified[1]};
    std::array<std::optional<bool>*, 1> q2Left{&classified[2]};
    halfIntersect(edge, left, q1Left, q2Left);
    std::array<std::optional<bool>*, 1> q3Right{&classified[3]};
    std::array<std::optional<bool>*, 1> q0Right{&classified[0]};
    halfIntersect(edge, right, q3Right, q0Right);
    std::array<std::optional<bool>*, 1> q0Top{&classified[0]};
    std::array<std::optional<bool>*, 1> q1Top{&classified[1]};
    halfIntersect(edge, top, q0Top, q1Top);
    std::array<std::optional<bool>*, 1> q2Bottom{&classified[2]};
    std::array<std::optional<bool>*, 1> q3Bottom{&classified[3]};
    halfIntersect(edge, bottom, q2Bottom, q3Bottom);
    std::array<std::optional<bool>*, 2> q1H{&classified[1], &classified[2]};
    std::array<std::optional<bool>*, 2> q0H{&classified[0], &classified[3]};
    halfIntersect(edge, hBisect, q1H, q0H);
    std::array<std::optional<bool>*, 2> q2V{&classified[2], &classified[3]};
    std::array<std::optional<bool>*, 2> q0V{&classified[0], &classified[1]};
    halfIntersect(edge, vBisect, q2V, q0V);

    std::array<bool, 4> result{};
    for (std::size_t i = 0; i < 4; ++i)
        result[i] = classified[i].value_or(false);
    return result;
}
}  // namespace quadtree_query

namespace quadtree_query
{
std::array<bool, 4> circleCollidesWithQuadrants(
    const Circle2d& circle, const std::array<Rect2d, 4>& quadrants) noexcept
{
    return {circle.IntersectsRustF32(quadrants[0]),
            circle.IntersectsRustF32(quadrants[1]),
            circle.IntersectsRustF32(quadrants[2]),
            circle.IntersectsRustF32(quadrants[3])};
}

// jagua-rs QTQueryable for Circle：按圆心所在象限优先的搜索顺序
std::array<std::size_t, 4> circleQuadrantOrder(
    const Circle2d& circle, const std::array<Rect2d, 4>& quadrants) noexcept
{
    const Point2d split{quadrants[0].xMin, quadrants[0].yMin};
    const bool right = circle.center.x >= split.x;
    const bool top = circle.center.y >= split.y;
    const std::size_t rowMask = top ? 0U : 2U;
    const std::size_t containingQuadrant =
        rowMask | static_cast<std::size_t>(right != top);
    const std::size_t firstAdjacentMask = rowMask | 1U;
    return {containingQuadrant,
            containingQuadrant ^ firstAdjacentMask,
            containingQuadrant ^ (firstAdjacentMask ^ 2U),
            containingQuadrant ^ 2U};
}
}  // namespace quadtree_query

void QuadTree::Node::AddHazard(NodeHazard hazard)
{
    // jagua-rs QTHazardVec::add：按 presence 降序插入（Entire > Partial > None）
    activeEdges += hazard.EdgeCount();
    const auto sortKey = [](HazardPresence presence) {
        switch (presence)
        {
        case HazardPresence::Entire:
            return 2;
        case HazardPresence::Partial:
            return 1;
        case HazardPresence::None:
        default:
            return 0;
        }
    };
    const int key = sortKey(hazard.presence);
    const auto it = std::find_if(
        hazards.begin(), hazards.end(),
        [key, sortKey](const NodeHazard& existing) {
            return sortKey(existing.presence) <= key;
        });
    hazards.insert(it, std::move(hazard));
}

bool QuadTree::Node::RemoveHazard(HazardHandle handle)
{
    const auto it = std::find_if(
        hazards.begin(), hazards.end(),
        [handle](const NodeHazard& hazard) {
            return hazard.handle == handle;
        });
    if (it == hazards.end())
        return false;
    activeEdges -= it->EdgeCount();
    hazards.erase(it);
    return true;
}

bool QuadTree::Node::HasPartialHazard() const noexcept
{
    return std::any_of(hazards.begin(), hazards.end(),
                       [](const NodeHazard& hazard) {
                           return hazard.presence == HazardPresence::Partial;
                       });
}

QuadTree::NodeHazard QuadTree::MakeRootHazard(HazardHandle handle,
                                              const Hazard& hazard,
                                              const Rect2d& rootBounds)
{
    // jagua-rs QTHazard::from_root：根节点一律 Partial（整形状边集）。
    NodeHazard root;
    root.qtBbox = rootBounds;
    root.handle = handle;
    root.entity = hazard.entity;
    root.presence = HazardPresence::Partial;
    root.partial = std::make_unique<PartialHazard>();
    if (hazard.shape)
    {
        root.partial->edges = hazard.shape->Edges();
        root.partial->ffBbox = hazard.shape->BoundingBox();
    }
    else
    {
        // bounds-only 注册（C++ 额外入口）：用一条根节点对角线代表整形状
        root.partial->edges = {
            Edge2d{Point2d{rootBounds.xMin, rootBounds.yMin},
                   Point2d{rootBounds.xMax, rootBounds.yMax}}};
        root.partial->ffBbox = rootBounds;
    }
    return root;
}

std::array<QuadTree::NodeHazard, 4> QuadTree::Constrict(
    const NodeHazard& hazard,
    const std::array<Rect2d, 4>& quadrants,
    const SimplePolygon& shape)
{
    std::array<std::optional<NodeHazard>, 4> constricted{};

    if (hazard.presence == HazardPresence::Entire)
    {
        for (std::size_t i = 0; i < 4; ++i)
        {
            NodeHazard clone;
            clone.qtBbox = quadrants[i];
            clone.handle = hazard.handle;
            clone.entity = hazard.entity;
            clone.presence = HazardPresence::Entire;
            constricted[i] = std::move(clone);
        }
        return {std::move(constricted[0].value()),
                std::move(constricted[1].value()),
                std::move(constricted[2].value()),
                std::move(constricted[3].value())};
    }

    // Partial：先找 hazard bbox 完全落入的象限
    const Rect2d shapeBounds = shape.BoundingBox();
    std::optional<std::size_t> enclosedQuadrant;
    for (std::size_t i = 0; i < 4; ++i)
    {
        if (rectContainsPolygonBBox(quadrants[i], shapeBounds))
        {
            enclosedQuadrant = i;
            break;
        }
    }

    if (enclosedQuadrant.has_value())
    {
        for (std::size_t i = 0; i < 4; ++i)
        {
            NodeHazard child;
            child.qtBbox = quadrants[i];
            child.handle = hazard.handle;
            child.entity = hazard.entity;
            child.presence = i == *enclosedQuadrant
                                 ? HazardPresence::Partial
                                 : HazardPresence::None;
            if (child.presence == HazardPresence::Partial)
            {
                child.partial = std::make_unique<PartialHazard>(*hazard.partial);
            }
            constricted[i] = std::move(child);
        }
    }
    else
    {
        // 多象限活跃：先按边与象限相交归 Partial
        for (std::size_t i = 0; i < 4; ++i)
        {
            std::vector<Edge2d> collidingEdges;
            for (const Edge2d& edge : hazard.partial->edges)
            {
                if (rectCollidesWithEdge(quadrants[i], edge))
                    collidingEdges.push_back(edge);
            }
            if (!collidingEdges.empty())
            {
                NodeHazard child;
                child.qtBbox = quadrants[i];
                child.handle = hazard.handle;
                child.entity = hazard.entity;
                child.presence = HazardPresence::Partial;

                auto partial = std::make_unique<PartialHazard>();
                if (collidingEdges.size() == hazard.partial->edges.size())
                {
                    partial->ffBbox = hazard.partial->ffBbox;
                }
                else
                {
                    float xMin = std::numeric_limits<float>::infinity();
                    float yMin = std::numeric_limits<float>::infinity();
                    float xMax = -std::numeric_limits<float>::infinity();
                    float yMax = -std::numeric_limits<float>::infinity();
                    for (const Edge2d& edge : collidingEdges)
                    {
                        xMin = std::min(
                            xMin, std::min(static_cast<float>(edge.start.x),
                                           static_cast<float>(edge.end.x)));
                        yMin = std::min(
                            yMin, std::min(static_cast<float>(edge.start.y),
                                           static_cast<float>(edge.end.y)));
                        xMax = std::max(
                            xMax, std::max(static_cast<float>(edge.start.x),
                                           static_cast<float>(edge.end.x)));
                        yMax = std::max(
                            yMax, std::max(static_cast<float>(edge.start.y),
                                           static_cast<float>(edge.end.y)));
                    }
                    if (xMin < xMax && yMin < yMax)
                    {
                        partial->ffBbox = Rect2d{
                            static_cast<double>(xMin), static_cast<double>(yMin),
                            static_cast<double>(xMax), static_cast<double>(yMax)};
                    }
                    else
                    {
                        partial->ffBbox = hazard.partial->ffBbox;
                    }
                }
                partial->edges = std::move(collidingEdges);
                child.partial = std::move(partial);
                constricted[i] = std::move(child);
            }
        }

#ifndef NDEBUG
        if (std::none_of(
                constricted.begin(), constricted.end(),
                [](const std::optional<NodeHazard>& child) {
                    return child.has_value();
                }))
        {
            throw std::logic_error(
                "debug assertion failed: constricted_hazards has no partial child");
        }
#endif

        // 无相交边的象限按邻居 presence 或质心包含判定 Entire/None
        for (std::size_t i = 0; i < 4; ++i)
        {
            if (constricted[i].has_value())
                continue;

            bool noneNeighbor = false;
            bool entireNeighbor = false;
            for (const std::size_t neighbor : kQuadrantNeighborLayout[i])
            {
                if (!constricted[neighbor].has_value())
                    continue;
                if (constricted[neighbor]->presence == HazardPresence::None)
                    noneNeighbor = true;
                if (constricted[neighbor]->presence == HazardPresence::Entire)
                    entireNeighbor = true;
            }

            HazardPresence presence;
            if (noneNeighbor)
            {
                presence = HazardPresence::None;
            }
            else if (entireNeighbor)
            {
                presence = HazardPresence::Entire;
            }
            else
            {
                const bool colliding =
                    shape.ContainsRustF32(quadrants[i].CentroidRustF32());
                const bool interiorScope =
                    hazard.entity.kind != HazardKind::Exterior;
                presence = interiorScope == colliding
                               ? HazardPresence::Entire
                               : HazardPresence::None;
            }

            NodeHazard child;
            child.qtBbox = quadrants[i];
            child.handle = hazard.handle;
            child.entity = hazard.entity;
            child.presence = presence;
            constricted[i] = std::move(child);
        }
    }

    return {std::move(constricted[0].value()),
            std::move(constricted[1].value()),
            std::move(constricted[2].value()),
            std::move(constricted[3].value())};
}

void QuadTree::RegisterIntoNode(Node& node,
                                NodeHazard hazard,
                                const std::vector<RegisteredShape>& shapes)
{
    const auto shapeIt = std::find_if(
        shapes.begin(), shapes.end(),
        [&hazard](const RegisteredShape& registered) {
            return registered.handle == hazard.handle;
        });

    // 懒扩展：节点无子节点、还可细分、且新 hazard 为 Partial
    if (node.level > 0 && node.children.front() == nullptr &&
        hazard.presence == HazardPresence::Partial)
    {
        const auto quadrants = node.bounds.QuadrantsRustF32();
        for (std::size_t i = 0; i < 4; ++i)
        {
            node.children[i] = std::make_unique<Node>();
            node.children[i]->bounds = quadrants[i];
            node.children[i]->level = static_cast<std::uint8_t>(node.level - 1);
        }
        // 把已有 hazard constrict 到新子节点——每个 hazard 用自己的形状
        // （jagua-rs qt_node.rs:44：constrict 按各自 hkey 从 haz_map 取形状）
        for (const NodeHazard& existing : node.hazards)
        {
            const auto existingShape = std::find_if(
                shapes.begin(), shapes.end(),
                [&existing](const RegisteredShape& registered) {
                    return registered.handle == existing.handle;
                });
            if (existingShape == shapes.end() || !existingShape->shape)
                continue;
            std::array<NodeHazard, 4> children =
                Constrict(existing, quadrants, *existingShape->shape);
            for (std::size_t i = 0; i < 4; ++i)
            {
                if (children[i].presence != HazardPresence::None)
                    RegisterIntoNode(*node.children[i], std::move(children[i]),
                                     shapes);
            }
        }
    }

    if (node.children.front() != nullptr)
    {
        // jagua-rs：constrict 到子节点需要完整形状
        if (shapeIt != shapes.end() && shapeIt->shape)
        {
            const auto quadrants = node.bounds.QuadrantsRustF32();
            std::array<NodeHazard, 4> children =
                Constrict(hazard, quadrants, *shapeIt->shape);
            for (std::size_t i = 0; i < 4; ++i)
            {
                if (children[i].presence != HazardPresence::None)
                    RegisterIntoNode(*node.children[i], std::move(children[i]),
                                     shapes);
            }
        }
    }
    node.AddHazard(std::move(hazard));
}void QuadTree::DeregisterFromNode(Node& node, HazardHandle handle)
{
    if (!node.RemoveHazard(handle))
        return;

    if (!node.HasPartialHazard())
    {
        // 无 Partial hazard 时折叠子节点（jagua-rs deregister_hazard）
        node.children = {};
    }
    else
    {
        for (auto& child : node.children)
        {
            if (child)
                DeregisterFromNode(*child, handle);
        }
    }
}

QuadTree::QuadTree(Rect2d bounds,
                   std::uint8_t quadtreeDepth,
                   std::uint8_t cdThreshold)
    : m_Root{bounds, quadtreeDepth, {}, {}, 0},
      m_CdThreshold(cdThreshold)
{
}

void QuadTree::RegisterHazard(HazardHandle handle, const Hazard& hazard)
{
    RegisteredShape registered;
    registered.handle = handle;
    registered.shape = hazard.shape;
    registered.bounds =
        hazard.shape ? hazard.shape->BoundingBox() : m_Root.bounds;
    registered.entity = hazard.entity;
    SetRegistered(std::move(registered));

    RegisterIntoNode(m_Root, MakeRootHazard(handle, hazard, m_Root.bounds),
                     m_Registered);
}

void QuadTree::RegisterHazard(HazardHandle handle,
                              const HazardEntity& entity,
                              std::shared_ptr<const SimplePolygon> shape,
                              bool dynamic)
{
    RegisterHazard(handle, Hazard{entity, std::move(shape), dynamic});
}

void QuadTree::RegisterHazard(HazardHandle handle, const Rect2d& hazardBounds)
{
    RegisteredShape registered;
    registered.handle = handle;
    registered.bounds = hazardBounds;
    registered.entity = HazardEntity{};
    SetRegistered(std::move(registered));

    // bounds-only：C++ 额外入口，等价于无完整几何的根 Partial
    NodeHazard root;
    root.qtBbox = m_Root.bounds;
    root.handle = handle;
    root.presence = HazardPresence::Partial;
    root.partial = std::make_unique<PartialHazard>();
    root.partial->edges = {
        Edge2d{Point2d{hazardBounds.xMin, hazardBounds.yMin},
               Point2d{hazardBounds.xMax, hazardBounds.yMax}}};
    root.partial->ffBbox = hazardBounds;
    RegisterIntoNode(m_Root, std::move(root), m_Registered);
}

void QuadTree::DeregisterHazard(HazardHandle handle)
{
    m_Registered.erase(
        std::remove_if(m_Registered.begin(), m_Registered.end(),
                       [handle](const RegisteredShape& registered) {
                           return registered.handle == handle;
                       }),
        m_Registered.end());
    DeregisterFromNode(m_Root, handle);
}

std::vector<HazardHandle> QuadTree::Query(const Rect2d& queryBounds) const
{
    // 兼容旧查询接口：在虚拟根子树内收集 Partial/Entire 的 hazard
    std::vector<HazardHandle> result;
    const Node& root = VirtualRootFor(m_Root, queryBounds);

    struct Walker
    {
        const Node* node;
    };
    std::vector<Walker> stack{{&root}};
    while (!stack.empty())
    {
        const Node& node = *stack.back().node;
        stack.pop_back();
        if (!node.bounds.IntersectsRustF32(queryBounds))
            continue;
        for (const NodeHazard& hazard : node.hazards)
        {
            if (hazard.presence != HazardPresence::None &&
                std::find(result.begin(), result.end(), hazard.handle) ==
                    result.end())
            {
                result.push_back(hazard.handle);
            }
        }
        for (const auto& child : node.children)
        {
            if (child)
                stack.push_back({child.get()});
        }
    }
    std::sort(result.begin(), result.end(),
              [](const HazardHandle& lhs, const HazardHandle& rhs) {
                  if (lhs.index != rhs.index)
                      return lhs.index < rhs.index;
                  return lhs.generation < rhs.generation;
              });
    return result;
}

std::vector<HazardHandle> QuadTree::Query(const Edge2d& queryEdge) const
{
    return Query(queryEdge.BoundingBox());
}

std::vector<HazardHandle> QuadTree::QueryVirtualRoot(
    const Rect2d& queryBounds) const
{
    return Query(queryBounds);
}

HazardPresence QuadTree::PresenceAt(HazardHandle handle,
                                    const Rect2d& nodeBounds) const noexcept
{
    // 在树中找该 handle 且所在节点包围盒匹配 nodeBounds 的条目
    const std::function<HazardPresence(const Node*)> findPresence =
        [&findPresence, handle, &nodeBounds](const Node* node) noexcept {
        for (const NodeHazard& hazard : node->hazards)
        {
            if (hazard.handle == handle && hazard.presence != HazardPresence::None &&
                hazard.qtBbox.xMin == nodeBounds.xMin &&
                hazard.qtBbox.yMin == nodeBounds.yMin &&
                hazard.qtBbox.xMax == nodeBounds.xMax &&
                hazard.qtBbox.yMax == nodeBounds.yMax)
                return hazard.presence;
        }
        for (const auto& child : node->children)
        {
            if (!child)
                continue;
            const HazardPresence presence = findPresence(child.get());
            if (presence != HazardPresence::None)
                return presence;
        }
        return HazardPresence::None;
    };
    return findPresence(&m_Root);
}

std::size_t QuadTree::NodeCount() const noexcept
{
    return CountNodes(m_Root);
}

Rect2d QuadTree::VirtualRootBounds(const Rect2d& queryBounds) const noexcept
{
    return VirtualRootFor(m_Root, queryBounds).bounds;
}

const QuadTree::Node& QuadTree::VirtualRootFor(const Node& root,
                                               const Rect2d& queryBounds) noexcept
{
    const Node* current = &root;
    while (true)
    {
        const Node* surroundingChild = nullptr;
        for (const auto& child : current->children)
        {
            if (child && child->bounds.RelationToRustF32(queryBounds) ==
                             RectRelation::Surrounding)
            {
                surroundingChild = child.get();
                break;
            }
        }
        if (!surroundingChild)
            return *current;
        current = surroundingChild;
    }
}

std::size_t QuadTree::CountNodes(const Node& node) noexcept
{
    std::size_t count = 1;
    for (const auto& child : node.children)
    {
        if (child)
            count += CountNodes(*child);
    }
    return count;
}

const QuadTree::RegisteredShape* QuadTree::Find(HazardHandle handle) const noexcept
{
    const auto it = std::find_if(
        m_Registered.begin(), m_Registered.end(),
        [handle](const RegisteredShape& registered) {
            return registered.handle == handle;
        });
    return it == m_Registered.end() ? nullptr : &*it;
}

void QuadTree::SetRegistered(RegisteredShape registered)
{
    const auto it = std::find_if(
        m_Registered.begin(), m_Registered.end(),
         [handle = registered.handle](const RegisteredShape& existing) {
             return existing.handle == handle;
         });
    if (it == m_Registered.end())
        m_Registered.push_back(std::move(registered));
    else
        *it = std::move(registered);
}
}
