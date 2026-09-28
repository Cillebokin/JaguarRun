#include "JaguarCore/Collision/Quadtree.h"

#include <algorithm>
#include <array>

namespace jaguar
{
namespace
{
std::array<Edge2d, 4> rectangleEdges(const Rect2d& rect)
{
    return {
        Edge2d{Point2d{rect.xMin, rect.yMin}, Point2d{rect.xMax, rect.yMin}},
        Edge2d{Point2d{rect.xMax, rect.yMin}, Point2d{rect.xMax, rect.yMax}},
        Edge2d{Point2d{rect.xMax, rect.yMax}, Point2d{rect.xMin, rect.yMax}},
        Edge2d{Point2d{rect.xMin, rect.yMax}, Point2d{rect.xMin, rect.yMin}},
    };
}

std::array<Point2d, 4> rectangleCorners(const Rect2d& rect)
{
    return {
        Point2d{rect.xMin, rect.yMin}, Point2d{rect.xMax, rect.yMin},
        Point2d{rect.xMax, rect.yMax}, Point2d{rect.xMin, rect.yMax},
    };
}

bool sameBounds(const Rect2d& first, const Rect2d& second) noexcept
{
    return first.xMin == second.xMin && first.yMin == second.yMin &&
           first.xMax == second.xMax && first.yMax == second.yMax;
}
}

QuadTree::QuadTree(Rect2d bounds,
                   std::uint8_t maxDepth,
                   std::uint8_t edgeThreshold)
    : m_Root{bounds, 0, {}, {}},
      m_MaxDepth(maxDepth),
      m_EdgeThreshold(edgeThreshold)
{
}

void QuadTree::RegisterHazard(HazardHandle handle, const Hazard& hazard)
{
    RegisteredHazard registered;
    registered.handle = handle;
    registered.hazard = hazard;
    registered.hasGeometry = hazard.shape != nullptr;
    registered.bounds = registered.hasGeometry ? hazard.shape->BoundingBox() : m_Root.bounds;
    SetRegistered(std::move(registered));
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
    RegisteredHazard registered;
    registered.handle = handle;
    registered.bounds = hazardBounds;
    registered.hasGeometry = false;
    registered.hazard.entity = HazardEntity{};
    SetRegistered(std::move(registered));
}

void QuadTree::DeregisterHazard(HazardHandle handle)
{
    m_Hazards.erase(
        std::remove_if(m_Hazards.begin(), m_Hazards.end(),
                       [handle](const RegisteredHazard& hazard) {
                           return hazard.handle == handle;
                       }),
        m_Hazards.end());
    Rebuild();
}

std::vector<HazardHandle> QuadTree::Query(const Rect2d& queryBounds) const
{
    std::vector<HazardHandle> result;
    QueryNode(VirtualRoot(queryBounds), queryBounds, m_Hazards, result);
    SortUnique(result);
    return result;
}

std::vector<HazardHandle> QuadTree::Query(const Edge2d& queryEdge) const
{
    return Query(queryEdge.BoundingBox());
}

std::vector<HazardHandle> QuadTree::QueryVirtualRoot(
    const Rect2d& queryBounds) const
{
    std::vector<HazardHandle> result;
    QueryNode(VirtualRoot(queryBounds), queryBounds, m_Hazards, result);
    SortUnique(result);
    return result;
}

HazardPresence QuadTree::PresenceAt(HazardHandle handle,
                                    const Rect2d& nodeBounds) const noexcept
{
    const RegisteredHazard* hazard = Find(handle);
    if (!hazard)
        return HazardPresence::None;
    return Classify(*hazard, nodeBounds).presence;
}

std::size_t QuadTree::NodeCount() const noexcept
{
    return CountNodes(m_Root);
}

Rect2d QuadTree::VirtualRootBounds(const Rect2d& queryBounds) const noexcept
{
    return VirtualRoot(queryBounds).bounds;
}

const QuadTree::Node& QuadTree::VirtualRoot(const Rect2d& queryBounds) const noexcept
{
    const Node* current = &m_Root;
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

void QuadTree::SortUnique(std::vector<HazardHandle>& result)
{
    std::sort(result.begin(), result.end(), [](const HazardHandle& lhs,
                                               const HazardHandle& rhs) {
        if (lhs.index != rhs.index)
            return lhs.index < rhs.index;
        return lhs.generation < rhs.generation;
    });
    result.erase(std::unique(result.begin(), result.end()), result.end());
}

const Rect2d& QuadTree::Bounds() const noexcept
{
    return m_Root.bounds;
}

bool QuadTree::PolygonIntersectsRect(const SimplePolygon& polygon,
                                     const Rect2d& rect) noexcept
{
    if (!polygon.BoundingBox().IntersectsRustF32(rect))
        return false;

    const auto rectEdges = rectangleEdges(rect);
    for (const Edge2d& polygonEdge : polygon.Edges())
    {
        for (const Edge2d& rectEdge : rectEdges)
        {
            if (polygonEdge.IntersectsRustF32(rectEdge))
                return true;
        }
    }

    for (const Point2d& point : polygon.Vertices())
    {
        if (rect.ContainsRustF32(point))
            return true;
    }
    for (const Point2d& point : rectangleCorners(rect))
    {
        if (polygon.ContainsRustF32(point))
            return true;
    }
    return false;
}

bool QuadTree::RectInsidePolygon(const SimplePolygon& polygon,
                                 const Rect2d& rect) noexcept
{
    for (const Point2d& point : rectangleCorners(rect))
    {
        if (!polygon.ContainsRustF32(point))
            return false;
    }

    const auto rectEdges = rectangleEdges(rect);
    for (const Edge2d& polygonEdge : polygon.Edges())
    {
        for (const Edge2d& rectEdge : rectEdges)
        {
            if (polygonEdge.IntersectsRustF32(rectEdge))
                return false;
        }
    }
    return true;
}

QuadTree::PresenceInfo QuadTree::Classify(const RegisteredHazard& hazard,
                                          const Rect2d& nodeBounds) noexcept
{
    if (hazard.hazard.entity.kind == HazardKind::Exterior &&
        !nodeBounds.IntersectsRustF32(hazard.bounds))
        return PresenceInfo{HazardPresence::Entire, 0};
    if (hazard.hazard.entity.kind != HazardKind::Exterior &&
        !nodeBounds.IntersectsRustF32(hazard.bounds))
        return {};

    if (!hazard.hasGeometry || !hazard.hazard.shape)
    {
        return PresenceInfo{HazardPresence::Partial, 1};
    }

    const SimplePolygon& shape = *hazard.hazard.shape;
    const std::size_t edgeCount = shape.VertexCount();
    if (hazard.hazard.entity.kind == HazardKind::Exterior)
    {
        if (RectInsidePolygon(shape, nodeBounds))
            return PresenceInfo{HazardPresence::None, 0};
        if (!PolygonIntersectsRect(shape, nodeBounds))
            return PresenceInfo{HazardPresence::Entire, 0};
        return PresenceInfo{HazardPresence::Partial, edgeCount};
    }

    if (RectInsidePolygon(shape, nodeBounds))
        return PresenceInfo{HazardPresence::Entire, 0};
    if (!PolygonIntersectsRect(shape, nodeBounds))
        return PresenceInfo{HazardPresence::None, 0};
    return PresenceInfo{HazardPresence::Partial, edgeCount};
}

void QuadTree::BuildNode(Node& node,
                         const std::vector<RegisteredHazard>& hazards,
                         std::uint8_t maxDepth,
                         std::uint8_t edgeThreshold)
{
    node.entries.clear();
    node.children = {};

    std::size_t activePartialEdges = 0;
    for (const RegisteredHazard& hazard : hazards)
    {
        const PresenceInfo info = Classify(hazard, node.bounds);
        if (info.presence == HazardPresence::None)
            continue;

        node.entries.push_back(Entry{hazard.handle, info.presence, info.edgeCount});
        if (info.presence == HazardPresence::Partial)
            activePartialEdges += info.edgeCount;
    }

    if (node.depth >= maxDepth || activePartialEdges <= edgeThreshold)
        return;

    const auto quadrants = node.bounds.QuadrantsRustF32();
    for (std::size_t i = 0; i < quadrants.size(); ++i)
    {
        node.children[i] = std::make_unique<Node>(
            Node{quadrants[i], static_cast<std::uint8_t>(node.depth + 1), {}, {}});
        BuildNode(*node.children[i], hazards, maxDepth, edgeThreshold);
    }
}

void QuadTree::QueryNode(const Node& node,
                         const Rect2d& queryBounds,
                         const std::vector<RegisteredHazard>& hazards,
                         std::vector<HazardHandle>& result)
{
    if (!node.bounds.IntersectsRustF32(queryBounds))
        return;

    for (const Entry& entry : node.entries)
    {
        const auto hazard = std::find_if(
            hazards.begin(), hazards.end(),
            [&entry](const RegisteredHazard& candidate) {
                return candidate.handle == entry.handle;
            });
        if (hazard != hazards.end() &&
            Classify(*hazard, queryBounds).presence != HazardPresence::None)
            result.push_back(entry.handle);
    }
    for (const auto& child : node.children)
    {
        if (child)
            QueryNode(*child, queryBounds, hazards, result);
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

const QuadTree::RegisteredHazard* QuadTree::Find(HazardHandle handle) const noexcept
{
    const auto it = std::find_if(
        m_Hazards.begin(), m_Hazards.end(), [handle](const RegisteredHazard& hazard) {
            return hazard.handle == handle;
        });
    return it == m_Hazards.end() ? nullptr : &*it;
}

void QuadTree::SetRegistered(RegisteredHazard hazard)
{
    const auto it = std::find_if(
        m_Hazards.begin(), m_Hazards.end(),
         [handle = hazard.handle](const RegisteredHazard& existing) {
            return existing.handle == handle;
        });
    if (it == m_Hazards.end())
        m_Hazards.push_back(std::move(hazard));
    else
        *it = std::move(hazard);
    Rebuild();
}

void QuadTree::Rebuild()
{
    BuildNode(m_Root, m_Hazards, m_MaxDepth, m_EdgeThreshold);
}
}
