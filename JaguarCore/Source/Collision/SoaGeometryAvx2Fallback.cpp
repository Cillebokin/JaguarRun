#include "SoaGeometry.h"

namespace jaguar
{
bool intersectsBoundaryAvx2Impl(const PolygonSoA& polygon,
                                const Edge2d& edge,
                                double tolerance) noexcept
{
    return polygon.intersectsBoundary(edge, tolerance, SoaExecutionMode::Scalar);
}
}
