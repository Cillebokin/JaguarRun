#include "JaguarCore/geometry/shape_modification.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace jaguar
{
namespace
{
double subArea(const std::vector<Point2d>& points, std::size_t start, std::size_t end)
{
    std::vector<Point2d> sub;
    for (std::size_t i = start; i < end; ++i)
        sub.push_back(points[i]);

    if (sub.size() < 3)
        return 0.0;

    double area = 0.0;
    for (std::size_t i = 0; i < sub.size(); ++i)
    {
        const std::size_t next = (i + 1) % sub.size();
        area += sub[i].x * sub[next].y - sub[next].x * sub[i].y;
    }
    return 0.5 * area;
}

bool candidateCrossesNonIncidentEdge(const std::vector<Point2d>& points,
                                     std::size_t start,
                                     std::size_t end)
{
    const auto candidate = Edge2d::create(points[start], points[end]);
    if (!candidate.hasValue())
        return true;

    for (std::size_t i = 0; i < points.size(); ++i)
    {
        const std::size_t next = (i + 1) % points.size();
        if (i == start || i == end || next == start || next == end)
            continue;

        const auto edge = Edge2d::create(points[i], points[next]);
        if (!edge.hasValue() || candidate.value().intersects(edge.value()))
            return true;
    }
    return false;
}
}

JAGUARCORE_API SimplePolygon closeNarrowConcavities(
    const SimplePolygon& original,
    ShapeModifyMode mode,
    std::pair<double, double> cutoff)
{
    if (!std::isfinite(cutoff.first) || !std::isfinite(cutoff.second) ||
        cutoff.first < 0.0 || cutoff.second < 0.0)
        return original;

    SimplePolygon current = original;
    for (std::size_t pass = 0; pass < original.vertexCount(); ++pass)
    {
        const auto& points = current.vertices();
        std::optional<std::pair<std::size_t, std::size_t>> best;

        for (std::size_t start = 0; start < points.size(); ++start)
        {
            for (std::size_t end = start + 2; end < points.size(); ++end)
            {
                if (start == 0 && end + 1 == points.size())
                    continue;

                const auto candidate = Edge2d::create(points[start], points[end]);
                if (!candidate.hasValue() ||
                    candidate.value().length() > cutoff.first * current.diameter() ||
                    candidateCrossesNonIncidentEdge(points, start, end))
                    continue;

                const double area = subArea(points, start, end);
                const bool correctDirection =
                    mode == ShapeModifyMode::Inflate ? area < 0.0 : area > 0.0;
                if (!correctDirection ||
                    std::abs(area) > cutoff.second * current.area())
                    continue;

                if (!best.has_value() ||
                    (end - start) > (best->second - best->first))
                    best = std::make_pair(start, end);
            }
        }

        if (!best.has_value())
            break;

        std::vector<Point2d> reduced;
        reduced.reserve(points.size() - (best->second - best->first - 1));
        for (std::size_t i = 0; i < points.size(); ++i)
        {
            if (i <= best->first || i >= best->second)
                reduced.push_back(points[i]);
        }

        const auto next = SimplePolygon::create(std::move(reduced));
        if (!next.hasValue())
            break;
        current = next.value();
    }

    return current;
}
}
