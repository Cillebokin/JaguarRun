#pragma once

#include "JaguarCore/Collision/Hazard.h"

#include <functional>

namespace jaguar
{
// 障碍物过滤器
struct HazardFilter
{
    std::function<bool(const HazardEntity&)> irrelevant;                     // 按实体过滤
    std::function<bool(HazardHandle, const HazardEntity&)> irrelevantHandle; // 按句柄过滤

    // 判断实体是否应被忽略
    inline bool IsIrrelevant(const HazardEntity& entity) const
    {
        return irrelevant && irrelevant(entity);
    }

    // 判断句柄对应的实体是否应被忽略
    inline bool IsIrrelevant(HazardHandle handle, const HazardEntity& entity) const
    {
        if (irrelevantHandle && irrelevantHandle(handle, entity))
            return true;
        return IsIrrelevant(entity);
    }
};

// 不过滤任何障碍物
struct NoFilter : HazardFilter
{
    // 创建空过滤器
    inline NoFilter()
    {
        irrelevant = [](const HazardEntity&) { return false; };
    }
};
}
