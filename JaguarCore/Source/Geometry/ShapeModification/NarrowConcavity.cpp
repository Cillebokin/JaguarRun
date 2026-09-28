#include "JaguarCore/Geometry/ShapeModification.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace jaguar
{
namespace
{
float asRustF32(double value) noexcept
{
    return static_cast<float>(value);
}

Edge2d candidateEdgeRustF32(const std::vector<Point2d>& points,
                            std::size_t start,
                            std::size_t end) noexcept
{
    const float startX = asRustF32(points[start].x);
    const float startY = asRustF32(points[start].y);
    const float endX = asRustF32(points[end].x);
    const float endY = asRustF32(points[end].y);
    const float dx = endX - startX;
    const float dy = endY - startY;
    const float factor = 0.9999F;
    const float halfScale = (factor - 1.0F) / 2.0F;

    return Edge2d{
        {static_cast<double>(startX - dx * halfScale),
         static_cast<double>(startY - dy * halfScale)},
        {static_cast<double>(endX + dx * halfScale),
         static_cast<double>(endY + dy * halfScale)}};
}

float edgeLengthRustF32(const Edge2d& edge) noexcept
{
    const float dx = asRustF32(edge.end.x) - asRustF32(edge.start.x);
    const float dy = asRustF32(edge.end.y) - asRustF32(edge.start.y);
    return std::sqrt(dx * dx + dy * dy);
}

float subAreaRustF32(const std::vector<Point2d>& points,
                     std::size_t start,
                     std::size_t end)
{
    std::vector<Point2d> sub;
    sub.reserve(points.size());
    if (end > start)
    {
        for (std::size_t i = start; i < end; ++i)
            sub.push_back(points[i]);
    }
    else
    {
        for (std::size_t i = start; i < points.size(); ++i)
            sub.push_back(points[i]);
        for (std::size_t i = 0U; i < end; ++i)
            sub.push_back(points[i]);
    }

    float sum = 0.0F;
    for (std::size_t i = 0U; i < sub.size(); ++i)
    {
        const std::size_t next = (i + 1U) % sub.size();
        const float x = asRustF32(sub[i].x);
        const float y = asRustF32(sub[i].y);
        const float nextX = asRustF32(sub[next].x);
        const float nextY = asRustF32(sub[next].y);
        sum += (y + nextY) * (x - nextX);
    }
    return 0.5F * sum;
}
}

JAGUARCORE_API SimplePolygon CloseNarrowConcavities(
    const SimplePolygon& original,
    ShapeModifyMode mode,
    std::pair<double, double> cutoff)
{
    SimplePolygon current = original;
    for (std::size_t pass = 0; pass < original.VertexCount(); ++pass)
    {
        const auto& points = current.Vertices();
        std::optional<std::pair<std::size_t, std::size_t>> best;
        std::size_t bestVertexElimination = 0U;
        const float cutoffDistanceRatio = asRustF32(cutoff.first);
        const float cutoffAreaRatio = asRustF32(cutoff.second);
        const float diameter = asRustF32(current.Diameter());
        const float area = asRustF32(current.Area());

        for (std::size_t start = 0U; start < points.size(); ++start)
        {
            for (std::size_t end = 0U; end < points.size(); ++end)
            {
                if (start == end || (start + 1U) % points.size() == end ||
                    (end + 1U) % points.size() == start)
                    continue;

                const Edge2d candidate = candidateEdgeRustF32(points, start, end);
                if (edgeLengthRustF32(candidate) > cutoffDistanceRatio * diameter)
                    continue;

                const bool startInside = current.ContainsRustF32(candidate.start);
                const bool endInside = current.ContainsRustF32(candidate.end);
                if (mode == ShapeModifyMode::Inflate &&
                    (startInside || endInside))
                    continue;
                if (mode == ShapeModifyMode::Deflate &&
                    !(startInside && endInside))
                    continue;

                bool intersectsShapeEdge = false;
                for (std::size_t edgeIndex = 0U; edgeIndex < points.size(); ++edgeIndex)
                {
                    const Edge2d edge{
                        points[edgeIndex],
                        points[(edgeIndex + 1U) % points.size()]};
                    if (edge.IntersectsRustF32(candidate))
                    {
                        intersectsShapeEdge = true;
                        break;
                    }
                }
                if (intersectsShapeEdge)
                    continue;

                const float subArea = subAreaRustF32(points, start, end);
                if (subArea >= 0.0F ||
                    std::abs(subArea) > cutoffAreaRatio * area)
                    continue;

                const std::size_t eliminated = end > start
                    ? end - start - 1U
                    : points.size() - start + end - 1U;

                if (!best.has_value() || eliminated > bestVertexElimination)
                {
                    best = std::make_pair(start, end);
                    bestVertexElimination = eliminated;
                }
            }
        }

        if (!best.has_value())
            break;

        std::vector<Point2d> reduced;
        reduced.reserve(points.size() - bestVertexElimination);
        for (std::size_t i = 0U; i < points.size(); ++i)
        {
            const bool eliminated = best->second > best->first
                ? i > best->first && i < best->second
                : i > best->first || i < best->second;
            if (!eliminated)
                reduced.push_back(points[i]);
        }

        const auto next = SimplePolygon::Create(std::move(reduced));
        if (!next.HasValue())
            break;
        current = next.Value();
    }

    return current;
}
}
