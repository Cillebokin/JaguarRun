#pragma once

#include "SparrowCore/Random/SparrowRng.h"
#include "SparrowCore/ResultAlias.h"

#include "JaguarCore/Entities/Item.h"
#include "JaguarCore/Geometry/Rect.h"
#include "JaguarCore/Geometry/Transformation.h"

#include <optional>
#include <vector>

namespace sparrow
{
class SPARROWCORE_API UniformRectSampler
{
public:
    static Result<std::optional<UniformRectSampler>> Create(
        jaguar::Rect2d bbox, const jaguar::Item& item);
    static Result<std::optional<UniformRectSampler>> Create(
        jaguar::Rect2d sampleBbox,
        const jaguar::Item& item,
        jaguar::Rect2d containerBbox);

    jaguar::Transform2d Sample(SparrowRng& rng) const noexcept;
    jaguar::Rect2d BoundingBox() const noexcept;

private:
    struct RotationEntry
    {
        float rotation = 0.0F;
        float xMin = 0.0F;
        float xMax = 0.0F;
        float yMin = 0.0F;
        float yMax = 0.0F;
    };

    UniformRectSampler(jaguar::Rect2d bbox,
                       std::vector<RotationEntry> entries);

    jaguar::Rect2d m_Bbox{};
    std::vector<RotationEntry> m_Entries;
};
}
