#pragma once

#include "SparrowCore/random/sparrow_rng.h"
#include "SparrowCore/result_alias.h"
#include "SparrowCore/sampling/rotation_distribution.h"

#include "JaguarCore/entities/item.h"
#include "JaguarCore/geometry/rect.h"
#include "JaguarCore/geometry/transformation.h"

namespace sparrow
{
class SPARROWCORE_API UniformRectSampler
{
public:
    static Result<UniformRectSampler> create(
        jaguar::Rect2d bbox, const jaguar::Item& item);

    jaguar::Transform2d sample(SparrowRng& rng) const noexcept;
    jaguar::Rect2d bbox() const noexcept;

private:
    UniformRectSampler(jaguar::Rect2d bbox,
                       RotationDistribution rotationDistribution,
                       float xMin,
                       float xMax,
                       float yMin,
                       float yMax);

    jaguar::Rect2d m_Bbox{};
    RotationDistribution m_RotationDistribution;
    float m_XMin = 0.0F;
    float m_XMax = 0.0F;
    float m_YMin = 0.0F;
    float m_YMax = 0.0F;
};
}
