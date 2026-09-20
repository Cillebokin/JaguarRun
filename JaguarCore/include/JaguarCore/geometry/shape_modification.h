#pragma once

#include "JaguarCore/export.h"
#include "JaguarCore/geometry/simple_polygon.h"
#include "JaguarCore/result.h"

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

JAGUARCORE_API Result<SimplePolygon> simplifyShape(const SimplePolygon& shape,
                                                   ShapeModifyMode mode,
                                                   double maxAreaChangeRatio);

JAGUARCORE_API bool shapeModificationValid(const SimplePolygon& original,
                                           const SimplePolygon& modified,
                                           ShapeModifyMode mode) noexcept;

JAGUARCORE_API Result<SimplePolygon> offsetShape(const SimplePolygon& shape,
                                                 ShapeModifyMode mode,
                                                 double distance);

// Returns every valid outer component produced by the straight-skeleton
// offset. The legacy offsetShape() API keeps jagua-rs compatibility by
// selecting the first component.
JAGUARCORE_API Result<std::vector<SimplePolygon>> offsetShapes(
    const SimplePolygon& shape, ShapeModifyMode mode, double distance);

JAGUARCORE_API SimplePolygon closeNarrowConcavities(
    const SimplePolygon& shape,
    ShapeModifyMode mode,
    std::pair<double, double> cutoff);
}
