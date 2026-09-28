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
struct Circle2d;

// 障碍物相对节点的覆盖状态
enum class HazardPresence
{
    None,    // 不覆盖
    Partial, // 部分覆盖
    Entire,  // 完全覆盖
};

// 碰撞障碍物的四叉树索引
class QuadTree
{
public:
    // 创建四叉树
    JAGUARCORE_API QuadTree(Rect2d bounds,
                            std::uint8_t maxDepth,
                            std::uint8_t edgeThreshold);

    // 注册完整障碍物
    JAGUARCORE_API void RegisterHazard(HazardHandle handle, const Hazard& hazard);
    // 注册障碍物数据
    JAGUARCORE_API void RegisterHazard(
        HazardHandle handle, const HazardEntity& entity,
        std::shared_ptr<const SimplePolygon> shape, bool dynamic);

    // 仅使用保守包围盒注册障碍物
    // 没有多边形几何时统一标记为部分覆盖
    JAGUARCORE_API void RegisterHazard(HazardHandle handle,
                                       const Rect2d& hazardBounds);
    // 注销障碍物
    JAGUARCORE_API void DeregisterHazard(HazardHandle handle);

    // 查询与矩形相交的障碍物
    JAGUARCORE_API std::vector<HazardHandle> Query(const Rect2d& queryBounds) const;
    // 查询与线段相交的障碍物
    JAGUARCORE_API std::vector<HazardHandle> Query(const Edge2d& queryEdge) const;
    // 查询虚拟根节点覆盖范围内的障碍物
    JAGUARCORE_API std::vector<HazardHandle> QueryVirtualRoot(
        const Rect2d& queryBounds) const;

    // 获取障碍物在节点中的覆盖状态
    JAGUARCORE_API HazardPresence PresenceAt(
        HazardHandle handle, const Rect2d& nodeBounds) const noexcept;
    // 获取节点数量
    JAGUARCORE_API std::size_t NodeCount() const noexcept;
    // 获取查询区域对应的虚拟根边界
    JAGUARCORE_API Rect2d VirtualRootBounds(const Rect2d& queryBounds) const noexcept;
    // 获取四叉树边界
    JAGUARCORE_API const Rect2d& Bounds() const noexcept;

private:
    friend class CollisionEngine;

    struct RegisteredHazard
    {
        HazardHandle handle{};    // 障碍物句柄
        Hazard hazard{};          // 障碍物数据
        Rect2d bounds{};          // 障碍物包围盒
        bool hasGeometry = false; // 是否包含多边形几何
    };

    struct Entry
    {
        HazardHandle handle{};                          // 障碍物句柄
        HazardPresence presence = HazardPresence::None; // 覆盖状态
        std::size_t edgeCount = 0;                      // 边数量
    };

    struct Node
    {
        Rect2d bounds;                                   // 节点边界
        std::uint8_t depth = 0;                          // 节点深度
        std::vector<Entry> entries;                      // 节点条目
        std::array<std::unique_ptr<Node>, 4> children{}; // 子节点
    };

    struct PresenceInfo
    {
        HazardPresence presence = HazardPresence::None; // 覆盖状态
        std::size_t edgeCount = 0;                      // 边数量
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
    bool HasEntirePresenceForCircle(
        HazardHandle handle, const Circle2d& circle) const noexcept;
    bool HasEntirePresenceForCircleAtDepth(
        HazardHandle handle,
        const Circle2d& circle,
        std::uint8_t maxDepth) const noexcept;
    bool HasEntirePresenceForEdgeAtDepth(
        HazardHandle handle,
        const Edge2d& edge,
        std::uint8_t maxDepth) const noexcept;
    void SetRegistered(RegisteredHazard hazard);
    void Rebuild();

    Node m_Root;                             // 根节点
    std::uint8_t m_MaxDepth = 0;             // 最大深度
    std::uint8_t m_EdgeThreshold = 0;        // 边数量阈值
    std::vector<RegisteredHazard> m_Hazards; // 已注册障碍物
};
}
