#pragma once

#include "JaguarCore/Export.h"
#include "JaguarCore/Geometry/SimplePolygon.h"
#include "JaguarCore/Result.h"

#include <optional>
#include <utility>
#include <vector>

namespace jaguar
{
// 多边形修改方向
enum class ShapeModifyMode
{
    Inflate, // 向外扩张
    Deflate, // 向内收缩
};

// 多边形修改参数
struct ShapeModifyConfig
{
    std::optional<double> simplifyTolerance;                        // 简化容差
    std::optional<double> offset;                                   // 偏移距离
    std::optional<std::pair<double, double>> narrowConcavityCutoff; // 窄凹陷判定阈值
};

// 按最大面积变化比例简化多边形
JAGUARCORE_API Result<SimplePolygon> SimplifyShape(const SimplePolygon& shape,
                                                   ShapeModifyMode mode,
                                                   double maxAreaChangeRatio);

// 检查修改后的多边形是否有效
JAGUARCORE_API bool ShapeModificationValid(const SimplePolygon& original,
                                           const SimplePolygon& modified,
                                           ShapeModifyMode mode) noexcept;

// 对多边形执行偏移
JAGUARCORE_API Result<SimplePolygon> OffsetShape(const SimplePolygon& shape,
                                                 ShapeModifyMode mode,
                                                 double distance);

// 返回偏移产生的所有有效外轮廓
// OffsetShape 接口保持兼容性，仅返回第一个轮廓
JAGUARCORE_API Result<std::vector<SimplePolygon>> OffsetShapes(
    const SimplePolygon& shape, ShapeModifyMode mode, double distance);

// 闭合多边形中的窄凹陷
JAGUARCORE_API SimplePolygon CloseNarrowConcavities(
    const SimplePolygon& shape,
    ShapeModifyMode mode,
    std::pair<double, double> cutoff);
}
