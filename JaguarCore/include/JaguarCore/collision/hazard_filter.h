#pragma once

#include "JaguarCore/collision/hazard.h"

#include <functional>

namespace jaguar
{
struct HazardFilter
{
    std::function<bool(const HazardEntity&)> irrelevant;
    std::function<bool(HazardHandle, const HazardEntity&)> irrelevantHandle;

    bool isIrrelevant(const HazardEntity& entity) const
    {
        return irrelevant && irrelevant(entity);
    }

    bool isIrrelevant(HazardHandle handle, const HazardEntity& entity) const
    {
        if (irrelevantHandle && irrelevantHandle(handle, entity))
            return true;
        return isIrrelevant(entity);
    }
};

struct NoFilter : HazardFilter
{
    NoFilter() { irrelevant = [](const HazardEntity&) { return false; }; }
};
}
