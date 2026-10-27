#pragma once

#include "JaguarCore/Collision/CollisionEngine.h"
#include "JaguarCore/Export.h"
#include "JaguarCore/Geometry/OriginalShape.h"
#include "JaguarCore/Result.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace jaguar
{

class Importer;
// 容器外部标识
using ContainerId = std::uint64_t;

// 容器质量区域
struct QualityZone
{
    std::size_t quality = 0;           // 质量等级
    std::vector<SimplePolygon> shapes; // 区域形状
};

// 容器实体
class Container
{
public:
    // 创建容器
    static JAGUARCORE_API Result<Container> Create(
        ContainerId id,
        OriginalShape originalOuter,
        std::vector<QualityZone> qualityZones,
        CDEConfig cdeConfig);
    // 禁止创建空容器
    Container() = delete;
    Container(const Container&) noexcept = default;
    Container(Container&&) noexcept = default;
    Container& operator=(const Container&) noexcept = default;
    Container& operator=(Container&&) noexcept = default;
    ~Container() = default;

    // 获取容器标识
    JAGUARCORE_API ContainerId Id() const noexcept;
    // 获取容器面积
    JAGUARCORE_API double Area() const noexcept;
    // 获取原始形状
    JAGUARCORE_API const OriginalShape& GetOriginalShape() const noexcept;
    // 获取碰撞形状
    JAGUARCORE_API const SimplePolygon& CollisionShape() const noexcept;
    // 获取按质量等级分组的质量区域
    JAGUARCORE_API const std::vector<QualityZone>& QualityZones() const noexcept;
    // 获取未执行形状修改的质量区域轮廓
    JAGUARCORE_API const std::vector<QualityZone>& OriginalQualityZones() const noexcept;
    // 获取包围盒
    JAGUARCORE_API const Rect2d& Bounds() const noexcept;
    // 判断是否来自同一基础容器
    JAGUARCORE_API bool SameBase(const Container& other) const noexcept;
    // 创建碰撞引擎
    JAGUARCORE_API CollisionEnginePtr CreateCollisionEngine() const;

private:
    friend class Importer;

    // Importer-only construction path retaining raw zone outlines for SVG.
    static Result<Container> CreateWithOriginalQualityZones(
        ContainerId id,
        OriginalShape originalOuter,
        std::vector<QualityZone> qualityZones,
        std::vector<QualityZone> originalQualityZones,
        bool hasOriginalQualityZones,
        CDEConfig cdeConfig);

    struct Data;

    explicit Container(std::shared_ptr<const Data> data);

    std::shared_ptr<const Data> m_Data; // 共享容器数据
};
}
