#pragma once

#include "JaguarCore/Collision/Hazard.h"

#include <functional>

namespace jaguar
{
struct HazardFilter
{
    std::function<bool(const HazardEntity&)> irrelevant;
    std::function<bool(HazardHandle, const HazardEntity&)> irrelevantHandle;

    inline bool IsIrrelevant(const HazardEntity& entity) const
    {
        return irrelevant && irrelevant(entity);
    }

    inline bool IsIrrelevant(HazardHandle handle, const HazardEntity& entity) const
    {
        if (irrelevantHandle && irrelevantHandle(handle, entity))
            return true;
        return IsIrrelevant(entity);
    }
};

struct NoFilter : HazardFilter
{
    inline NoFilter()
    {
        irrelevant = [](const HazardEntity&) { return false; };
    }
};
}
