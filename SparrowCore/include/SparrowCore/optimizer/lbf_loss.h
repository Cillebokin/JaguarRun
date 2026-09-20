#pragma once

#include "SparrowCore/export.h"

#include "JaguarCore/geometry/rect.h"

namespace sparrow
{
struct SPARROWCORE_API LbfLoss
{
    float xMax = 0.0F;
    float yMax = 0.0F;

    static LbfLoss fromBoundingBox(const jaguar::Rect2d& bbox) noexcept;

    float cost() const noexcept;
    jaguar::Rect2d tightenSampleBBox(const jaguar::Rect2d& sample) const noexcept;
};

bool operator<(const LbfLoss& left, const LbfLoss& right) noexcept;
}
