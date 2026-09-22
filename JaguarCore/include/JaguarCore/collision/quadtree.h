#pragma once

#include "JaguarCore/Collision/Hazard.h"
#include "JaguarCore/Export.h"
#include "JaguarCore/Geometry/Edge.h"
#include "JaguarCore/Geometry/Rect.h"

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

    JAGUARCORE_API void RegisterHazard(HazardHandle handle, const Hazard& hazard);
    JAGUARCORE_API void RegisterHazard(
        HazardHandle handle, const HazardEntity& entity,
        std::shared_ptr<const SimplePolygon> shape, bool dynamic);

    // Kept for callers that only have a conservative bounding box. Such entries
    // are always indexed as Partial because no polygon semantics are available.
    JAGUARCORE_API void RegisterHazard(HazardHandle handle,
                                       const Rect2d& hazardBounds);
    JAGUARCORE_API void DeregisterHazard(HazardHandle handle);

    JAGUARCORE_API std::vector<HazardHandle> Query(const Rect2d& queryBounds) const;
    JAGUARCORE_API std::vector<HazardHandle> Query(const Edge2d& queryEdge) const;
    JAGUARCORE_API std::vector<HazardHandle> QueryVirtualRoot(
        const Rect2d& queryBounds) const;

    JAGUARCORE_API HazardPresence PresenceAt(
        HazardHandle handle, const Rect2d& nodeBounds) const noexcept;
    JAGUARCORE_API std::size_t NodeCount() const noexcept;
    JAGUARCORE_API Rect2d VirtualRootBounds(const Rect2d& queryBounds) const noexcept;
    JAGUARCORE_API const Rect2d& Bounds() const noexcept;

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

    static bool PolygonIntersectsRect(const SimplePolygon& polygon,
                                      const Rect2d& rect) noexcept;
    static bool RectInsidePolygon(const SimplePolygon& polygon,
                                  const Rect2d& rect) noexcept;
    static PresenceInfo Classify(const RegisteredHazard& hazard,
                                 const Rect2d& nodeBounds) noexcept;
    static void BuildNode(Node& node,
                          const std::vector<RegisteredHazard>& hazards,
                          std::uint8_t maxDepth,
                          std::uint8_t edgeThreshold);
    static void QueryNode(const Node& node,
                          const Rect2d& queryBounds,
                          const std::vector<RegisteredHazard>& hazards,
                          std::vector<HazardHandle>& result);
    const Node& VirtualRoot(const Rect2d& queryBounds) const noexcept;
    static void SortUnique(std::vector<HazardHandle>& result);
    static std::size_t CountNodes(const Node& node) noexcept;

    const RegisteredHazard* Find(HazardHandle handle) const noexcept;
    void SetRegistered(RegisteredHazard hazard);
    void Rebuild();

    Node m_Root;
    std::uint8_t m_MaxDepth = 0;
    std::uint8_t m_EdgeThreshold = 0;
    std::vector<RegisteredHazard> m_Hazards;
};
}
