#pragma once

#include "JaguarCore/Collision/HazardCollector.h"
#include "JaguarCore/Collision/HazardFilter.h"
#include "JaguarCore/Collision/Quadtree.h"
#include "JaguarCore/FailFast/Surrogate.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace jaguar
{
// 批量碰撞查询配置
struct BatchCollisionOptions
{
    std::size_t workerCount = 0; // 工作线程数量
    std::size_t grainSize = 32;  // 每批处理的对象数量
};

// 碰撞查询停止回调
using CollisionStopCallback = std::function<bool(const HazardEntity&)>;

class ThreadPool;
class PolygonSoA;
class Layout;

// 碰撞检测引擎配置
struct CDEConfig
{
    std::uint8_t quadtreeDepth = 4;        // 四叉树最大深度
    std::uint8_t cdThreshold = 32;         // 碰撞检测阈值
    SurrogateConfig itemSurrogateConfig{}; // 物品代理配置
};

class CollisionEngine;

// 碰撞引擎智能指针删除器
struct CollisionEngineDeleter
{
    JAGUARCORE_API void operator()(CollisionEngine* engine) const noexcept;
};

using CollisionEnginePtr = std::unique_ptr<CollisionEngine, CollisionEngineDeleter>;

// 碰撞引擎状态快照
struct CollisionSnapshot
{
    std::vector<Hazard> dynamicHazards; // 动态障碍物
};

// 常量查询可以在同一个引擎上并发执行
// 注册、注销和恢复会修改引擎状态，必须与查询及其他修改操作外部串行化
// 并行查询期间过滤器可能被多个工作线程同时调用
// 碰撞检测引擎
class CollisionEngine
{
public:
    // 创建碰撞检测引擎
    JAGUARCORE_API CollisionEngine(Rect2d bounds, CDEConfig config);
    // 销毁碰撞检测引擎
    JAGUARCORE_API ~CollisionEngine();
    CollisionEngine(const CollisionEngine&) = delete;
    CollisionEngine& operator=(const CollisionEngine&) = delete;

    // 注册障碍物
    JAGUARCORE_API Result<HazardHandle> RegisterHazard(Hazard hazard);
    // 注销障碍物
    JAGUARCORE_API Result<Hazard> DeregisterHazard(HazardHandle handle);

    // 检测单个多边形是否碰撞
    JAGUARCORE_API bool DetectPolygonCollision(const SimplePolygon& shape,
                                               const HazardFilter& filter) const;
    // 批量检测多边形碰撞
    JAGUARCORE_API std::vector<std::uint8_t> DetectPolygonCollisions(
        const std::vector<SimplePolygon>& shapes,
        const HazardFilter& filter = NoFilter{}) const;
    // 并行批量检测多边形碰撞
    JAGUARCORE_API Result<std::vector<std::uint8_t>> DetectPolygonCollisionsParallel(
        const std::vector<SimplePolygon>& shapes,
        const HazardFilter& filter = NoFilter{},
        const BatchCollisionOptions& options = {}) const;
    // 获取多边形碰撞的障碍物
    JAGUARCORE_API std::vector<HazardEntity> CollectPolygonCollisions(
        const SimplePolygon& shape) const;
    // 按过滤器获取多边形碰撞的障碍物
    JAGUARCORE_API std::vector<HazardEntity> CollectPolygonCollisions(
        const SimplePolygon& shape, const HazardFilter& filter) const;
    // 收集多边形碰撞并支持提前停止
    JAGUARCORE_API bool CollectPolygonCollisionsUntil(
        const SimplePolygon& shape,
        HazardCollector& collector,
        const HazardFilter& filter,
        const CollisionStopCallback& stopAfterCollision) const;
    // 收集多边形碰撞并支持提前停止
    JAGUARCORE_API bool CollectPolygonCollisionsUntil(
        const SimplePolygon& shape,
        HazardCollector& collector,
        const CollisionStopCallback& stopAfterCollision) const;

    // 检测快速碰撞代理是否碰撞
    JAGUARCORE_API Result<bool> DetectSurrogateCollision(
        const Surrogate&, const Transform2d&, const HazardFilter&) const;
    // 收集快速碰撞代理碰撞并支持提前停止
    JAGUARCORE_API Result<bool> CollectSurrogateCollisionsUntil(
        const Surrogate& surrogate,
        const Transform2d& transform,
        HazardCollector& collector,
        const HazardFilter& filter,
        const CollisionStopCallback& stopAfterCollision) const;
    // 收集快速碰撞代理碰撞并支持提前停止
    JAGUARCORE_API Result<bool> CollectSurrogateCollisionsUntil(
        const Surrogate& surrogate,
        const Transform2d& transform,
        HazardCollector& collector,
        const CollisionStopCallback& stopAfterCollision) const;

    // 保存碰撞引擎快照
    JAGUARCORE_API CollisionSnapshot Save() const;
    // 恢复碰撞引擎快照
    JAGUARCORE_API Result<void> Restore(const CollisionSnapshot& snapshot);

    // 获取引擎边界
    JAGUARCORE_API const Rect2d& Bounds() const noexcept;

private:
    friend class Layout;

    struct Slot
    {
        std::optional<Hazard> hazard;           // 障碍物
        std::unique_ptr<PolygonSoA> shapeCache; // 多边形缓存
        std::uint32_t generation = 1;           // 句柄代数
    };

    const Hazard* Get(HazardHandle handle) const noexcept;
    Hazard* Get(HazardHandle handle) noexcept;
    std::optional<HazardHandle> FindHazardHandle(
        const HazardEntity& entity) const noexcept;
    const PolygonSoA* GetShapeCache(HazardHandle handle) const noexcept;
    HazardHandle AllocateHandle();
    bool ShapesCollide(const SimplePolygon& first, const SimplePolygon& second) const noexcept;
    bool ShapesCollide(const PolygonSoA& first, const PolygonSoA& second) const noexcept;
    bool CircleCollides(const Circle2d& circle, const SimplePolygon& shape) const noexcept;
    bool EdgeCollides(const Edge2d& edge, const SimplePolygon& shape) const noexcept;
    bool NodeCollidesWithEdge(const QuadTree::Node& node,
                              const Edge2d& edge,
                              const HazardFilter& filter) const noexcept;
    bool NodeCollidesWithCircle(const QuadTree::Node& node,
                                const Circle2d& circle,
                                const HazardFilter& filter) const noexcept;
    bool CollectEdgeCollisionsUntil(
        const QuadTree::Node& node,
        const Edge2d& edge,
        HazardCollector& collector,
        const HazardFilter& filter,
        const CollisionStopCallback& stopAfterCollision,
        const std::function<bool(HazardHandle, const HazardEntity&)>& report) const;
    bool CollectCircleCollisionsUntil(
        const QuadTree::Node& node,
        const Circle2d& circle,
        HazardCollector& collector,
        const HazardFilter& filter,
        const CollisionStopCallback& stopAfterCollision,
        const std::function<bool(HazardHandle, const HazardEntity&)>& report) const;
    ThreadPool& GetThreadPool(std::size_t workerCount) const;

    Rect2d m_Bounds;                                  // 引擎边界
    CDEConfig m_Config;                               // 引擎配置
    QuadTree m_QuadTree;                              // 空间索引
    std::vector<Slot> m_Slots;                        // 障碍物槽位
    std::vector<std::size_t> m_FreeSlotIndices;       // LIFO 空闲槽位
    mutable std::mutex m_ThreadPoolMutex;             // 线程池互斥量
    mutable std::mutex m_ParallelQueryMutex;          // 并行查询互斥量
    mutable std::unique_ptr<ThreadPool> m_ThreadPool; // 线程池
};
}
