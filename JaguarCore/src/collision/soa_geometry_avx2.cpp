#include "soa_geometry.h"

#include <immintrin.h>

namespace jaguar
{
bool intersectsBoundaryAvx2Impl(const PolygonSoA& polygon,
                                const Edge2d& edge,
                                double tolerance) noexcept
{
    const Rect2d edgeBounds = edge.boundingBox();
    const __m256d queryMinX = _mm256_set1_pd(edgeBounds.xMin - tolerance);
    const __m256d queryMaxX = _mm256_set1_pd(edgeBounds.xMax + tolerance);
    const __m256d queryMinY = _mm256_set1_pd(edgeBounds.yMin - tolerance);
    const __m256d queryMaxY = _mm256_set1_pd(edgeBounds.yMax + tolerance);
    const std::size_t count = polygon.edgeCount();
    std::size_t i = 0;
    for (; i + 4 <= count; i += 4)
    {
        const __m256d minX = _mm256_loadu_pd(polygon.m_EdgeMinX.data() + i);
        const __m256d maxX = _mm256_loadu_pd(polygon.m_EdgeMaxX.data() + i);
        const __m256d minY = _mm256_loadu_pd(polygon.m_EdgeMinY.data() + i);
        const __m256d maxY = _mm256_loadu_pd(polygon.m_EdgeMaxY.data() + i);
        const __m256d overlapX = _mm256_and_pd(
            _mm256_cmp_pd(maxX, queryMinX, _CMP_GE_OQ),
            _mm256_cmp_pd(minX, queryMaxX, _CMP_LE_OQ));
        const __m256d overlapY = _mm256_and_pd(
            _mm256_cmp_pd(maxY, queryMinY, _CMP_GE_OQ),
            _mm256_cmp_pd(minY, queryMaxY, _CMP_LE_OQ));
        const int mask = _mm256_movemask_pd(_mm256_and_pd(overlapX, overlapY));
        for (std::size_t lane = 0; lane < 4; ++lane)
        {
            if ((mask & (1 << lane)) != 0 &&
                polygon.edge(i + lane).intersects(edge, tolerance))
                return true;
        }
    }

    for (; i < count; ++i)
    {
        const Rect2d candidateBounds = polygon.edge(i).boundingBox();
        if (candidateBounds.xMax < edgeBounds.xMin - tolerance ||
            edgeBounds.xMax < candidateBounds.xMin - tolerance ||
            candidateBounds.yMax < edgeBounds.yMin - tolerance ||
            edgeBounds.yMax < candidateBounds.yMin - tolerance)
            continue;
        if (polygon.edge(i).intersects(edge, tolerance))
            return true;
    }
    return false;
}
}
