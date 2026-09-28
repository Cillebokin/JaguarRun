#pragma once

#include "JaguarCore/Export.h"
#include "JaguarCore/Geometry/Circle.h"
#include "JaguarCore/Geometry/Edge.h"
#include "JaguarCore/Geometry/SimplePolygon.h"
#include "JaguarCore/Geometry/Transformation.h"
#include "JaguarCore/Result.h"

#include <array>
#include <cstddef>
#include <vector>

namespace jaguar
{
// 快速碰撞代理的生成配置
struct SurrogateConfig
{
    // 极点数量上限及覆盖率阈值
    std::array<std::pair<std::size_t, double>, 3> poleLimits{{{0, 0.0}, {0, 0.0}, {0, 0.0}}};
    double failFastPoleAreaRatio = 0.0; // 快速筛选极点面积比例
    std::size_t failFastPiers = 0;      // 快速筛选边段数量

    // 判断两个配置是否相等
    friend inline bool operator==(const SurrogateConfig& lhs,
                                  const SurrogateConfig& rhs) noexcept
    {
        if (lhs.failFastPiers != rhs.failFastPiers ||
            static_cast<float>(lhs.failFastPoleAreaRatio) !=
                static_cast<float>(rhs.failFastPoleAreaRatio))
            return false;

        for (std::size_t index = 0; index < lhs.poleLimits.size(); ++index)
        {
            if (lhs.poleLimits[index].first != rhs.poleLimits[index].first ||
                static_cast<float>(lhs.poleLimits[index].second) !=
                    static_cast<float>(rhs.poleLimits[index].second))
                return false;
        }
        return true;
    }

    // 判断两个配置是否不等
    friend inline bool operator!=(const SurrogateConfig& lhs,
                                  const SurrogateConfig& rhs) noexcept
    {
        return !(lhs == rhs);
    }
};

// 多边形的快速碰撞代理
class Surrogate
{
public:
    // 根据多边形和配置创建代理
    static JAGUARCORE_API Result<Surrogate> Create(const SimplePolygon& shape,
                                                   const SurrogateConfig& config);

    // 获取极点圆列表
    JAGUARCORE_API const std::vector<Circle2d>& Poles() const noexcept;
    // 获取边段列表
    JAGUARCORE_API const std::vector<Edge2d>& Piers() const noexcept;
    // 获取用于快速筛选的极点数量
    JAGUARCORE_API std::size_t FailFastPoleCount() const noexcept;
    // 获取原多边形凸包面积
    JAGUARCORE_API double ConvexHullArea() const noexcept;
    // 获取构成原多边形凸包的顶点索引
    JAGUARCORE_API const std::vector<std::size_t>& ConvexHullIndices() const noexcept;
    // 获取代理配置
    JAGUARCORE_API const SurrogateConfig& Config() const noexcept;
    // 应用变换并返回新的代理
    JAGUARCORE_API Result<Surrogate> Transformed(const Transform2d& transform) const;
    // 按 Rust f32 精度规则应用变换
    JAGUARCORE_API Result<Surrogate> TransformedRustF32(
        const Transform2d& transform) const;

private:
    Surrogate(std::vector<Circle2d> poles,
              std::vector<Edge2d> piers,
              std::size_t failFastPoleCount,
              std::vector<std::size_t> convexHullIndices,
              double convexHullArea,
              SurrogateConfig config);

    std::vector<Circle2d> m_Poles;       // 极点圆列表
    std::vector<Edge2d> m_Piers;         // 边段列表
    std::size_t m_FailFastPoleCount = 0; // 快速筛选极点数量
    std::vector<std::size_t> m_ConvexHullIndices; // 凸包顶点索引
    double m_ConvexHullArea = 0.0;       // 原多边形凸包面积
    SurrogateConfig m_Config{};          // 代理配置
};
}
