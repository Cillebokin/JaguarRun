#pragma once

#include "JaguarCore/Export.h"
#include "JaguarCore/Geometry/SimplePolygon.h"
#include "JaguarCore/Geometry/ShapeModification.h"
#include "JaguarCore/Geometry/Transformation.h"

namespace jaguar
{
// 原始形状及其预处理配置
struct OriginalShape
{
    SimplePolygon shape;                                   // 原始多边形
    Transform2d preTransform = Transform2d::Identity();    // 预变换
    ShapeModifyMode modifyMode = ShapeModifyMode::Inflate; // 形状修改模式
    ShapeModifyConfig modifyConfig{};                      // 形状修改参数

    // 转换为内部多边形
    JAGUARCORE_API Result<SimplePolygon> ConvertToInternal() const;
};
}

