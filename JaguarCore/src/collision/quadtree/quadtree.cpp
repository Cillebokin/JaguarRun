#include "JaguarCore/collision/quadtree.h"

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

void QuadTree::registerHazard(HazardHandle handle, const Hazard& hazard)
{
    RegisteredHazard registered;
    registered.handle = handle;
    registered.hazard = hazard;
    registered.hasGeometry = hazard.shape != nullptr;
    registered.bounds = registered.hasGeometry ? hazard.shape->boundingBox() : m_Root.bounds;
    setRegistered(std::move(registered));
}

void QuadTree::registerHazard(HazardHandle handle,
                              const HazardEntity& entity,
                              std::shared_ptr<const SimplePolygon> shape,
                              bool dynamic)
{
    registerHazard(handle, Hazard{entity, std::move(shape), dynamic});
}

void QuadTree::registerHazard(HazardHandle handle, const Rect2d& hazardBounds)
{
    RegisteredHazard registered;
    registered.handle = handle;
    registered.bounds = hazardBounds;
    registered.hasGeometry = false;
    registered.hazard.entity = HazardEntity{};
    setRegistered(std::move(registered));
}

void QuadTree::deregisterHazard(HazardHandle handle)
{
    m_Hazards.erase(
        std::remove_if(m_Hazards.begin(), m_Hazards.end(),
                       [handle](const RegisteredHazard& hazard) {
                           return hazard.handle == handle;
                       }),
        m_Hazards.end());
    rebuild();
}

std::vector<HazardHandle> QuadTree::query(const Rect2d& queryBounds) const
{
    std::vector<HazardHandle> result;
    queryNode(virtualRoot(queryBounds), queryBounds, m_Hazards, result);
    sortUnique(result);
    return result;
}

std::vector<HazardHandle> QuadTree::query(const Edge2d& queryEdge) const
{
    return query(queryEdge.boundingBox());
}

std::vector<HazardHandle> QuadTree::queryVirtualRoot(
    const Rect2d& queryBounds) const
{
    std::vector<HazardHandle> result;
    queryNode(virtualRoot(queryBounds), queryBounds, m_Hazards, result);
    sortUnique(result);
    return result;
}

HazardPresence QuadTree::presenceAt(HazardHandle handle,
                                    const Rect2d& nodeBounds) const noexcept
{
    const RegisteredHazard* hazard = find(handle);
    if (!hazard)
        return HazardPresence::None;
    return classify(*hazard, nodeBounds).presence;
}

std::size_t QuadTree::nodeCount() const noexcept
{
    return countNodes(m_Root);
}

Rect2d QuadTree::virtualRootBounds(const Rect2d& queryBounds) const noexcept
{
    return virtualRoot(queryBounds).bounds;
}

const QuadTree::Node& QuadTree::virtualRoot(const Rect2d& queryBounds) const noexcept
{
    const Node* current = &m_Root;
    while (true)
    {
        const Node* surroundingChild = nullptr;
        for (const auto& child : current->children)
        {
            if (child && child->bounds.relationTo(queryBounds) ==
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

void QuadTree::sortUnique(std::vector<HazardHandle>& result)
{
    std::sort(result.begin(), result.end(), [](const HazardHandle& lhs,
                                               const HazardHandle& rhs) {
        if (lhs.index != rhs.index)
            return lhs.index < rhs.index;
        return lhs.generation < rhs.generation;
    });
    result.erase(std::unique(result.begin(), result.end()), result.end());
}

const Rect2d& QuadTree::bounds() const noexcept
{
    return m_Root.bounds;
}

bool QuadTree::polygonIntersectsRect(const SimplePolygon& polygon,
                                     const Rect2d& rect) noexcept
{
    if (!polygon.boundingBox().intersects(rect))
        return false;

    const auto rectEdges = rectangleEdges(rect);
    for (const Edge2d& polygonEdge : polygon.edges())
    {
        for (const Edge2d& rectEdge : rectEdges)
        {
            if (polygonEdge.intersects(rectEdge))
                return true;
        }
    }

    for (const Point2d& point : polygon.vertices())
    {
        if (rect.contains(point))
            return true;
    }
    for (const Point2d& point : rectangleCorners(rect))
    {
        if (polygon.contains(point))
            return true;
    }
    return false;
}

bool QuadTree::rectInsidePolygon(const SimplePolygon& polygon,
                                 const Rect2d& rect) noexcept
{
    for (const Point2d& point : rectangleCorners(rect))
    {
        if (!polygon.contains(point))
            return false;
    }

    const auto rectEdges = rectangleEdges(rect);
    for (const Edge2d& polygonEdge : polygon.edges())
    {
        for (const Edge2d& rectEdge : rectEdges)
        {
            if (polygonEdge.intersects(rectEdge))
                return false;
        }
    }
    return true;
}

QuadTree::PresenceInfo QuadTree::classify(const RegisteredHazard& hazard,
                                          const Rect2d& nodeBounds) noexcept
{
    if (hazard.hazard.entity.kind == HazardKind::Exterior &&
        !nodeBounds.intersects(hazard.bounds))
        return PresenceInfo{HazardPresence::Entire, 0};
    if (hazard.hazard.entity.kind != HazardKind::Exterior &&
        !nodeBounds.intersects(hazard.bounds))
        return {};

    if (!hazard.hasGeometry || !hazard.hazard.shape)
    {
        return PresenceInfo{HazardPresence::Partial, 1};
    }

    const SimplePolygon& shape = *hazard.hazard.shape;
    const std::size_t edgeCount = shape.vertexCount();
    if (hazard.hazard.entity.kind == HazardKind::Exterior)
    {
        if (rectInsidePolygon(shape, nodeBounds))
            return PresenceInfo{HazardPresence::None, 0};
        if (!polygonIntersectsRect(shape, nodeBounds))
            return PresenceInfo{HazardPresence::Entire, 0};
        return PresenceInfo{HazardPresence::Partial, edgeCount};
    }

    if (rectInsidePolygon(shape, nodeBounds))
        return PresenceInfo{HazardPresence::Entire, 0};
    if (!polygonIntersectsRect(shape, nodeBounds))
        return PresenceInfo{HazardPresence::None, 0};
    return PresenceInfo{HazardPresence::Partial, edgeCount};
}

void QuadTree::buildNode(Node& node,
                         const std::vector<RegisteredHazard>& hazards,
                         std::uint8_t maxDepth,
                         std::uint8_t edgeThreshold)
{
    node.entries.clear();
    node.children = {};

    std::size_t activePartialEdges = 0;
    for (const RegisteredHazard& hazard : hazards)
    {
        const PresenceInfo info = classify(hazard, node.bounds);
        if (info.presence == HazardPresence::None)
            continue;

        node.entries.push_back(Entry{hazard.handle, info.presence, info.edgeCount});
        if (info.presence == HazardPresence::Partial)
            activePartialEdges += info.edgeCount;
    }

    if (node.depth >= maxDepth || activePartialEdges <= edgeThreshold)
        return;

    const auto quadrants = node.bounds.quadrants();
    for (std::size_t i = 0; i < quadrants.size(); ++i)
    {
        node.children[i] = std::make_unique<Node>(
            Node{quadrants[i], static_cast<std::uint8_t>(node.depth + 1), {}, {}});
        buildNode(*node.children[i], hazards, maxDepth, edgeThreshold);
    }
}

void QuadTree::queryNode(const Node& node,
                         const Rect2d& queryBounds,
                         const std::vector<RegisteredHazard>& hazards,
                         std::vector<HazardHandle>& result)
{
    if (!node.bounds.intersects(queryBounds))
        return;

    for (const Entry& entry : node.entries)
    {
        const auto hazard = std::find_if(
            hazards.begin(), hazards.end(),
            [&entry](const RegisteredHazard& candidate) {
                return candidate.handle == entry.handle;
            });
        if (hazard != hazards.end() &&
            classify(*hazard, queryBounds).presence != HazardPresence::None)
            result.push_back(entry.handle);
    }
    for (const auto& child : node.children)
    {
        if (child)
            queryNode(*child, queryBounds, hazards, result);
    }
}

std::size_t QuadTree::countNodes(const Node& node) noexcept
{
    std::size_t count = 1;
    for (const auto& child : node.children)
    {
        if (child)
            count += countNodes(*child);
    }
    return count;
}

const QuadTree::RegisteredHazard* QuadTree::find(HazardHandle handle) const noexcept
{
    const auto it = std::find_if(
        m_Hazards.begin(), m_Hazards.end(), [handle](const RegisteredHazard& hazard) {
            return hazard.handle == handle;
        });
    return it == m_Hazards.end() ? nullptr : &*it;
}

void QuadTree::setRegistered(RegisteredHazard hazard)
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
    rebuild();
}

void QuadTree::rebuild()
{
    buildNode(m_Root, m_Hazards, m_MaxDepth, m_EdgeThreshold);
}
}
