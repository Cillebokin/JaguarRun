#include "JaguarCore/Geometry/ShapeModification.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace jaguar
{
namespace
{
enum class CandidateKind
{
    Concave,
    ConvexConvex,
    Collinear
};

struct Corner
{
    std::size_t previous = 0U;
    std::size_t current = 0U;
    std::size_t next = 0U;
};

struct Candidate
{
    CandidateKind kind = CandidateKind::Collinear;
    Corner first{};
    Corner second{};
};

enum class CornerKind
{
    Concave,
    Convex,
    Collinear
};

float asRustF32(double value) noexcept
{
    return static_cast<float>(value);
}

float signedAreaRustF32(const std::vector<Point2d>& points) noexcept
{
    float sum = 0.0F;
    for (std::size_t i = 0U; i < points.size(); ++i)
    {
        const std::size_t next = (i + 1U) % points.size();
        const float x = asRustF32(points[i].x);
        const float y = asRustF32(points[i].y);
        const float nextX = asRustF32(points[next].x);
        const float nextY = asRustF32(points[next].y);
        sum += (y + nextY) * (x - nextX);
    }
    return 0.5F * sum;
}

CornerKind cornerKind(const std::vector<Point2d>& points,
                      const Corner& corner) noexcept
{
    const Point2d& first = points[corner.previous];
    const Point2d& second = points[corner.current];
    const Point2d& third = points[corner.next];
    const float firstX = asRustF32(second.x) - asRustF32(first.x);
    const float firstY = asRustF32(second.y) - asRustF32(first.y);
    const float secondX = asRustF32(third.x) - asRustF32(first.x);
    const float secondY = asRustF32(third.y) - asRustF32(first.y);
    const float crossProduct = firstX * secondY - firstY * secondX;
    if (crossProduct < 0.0F)
        return CornerKind::Concave;
    if (crossProduct > 0.0F)
        return CornerKind::Convex;
    return CornerKind::Collinear;
}

std::optional<Point2d> replacingVertex(const std::vector<Point2d>& points,
                                       const Corner& first,
                                       const Corner& second) noexcept
{
    if (first.next != second.current || first.current != second.previous)
        return std::nullopt;

    const float x1 = asRustF32(points[first.previous].x);
    const float y1 = asRustF32(points[first.previous].y);
    const float x2 = asRustF32(points[first.current].x);
    const float y2 = asRustF32(points[first.current].y);
    const float x3 = asRustF32(points[second.next].x);
    const float y3 = asRustF32(points[second.next].y);
    const float x4 = asRustF32(points[second.current].x);
    const float y4 = asRustF32(points[second.current].y);

    const float tNumerator = (x2 - x4) * (y4 - y3) -
                             (y2 - y4) * (x4 - x3);
    const float tDenominator = (x2 - x1) * (y4 - y3) -
                               (y2 - y1) * (x4 - x3);
    const float uNumerator = (x2 - x4) * (y2 - y1) -
                             (y2 - y4) * (x2 - x1);
    const float uDenominator = (x2 - x1) * (y4 - y3) -
                               (y2 - y1) * (x4 - x3);

    const float t = tDenominator == 0.0F ? 0.0F : tNumerator / tDenominator;
    const float u = uDenominator == 0.0F ? 0.0F : uNumerator / uDenominator;
    if (t < 0.0F && u < 0.0F)
    {
        return Point2d{
            static_cast<double>(x2 + t * (x1 - x2)),
            static_cast<double>(y2 + t * (y1 - y2))};
    }
    return std::nullopt;
}

float triangleArea(const Point2d& first,
                   const Point2d& second,
                   const Point2d& third) noexcept
{
    const float x0 = asRustF32(first.x);
    const float y0 = asRustF32(first.y);
    const float x1 = asRustF32(second.x);
    const float y1 = asRustF32(second.y);
    const float x2 = asRustF32(third.x);
    const float y2 = asRustF32(third.y);
    const float signedArea =
        (x0 * y1 + x1 * y2 + x2 * y0 - x0 * y2 - x1 * y0 - x2 * y1) /
        2.0F;
    return std::abs(signedArea);
}

float candidateAreaDelta(const std::vector<Point2d>& points,
                         const Candidate& candidate) noexcept
{
    if (candidate.kind == CandidateKind::Collinear)
        return 0.0F;

    if (candidate.kind == CandidateKind::Concave)
    {
        return triangleArea(points[candidate.first.previous],
                            points[candidate.first.current],
                            points[candidate.first.next]);
    }

    const auto replacement = replacingVertex(
        points, candidate.first, candidate.second);
    if (!replacement)
        return std::numeric_limits<float>::infinity();
    return triangleArea(points[candidate.first.current], *replacement,
                        points[candidate.second.current]);
}

bool hasAffectedEndpoint(const Edge2d& edge,
                         const std::array<Point2d, 4>& affected,
                         std::size_t affectedCount) noexcept
{
    for (std::size_t i = 0U; i < affectedCount; ++i)
    {
        if (edge.start == affected[i] || edge.end == affected[i])
            return true;
    }
    return false;
}

bool candidateIsValid(const std::vector<Point2d>& points,
                      const Candidate& candidate) noexcept
{
    if (candidate.kind == CandidateKind::Collinear)
        return true;

    if (candidate.kind == CandidateKind::Concave)
    {
        const Corner& corner = candidate.first;
        const Edge2d newEdge{points[corner.previous], points[corner.next]};
        const std::array<Point2d, 4> affected{
            points[corner.previous], points[corner.current],
            points[corner.next], Point2d{}};
        for (std::size_t i = 0U; i < points.size(); ++i)
        {
            const Edge2d edge{points[i], points[(i + 1U) % points.size()]};
            if (hasAffectedEndpoint(edge, affected, 3U))
                continue;
            if (edge.IntersectsRustF32(newEdge))
                return false;
        }
        return true;
    }

    const auto replacement = replacingVertex(
        points, candidate.first, candidate.second);
    if (!replacement)
        return false;

    const Edge2d firstNewEdge{points[candidate.first.previous], *replacement};
    const Edge2d secondNewEdge{*replacement, points[candidate.second.next]};
    const std::array<Point2d, 4> affected{
        points[candidate.first.current], points[candidate.first.previous],
        points[candidate.second.current], points[candidate.second.next]};
    for (std::size_t i = 0U; i < points.size(); ++i)
    {
        const Edge2d edge{points[i], points[(i + 1U) % points.size()]};
        if (hasAffectedEndpoint(edge, affected, affected.size()))
            continue;
        if (edge.IntersectsRustF32(firstNewEdge) ||
            edge.IntersectsRustF32(secondNewEdge))
        {
            return false;
        }
    }
    return true;
}

std::vector<Point2d> executeCandidate(const std::vector<Point2d>& points,
                                      const Candidate& candidate)
{
    std::vector<Point2d> result = points;
    if (candidate.kind != CandidateKind::ConvexConvex)
    {
        result.erase(result.begin() +
                     static_cast<std::ptrdiff_t>(candidate.first.current));
        return result;
    }

    const Point2d replacement = *replacingVertex(
        points, candidate.first, candidate.second);
    const std::size_t otherIndex =
        candidate.first.current < candidate.second.current
            ? candidate.second.current - 1U
            : candidate.second.current;
    result.erase(result.begin() +
                 static_cast<std::ptrdiff_t>(candidate.first.current));
    result.erase(result.begin() + static_cast<std::ptrdiff_t>(otherIndex));
    result.insert(result.begin() + static_cast<std::ptrdiff_t>(otherIndex),
                  replacement);
    return result;
}

std::vector<Candidate> generateCandidates(const std::vector<Point2d>& points,
                                          ShapeModifyMode mode)
{
    std::vector<Corner> corners;
    corners.reserve(points.size());
    for (std::size_t i = 0U; i < points.size(); ++i)
    {
        corners.push_back(Corner{
            i == 0U ? points.size() - 1U : i - 1U,
            i,
            (i + 1U) % points.size()});
    }

    if (mode == ShapeModifyMode::Deflate)
    {
        std::reverse(corners.begin(), corners.end());
        for (Corner& corner : corners)
            std::swap(corner.previous, corner.next);
    }

    std::vector<Candidate> candidates;
    Corner previousCorner = corners.back();
    CornerKind previousKind = cornerKind(points, previousCorner);
    for (const Corner& corner : corners)
    {
        const CornerKind kind = cornerKind(points, corner);
        if (kind == CornerKind::Concave)
            candidates.push_back({CandidateKind::Concave, corner, {}});
        else if (kind == CornerKind::Collinear)
            candidates.push_back({CandidateKind::Collinear, corner, {}});
        else if (previousKind == CornerKind::Convex)
            candidates.push_back(
                {CandidateKind::ConvexConvex, previousCorner, corner});

        previousCorner = corner;
        previousKind = kind;
    }
    return candidates;
}
}

JAGUARCORE_API bool ShapeModificationValid(const SimplePolygon& original,
                                           const SimplePolygon& modified,
                                           ShapeModifyMode mode) noexcept
{
    const float edgeTolerance =
        static_cast<float>(modified.Diameter()) * 1.0e-6F;
    for (const Point2d& point : original.Vertices())
    {
        bool onModifiedEdge = false;
        for (const Edge2d& edge : modified.Edges())
        {
            if (static_cast<float>(edge.DistanceToRustF32(point)) <
                edgeTolerance)
            {
                onModifiedEdge = true;
                break;
            }
        }

        if (onModifiedEdge)
            continue;

        const bool insideModified = modified.ContainsRustF32(point);
        if (mode == ShapeModifyMode::Inflate && !insideModified)
            return false;
        if (mode == ShapeModifyMode::Deflate && insideModified)
            return false;
    }
    return true;
}

JAGUARCORE_API Result<SimplePolygon> SimplifyShape(const SimplePolygon& shape,
                                                   ShapeModifyMode mode,
                                                   double maxAreaChangeRatio)
{
    std::vector<Point2d> points = shape.Vertices();
    const float originalArea = asRustF32(shape.Area());
    const float maxAreaChangeRatioF32 = asRustF32(maxAreaChangeRatio);

    for (std::size_t iteration = 0U; iteration < shape.VertexCount(); ++iteration)
    {
        if (points.size() < 4U)
            break;

        std::vector<Candidate> candidates = generateCandidates(points, mode);
        std::stable_sort(candidates.begin(), candidates.end(),
                         [&points](const Candidate& first,
                                   const Candidate& second) {
                             return candidateAreaDelta(points, first) <
                                    candidateAreaDelta(points, second);
                         });
        const auto bestCandidate = std::find_if(
            candidates.begin(), candidates.end(),
            [&points](const Candidate& candidate) {
                return candidateIsValid(points, candidate);
            });
        if (bestCandidate == candidates.end())
            break;

        std::vector<Point2d> newPoints = executeCandidate(points, *bestCandidate);
        const float newShapeArea = signedAreaRustF32(newPoints);
        const float areaDelta = std::abs(newShapeArea - originalArea) /
                                originalArea;
        if (!(areaDelta <= maxAreaChangeRatioF32))
            break;
        points = std::move(newPoints);
    }

    return SimplePolygon::Create(std::move(points));
}
}
