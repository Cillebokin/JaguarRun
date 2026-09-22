#pragma once

#include "JaguarCore/Export.h"
#include "JaguarCore/Geometry/SimplePolygon.h"
#include "JaguarCore/Result.h"

#include <optional>
#include <utility>
#include <vector>

namespace jaguar
{
enum class ShapeModifyMode
{
    Inflate,
    Deflate,
};

struct ShapeModifyConfig
{
    std::optional<double> simplifyTolerance;
    std::optional<double> offset;
    std::optional<std::pair<double, double>> narrowConcavityCutoff;
};

JAGUARCORE_API Result<SimplePolygon> SimplifyShape(const SimplePolygon& shape,
                                                   ShapeModifyMode mode,
                                                   double maxAreaChangeRatio);

JAGUARCORE_API bool ShapeModificationValid(const SimplePolygon& original,
                                           const SimplePolygon& modified,
                                           ShapeModifyMode mode) noexcept;

JAGUARCORE_API Result<SimplePolygon> OffsetShape(const SimplePolygon& shape,
                                                 ShapeModifyMode mode,
                                                 double distance);

// Returns every valid outer component produced by the straight-skeleton
// offset. The legacy OffsetShape() API keeps jagua-rs compatibility by
// selecting the first component.
JAGUARCORE_API Result<std::vector<SimplePolygon>> OffsetShapes(
    const SimplePolygon& shape, ShapeModifyMode mode, double distance);

JAGUARCORE_API SimplePolygon CloseNarrowConcavities(
    const SimplePolygon& shape,
    ShapeModifyMode mode,
    std::pair<double, double> cutoff);
}
