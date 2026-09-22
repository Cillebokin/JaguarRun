#pragma once

#include "SparrowCore/Random/SparrowRng.h"
#include "SparrowCore/ResultAlias.h"
#include "SparrowCore/Sampling/RotationDistribution.h"

#include "JaguarCore/Entities/Item.h"
#include "JaguarCore/Geometry/Rect.h"
#include "JaguarCore/Geometry/Transformation.h"

namespace sparrow
{
class SPARROWCORE_API UniformRectSampler
{
public:
    static Result<UniformRectSampler> Create(
        jaguar::Rect2d bbox, const jaguar::Item& item);

    jaguar::Transform2d Sample(SparrowRng& rng) const noexcept;
    jaguar::Rect2d BoundingBox() const noexcept;

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
