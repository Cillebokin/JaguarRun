#include "JaguarCore/FailFast/Surrogate.h"

#include "JaguarCore/Geometry/ConvexHull.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <deque>
#include <optional>
#include <utility>

namespace jaguar
{
namespace
{
struct Cell
{
    Rect2d box;
    Point2d center;
    double clearance = 0.0;
    double upperBound = 0.0;
    std::size_t depth = 0;
};

double distanceToBoundary(const SimplePolygon& shape, const Point2d& point)
{
    double distance = std::numeric_limits<double>::max();
    for (const Edge2d& edge : shape.Edges())
        distance = std::min(distance, edge.ClosestPoint(point).SquaredDistanceTo(point));
    return std::sqrt(distance);
}

double signedClearance(const SimplePolygon& shape,
                       const Point2d& point,
                       const std::vector<Circle2d>& excluded)
{
    const double boundaryDistance = distanceToBoundary(shape, point);
    if (!shape.Contains(point))
        return -boundaryDistance;

    double clearance = boundaryDistance;
    for (const Circle2d& circle : excluded)
    {
        clearance = std::min(clearance, std::sqrt(circle.center.SquaredDistanceTo(point)) -
                                             circle.radius);
    }
    return clearance;
}

Rect2d squareSearchBox(const Rect2d& box)
{
    const double side = std::max(box.Width(), box.Height());
    const double dx = (side - box.Width()) * 0.5;
    const double dy = (side - box.Height()) * 0.5;
    return Rect2d{box.xMin - dx, box.yMin - dy, box.xMax + dx, box.yMax + dy};
}

std::optional<Circle2d> computePole(const SimplePolygon& shape,
                                    const std::vector<Circle2d>& excluded)
{
    const Rect2d root = squareSearchBox(shape.BoundingBox());
    const Point2d rootCenter{(root.xMin + root.xMax) * 0.5, (root.yMin + root.yMax) * 0.5};
    const double rootHalfDiagonal = root.Width() * std::sqrt(0.5);

    std::deque<Cell> queue;
    const double rootClearance = signedClearance(shape, rootCenter, excluded);
    queue.push_back(Cell{root, rootCenter, rootClearance,
                         rootClearance + rootHalfDiagonal, 0});

    double bestRadius = 0.0;
    Point2d bestCenter{};
    constexpr std::size_t maxDepth = 10;
    constexpr double minimumRadius = 1.0e-8;

    while (!queue.empty())
    {
        const Cell current = queue.front();
        queue.pop_front();

        if (current.clearance > bestRadius)
        {
            bestRadius = current.clearance;
            bestCenter = current.center;
        }

        if (current.upperBound <= bestRadius)
            continue;

        if (current.depth >= maxDepth)
            continue;

        const auto quadrants = current.box.Quadrants();
        // Rect2d::Quadrants() is ordered for the Cartesian/quadtree API as
        // [bottom-left, bottom-right, top-right, top-left]. jagua-rs's
        // Pole BFS visits [top-right, top-left, bottom-left, bottom-right].
        constexpr std::array<std::size_t, 4> rustOrder{{2, 3, 0, 1}};
        for (const std::size_t quadrantIndex : rustOrder)
        {
            const Rect2d& quadrant = quadrants[quadrantIndex];
            const Point2d center{(quadrant.xMin + quadrant.xMax) * 0.5,
                                 (quadrant.yMin + quadrant.yMax) * 0.5};
            const double clearance = signedClearance(shape, center, excluded);
            const double halfDiagonal = quadrant.Width() * std::sqrt(0.5);
            queue.push_back(Cell{quadrant, center, clearance,
                                 clearance + halfDiagonal, current.depth + 1});
        }
    }

    if (bestRadius <= minimumRadius)
        return std::nullopt;
    return Circle2d::Create(bestCenter, bestRadius).Value();
}

std::vector<Circle2d> generatePoles(const SimplePolygon& shape,
                                    const SurrogateConfig& config)
{
    const bool hasConfiguredLimit = std::any_of(
        config.poleLimits.begin(), config.poleLimits.end(),
        [](const auto& entry) { return entry.first > 0; });

    double coveredArea = 0.0;
    std::vector<Circle2d> poles;

    while (poles.size() < 1000)
    {
        const auto pole = computePole(shape, poles);
        if (!pole.has_value())
            break;

        poles.push_back(*pole);
        coveredArea += pole->Area();

        if (!hasConfiguredLimit)
            break;

        std::size_t activeLimit = 0;
        for (const auto& [limit, threshold] : config.poleLimits)
        {
            if (limit > 0 && coveredArea / shape.Area() > threshold &&
                (activeLimit == 0 || limit < activeLimit))
                activeLimit = limit;
        }
        if (activeLimit > 0 && poles.size() >= activeLimit)
            break;
    }
    return poles;
}

double lineCross(const Point2d& a, const Point2d& b, const Point2d& c) noexcept
{
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

Edge2d trimEdge(const Edge2d& edge, double factor)
{
    const Point2d midpoint{(edge.start.x + edge.end.x) * 0.5,
                           (edge.start.y + edge.end.y) * 0.5};
    const Point2d start{
        midpoint.x + (edge.start.x - midpoint.x) * factor,
        midpoint.y + (edge.start.y - midpoint.y) * factor};
    const Point2d end{
        midpoint.x + (edge.end.x - midpoint.x) * factor,
        midpoint.y + (edge.end.y - midpoint.y) * factor};
    return Edge2d::Create(start, end).Value();
}

double vectorCross(const Point2d& first, const Point2d& second) noexcept
{
    return first.x * second.y - first.y * second.x;
}

double vectorDot(const Point2d& first, const Point2d& second) noexcept
{
    return first.x * second.x + first.y * second.y;
}

Point2d subtract(const Point2d& first, const Point2d& second) noexcept
{
    return {first.x - second.x, first.y - second.y};
}

Point2d add(const Point2d& first, const Point2d& second) noexcept
{
    return {first.x + second.x, first.y + second.y};
}

Point2d multiply(const Point2d& point, double factor) noexcept
{
    return {point.x * factor, point.y * factor};
}

double pointToEdgeDistance(const Point2d& point, const Edge2d& edge) noexcept
{
    return std::sqrt(edge.ClosestPoint(point).SquaredDistanceTo(point));
}

std::vector<Edge2d> clipRay(const SimplePolygon& shape, const Edge2d& ray)
{
    struct Intersection
    {
        double rayParameter = 0.0;
        Point2d point{};
    };

    const Point2d rayDirection = subtract(ray.end, ray.start);
    std::vector<Intersection> intersections;
    for (const Edge2d& edge : shape.Edges())
    {
        const Point2d edgeDirection = subtract(edge.end, edge.start);
        const double denominator = vectorCross(rayDirection, edgeDirection);
        if (std::abs(denominator) <= 1.0e-12)
            continue;

        const Point2d relative = subtract(edge.start, ray.start);
        const double rayParameter = vectorCross(relative, edgeDirection) / denominator;
        const double edgeParameter = vectorCross(relative, rayDirection) / denominator;
        if (edgeParameter < -1.0e-9 || edgeParameter > 1.0 + 1.0e-9)
            continue;

        intersections.push_back({rayParameter,
                                 add(ray.start, multiply(rayDirection, rayParameter))});
    }

    std::sort(intersections.begin(), intersections.end(),
              [](const Intersection& lhs, const Intersection& rhs) {
                  return lhs.rayParameter < rhs.rayParameter;
              });
    intersections.erase(
        std::unique(intersections.begin(), intersections.end(), [](const Intersection& lhs,
                                                                   const Intersection& rhs) {
            return lhs.point.SquaredDistanceTo(rhs.point) <= 1.0e-18;
        }),
        intersections.end());

    std::vector<Edge2d> clipped;
    for (std::size_t i = 1; i < intersections.size(); i += 2)
    {
        const auto edge = Edge2d::Create(intersections[i - 1].point, intersections[i].point);
        if (!edge.HasValue())
            continue;

        const Point2d midpoint{(edge.Value().start.x + edge.Value().end.x) * 0.5,
                               (edge.Value().start.y + edge.Value().end.y) * 0.5};
        if (!shape.Contains(midpoint))
            continue;
        clipped.push_back(trimEdge(edge.Value(), 0.999));
    }
    return clipped;
}

std::vector<Edge2d> generateCandidatePiers(const SimplePolygon& shape)
{
#ifdef NDEBUG
    constexpr std::size_t raysPerAngle = 200;
    constexpr std::size_t angleCount = 90;
#else
    constexpr std::size_t raysPerAngle = 10;
    constexpr std::size_t angleCount = 4;
#endif

    const double pi = std::acos(-1.0);
    const Rect2d expandedBox = squareSearchBox(shape.BoundingBox());
    const double side = expandedBox.Width();
    const Point2d center = shape.Centroid();
    const Point2d baseStart{center.x, center.y - 2.0 * expandedBox.Height()};
    const Point2d baseEnd{center.x, center.y + 2.0 * expandedBox.Height()};
    std::vector<Edge2d> candidates;

    for (std::size_t angleIndex = 0; angleIndex < angleCount; ++angleIndex)
    {
        const double angle = pi * static_cast<double>(angleIndex) /
                             static_cast<double>(angleCount);
        for (std::size_t translationIndex = 0;
             translationIndex < raysPerAngle; ++translationIndex)
        {
            // Match jagua-rs generate_ray_transformations: create a vertical
            // ray through the polygon centroid, translate it by
            // (x_min + i * dx, 0), then rotate the complete transform around
            // the origin. In matrix form this is R * T, so the translation
            // component is also rotated.
            const double x = expandedBox.xMin +
                             side * static_cast<double>(translationIndex) /
                                 static_cast<double>(raysPerAngle);
            const double cosine = std::cos(angle);
            const double sine = std::sin(angle);
            const Transform2d rayTransform{
                angle, Point2d{cosine * x, sine * x}};
            const Point2d start = rayTransform.Apply(baseStart);
            const Point2d end = rayTransform.Apply(baseEnd);
            const auto ray = Edge2d::Create(start, end);
            if (!ray.HasValue())
                continue;

            const auto clipped = clipRay(shape, ray.Value());
            candidates.insert(candidates.end(), clipped.begin(), clipped.end());
        }
    }
    return candidates;
}

std::uint32_t orderedFloatBits(float value) noexcept
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    if ((bits & 0x80000000U) != 0U)
        return ~bits + 1U;
    return bits | 0x80000000U;
}

bool rustFpaEqual(float first, float second) noexcept
{
    if (first == second)
        return true;
    if (!std::isfinite(first) || !std::isfinite(second))
        return false;
    const std::uint32_t firstBits = orderedFloatBits(first);
    const std::uint32_t secondBits = orderedFloatBits(second);
    const std::uint32_t difference = firstBits > secondBits
                                         ? firstBits - secondBits
                                         : secondBits - firstBits;
    return difference <= 4U;
}

bool rustF32EdgeIntersects(float firstStartX,
                           float firstStartY,
                           float firstEndX,
                           float firstEndY,
                           float secondStartX,
                           float secondStartY,
                           float secondEndX,
                           float secondEndY) noexcept
{
    if (std::max(std::min(firstStartX, firstEndX),
                 std::min(secondStartX, secondEndX)) >
            std::min(std::max(firstStartX, firstEndX),
                     std::max(secondStartX, secondEndX)) ||
        std::max(std::min(firstStartY, firstEndY),
                 std::min(secondStartY, secondEndY)) >
            std::min(std::max(firstStartY, firstEndY),
                     std::max(secondStartY, secondEndY)))
        return false;

    const float tNumerator =
        (firstEndX - secondEndX) * (secondEndY - secondStartY) -
        (firstEndY - secondEndY) * (secondEndX - secondStartX);
    const float tDenominator =
        (firstEndX - firstStartX) * (secondEndY - secondStartY) -
        (firstEndY - firstStartY) * (secondEndX - secondStartX);
    const float uNumerator =
        (firstEndX - secondEndX) * (firstEndY - firstStartY) -
        (firstEndY - secondEndY) * (firstEndX - firstStartX);
    const float uDenominator =
        (firstEndX - firstStartX) * (secondEndY - secondStartY) -
        (firstEndY - firstStartY) * (secondEndX - secondStartX);

    if (tDenominator == 0.0f || uDenominator == 0.0f)
        return false;
    const float t = tNumerator / tDenominator;
    const float u = uNumerator / uDenominator;
    return t >= 0.0f && t <= 1.0f && u >= 0.0f && u <= 1.0f;
}

bool rustF32PointInside(const SimplePolygon& shape,
                        float pointX,
                        float pointY) noexcept
{
    const float xMin = static_cast<float>(shape.BoundingBox().xMin);
    const float yMin = static_cast<float>(shape.BoundingBox().yMin);
    const float xMax = static_cast<float>(shape.BoundingBox().xMax);
    const float yMax = static_cast<float>(shape.BoundingBox().yMax);
    if (pointX < xMin || pointX > xMax || pointY < yMin || pointY > yMax)
        return false;

    const float pointOutsideX = xMax + (xMax - xMin);
    bool intersections = false;
    const auto& vertices = shape.Vertices();
    for (std::size_t index = 0; index < vertices.size(); ++index)
    {
        const std::size_t next = (index + 1) % vertices.size();
        const float startX = static_cast<float>(vertices[index].x);
        const float startY = static_cast<float>(vertices[index].y);
        const float endX = static_cast<float>(vertices[next].x);
        const float endY = static_cast<float>(vertices[next].y);

        if ((rustFpaEqual(startY, pointY) && startX > pointX) ||
            (rustFpaEqual(endY, pointY) && endX > pointX))
        {
            if (startY < pointY || endY < pointY)
                intersections = !intersections;
        }
        else if (rustF32EdgeIntersects(pointX, pointY, pointOutsideX, pointY,
                                       startX, startY, endX, endY))
        {
            intersections = !intersections;
        }
    }
    return intersections;
}

bool rustF32CircleContains(const Circle2d& circle,
                           float pointX,
                           float pointY) noexcept
{
    const float centerX = static_cast<float>(circle.center.x);
    const float centerY = static_cast<float>(circle.center.y);
    const float radius = static_cast<float>(circle.radius);
    const float dx = pointX - centerX;
    const float dy = pointY - centerY;
    return dx * dx + dy * dy <= radius * radius;
}

std::vector<Point2d> generateUnrepresentedGrid(
    const SimplePolygon& shape,
    const std::vector<Circle2d>& poles)
{
#ifdef NDEBUG
    constexpr std::size_t pointsPerDimension = 100;
#else
    constexpr std::size_t pointsPerDimension = 10;
#endif

    const Rect2d box = squareSearchBox(shape.BoundingBox());
    const float xMin = static_cast<float>(box.xMin);
    const float yMin = static_cast<float>(box.yMin);
    const float width = static_cast<float>(box.Width());
    const float height = static_cast<float>(box.Height());
    std::vector<Point2d> points;
    points.reserve(pointsPerDimension * pointsPerDimension);
    for (std::size_t xIndex = 0; xIndex < pointsPerDimension; ++xIndex)
    {
        const float x = pointsPerDimension == 1
                            ? xMin
                            : xMin + width * static_cast<float>(xIndex) /
                                         static_cast<float>(pointsPerDimension - 1);
        for (std::size_t yIndex = 0; yIndex < pointsPerDimension; ++yIndex)
        {
            const float y = pointsPerDimension == 1
                                ? yMin
                                : yMin + height * static_cast<float>(yIndex) /
                                              static_cast<float>(pointsPerDimension - 1);
            if (!rustF32PointInside(shape, x, y))
                continue;

            const bool representedByPole = std::any_of(
                poles.begin(), poles.end(), [x, y](const Circle2d& pole) {
                    return rustF32CircleContains(pole, x, y);
                });
            if (!representedByPole)
                points.push_back(Point2d{static_cast<double>(x),
                                         static_cast<double>(y)});
        }
    }
    return points;
}

std::vector<double> distancesToPiers(const std::vector<Point2d>& points,
                                     const std::vector<Edge2d>& piers,
                                     double forfeitDistance)
{
    std::vector<double> distances;
    distances.reserve(points.size());
    for (const Point2d& point : points)
    {
        double distance = forfeitDistance;
        for (const Edge2d& pier : piers)
            distance = std::min(distance, pointToEdgeDistance(point, pier));
        distances.push_back(distance);
    }
    return distances;
}

std::vector<double> distancesToPoles(const std::vector<Point2d>& points,
                                     const std::vector<Circle2d>& poles,
                                     double forfeitDistance)
{
    std::vector<double> distances;
    distances.reserve(points.size());
    for (const Point2d& point : points)
    {
        double distance = forfeitDistance;
        for (const Circle2d& pole : poles)
        {
            const double centerDistance =
                std::sqrt(pole.center.SquaredDistanceTo(point));
            distance = std::min(distance, std::max(0.0, centerDistance - pole.radius));
        }
        distances.push_back(distance);
    }
    return distances;
}

double pierLoss(const Edge2d& candidate,
                const std::vector<Point2d>& points,
                const std::vector<double>& distancesToSelectedPiers,
                const std::vector<double>& distancesToPolesForPoints,
                double radiusOfInfluence)
{
    double loss = 0.0;
    for (std::size_t i = 0; i < points.size(); ++i)
    {
        const double distanceToCandidate = pointToEdgeDistance(points[i], candidate);
        const double distanceToPier =
            std::min(distancesToSelectedPiers[i], distanceToCandidate);
        const double representedDistance =
            distanceToPier < radiusOfInfluence
                ? std::min(distancesToPolesForPoints[i], distanceToPier)
                : distancesToPolesForPoints[i];
        loss += representedDistance * representedDistance;
    }
    return loss;
}

std::vector<Edge2d> generatePiers(const SimplePolygon& shape,
                                  std::size_t count,
                                  const std::vector<Circle2d>& poles)
{
    if (count == 0)
        return {};

    const std::vector<Edge2d> candidates = generateCandidatePiers(shape);
    if (candidates.empty())
        return {};

    const Rect2d box = shape.BoundingBox();
    const double forfeitDistance = box.Width() * box.Height();
    const std::vector<Point2d> points = generateUnrepresentedGrid(shape, poles);
    const double radiusOfInfluence = 0.10 * squareSearchBox(box).Width();
    std::vector<Edge2d> selected;

    for (std::size_t i = 0; i < count; ++i)
    {
        const std::vector<double> selectedDistances =
            distancesToPiers(points, selected, forfeitDistance);
        const std::vector<double> poleDistances =
            distancesToPoles(points, poles, forfeitDistance);

        auto best = candidates.begin();
        double bestLoss = pierLoss(*best, points, selectedDistances,
                                   poleDistances, radiusOfInfluence);
        for (auto candidate = candidates.begin() + 1;
             candidate != candidates.end(); ++candidate)
        {
            const double loss = pierLoss(*candidate, points, selectedDistances,
                                         poleDistances, radiusOfInfluence);
            if (loss < bestLoss)
            {
                best = candidate;
                bestLoss = loss;
            }
        }
        selected.push_back(*best);
    }
    return selected;
}

double polygonArea(const std::vector<Point2d>& points)
{
    if (points.size() < 3)
        return 0.0;

    double area = 0.0;
    for (std::size_t i = 0; i < points.size(); ++i)
    {
        const std::size_t next = (i + 1) % points.size();
        area += points[i].x * points[next].y - points[next].x * points[i].y;
    }
    return std::abs(area * 0.5);
}
}

Surrogate::Surrogate(std::vector<Circle2d> poles,
                     std::vector<Edge2d> piers,
                     std::size_t failFastPoleCount,
                     double convexHullArea,
                     SurrogateConfig config)
    : m_Poles(std::move(poles)),
      m_Piers(std::move(piers)),
      m_FailFastPoleCount(failFastPoleCount),
      m_ConvexHullArea(convexHullArea),
      m_Config(config)
{
}

JAGUARCORE_API Result<Surrogate> Surrogate::Create(const SimplePolygon& shape,
                                                   const SurrogateConfig& config)
{
    if (!std::isfinite(config.failFastPoleAreaRatio) ||
        config.failFastPoleAreaRatio < 0.0 ||
        config.failFastPoleAreaRatio > 1.0)
    {
        return Result<Surrogate>::FromError(
            {ErrorCode::InvalidInput, "invalid fail-fast pole area ratio"});
    }

    for (const auto& [limit, threshold] : config.poleLimits)
    {
        if (!std::isfinite(threshold) || threshold < 0.0 || threshold > 1.0)
        {
            return Result<Surrogate>::FromError(
                {ErrorCode::InvalidInput, "invalid pole coverage threshold"});
        }
        if (limit > 1000)
        {
            return Result<Surrogate>::FromError(
                {ErrorCode::InvalidInput, "pole limit is too large"});
        }
    }

    const std::vector<Circle2d> poles = generatePoles(shape, config);
    if (poles.empty())
    {
        return Result<Surrogate>::FromError(
            {ErrorCode::InvalidPolygon, "could not generate a pole"});
    }

    double poleArea = 0.0;
    std::size_t failFastCount = 0;
    const double targetArea = shape.Area() * config.failFastPoleAreaRatio;
    for (const Circle2d& pole : poles)
    {
        if (poleArea >= targetArea)
            break;
        poleArea += pole.Area();
        ++failFastCount;
    }

    const std::vector<Circle2d> polesForPiers(
        poles.begin(), poles.begin() + failFastCount);
    const std::vector<Edge2d> piers = generatePiers(
        shape, config.failFastPiers, polesForPiers);
    const std::vector<Point2d> hull = ConvexHull(shape.Vertices());
    const double hullArea = polygonArea(hull);

    return Result<Surrogate>::FromValue(
        Surrogate{poles, piers, failFastCount, hullArea, config});
}

JAGUARCORE_API const std::vector<Circle2d>& Surrogate::Poles() const noexcept
{
    return m_Poles;
}

JAGUARCORE_API const std::vector<Edge2d>& Surrogate::Piers() const noexcept
{
    return m_Piers;
}

JAGUARCORE_API std::size_t Surrogate::FailFastPoleCount() const noexcept
{
    return m_FailFastPoleCount;
}

JAGUARCORE_API double Surrogate::ConvexHullArea() const noexcept
{
    return m_ConvexHullArea;
}

JAGUARCORE_API const SurrogateConfig& Surrogate::Config() const noexcept
{
    return m_Config;
}

JAGUARCORE_API Result<Surrogate> Surrogate::Transformed(const Transform2d& transform) const
{
    if (!transform.IsFinite())
    {
        return Result<Surrogate>::FromError(
            {ErrorCode::InvalidInput, "transform contains a non-finite value"});
    }

    std::vector<Circle2d> transformedPoles = m_Poles;
    for (Circle2d& pole : transformedPoles)
        pole.center = transform.Apply(pole.center);

    std::vector<Edge2d> transformedPiers = m_Piers;
    for (Edge2d& pier : transformedPiers)
    {
        pier.start = transform.Apply(pier.start);
        pier.end = transform.Apply(pier.end);
    }

    return Result<Surrogate>::FromValue(
        Surrogate{std::move(transformedPoles), std::move(transformedPiers),
                  m_FailFastPoleCount, m_ConvexHullArea, m_Config});
}
}
