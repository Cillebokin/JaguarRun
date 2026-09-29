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
class CollisionEngine;

// 障碍物相对节点的覆盖状态
enum class HazardPresence
{
    None,    // 不覆盖
    Partial, // 部分覆盖
    Entire,  // 完全覆盖
};

// 碰撞障碍物的四叉树索引
// 逐行移植 jagua-rs quadtree：节点持有带边子集的 QTHazard 列表（按
// presence 降序排序），懒扩展子节点，增量 constrict/deregister。
class QuadTree
{
public:
    // 创建四叉树
    JAGUARCORE_API QuadTree(Rect2d bounds,
                            std::uint8_t quadtreeDepth,
                            std::uint8_t cdThreshold);

    // 注册完整障碍物（以根节点 Partial presence 起步，按需向子节点 constrict）
    JAGUARCORE_API void RegisterHazard(HazardHandle handle, const Hazard& hazard);
    // 注册障碍物数据
    JAGUARCORE_API void RegisterHazard(
        HazardHandle handle, const HazardEntity& entity,
        std::shared_ptr<const SimplePolygon> shape, bool dynamic);
    // 仅使用保守包围盒注册障碍物
    // 没有多边形几何时统一标记为部分覆盖
    JAGUARCORE_API void RegisterHazard(HazardHandle handle,
                                       const Rect2d& hazardBounds);
    // 注销障碍物（子节点递归移除，无 Partial hazard 时折叠子节点）
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

    // 部分覆盖节点内的边子集（jagua-rs QTHazPartial）
    struct PartialHazard
    {
        std::vector<Edge2d> edges; // 节点内活跃的边
        Rect2d ffBbox;             // 覆盖所有边的快速失败包围盒
    };

    // 节点内一个障碍物的表示（jagua-rs QTHazard）
    struct NodeHazard
    {
        Rect2d qtBbox;                       // 所在节点包围盒
        HazardHandle handle;                 // 障碍物句柄
        HazardEntity entity;                 // 诱发实体
        HazardPresence presence = HazardPresence::None;
        std::unique_ptr<PartialHazard> partial; // presence == Partial 时有效

        std::size_t EdgeCount() const noexcept
        {
            return presence == HazardPresence::Partial && partial
                       ? partial->edges.size()
                       : 0;
        }
    };

    // 节点（jagua-rs QTNode）
    struct Node
    {
        Rect2d bounds;                                     // 节点包围盒
        std::uint8_t level = 0;                            // 剩余可细分层数
        std::array<std::unique_ptr<Node>, 4> children{};   // 子节点（懒扩展）
        std::vector<NodeHazard> hazards;                   // 按 presence 降序
        std::size_t activeEdges = 0;                       // Partial 边数缓存

        void AddHazard(NodeHazard hazard);
        bool RemoveHazard(HazardHandle handle);
        bool HasPartialHazard() const noexcept;
    };

    // 注册形状登记表（供 constrict 查询完整多边形）
    struct RegisteredShape
    {
        HazardHandle handle;
        std::shared_ptr<const SimplePolygon> shape; // 可为空（bounds-only 注册）
        Rect2d bounds;
        HazardEntity entity;
    };

    static NodeHazard MakeRootHazard(HazardHandle handle,
                                     const Hazard& hazard,
                                     const Rect2d& rootBounds);
    static std::array<NodeHazard, 4> Constrict(
        const NodeHazard& hazard,
        const std::array<Rect2d, 4>& quadrants,
        const SimplePolygon& shape);
    static void RegisterIntoNode(Node& node, NodeHazard hazard,
                                 const std::vector<RegisteredShape>& shapes);
    static void DeregisterFromNode(Node& node, HazardHandle handle);
    static const Node& VirtualRootFor(const Node& root,
                                      const Rect2d& queryBounds) noexcept;
    static std::size_t CountNodes(const Node& node) noexcept;

    const RegisteredShape* Find(HazardHandle handle) const noexcept;
    void SetRegistered(RegisteredShape registered);

    Node m_Root;                                    // 根节点
    std::uint8_t m_CdThreshold = 0;                 // 查询时的活动边数阈值
    std::vector<RegisteredShape> m_Registered;      // 已注册障碍物
};
}
