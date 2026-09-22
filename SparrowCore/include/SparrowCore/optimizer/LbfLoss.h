#pragma once

#include "SparrowCore/Export.h"

#include "JaguarCore/Geometry/Rect.h"

namespace sparrow
{
struct SPARROWCORE_API LbfLoss
{
    float xMax = 0.0F;
    float yMax = 0.0F;

    static LbfLoss FromBoundingBox(const jaguar::Rect2d& bbox) noexcept;

    float Cost() const noexcept;
    jaguar::Rect2d TightenSampleBBox(const jaguar::Rect2d& sample) const noexcept;
};

bool operator<(const LbfLoss& left, const LbfLoss& right) noexcept;
}
