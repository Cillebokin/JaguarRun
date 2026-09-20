#pragma once

#include "JaguarCore/collision/hazard.h"
#include "JaguarCore/export.h"
#include "JaguarCore/geometry/edge.h"
#include "JaguarCore/geometry/rect.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace jaguar
{
enum class HazardPresence
{
    None,
    Partial,
    Entire,
};

class QuadTree
{
public:
    JAGUARCORE_API QuadTree(Rect2d bounds,
                            std::uint8_t maxDepth,
                            std::uint8_t edgeThreshold);

    JAGUARCORE_API void registerHazard(HazardHandle handle, const Hazard& hazard);
    JAGUARCORE_API void registerHazard(
        HazardHandle handle, const HazardEntity& entity,
        std::shared_ptr<const SimplePolygon> shape, bool dynamic);

    // Kept for callers that only have a conservative bounding box. Such entries
    // are always indexed as Partial because no polygon semantics are available.
    JAGUARCORE_API void registerHazard(HazardHandle handle,
                                       const Rect2d& hazardBounds);
    JAGUARCORE_API void deregisterHazard(HazardHandle handle);

    JAGUARCORE_API std::vector<HazardHandle> query(const Rect2d& queryBounds) const;
    JAGUARCORE_API std::vector<HazardHandle> query(const Edge2d& queryEdge) const;
    JAGUARCORE_API std::vector<HazardHandle> queryVirtualRoot(
        const Rect2d& queryBounds) const;

    JAGUARCORE_API HazardPresence presenceAt(
        HazardHandle handle, const Rect2d& nodeBounds) const noexcept;
    JAGUARCORE_API std::size_t nodeCount() const noexcept;
    JAGUARCORE_API Rect2d virtualRootBounds(const Rect2d& queryBounds) const noexcept;
    JAGUARCORE_API const Rect2d& bounds() const noexcept;

private:
    struct RegisteredHazard
    {
        HazardHandle handle{};
        Hazard hazard{};
        Rect2d bounds{};
        bool hasGeometry = false;
    };

    struct Entry
    {
        HazardHandle handle{};
        HazardPresence presence = HazardPresence::None;
        std::size_t edgeCount = 0;
    };

    struct Node
    {
        Rect2d bounds;
        std::uint8_t depth = 0;
        std::vector<Entry> entries;
        std::array<std::unique_ptr<Node>, 4> children{};
    };

    struct PresenceInfo
    {
        HazardPresence presence = HazardPresence::None;
        std::size_t edgeCount = 0;
    };

    static bool polygonIntersectsRect(const SimplePolygon& polygon,
                                      const Rect2d& rect) noexcept;
    static bool rectInsidePolygon(const SimplePolygon& polygon,
                                  const Rect2d& rect) noexcept;
    static PresenceInfo classify(const RegisteredHazard& hazard,
                                 const Rect2d& nodeBounds) noexcept;
    static void buildNode(Node& node,
                          const std::vector<RegisteredHazard>& hazards,
                          std::uint8_t maxDepth,
                          std::uint8_t edgeThreshold);
    static void queryNode(const Node& node,
                          const Rect2d& queryBounds,
                          const std::vector<RegisteredHazard>& hazards,
                          std::vector<HazardHandle>& result);
    const Node& virtualRoot(const Rect2d& queryBounds) const noexcept;
    static void sortUnique(std::vector<HazardHandle>& result);
    static std::size_t countNodes(const Node& node) noexcept;

    const RegisteredHazard* find(HazardHandle handle) const noexcept;
    void setRegistered(RegisteredHazard hazard);
    void rebuild();

    Node m_Root;
    std::uint8_t m_MaxDepth = 0;
    std::uint8_t m_EdgeThreshold = 0;
    std::vector<RegisteredHazard> m_Hazards;
};
}
