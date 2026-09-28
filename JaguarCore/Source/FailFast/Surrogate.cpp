#include "JaguarCore/FailFast/Surrogate.h"

#include "JaguarCore/Geometry/PointOfInaccessibility.h"
#include "JaguarCore/Geometry/RustF32.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

namespace jaguar
{
namespace
{
SurrogateConfig normalizeSurrogateConfigRustF32(
    SurrogateConfig config) noexcept
{
    for (auto& [limit, threshold] : config.poleLimits)
    {
        (void)limit;
        threshold = static_cast<double>(static_cast<float>(threshold));
    }
    config.failFastPoleAreaRatio = static_cast<double>(
        static_cast<float>(config.failFastPoleAreaRatio));
    return config;
}

float circleAreaRustF32(const Circle2d& circle) noexcept
{
    return static_cast<float>(circle.AreaRustF32());
}

Rect2d squareSearchBoxRustF32(const Rect2d& box)
{
    const float xMin = static_cast<float>(box.xMin);
    const float yMin = static_cast<float>(box.yMin);
    const float xMax = static_cast<float>(box.xMax);
    const float yMax = static_cast<float>(box.yMax);
    const float width = xMax - xMin;
    const float height = yMax - yMin;
    const float dx = width < height ? (height - width) * 0.5F : 0.0F;
    const float dy = height < width ? (width - height) * 0.5F : 0.0F;
    return Rect2d{static_cast<double>(xMin - dx),
                  static_cast<double>(yMin - dy),
                  static_cast<double>(xMax + dx),
                  static_cast<double>(yMax + dy)};
}

Result<std::vector<Circle2d>> generatePoles(
    const SimplePolygon& shape,
    const SurrogateConfig& config)
{
    std::vector<Circle2d> poles{shape.PointOfInaccessibility()};
    float coveredArea = circleAreaRustF32(poles.front());

    while (true)
    {
        const auto pole = ComputePointOfInaccessibility(shape, poles);
        if (!pole.HasValue())
            return Result<std::vector<Circle2d>>::FromError(pole.GetError());

        poles.push_back(pole.Value());
        coveredArea += circleAreaRustF32(pole.Value());

        std::optional<std::size_t> activeLimit;
        for (const auto& [limit, threshold] : config.poleLimits)
        {
            if (coveredArea / static_cast<float>(shape.Area()) >
                    static_cast<float>(threshold) &&
                (!activeLimit.has_value() || limit < *activeLimit))
                activeLimit = limit;
        }
        if (activeLimit.has_value() && poles.size() >= *activeLimit)
            break;

        if (poles.size() >= 1000U)
        {
            return Result<std::vector<Circle2d>>::FromError(
                {ErrorCode::InvalidInput,
                 "more than 1000 poles were generated; check the surrogate configuration"});
        }
    }
    return Result<std::vector<Circle2d>>::FromValue(std::move(poles));
}

float pointToEdgeDistanceRustF32(const Point2d& point,
                                  const Edge2d& edge) noexcept
{
    return static_cast<float>(edge.DistanceToRustF32(point));
}

std::optional<Point2d> intersectRustF32(const Edge2d& first,
                                        const Edge2d& second)
{
    const float x1 = static_cast<float>(first.start.x);
    const float y1 = static_cast<float>(first.start.y);
    const float x2 = static_cast<float>(first.end.x);
    const float y2 = static_cast<float>(first.end.y);
    const float x3 = static_cast<float>(second.start.x);
    const float y3 = static_cast<float>(second.start.y);
    const float x4 = static_cast<float>(second.end.x);
    const float y4 = static_cast<float>(second.end.y);
    if (std::max(std::min(x1, x2), std::min(x3, x4)) >
            std::min(std::max(x1, x2), std::max(x3, x4)) ||
        std::max(std::min(y1, y2), std::min(y3, y4)) >
            std::min(std::max(y1, y2), std::max(y3, y4)))
    {
        return std::nullopt;
    }

    const float tNumerator = (x2 - x4) * (y4 - y3) -
                             (y2 - y4) * (x4 - x3);
    const float tDenominator = (x2 - x1) * (y4 - y3) -
                               (y2 - y1) * (x4 - x3);
    const float uNumerator = (x2 - x4) * (y2 - y1) -
                             (y2 - y4) * (x2 - x1);
    const float uDenominator = (x2 - x1) * (y4 - y3) -
                               (y2 - y1) * (x4 - x3);
    if (tDenominator == 0.0F || uDenominator == 0.0F)
        return std::nullopt;

    const float t = tNumerator / tDenominator;
    const float u = uNumerator / uDenominator;
    if (t < 0.0F || t > 1.0F || u < 0.0F || u > 1.0F)
        return std::nullopt;

    return Point2d{static_cast<double>(x2 + t * (x1 - x2)),
                   static_cast<double>(y2 + t * (y1 - y2))};
}

Edge2d trimEdgeRustF32(const Edge2d& edge, float factor)
{
    const float x1 = static_cast<float>(edge.start.x);
    const float y1 = static_cast<float>(edge.start.y);
    const float x2 = static_cast<float>(edge.end.x);
    const float y2 = static_cast<float>(edge.end.y);
    const float dx = x2 - x1;
    const float dy = y2 - y1;
    const float offset = factor - 1.0F;
    const Point2d start{
        static_cast<double>(x1 - dx * offset / 2.0F),
        static_cast<double>(y1 - dy * offset / 2.0F)};
    const Point2d end{
        static_cast<double>(x2 + dx * offset / 2.0F),
        static_cast<double>(y2 + dy * offset / 2.0F)};
    return Edge2d::Create(start, end).Value();
}

std::vector<Edge2d> clipRayRustF32(const SimplePolygon& shape,
                                    const Edge2d& ray)
{
    struct Intersection
    {
        float distance = 0.0F;
        Point2d point{};
    };

    std::vector<Intersection> intersections;
    for (const Edge2d& edge : shape.Edges())
    {
        const auto point = intersectRustF32(edge, ray);
        if (!point.has_value())
            continue;
        const float dx = static_cast<float>(point->x - ray.start.x);
        const float dy = static_cast<float>(point->y - ray.start.y);
        intersections.push_back(
            {std::sqrt(dx * dx + dy * dy), *point});
    }

    std::stable_sort(intersections.begin(), intersections.end(),
                     [](const Intersection& lhs, const Intersection& rhs) {
                         return lhs.distance < rhs.distance;
                     });

    std::vector<Edge2d> clipped;
    for (std::size_t i = 1; i < intersections.size(); i += 2)
    {
        const auto edge = Edge2d::Create(intersections[i - 1].point, intersections[i].point);
        if (!edge.HasValue())
            continue;
        clipped.push_back(trimEdgeRustF32(edge.Value(), 0.999F));
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

    constexpr float pi = 3.14159265358979323846F;
    const Rect2d expandedBox = squareSearchBoxRustF32(shape.BoundingBox());
    const float side = static_cast<float>(expandedBox.Width());
    const float translationStep = side / static_cast<float>(raysPerAngle);
    const float angleStep = pi / static_cast<float>(angleCount);
    const Point2d center = shape.Centroid();
    const float centerX = static_cast<float>(center.x);
    const float centerY = static_cast<float>(center.y);
    const float halfRayLength = 2.0F * static_cast<float>(expandedBox.Height());
    const Point2d baseStart{static_cast<double>(centerX),
                            static_cast<double>(centerY - halfRayLength)};
    const Point2d baseEnd{static_cast<double>(centerX),
                          static_cast<double>(centerY + halfRayLength)};
    std::vector<Edge2d> candidates;

    for (std::size_t angleIndex = 0; angleIndex < angleCount; ++angleIndex)
    {
        const float angle = angleStep * static_cast<float>(angleIndex);
        for (std::size_t translationIndex = 0;
             translationIndex < raysPerAngle; ++translationIndex)
        {
            // Match jagua-rs generate_ray_transformations: create a vertical
            // ray through the polygon centroid, translate it by
            // (x_min + i * dx, 0), then rotate the complete transform around
            // the origin. In matrix form this is R * T, so the translation
            // component is also rotated.
            const float x = static_cast<float>(expandedBox.xMin) +
                            translationStep * static_cast<float>(translationIndex);
            const float cosine = std::cos(angle);
            const float sine = std::sin(angle);
            const Transform2d rayTransform{
                static_cast<double>(angle),
                Point2d{static_cast<double>(cosine * x),
                        static_cast<double>(sine * x)}};
            const Point2d start = rayTransform.ApplyRustF32(baseStart);
            const Point2d end = rayTransform.ApplyRustF32(baseEnd);
            const auto ray = Edge2d::Create(start, end);
            if (!ray.HasValue())
                continue;

            const auto clipped = clipRayRustF32(shape, ray.Value());
            candidates.insert(candidates.end(), clipped.begin(), clipped.end());
        }
    }
    return candidates;
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

        if ((detail::AlmostEqualRustF32(startY, pointY) &&
             detail::GreaterRustF32(startX, pointX)) ||
            (detail::AlmostEqualRustF32(endY, pointY) &&
             detail::GreaterRustF32(endX, pointX)))
        {
            if (detail::LessRustF32(startY, pointY) ||
                detail::LessRustF32(endY, pointY))
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
    return circle.ContainsRustF32(Point2d{pointX, pointY});
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

    const Rect2d box = squareSearchBoxRustF32(shape.BoundingBox());
    const float xMin = static_cast<float>(box.xMin);
    const float yMin = static_cast<float>(box.yMin);
    const float xMax = static_cast<float>(box.xMax);
    const float yMax = static_cast<float>(box.yMax);
    const float xStep = pointsPerDimension == 1
                            ? 0.0F
                            : (xMax - xMin) /
                                  static_cast<float>(pointsPerDimension - 1);
    const float yStep = pointsPerDimension == 1
                            ? 0.0F
                            : (yMax - yMin) /
                                  static_cast<float>(pointsPerDimension - 1);
    std::vector<Point2d> points;
    points.reserve(pointsPerDimension * pointsPerDimension);
    for (std::size_t xIndex = 0; xIndex < pointsPerDimension; ++xIndex)
    {
        const float x = xMin + xStep * static_cast<float>(xIndex);
        for (std::size_t yIndex = 0; yIndex < pointsPerDimension; ++yIndex)
        {
            const float y = yMin + yStep * static_cast<float>(yIndex);
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

std::vector<float> distancesToPiers(const std::vector<Point2d>& points,
                                     const std::vector<Edge2d>& piers,
                                     float forfeitDistance)
{
    std::vector<float> distances;
    distances.reserve(points.size());
    for (const Point2d& point : points)
    {
        float distance = forfeitDistance;
        for (const Edge2d& pier : piers)
            distance = std::min(distance, pointToEdgeDistanceRustF32(point, pier));
        distances.push_back(distance);
    }
    return distances;
}

std::vector<float> distancesToPoles(const std::vector<Point2d>& points,
                                     const std::vector<Circle2d>& poles,
                                     float forfeitDistance)
{
    std::vector<float> distances;
    distances.reserve(points.size());
    for (const Point2d& point : points)
    {
        float distance = forfeitDistance;
        for (const Circle2d& pole : poles)
        {
            const float dx = static_cast<float>(point.x) -
                             static_cast<float>(pole.center.x);
            const float dy = static_cast<float>(point.y) -
                             static_cast<float>(pole.center.y);
            const float radius = static_cast<float>(pole.radius);
            const float squaredDistance = dx * dx + dy * dy;
            const float poleDistance = squaredDistance < radius * radius
                                           ? 0.0F
                                           : std::sqrt(squaredDistance) - radius;
            distance = std::min(distance, poleDistance);
        }
        distances.push_back(distance);
    }
    return distances;
}

float pierLoss(const Edge2d& candidate,
               const std::vector<Point2d>& points,
               const std::vector<float>& distancesToSelectedPiers,
               const std::vector<float>& distancesToPolesForPoints,
               float radiusOfInfluence)
{
    float loss = 0.0F;
    for (std::size_t i = 0; i < points.size(); ++i)
    {
        const float distanceToCandidate =
            pointToEdgeDistanceRustF32(points[i], candidate);
        const float distanceToPier =
            std::min(distancesToSelectedPiers[i], distanceToCandidate);
        const float representedDistance =
            distanceToPier < radiusOfInfluence
                ? std::min(distancesToPolesForPoints[i], distanceToPier)
                : distancesToPolesForPoints[i];
        loss += representedDistance * representedDistance;
    }
    return loss;
}

Result<std::vector<Edge2d>> generatePiers(
    const SimplePolygon& shape,
    std::size_t count,
    const std::vector<Circle2d>& poles)
{
    if (count == 0)
        return Result<std::vector<Edge2d>>::FromValue({});

    const std::vector<Edge2d> candidates = generateCandidatePiers(shape);
    if (candidates.empty())
    {
        return Result<std::vector<Edge2d>>::FromError(
            {ErrorCode::Infeasible, "no ray found"});
    }

    const Rect2d box = shape.BoundingBox();
    const float boxWidth = static_cast<float>(box.Width());
    const float boxHeight = static_cast<float>(box.Height());
    const float forfeitDistance =
        std::sqrt(boxWidth * boxWidth * (boxHeight * boxHeight));
    const std::vector<Point2d> points = generateUnrepresentedGrid(shape, poles);
    const float expandedWidth = static_cast<float>(
        squareSearchBoxRustF32(box).Width());
    const float radiusOfInfluence = 0.10F * expandedWidth;
    std::vector<Edge2d> selected;

    for (std::size_t i = 0; i < count; ++i)
    {
        const std::vector<float> selectedDistances =
            distancesToPiers(points, selected, forfeitDistance);
        const std::vector<float> poleDistances =
            distancesToPoles(points, poles, forfeitDistance);

        auto best = candidates.begin();
        float bestLoss = pierLoss(*best, points, selectedDistances,
                                  poleDistances, radiusOfInfluence);
        for (auto candidate = candidates.begin() + 1;
             candidate != candidates.end(); ++candidate)
        {
            const float loss = pierLoss(*candidate, points, selectedDistances,
                                        poleDistances, radiusOfInfluence);
            if (loss < bestLoss)
            {
                best = candidate;
                bestLoss = loss;
            }
        }
        selected.push_back(*best);
    }
    return Result<std::vector<Edge2d>>::FromValue(std::move(selected));
}

float hullCrossRustF32(const Point2d& origin,
                       const Point2d& first,
                       const Point2d& second) noexcept
{
    const float firstX = static_cast<float>(first.x) -
                         static_cast<float>(origin.x);
    const float firstY = static_cast<float>(first.y) -
                         static_cast<float>(origin.y);
    const float secondX = static_cast<float>(second.x) -
                          static_cast<float>(origin.x);
    const float secondY = static_cast<float>(second.y) -
                          static_cast<float>(origin.y);
    return firstX * secondY - firstY * secondX;
}

std::vector<Point2d> convexHullRustF32(std::vector<Point2d> points)
{
    if (points.size() <= 1)
        return points;

    std::stable_sort(points.begin(), points.end(), [](const Point2d& lhs,
                                                      const Point2d& rhs) {
        return static_cast<float>(lhs.x) < static_cast<float>(rhs.x);
    });

    std::vector<Point2d> lower;
    for (const Point2d& point : points)
    {
        while (lower.size() >= 2 &&
               hullCrossRustF32(lower[lower.size() - 2], lower.back(), point) <= 0.0F)
            lower.pop_back();
        lower.push_back(point);
    }

    std::vector<Point2d> upper;
    for (auto it = points.rbegin(); it != points.rend(); ++it)
    {
        while (upper.size() >= 2 &&
               hullCrossRustF32(upper[upper.size() - 2], upper.back(), *it) <= 0.0F)
            upper.pop_back();
        upper.push_back(*it);
    }

    lower.pop_back();
    upper.pop_back();
    lower.insert(lower.end(), upper.begin(), upper.end());
    return lower;
}

float polygonAreaRustF32(const std::vector<Point2d>& points)
{
    if (points.empty())
        return 0.0F;

    float sigma = 0.0F;
    for (std::size_t i = 0; i < points.size(); ++i)
    {
        const std::size_t next = (i + 1) % points.size();
        const float xi = static_cast<float>(points[i].x);
        const float yi = static_cast<float>(points[i].y);
        const float xj = static_cast<float>(points[next].x);
        const float yj = static_cast<float>(points[next].y);
        sigma += (yi + yj) * (xi - xj);
    }
    return 0.5F * sigma;
}
}

Surrogate::Surrogate(std::vector<Circle2d> poles,
                     std::vector<Edge2d> piers,
                     std::size_t failFastPoleCount,
                     std::vector<std::size_t> convexHullIndices,
                     double convexHullArea,
                     SurrogateConfig config)
    : m_Poles(std::move(poles)),
      m_Piers(std::move(piers)),
      m_FailFastPoleCount(failFastPoleCount),
      m_ConvexHullIndices(std::move(convexHullIndices)),
      m_ConvexHullArea(convexHullArea),
      m_Config(config)
{
}

JAGUARCORE_API Result<Surrogate> Surrogate::Create(const SimplePolygon& shape,
                                                   const SurrogateConfig& config)
{
    const SurrogateConfig rustConfig =
        normalizeSurrogateConfigRustF32(config);
    if (!std::isfinite(rustConfig.failFastPoleAreaRatio) ||
        rustConfig.failFastPoleAreaRatio < 0.0 ||
        rustConfig.failFastPoleAreaRatio > 1.0)
    {
        return Result<Surrogate>::FromError(
            {ErrorCode::InvalidInput, "invalid fail-fast pole area ratio"});
    }

    const auto polesResult = generatePoles(shape, rustConfig);
    if (!polesResult.HasValue())
        return Result<Surrogate>::FromError(polesResult.GetError());
    const std::vector<Circle2d>& poles = polesResult.Value();

    float poleArea = 0.0F;
    std::size_t failFastCount = 0;
    const float targetArea = static_cast<float>(shape.Area()) *
                             static_cast<float>(rustConfig.failFastPoleAreaRatio);
    if (static_cast<float>(rustConfig.failFastPoleAreaRatio) != 0.0F)
    {
        for (const Circle2d& pole : poles)
        {
            poleArea += circleAreaRustF32(pole);
            ++failFastCount;
            if (poleArea >= targetArea)
                break;
        }
    }

    const std::vector<Circle2d> polesForPiers(
        poles.begin(), poles.begin() + failFastCount);
    auto piersResult = generatePiers(
        shape, config.failFastPiers, polesForPiers);
    if (!piersResult.HasValue())
        return Result<Surrogate>::FromError(piersResult.GetError());
    std::vector<Edge2d> piers = std::move(piersResult).Value();
    const std::vector<Point2d> hull = convexHullRustF32(shape.Vertices());
    std::vector<std::size_t> hullIndices;
    hullIndices.reserve(hull.size());
    for (const Point2d& hullPoint : hull)
    {
        const auto& vertices = shape.Vertices();
        const auto found = std::find_if(
            vertices.begin(), vertices.end(), [&hullPoint](const Point2d& vertex) {
                return static_cast<float>(vertex.x) ==
                           static_cast<float>(hullPoint.x) &&
                       static_cast<float>(vertex.y) ==
                           static_cast<float>(hullPoint.y);
            });
        if (found == vertices.end())
        {
            return Result<Surrogate>::FromError(
                {ErrorCode::InvalidPolygon,
                 "convex hull point is not present in the polygon"});
        }
        hullIndices.push_back(static_cast<std::size_t>(found - vertices.begin()));
    }
    const double hullArea = static_cast<double>(polygonAreaRustF32(hull));

    return Result<Surrogate>::FromValue(
        Surrogate{poles, std::move(piers), failFastCount, std::move(hullIndices),
                  hullArea, rustConfig});
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

JAGUARCORE_API const std::vector<std::size_t>&
Surrogate::ConvexHullIndices() const noexcept
{
    return m_ConvexHullIndices;
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
                  m_FailFastPoleCount, m_ConvexHullIndices,
                  m_ConvexHullArea, m_Config});
}

JAGUARCORE_API Result<Surrogate> Surrogate::TransformedRustF32(
    const Transform2d& transform) const
{
    if (!transform.IsFinite())
    {
        return Result<Surrogate>::FromError(
            {ErrorCode::InvalidInput, "transform contains a non-finite value"});
    }

    std::vector<Circle2d> transformedPoles = m_Poles;
    for (Circle2d& pole : transformedPoles)
        pole.center = transform.ApplyRustF32(pole.center);

    std::vector<Edge2d> transformedPiers = m_Piers;
    for (Edge2d& pier : transformedPiers)
    {
        pier.start = transform.ApplyRustF32(pier.start);
        pier.end = transform.ApplyRustF32(pier.end);
    }

    return Result<Surrogate>::FromValue(
        Surrogate{std::move(transformedPoles), std::move(transformedPiers),
                  m_FailFastPoleCount, m_ConvexHullIndices,
                  m_ConvexHullArea, m_Config});
}
}
