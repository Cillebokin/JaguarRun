#include "JaguarCore/Geometry/ShapeModification.h"

#include "StraightSkeleton.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <utility>
#include <vector>

namespace jaguar
{
namespace
{
struct Line2d
{
    Point2d start{};
    Point2d end{};
};

struct NormalizedOffset
{
    ShapeModifyMode mode = ShapeModifyMode::Inflate;
    double distance = 0.0;
};

struct OverlayPoint
{
    std::int32_t x = 0;
    std::int32_t y = 0;

    bool operator==(const OverlayPoint& other) const noexcept
    {
        return x == other.x && y == other.y;
    }

    bool operator!=(const OverlayPoint& other) const noexcept
    {
        return !(*this == other);
    }
};

bool hasSafeInt64CrossProductExtent(
    const std::vector<OverlayPoint>& points) noexcept
{
    if (points.empty())
        return false;

    std::int32_t minX = points.front().x;
    std::int32_t maxX = points.front().x;
    std::int32_t minY = points.front().y;
    std::int32_t maxY = points.front().y;
    for (const OverlayPoint& point : points)
    {
        minX = std::min(minX, point.x);
        maxX = std::max(maxX, point.x);
        minY = std::min(minY, point.y);
        maxY = std::max(maxY, point.y);
    }

    constexpr std::int64_t maxSafeExtent =
        std::numeric_limits<std::int32_t>::max();
    return static_cast<std::int64_t>(maxX) - minX <= maxSafeExtent &&
           static_cast<std::int64_t>(maxY) - minY <= maxSafeExtent;
}

struct OverlayAdapter
{
    Point2d offset{};
    double dirScale = 1.0;
    double invScale = 1.0;

    std::optional<OverlayPoint> toInt(const Point2d& point) const noexcept
    {
        const double x = std::round((point.x - offset.x) * dirScale);
        const double y = std::round((point.y - offset.y) * dirScale);
        if (!std::isfinite(x) || !std::isfinite(y) ||
            x < std::numeric_limits<std::int32_t>::min() ||
            x > std::numeric_limits<std::int32_t>::max() ||
            y < std::numeric_limits<std::int32_t>::min() ||
            y > std::numeric_limits<std::int32_t>::max())
        {
            return std::nullopt;
        }
        return OverlayPoint{static_cast<std::int32_t>(x),
                           static_cast<std::int32_t>(y)};
    }

    Point2d toFloat(const OverlayPoint& point) const noexcept
    {
        return {static_cast<double>(point.x) * invScale + offset.x,
                static_cast<double>(point.y) * invScale + offset.y};
    }
};

struct OverlaySection
{
    OverlayPoint a{};
    OverlayPoint b{};
    OverlayPoint aTop{};
    OverlayPoint bTop{};
    Point2d direction{};
};

bool preservesModificationMode(const SimplePolygon& original,
                               const SimplePolygon& modified,
                               ShapeModifyMode mode);

NormalizedOffset normalizeOffsetRustF32(ShapeModifyMode mode,
                                        double distance) noexcept
{
    const double f32Distance =
        static_cast<double>(static_cast<float>(distance));
    const double signedDistance =
        mode == ShapeModifyMode::Inflate ? f32Distance : -f32Distance;
    return {signedDistance < 0.0 ? ShapeModifyMode::Deflate
                                 : ShapeModifyMode::Inflate,
            std::abs(signedDistance)};
}

std::optional<OverlayAdapter> makeOverlayAdapter(const SimplePolygon& shape,
                                                  double distance)
{
    const Rect2d& bounds = shape.BoundingBox();
    const double extra = std::abs(1.1 * distance) + std::abs(1.1 * distance);
    const double minX = bounds.xMin - extra;
    const double maxX = bounds.xMax + extra;
    const double minY = bounds.yMin - extra;
    const double maxY = bounds.yMax + extra;
    const double halfWidth = (maxX - minX) * 0.5;
    const double halfHeight = (maxY - minY) * 0.5;
    const double maxHalfExtent = std::max(halfWidth, halfHeight);

    OverlayAdapter adapter;
    adapter.offset = {minX + halfWidth, minY + halfHeight};
    if (maxHalfExtent != 0.0)
    {
        // i_float::FloatNumber::to_i32 rounds to the nearest integer,
        // with halfway values away from zero; it does not truncate log2.
        const auto exponent =
            static_cast<int>(std::round(std::log2(maxHalfExtent)));
        const int scaleExponent = 29 - exponent;
        adapter.dirScale = std::exp2(static_cast<double>(scaleExponent));
        adapter.invScale = std::exp2(-static_cast<double>(scaleExponent));
    }
    if (!std::isfinite(adapter.offset.x) ||
        !std::isfinite(adapter.offset.y) ||
        !std::isfinite(adapter.dirScale) ||
        !std::isfinite(adapter.invScale))
    {
        return std::nullopt;
    }
    return adapter;
}

bool isCounterClockwise(const std::vector<OverlayPoint>& points) noexcept
{
    if (points.size() < 3U || !hasSafeInt64CrossProductExtent(points))
        return false;

    long double signedAreaTwice = 0.0L;
    for (std::size_t i = 0U; i < points.size(); ++i)
    {
        const OverlayPoint& current = points[i];
        const OverlayPoint& next = points[(i + 1U) % points.size()];
        signedAreaTwice += static_cast<long double>(current.x) * next.y -
                           static_cast<long double>(current.y) * next.x;
    }
    return signedAreaTwice > 0.0L;
}

std::int64_t crossInt(const OverlayPoint& origin,
                      const OverlayPoint& first,
                      const OverlayPoint& second) noexcept
{
    const std::int64_t firstX =
        static_cast<std::int64_t>(first.x) - origin.x;
    const std::int64_t firstY =
        static_cast<std::int64_t>(first.y) - origin.y;
    const std::int64_t secondX =
        static_cast<std::int64_t>(second.x) - origin.x;
    const std::int64_t secondY =
        static_cast<std::int64_t>(second.y) - origin.y;
    return firstX * secondY - firstY * secondX;
}

bool pointOnSegment(const OverlayPoint& point,
                    const OverlayPoint& start,
                    const OverlayPoint& end) noexcept
{
    return crossInt(start, end, point) == 0 &&
           point.x >= std::min(start.x, end.x) &&
           point.x <= std::max(start.x, end.x) &&
           point.y >= std::min(start.y, end.y) &&
           point.y <= std::max(start.y, end.y);
}

bool segmentsIntersect(const OverlayPoint& firstStart,
                       const OverlayPoint& firstEnd,
                       const OverlayPoint& secondStart,
                       const OverlayPoint& secondEnd) noexcept
{
    const std::int64_t a =
        crossInt(firstStart, firstEnd, secondStart);
    const std::int64_t b = crossInt(firstStart, firstEnd, secondEnd);
    const std::int64_t c =
        crossInt(secondStart, secondEnd, firstStart);
    const std::int64_t d = crossInt(secondStart, secondEnd, firstEnd);
    if (((a < 0 && b > 0) || (a > 0 && b < 0)) &&
        ((c < 0 && d > 0) || (c > 0 && d < 0)))
    {
        return true;
    }
    return (a == 0 && pointOnSegment(secondStart, firstStart, firstEnd)) ||
           (b == 0 && pointOnSegment(secondEnd, firstStart, firstEnd)) ||
           (c == 0 && pointOnSegment(firstStart, secondStart, secondEnd)) ||
           (d == 0 && pointOnSegment(firstEnd, secondStart, secondEnd));
}

struct OverlayPointLess
{
    bool operator()(const OverlayPoint& left,
                    const OverlayPoint& right) const noexcept
    {
        return left.x < right.x ||
               (left.x == right.x && left.y < right.y);
    }
};

struct OutlineSplitMark
{
    long double parameter = 0.0L;
    OverlayPoint point{};
};

struct OutlineHalfEdge
{
    std::size_t from = 0U;
    std::size_t to = 0U;
    std::size_t twin = 0U;
    std::size_t next = 0U;
    std::size_t face = std::numeric_limits<std::size_t>::max();
    bool boundary = false;
    int windingDelta = 0;
};

struct UInt128Product
{
    std::uint64_t high = 0U;
    std::uint64_t low = 0U;
};

std::uint32_t leadingZeroCount(std::uint64_t value) noexcept
{
    if (value == 0U)
        return 64U;

    std::uint32_t count = 0U;
    constexpr std::uint64_t topBit = std::uint64_t{1U} << 63U;
    while ((value & topBit) == 0U)
    {
        value <<= 1U;
        ++count;
    }
    return count;
}

UInt128Product multiplyTo128(std::uint64_t first,
                             std::uint64_t second) noexcept
{
    if (leadingZeroCount(first) + leadingZeroCount(second) >= 64U)
        return {0U, first * second};

    constexpr std::uint64_t lowMask = 0xffffffffU;
    const std::uint64_t firstHigh = first >> 32U;
    const std::uint64_t firstLow = first & lowMask;
    const std::uint64_t secondHigh = second >> 32U;
    const std::uint64_t secondLow = second & lowMask;
    const std::uint64_t lowProduct = firstLow * secondLow;

    const std::uint64_t crossProduct0 = firstLow * secondHigh;
    const std::uint64_t crossProduct1 = firstHigh * secondLow;
    const std::uint64_t firstSum = crossProduct0 + crossProduct1;
    const std::uint64_t firstCarry = firstSum < crossProduct0 ? 1U : 0U;
    const std::uint64_t secondSum = firstSum + (lowProduct >> 32U);
    const std::uint64_t secondCarry = secondSum < firstSum ? 1U : 0U;
    const std::uint64_t middleHigh = firstCarry + secondCarry;
    const std::uint64_t high = firstHigh * secondHigh +
                               (secondSum >> 32U) +
                               (middleHigh << 32U);
    const std::uint64_t low = (secondSum << 32U) |
                              (lowProduct & lowMask);
    return {high, low};
}

std::uint64_t divide128WithRounding(const UInt128Product& dividend,
                                    std::uint64_t divisor) noexcept
{
    if (dividend.high == 0U)
    {
        const std::uint64_t quotient = dividend.low / divisor;
        const std::uint64_t remainder = dividend.low - quotient * divisor;
        const std::uint64_t halfRoundedUp = divisor / 2U + divisor % 2U;
        return remainder >= halfRoundedUp ? quotient + 1U : quotient;
    }

    const std::uint32_t shift = leadingZeroCount(divisor);
    const std::uint64_t normalizedDivisor = divisor << shift;
    std::uint64_t normalizedHigh =
        (dividend.high << shift) |
        (dividend.low >> (64U - shift));
    std::uint64_t normalizedLow = dividend.low << shift;
    std::uint64_t quotient = 0U;
    constexpr std::uint64_t topBit = std::uint64_t{1U} << 63U;

    for (std::uint32_t bitIndex = 0U; bitIndex < 64U; ++bitIndex)
    {
        const bool bit = (normalizedHigh & topBit) != 0U;
        normalizedHigh = (normalizedHigh << 1U) |
                         (normalizedLow >> 63U);
        normalizedLow <<= 1U;
        quotient <<= 1U;
        if (normalizedHigh >= normalizedDivisor || bit)
        {
            normalizedHigh -= normalizedDivisor;
            quotient |= 1U;
        }
    }

    const std::uint64_t remainder =
        (normalizedHigh << (64U - shift)) | (normalizedLow >> shift);
    const std::uint64_t halfRoundedUp = divisor / 2U + divisor % 2U;
    return remainder >= halfRoundedUp ? quotient + 1U : quotient;
}

std::uint64_t unsignedMagnitude(std::int64_t value) noexcept
{
    return static_cast<std::uint64_t>(value < 0 ? -value : value);
}

std::int64_t signum(std::int64_t value) noexcept
{
    return (value > 0) - (value < 0);
}

// Mirror i_overlay's canonical segment order and integer cross_point rules.
std::optional<OverlayPoint> iOverlayCrossPoint(
    const OverlayPoint& firstStart,
    const OverlayPoint& firstEnd,
    const OverlayPoint& secondStart,
    const OverlayPoint& secondEnd) noexcept
{
    const OverlayPointLess pointLess;
    OverlayPoint targetStart = firstStart;
    OverlayPoint targetEnd = firstEnd;
    OverlayPoint otherStart = secondStart;
    OverlayPoint otherEnd = secondEnd;
    if (pointLess(targetEnd, targetStart))
        std::swap(targetStart, targetEnd);
    if (pointLess(otherEnd, otherStart))
        std::swap(otherStart, otherEnd);

    const auto segmentLess = [&pointLess](const OverlayPoint& leftStart,
                                         const OverlayPoint& leftEnd,
                                         const OverlayPoint& rightStart,
                                         const OverlayPoint& rightEnd) {
        return pointLess(leftStart, rightStart) ||
               (leftStart == rightStart && pointLess(leftEnd, rightEnd));
    };
    if (segmentLess(otherStart, otherEnd, targetStart, targetEnd))
    {
        std::swap(targetStart, otherStart);
        std::swap(targetEnd, otherEnd);
    }

    const std::int64_t a0x = targetStart.x;
    const std::int64_t a0y = targetStart.y;
    const std::int64_t a1x = static_cast<std::int64_t>(targetEnd.x) - a0x;
    const std::int64_t a1y = static_cast<std::int64_t>(targetEnd.y) - a0y;
    const std::int64_t b0x = static_cast<std::int64_t>(otherStart.x) - a0x;
    const std::int64_t b0y = static_cast<std::int64_t>(otherStart.y) - a0y;
    const std::int64_t b1x = static_cast<std::int64_t>(otherEnd.x) - a0x;
    const std::int64_t b1y = static_cast<std::int64_t>(otherEnd.y) - a0y;
    const std::int64_t dyB = b0y - b1y;
    const std::int64_t dxB = b0x - b1x;
    const std::int64_t xyB = b0x * b1y - b0y * b1x;

    std::int64_t x0 = 0;
    std::int64_t y0 = 0;
    if (a1x == 0)
    {
        if (dxB == 0)
            return std::nullopt;
        y0 = xyB / dxB;
    }
    else if (a1y == 0)
    {
        if (dyB == 0)
            return std::nullopt;
        x0 = -xyB / dyB;
    }
    else
    {
        const std::int64_t divider = a1y * dxB - a1x * dyB;
        if (divider == 0)
            return std::nullopt;

        const std::int64_t direction = signum(divider) * signum(xyB);
        const std::int64_t xDirection = signum(a1x) * direction;
        const std::int64_t yDirection = signum(a1y) * direction;
        const std::uint64_t unsignedDivider = unsignedMagnitude(divider);
        const std::uint64_t unsignedXYB = unsignedMagnitude(xyB);
        const std::uint64_t unsignedX = divide128WithRounding(
            multiplyTo128(unsignedMagnitude(a1x), unsignedXYB),
            unsignedDivider);
        const std::uint64_t unsignedY = divide128WithRounding(
            multiplyTo128(unsignedMagnitude(a1y), unsignedXYB),
            unsignedDivider);
        if (unsignedX > static_cast<std::uint64_t>(
                            std::numeric_limits<std::int32_t>::max()) ||
            unsignedY > static_cast<std::uint64_t>(
                            std::numeric_limits<std::int32_t>::max()))
        {
            return std::nullopt;
        }
        x0 = xDirection * static_cast<std::int64_t>(unsignedX);
        y0 = yDirection * static_cast<std::int64_t>(unsignedY);
    }

    const std::int64_t x = x0 + a0x;
    const std::int64_t y = y0 + a0y;
    if (x < std::numeric_limits<std::int32_t>::min() ||
        x > std::numeric_limits<std::int32_t>::max() ||
        y < std::numeric_limits<std::int32_t>::min() ||
        y > std::numeric_limits<std::int32_t>::max())
    {
        return std::nullopt;
    }
    return OverlayPoint{static_cast<std::int32_t>(x),
                        static_cast<std::int32_t>(y)};
}

std::optional<std::vector<std::vector<OverlayPoint>>>
resolveSelfIntersectingOutline(const std::vector<OverlayPoint>& outline,
                               bool allowCollinearCancellationRebuild)
{
    const std::size_t pointCount = outline.size();
    const bool safeExtent = hasSafeInt64CrossProductExtent(outline);
    if (pointCount < 4U || pointCount > 8192U || !safeExtent)
        return std::nullopt;

    struct WorkingSegment
    {
        OverlayPoint a{};
        OverlayPoint b{};
        int windingDelta = 0;
    };

    const OverlayPointLess pointLess;
    bool canceledZeroWindingSegment = false;
    const auto appendDirectedSegment = [&pointLess](
                                           std::vector<WorkingSegment>& target,
                                           const OverlayPoint& from,
                                           const OverlayPoint& to,
                                           int windingDelta) {
        if (from == to)
            return;
        if (pointLess(from, to))
            target.push_back({from, to, windingDelta});
        else
            target.push_back({to, from, -windingDelta});
    };
    const auto mergeSegments = [&pointLess, &canceledZeroWindingSegment,
                                allowCollinearCancellationRebuild](
                                  std::vector<WorkingSegment>& segments) {
        std::sort(segments.begin(), segments.end(),
                  [&pointLess](const WorkingSegment& left,
                               const WorkingSegment& right) {
                      return pointLess(left.a, right.a) ||
                             (left.a == right.a &&
                              pointLess(left.b, right.b));
                  });
        std::vector<WorkingSegment> merged;
        merged.reserve(segments.size());
        for (const WorkingSegment& segment : segments)
        {
            if (!merged.empty() && merged.back().a == segment.a &&
                merged.back().b == segment.b)
            {
                merged.back().windingDelta += segment.windingDelta;
            }
            else
            {
                merged.push_back(segment);
            }
        }
        if (allowCollinearCancellationRebuild)
        {
            const auto zeroWindingEnd = std::remove_if(
                merged.begin(), merged.end(),
                [&canceledZeroWindingSegment](
                    const WorkingSegment& segment) {
                    const bool isZeroWinding = segment.windingDelta == 0;
                    canceledZeroWindingSegment =
                        canceledZeroWindingSegment || isZeroWinding;
                    return isZeroWinding;
                });
            merged.erase(zeroWindingEnd, merged.end());
        }
        segments = std::move(merged);
    };

    std::vector<WorkingSegment> segments;
    segments.reserve(pointCount);
    for (std::size_t i = 0U; i < pointCount; ++i)
    {
        appendDirectedSegment(segments, outline[i],
                              outline[(i + 1U) % pointCount], 1);
    }
    mergeSegments(segments);
    const bool initiallyCanceledZeroWindingSegment =
        canceledZeroWindingSegment;

    bool needToFix = true;
    bool hasIntersectionMarks = false;
    std::int64_t snapRadius = 1;
    for (std::size_t round = 0U; needToFix; ++round)
    {
        if (round >= 64U || segments.size() > 8192U)
            return std::nullopt;

        needToFix = false;
        std::vector<std::vector<OverlayPoint>> marks(segments.size());
        const auto appendMark = [&segments, &marks](std::size_t segment,
                                                    const OverlayPoint& point) {
            if (point != segments[segment].a && point != segments[segment].b)
                marks[segment].push_back(point);
        };

        for (std::size_t first = 0U; first + 1U < segments.size(); ++first)
        {
            const WorkingSegment& target = segments[first];
            const OverlayPoint& a = target.a;
            const OverlayPoint& b = target.b;
            const std::int32_t targetMinY = std::min(a.y, b.y);
            const std::int32_t targetMaxY = std::max(a.y, b.y);
            for (std::size_t second = first + 1U; second < segments.size();
                 ++second)
            {
                const WorkingSegment& other = segments[second];
                const OverlayPoint& c = other.a;
                const OverlayPoint& d = other.b;
                if (b.x < c.x)
                    break;
                if (std::min(c.y, d.y) > targetMaxY ||
                    std::max(c.y, d.y) < targetMinY ||
                    !segmentsIntersect(a, b, c, d))
                {
                    continue;
                }

                const std::int64_t aSideC = crossInt(a, b, c);
                const std::int64_t aSideD = crossInt(a, b, d);
                const std::int64_t cSideA = crossInt(c, d, a);
                const std::int64_t cSideB = crossInt(c, d, b);
                const bool strictCross =
                    ((aSideC < 0 && aSideD > 0) ||
                     (aSideC > 0 && aSideD < 0)) &&
                    ((cSideA < 0 && cSideB > 0) ||
                     (cSideA > 0 && cSideB < 0));

                if (strictCross)
                {
                    const auto intersection = iOverlayCrossPoint(a, b, c, d);
                    if (!intersection.has_value())
                        return std::nullopt;

                    OverlayPoint splitPoint = *intersection;
                    const bool isRound =
                        crossInt(a, b, splitPoint) != 0 ||
                        crossInt(c, d, splitPoint) != 0;
                    if (isRound)
                    {
                        const auto squaredDistance = [](
                                                        const OverlayPoint& left,
                                                        const OverlayPoint& right) {
                            const std::int64_t dx =
                                static_cast<std::int64_t>(left.x) - right.x;
                            const std::int64_t dy =
                                static_cast<std::int64_t>(left.y) - right.y;
                            return dx * dx + dy * dy;
                        };
                        const std::int64_t ra0 = squaredDistance(a, splitPoint);
                        const std::int64_t rb0 = squaredDistance(b, splitPoint);
                        const std::int64_t ra1 = squaredDistance(c, splitPoint);
                        const std::int64_t rb1 = squaredDistance(d, splitPoint);

                        bool snapped = false;
                        if (ra0 <= snapRadius || ra1 <= snapRadius ||
                            rb0 <= snapRadius || rb1 <= snapRadius)
                        {
                            const std::int64_t r0 = std::min(ra0, rb0);
                            const std::int64_t r1 = std::min(ra1, rb1);
                            if (r0 <= r1)
                            {
                                splitPoint = ra0 < rb0 ? a : b;
                                if (crossInt(c, splitPoint, d) != 0)
                                {
                                    appendMark(second, splitPoint);
                                    snapped = true;
                                }
                            }
                            else
                            {
                                splitPoint = ra1 < rb1 ? c : d;
                                if (crossInt(a, splitPoint, b) != 0)
                                {
                                    appendMark(first, splitPoint);
                                    snapped = true;
                                }
                            }
                        }

                        if (!snapped)
                        {
                            appendMark(first, *intersection);
                            appendMark(second, *intersection);
                        }
                        needToFix = needToFix || isRound;
                    }
                    else
                    {
                        appendMark(first, splitPoint);
                        appendMark(second, splitPoint);
                    }
                    continue;
                }

                if (aSideC == 0 && aSideD == 0 && cSideA == 0 && cSideB == 0)
                {
                    for (const OverlayPoint& point : {a, b})
                    {
                        if (pointOnSegment(point, c, d))
                            appendMark(second, point);
                    }
                    for (const OverlayPoint& point : {c, d})
                    {
                        if (pointOnSegment(point, a, b))
                            appendMark(first, point);
                    }
                }
                else
                {
                    for (const OverlayPoint& point : {a, b, c, d})
                    {
                        if (!pointOnSegment(point, a, b) ||
                            !pointOnSegment(point, c, d))
                        {
                            continue;
                        }
                        appendMark(first, point);
                        appendMark(second, point);
                    }
                }

            }
        }

        const bool hasMarks = std::any_of(
            marks.begin(), marks.end(),
            [](const std::vector<OverlayPoint>& segmentMarks) {
                return !segmentMarks.empty();
            });
        if (!hasMarks)
            break;
        hasIntersectionMarks = true;

        std::vector<WorkingSegment> splitSegments;
        splitSegments.reserve(segments.size() +
                              std::accumulate(marks.begin(), marks.end(),
                                              std::size_t{0U},
                                              [](std::size_t total,
                                                 const auto& segmentMarks) {
                                                  return total +
                                                         segmentMarks.size();
                                              }));
        for (std::size_t i = 0U; i < segments.size(); ++i)
        {
            const WorkingSegment& segment = segments[i];
            auto& segmentMarks = marks[i];
            std::sort(segmentMarks.begin(), segmentMarks.end(), pointLess);
            segmentMarks.erase(std::unique(segmentMarks.begin(),
                                           segmentMarks.end()),
                               segmentMarks.end());
            std::size_t groupStart = 0U;
            while (groupStart < segmentMarks.size())
            {
                std::size_t groupEnd = groupStart + 1U;
                while (groupEnd < segmentMarks.size() &&
                       segmentMarks[groupEnd].x ==
                           segmentMarks[groupStart].x)
                {
                    ++groupEnd;
                }
                const std::int32_t y0 =
                    groupStart == 0U ? segment.a.y
                                     : segmentMarks[groupStart - 1U].y;
                const std::int32_t y1 =
                    groupEnd == segmentMarks.size() ? segment.b.y
                                                     : segmentMarks[groupEnd].y;
                if (groupStart + 1U < groupEnd && y0 > y1)
                {
                    std::reverse(segmentMarks.begin() +
                                     static_cast<std::ptrdiff_t>(groupStart),
                                 segmentMarks.begin() +
                                     static_cast<std::ptrdiff_t>(groupEnd));
                }
                groupStart = groupEnd;
            }
            OverlayPoint previous = segment.a;
            for (const OverlayPoint& point : segmentMarks)
            {
                appendDirectedSegment(splitSegments, previous, point,
                                      segment.windingDelta);
                previous = point;
            }
            appendDirectedSegment(splitSegments, previous, segment.b,
                                  segment.windingDelta);
        }
        mergeSegments(splitSegments);
        segments = std::move(splitSegments);
        snapRadius = std::min<std::int64_t>(snapRadius * 2, 1LL << 60);
    }

    if (!hasIntersectionMarks &&
        !(allowCollinearCancellationRebuild &&
          initiallyCanceledZeroWindingSegment))
    {
        return std::nullopt;
    }

    std::map<OverlayPoint, std::size_t, OverlayPointLess> nodeIds;
    std::vector<OverlayPoint> nodes;
    std::map<std::pair<std::size_t, std::size_t>, int> edgeWinding;
    const auto getNode = [&nodeIds, &nodes](const OverlayPoint& point) {
        const auto found = nodeIds.find(point);
        if (found != nodeIds.end())
            return found->second;
        const std::size_t id = nodes.size();
        nodeIds.emplace(point, id);
        nodes.push_back(point);
        return id;
    };
    for (const WorkingSegment& segment : segments)
    {
        const std::size_t from = getNode(segment.a);
        const std::size_t to = getNode(segment.b);
        if (from != to)
        {
            const auto key = std::minmax(from, to);
            edgeWinding[key] += from < to ? segment.windingDelta
                                          : -segment.windingDelta;
        }
    }
    if (edgeWinding.empty())
        return std::nullopt;

    std::vector<OutlineHalfEdge> halfEdges;
    halfEdges.reserve(edgeWinding.size() * 2U);
    std::vector<std::vector<std::size_t>> outgoing(nodes.size());
    for (const auto& [edge, windingDelta] : edgeWinding)
    {
        const std::size_t forward = halfEdges.size();
        const std::size_t reverse = forward + 1U;
        halfEdges.push_back(
            {edge.first, edge.second, reverse, 0U,
             std::numeric_limits<std::size_t>::max(), false, windingDelta});
        halfEdges.push_back(
            {edge.second, edge.first, forward, 0U,
             std::numeric_limits<std::size_t>::max(), false, -windingDelta});
        outgoing[edge.first].push_back(forward);
        outgoing[edge.second].push_back(reverse);
    }

    for (std::size_t node = 0U; node < outgoing.size(); ++node)
    {
        std::sort(outgoing[node].begin(), outgoing[node].end(),
                  [&halfEdges, &nodes, node](std::size_t left,
                                             std::size_t right) {
                      const OverlayPoint& origin = nodes[node];
                      const OverlayPoint& leftPoint =
                          nodes[halfEdges[left].to];
                      const OverlayPoint& rightPoint =
                          nodes[halfEdges[right].to];
                      const double leftAngle = std::atan2(
                          static_cast<double>(leftPoint.y) - origin.y,
                          static_cast<double>(leftPoint.x) - origin.x);
                      const double rightAngle = std::atan2(
                          static_cast<double>(rightPoint.y) - origin.y,
                          static_cast<double>(rightPoint.x) - origin.x);
                      return leftAngle == rightAngle ? left < right
                                                     : leftAngle < rightAngle;
                  });
    }

    for (std::size_t edge = 0U; edge < halfEdges.size(); ++edge)
    {
        const std::size_t destination = halfEdges[edge].to;
        const auto& adjacent = outgoing[destination];
        const auto twin = std::find(adjacent.begin(), adjacent.end(),
                                    halfEdges[edge].twin);
        if (twin == adjacent.end() || adjacent.empty())
            return std::nullopt;
        const std::size_t twinIndex =
            static_cast<std::size_t>(twin - adjacent.begin());
        halfEdges[edge].next =
            adjacent[(twinIndex + adjacent.size() - 1U) % adjacent.size()];
    }

    struct Face
    {
        std::vector<std::size_t> edges;
        long double areaTwice = 0.0L;
        bool filled = false;
    };
    std::vector<Face> faces;
    const std::size_t invalidFace = std::numeric_limits<std::size_t>::max();
    for (std::size_t start = 0U; start < halfEdges.size(); ++start)
    {
        if (halfEdges[start].face != invalidFace)
            continue;
        Face face;
        std::size_t current = start;
        for (std::size_t step = 0U; step <= halfEdges.size(); ++step)
        {
            if (current == start && !face.edges.empty())
                break;
            if (halfEdges[current].face != invalidFace)
                return std::nullopt;
            halfEdges[current].face = faces.size();
            face.edges.push_back(current);
            current = halfEdges[current].next;
        }
        if (face.edges.empty() || current != start)
            return std::nullopt;

        for (const std::size_t edgeIndex : face.edges)
        {
            const OverlayPoint& a = nodes[halfEdges[edgeIndex].from];
            const OverlayPoint& b = nodes[halfEdges[edgeIndex].to];
            face.areaTwice += static_cast<long double>(a.x) * b.y -
                              static_cast<long double>(a.y) * b.x;
        }
        faces.push_back(std::move(face));
    }

    if (faces.empty())
        return std::nullopt;
    std::vector<std::optional<int>> faceWindings(faces.size());
    std::vector<std::size_t> pendingFaces;
    // Disconnected outline components each have their own exterior face cycle.
    for (std::size_t i = 0U; i < faces.size(); ++i)
    {
        if (faces[i].areaTwice < 0.0L)
        {
            faceWindings[i] = 0;
            pendingFaces.push_back(i);
        }
    }
    if (pendingFaces.empty())
    {
        const auto exteriorFace = std::min_element(
            faces.begin(), faces.end(),
            [](const Face& left, const Face& right) {
                return left.areaTwice < right.areaTwice;
            });
        const std::size_t exteriorIndex =
            static_cast<std::size_t>(exteriorFace - faces.begin());
        faceWindings[exteriorIndex] = 0;
        pendingFaces.push_back(exteriorIndex);
    }
    for (std::size_t cursor = 0U; cursor < pendingFaces.size(); ++cursor)
    {
        const std::size_t faceIndex = pendingFaces[cursor];
        const int currentWinding = *faceWindings[faceIndex];
        for (const std::size_t edgeIndex : faces[faceIndex].edges)
        {
            const OutlineHalfEdge& edge = halfEdges[edgeIndex];
            const std::size_t leftFace = edge.face;
            const std::size_t rightFace = halfEdges[edge.twin].face;
            const bool currentIsLeft = leftFace == faceIndex;
            const std::size_t adjacentFace =
                currentIsLeft ? rightFace : leftFace;
            const int adjacentWinding =
                currentIsLeft ? currentWinding - edge.windingDelta
                              : currentWinding + edge.windingDelta;
            if (!faceWindings[adjacentFace].has_value())
            {
                faceWindings[adjacentFace] = adjacentWinding;
                pendingFaces.push_back(adjacentFace);
            }
            else if (*faceWindings[adjacentFace] != adjacentWinding)
            {
                return std::nullopt;
            }
        }
    }
    if (pendingFaces.size() != faces.size())
        return std::nullopt;
    for (std::size_t i = 0U; i < faces.size(); ++i)
        faces[i].filled = *faceWindings[i] > 0;

    for (OutlineHalfEdge& edge : halfEdges)
    {
        const bool leftFilled = faces[edge.face].filled;
        const bool rightFilled = faces[halfEdges[edge.twin].face].filled;
        edge.boundary = leftFilled && !rightFilled;
    }

    std::vector<bool> visited(halfEdges.size(), false);
    std::vector<std::vector<OverlayPoint>> contours;
    for (std::size_t start = 0U; start < halfEdges.size(); ++start)
    {
        if (!halfEdges[start].boundary || visited[start])
            continue;
        std::vector<OverlayPoint> contour;
        std::size_t current = start;
        bool closed = false;
        for (std::size_t step = 0U; step <= halfEdges.size(); ++step)
        {
            if (current == start && !contour.empty())
            {
                closed = true;
                break;
            }
            if (visited[current] || !halfEdges[current].boundary)
                return std::nullopt;
            visited[current] = true;
            contour.push_back(nodes[halfEdges[current].from]);

            const std::size_t destination = halfEdges[current].to;
            const auto& adjacent = outgoing[destination];
            const auto twin = std::find(adjacent.begin(), adjacent.end(),
                                        halfEdges[current].twin);
            if (twin == adjacent.end())
                return std::nullopt;
            const std::size_t twinIndex =
                static_cast<std::size_t>(twin - adjacent.begin());
            bool foundNext = false;
            for (std::size_t turn = 1U; turn <= adjacent.size(); ++turn)
            {
                const std::size_t candidate =
                    adjacent[(twinIndex + adjacent.size() - turn) %
                             adjacent.size()];
                if (halfEdges[candidate].boundary)
                {
                    current = candidate;
                    foundNext = true;
                    break;
                }
            }
            if (!foundNext)
                return std::nullopt;
        }
        if (!closed || contour.size() < 3U)
            return std::nullopt;

        bool removed = true;
        while (removed && contour.size() > 3U)
        {
            removed = false;
            for (std::size_t i = 0U; i < contour.size(); ++i)
            {
                const OverlayPoint& previous =
                    contour[(i + contour.size() - 1U) % contour.size()];
                const OverlayPoint& currentPoint = contour[i];
                const OverlayPoint& next = contour[(i + 1U) % contour.size()];
                const long double between =
                    (static_cast<long double>(currentPoint.x) - previous.x) *
                        (static_cast<long double>(currentPoint.x) - next.x) +
                    (static_cast<long double>(currentPoint.y) - previous.y) *
                        (static_cast<long double>(currentPoint.y) - next.y);
                if (currentPoint == previous || currentPoint == next ||
                    (crossInt(previous, currentPoint, next) == 0 &&
                     between <= 0.0L))
                {
                    contour.erase(contour.begin() +
                                  static_cast<std::ptrdiff_t>(i));
                    removed = true;
                    break;
                }
            }
        }
        long double areaTwice = 0.0L;
        for (std::size_t i = 0U; i < contour.size(); ++i)
        {
            const OverlayPoint& a = contour[i];
            const OverlayPoint& b = contour[(i + 1U) % contour.size()];
            areaTwice += static_cast<long double>(a.x) * b.y -
                         static_cast<long double>(a.y) * b.x;
        }
        if (areaTwice <= 0.0L)
            continue;

        auto first = std::min_element(
            contour.begin(), contour.end(), [](const OverlayPoint& left,
                                               const OverlayPoint& right) {
                return left.x < right.x ||
                       (left.x == right.x && left.y > right.y);
            });
        const std::int32_t minX = first->x;
        const auto minXCount = std::count_if(
            contour.begin(), contour.end(), [minX](const OverlayPoint& point) {
                return point.x == minX;
            });
        if (minXCount == 1U)
            first = first == contour.begin() ? contour.end() - 1 : first - 1;
        std::rotate(contour.begin(), first, contour.end());
        contours.push_back(std::move(contour));
    }

    if (contours.empty())
        return std::vector<std::vector<OverlayPoint>>{};
    std::stable_sort(
        contours.begin(), contours.end(),
        [](const std::vector<OverlayPoint>& left,
           const std::vector<OverlayPoint>& right) {
            const auto leftMin = std::min_element(
                left.begin(), left.end(), OverlayPointLess{});
            const auto rightMin = std::min_element(
                right.begin(), right.end(), OverlayPointLess{});
            return OverlayPointLess{}(*leftMin, *rightMin);
        });
    return contours;
}

bool isConvexCounterClockwise(const SimplePolygon& shape, double distance)
{
    if (shape.VertexCount() < 3U)
        return false;

    const auto adapter = makeOverlayAdapter(shape, distance);
    if (!adapter.has_value())
        return false;

    std::vector<OverlayPoint> points;
    points.reserve(shape.VertexCount());
    for (const Point2d& point : shape.Vertices())
    {
        const auto converted = adapter->toInt(point);
        if (!converted.has_value())
            return false;
        if (points.empty() || points.back() != *converted)
            points.push_back(*converted);
    }
    if (points.size() > 1U && points.front() == points.back())
        points.pop_back();
    if (points.size() < 3U)
        return false;

    long double signedAreaTwice = 0.0L;
    bool hasPositiveTurn = false;
    for (std::size_t i = 0U; i < points.size(); ++i)
    {
        const OverlayPoint& previous = points[(i + points.size() - 1U) % points.size()];
        const OverlayPoint& current = points[i];
        const OverlayPoint& next = points[(i + 1U) % points.size()];
        const long double firstX =
            static_cast<long double>(current.x) - previous.x;
        const long double firstY =
            static_cast<long double>(current.y) - previous.y;
        const long double secondX =
            static_cast<long double>(next.x) - current.x;
        const long double secondY =
            static_cast<long double>(next.y) - current.y;
        const long double turn = firstX * secondY - firstY * secondX;
        if (turn < 0.0L)
            return false;
        hasPositiveTurn = hasPositiveTurn || turn > 0.0L;
        signedAreaTwice +=
            static_cast<long double>(current.x) * next.y -
            static_cast<long double>(current.y) * next.x;
    }
    return hasPositiveTurn && signedAreaTwice > 0.0L;
}

std::optional<SimplePolygon> rotateConvexContourToIOverlayStart(
    const SimplePolygon& polygon,
    const SimplePolygon& source,
    double distance)
{
    if (polygon.VertexCount() < 3U)
        return std::nullopt;

    const auto adapter = makeOverlayAdapter(source, distance);
    if (!adapter.has_value())
        return std::nullopt;

    std::vector<Point2d> ordered = polygon.Vertices();
    std::vector<OverlayPoint> quantized;
    quantized.reserve(ordered.size());
    for (const Point2d& point : ordered)
    {
        const auto converted = adapter->toInt(point);
        if (!converted.has_value())
            return std::nullopt;
        quantized.push_back(*converted);
    }

    if (ordered.size() > 1U && quantized.front() == quantized.back())
    {
        ordered.pop_back();
        quantized.pop_back();
    }
    bool removed = true;
    while (removed && ordered.size() > 3U)
    {
        removed = false;
        for (std::size_t i = 0U; i < ordered.size(); ++i)
        {
            const std::size_t previous =
                (i + ordered.size() - 1U) % ordered.size();
            const std::size_t next = (i + 1U) % ordered.size();
            const OverlayPoint& a = quantized[previous];
            const OverlayPoint& b = quantized[i];
            const OverlayPoint& c = quantized[next];
            const long double firstX =
                static_cast<long double>(b.x) - a.x;
            const long double firstY =
                static_cast<long double>(b.y) - a.y;
            const long double secondX =
                static_cast<long double>(c.x) - b.x;
            const long double secondY =
                static_cast<long double>(c.y) - b.y;
            const long double between =
                (static_cast<long double>(b.x) - a.x) *
                    (static_cast<long double>(b.x) - c.x) +
                (static_cast<long double>(b.y) - a.y) *
                    (static_cast<long double>(b.y) - c.y);
            if (b == a || b == c ||
                (firstX * secondY - firstY * secondX == 0.0L &&
                 between <= 0.0L))
            {
                ordered.erase(ordered.begin() +
                              static_cast<std::ptrdiff_t>(i));
                quantized.erase(quantized.begin() +
                                static_cast<std::ptrdiff_t>(i));
                removed = true;
                break;
            }
        }
    }
    if (ordered.size() < 3U)
        return std::nullopt;

    std::size_t first = 0U;
    for (std::size_t i = 1U; i < quantized.size(); ++i)
    {
        if (quantized[i].x < quantized[first].x ||
            (quantized[i].x == quantized[first].x &&
             quantized[i].y > quantized[first].y))
        {
            first = i;
        }
    }
    const std::int32_t minX = quantized[first].x;
    const auto minXCount = std::count_if(
        quantized.begin(), quantized.end(), [minX](const OverlayPoint& point) {
            return point.x == minX;
        });
    const std::size_t start = minXCount == 1U
                                  ? (first + ordered.size() - 1U) % ordered.size()
                                  : first;
    std::rotate(ordered.begin(), ordered.begin() + start, ordered.end());
    const auto imported = detail::createRustImportedOffsetPolygon(
        std::move(ordered));
    if (!imported.HasValue())
        return std::nullopt;
    return imported.Value();
}

// A simple contour with no self-intersections can use the same quantized
// round-join outline path for either inflate or deflate. Topology changes and
// self-intersections still require the overlay stage.
std::optional<std::vector<SimplePolygon>> simpleOffsetWithIOverlayOutlineRules(
    const SimplePolygon& shape, ShapeModifyMode mode, double distance)
{
    if (shape.VertexCount() < 3U || !(distance > 0.0))
        return std::nullopt;

    const auto adapter = makeOverlayAdapter(shape, distance);
    if (!adapter.has_value())
        return std::nullopt;

    std::vector<OverlayPoint> source;
    source.reserve(shape.VertexCount());
    for (const Point2d& point : shape.Vertices())
    {
        const auto converted = adapter->toInt(point);
        if (!converted.has_value())
            return std::nullopt;
        if (source.empty() || source.back() != *converted)
            source.push_back(*converted);
    }
    if (source.size() > 1U && source.front() == source.back())
        source.pop_back();
    if (!isCounterClockwise(source))
        return std::nullopt;

    bool sourceHasCollinearVertex = false;
    for (std::size_t i = 0U; i < source.size(); ++i)
    {
        const OverlayPoint& previous =
            source[(i + source.size() - 1U) % source.size()];
        const OverlayPoint& current = source[i];
        const OverlayPoint& next = source[(i + 1U) % source.size()];
        if (previous != next && pointOnSegment(current, previous, next))
        {
            sourceHasCollinearVertex = true;
            break;
        }
    }

    constexpr double roundRatio = 0.1;
    const double limitDotProduct = std::cos(roundRatio);
    const double signedRadius =
        mode == ShapeModifyMode::Inflate ? -distance : distance;
    const double rotationDirection = signedRadius >= 0.0 ? -1.0 : 1.0;

    std::vector<OverlaySection> sections;
    sections.reserve(source.size());
    for (std::size_t i = 0U; i < source.size(); ++i)
    {
        const OverlayPoint a = source[i];
        const OverlayPoint b = source[(i + 1U) % source.size()];
        const Point2d floatA = adapter->toFloat(a);
        const Point2d floatB = adapter->toFloat(b);
        const double dx = floatB.x - floatA.x;
        const double dy = floatB.y - floatA.y;
        const double length = std::sqrt(dx * dx + dy * dy);
        if (!(length > 0.0) || !std::isfinite(length))
            return std::nullopt;

        const Point2d direction{dx / length, dy / length};
        const Point2d normal{-direction.y * signedRadius,
                             direction.x * signedRadius};
        const auto aTop = adapter->toInt(
            {floatA.x + normal.x, floatA.y + normal.y});
        const auto bTop = adapter->toInt(
            {floatB.x + normal.x, floatB.y + normal.y});
        if (!aTop.has_value() || !bTop.has_value())
            return std::nullopt;
        sections.push_back({a, b, *aTop, *bTop, direction});
    }

    std::vector<OverlayPoint> outline;
    outline.reserve(source.size() * 18U);
    const auto appendOutlinePoint = [&outline](const OverlayPoint& point) {
        if (outline.empty() || outline.back() != point)
            outline.push_back(point);
    };
    appendOutlinePoint(sections.front().aTop);
    const double inverseRoundRatio = 1.0 / roundRatio;
    for (std::size_t i = 0U; i < sections.size(); ++i)
    {
        const OverlaySection& previous = sections[i];
        const OverlaySection& next = sections[(i + 1U) % sections.size()];
        appendOutlinePoint(previous.bTop);

        const std::int64_t nextX =
            static_cast<std::int64_t>(next.b.x) - next.a.x;
        const std::int64_t nextY =
            static_cast<std::int64_t>(next.b.y) - next.a.y;
        const std::int64_t previousX =
            static_cast<std::int64_t>(previous.b.x) - previous.a.x;
        const std::int64_t previousY =
            static_cast<std::int64_t>(previous.b.y) - previous.a.y;
        const std::int64_t cornerCross =
            nextX * previousY - nextY * previousX;
        const std::int64_t cornerDot =
            nextX * previousX + nextY * previousY;
        const bool outerCorner =
            cornerCross != 0
                ? ((cornerCross > 0) == (signedRadius > 0.0))
                : cornerDot < 0;

        if (!outerCorner)
        {
            // i_overlay closes an inner corner through the source vertex;
            // intersecting the offset sections here changes the segment set.
            if (previous.bTop != previous.b)
                appendOutlinePoint(previous.b);
            if (next.aTop != next.a)
                appendOutlinePoint(next.aTop);
            continue;
        }

        if (previous.bTop == next.aTop)
            continue;

        const double dot = previous.direction.x * next.direction.x +
                           previous.direction.y * next.direction.y;
        if (limitDotProduct < dot)
        {
            appendOutlinePoint(next.aTop);
            continue;
        }

        const double angle = std::acos(std::clamp(dot, -1.0, 1.0));
        const auto segmentCount = static_cast<std::size_t>(
            angle * inverseRoundRatio + 0.5);
        if (segmentCount == 0U)
            return std::nullopt;

        const double deltaAngle = angle / static_cast<double>(segmentCount);
        const double rotationAngle = rotationDirection * deltaAngle;
        const double rotationSin = std::sin(rotationAngle);
        const double rotationCos = std::cos(rotationAngle);
        const double centerX = adapter->toFloat(previous.b).x;
        const double centerY = adapter->toFloat(previous.b).y;
        double vectorX = -previous.direction.y;
        double vectorY = previous.direction.x;
        OverlayPoint last = previous.bTop;
        for (std::size_t step = 1U; step < segmentCount; ++step)
        {
            const double rotatedX =
                rotationCos * vectorX - rotationSin * vectorY;
            const double rotatedY =
                rotationSin * vectorX + rotationCos * vectorY;
            vectorX = rotatedX;
            vectorY = rotatedY;
            const Point2d point{
                centerX + vectorX * signedRadius,
                centerY + vectorY * signedRadius};
            const auto converted = adapter->toInt(point);
            if (!converted.has_value())
                return std::nullopt;
            if (last != *converted)
            {
                appendOutlinePoint(*converted);
                last = *converted;
            }
        }
        if (last != next.aTop)
            appendOutlinePoint(next.aTop);
    }

    if (outline.size() < 3U)
        return std::nullopt;

    // i_overlay rebuilds every path through build_graph_view +
    // extract_contours_into (mesh/outline/offset.rs), and the default
    // OverlayOptions have preserve_output_collinear == false, so vertices
    // that are exactly collinear pass-throughs on the integer grid are
    // dropped during contour extraction. Mirror that on the outline ring
    // before choosing the canonical contour start; otherwise quantization
    // kinks below the adapter resolution survive as extra ring vertices.
    const auto isIntPassThrough = [](const OverlayPoint& a,
                                     const OverlayPoint& b,
                                     const OverlayPoint& c) {
        const std::int64_t abX = static_cast<std::int64_t>(b.x) - a.x;
        const std::int64_t abY = static_cast<std::int64_t>(b.y) - a.y;
        const std::int64_t bcX = static_cast<std::int64_t>(c.x) - b.x;
        const std::int64_t bcY = static_cast<std::int64_t>(c.y) - b.y;
        return abX * bcY - abY * bcX == 0 && abX * bcX + abY * bcY > 0;
    };
    bool simplifiedAgain = true;
    while (simplifiedAgain && outline.size() > 3U)
    {
        simplifiedAgain = false;
        std::size_t anchor = outline.size();
        for (std::size_t i = 0U; i < outline.size(); ++i)
        {
            if (!isIntPassThrough(
                    outline[(i + outline.size() - 1U) % outline.size()],
                    outline[i], outline[(i + 1U) % outline.size()]))
            {
                anchor = i;
                break;
            }
        }
        if (anchor == outline.size())
            break; // fully collinear ring; leave it for the import to reject
        std::vector<OverlayPoint> simplified;
        simplified.reserve(outline.size());
        for (std::size_t k = 0U; k < outline.size(); ++k)
        {
            const OverlayPoint& candidate =
                outline[(anchor + k) % outline.size()];
            if (simplified.size() >= 2U &&
                isIntPassThrough(simplified[simplified.size() - 2U],
                                 simplified.back(), candidate))
            {
                simplified.back() = candidate;
            }
            else
            {
                simplified.push_back(candidate);
            }
        }
        while (simplified.size() > 3U &&
               isIntPassThrough(simplified[simplified.size() - 2U],
                                simplified.back(), simplified.front()))
        {
            simplified.pop_back();
        }
        simplifiedAgain = simplified.size() != outline.size();
        outline = std::move(simplified);
    }
    if (outline.size() < 3U)
        return std::nullopt;

    // Match i_overlay's tested left-top contour start. A vertical leftmost
    // edge starts at its upper endpoint; with a unique leftmost point, the
    // extracted contour starts at its predecessor in the CCW ring.
    auto first = std::min_element(
        outline.begin(), outline.end(), [](const OverlayPoint& left,
                                           const OverlayPoint& right) {
            return left.x < right.x ||
                   (left.x == right.x && left.y > right.y);
        });
    const std::int32_t minX = first->x;
    const auto minXCount = std::count_if(
        outline.begin(), outline.end(), [minX](const OverlayPoint& point) {
            return point.x == minX;
        });
    if (minXCount == 1U)
    {
        first = first == outline.begin() ? outline.end() - 1 : first - 1;
    }
    std::rotate(outline.begin(), first, outline.end());

    // Quantization can collapse adjacent offset sections onto the same point.
    outline.erase(std::unique(outline.begin(), outline.end()), outline.end());
    if (outline.size() > 1U && outline.front() == outline.back())
        outline.pop_back();
    if (outline.size() < 3U)
        return std::nullopt;

    std::vector<Point2d> points;
    points.reserve(outline.size());
    for (const OverlayPoint& point : outline)
        points.push_back(adapter->toFloat(point));
    const auto polygon = detail::createRustImportedOffsetPolygon(std::move(points));
    if (polygon.HasValue())
        return std::vector<SimplePolygon>{polygon.Value()};

    const auto contours = resolveSelfIntersectingOutline(
        outline, sourceHasCollinearVertex);
    if (!contours.has_value())
        return std::nullopt;

    std::vector<SimplePolygon> components;
    components.reserve(contours->size());
    for (const std::vector<OverlayPoint>& contour : *contours)
    {
        std::vector<Point2d> contourPoints;
        contourPoints.reserve(contour.size());
        for (const OverlayPoint& point : contour)
            contourPoints.push_back(adapter->toFloat(point));
        const auto imported = detail::createRustImportedOffsetPolygon(
            std::move(contourPoints));
        if (!imported.HasValue())
            return std::nullopt;
        components.push_back(imported.Value());
    }
    return components;
}

Result<SimplePolygon> reimportOffsetPolygon(const SimplePolygon& polygon)
{
    return detail::createRustImportedOffsetPolygon(polygon.Vertices());
}

std::optional<Point2d> intersectLines(const Line2d& first, const Line2d& second)
{
    const double firstDx = first.end.x - first.start.x;
    const double firstDy = first.end.y - first.start.y;
    const double secondDx = second.end.x - second.start.x;
    const double secondDy = second.end.y - second.start.y;
    const double denominator = firstDx * secondDy - firstDy * secondDx;
    const double determinantScale =
        std::hypot(firstDx, firstDy) * std::hypot(secondDx, secondDy);
    if (!(determinantScale > 0.0) ||
        std::abs(denominator) <= 1.0e-12 * determinantScale)
        return std::nullopt;

    const double dx = second.start.x - first.start.x;
    const double dy = second.start.y - first.start.y;
    const double t = (dx * secondDy - dy * secondDx) / denominator;
    return Point2d{first.start.x + t * firstDx, first.start.y + t * firstDy};
}

Point2d outwardNormal(const Point2d& start, const Point2d& end)
{
    const double dx = end.x - start.x;
    const double dy = end.y - start.y;
    const double length = std::sqrt(dx * dx + dy * dy);
    if (length <= 1.0e-12)
        return {};
    return Point2d{dy / length, -dx / length};
}

double turnAt(const Point2d& previous,
              const Point2d& current,
              const Point2d& next) noexcept
{
    const double firstX = current.x - previous.x;
    const double firstY = current.y - previous.y;
    const double secondX = next.x - current.x;
    const double secondY = next.y - current.y;
    return firstX * secondY - firstY * secondX;
}

void appendRoundedJoin(std::vector<Point2d>& points,
                       const Point2d& center,
                       const Point2d& previousNormal,
                       const Point2d& nextNormal,
                       double radius,
                       double turn)
{
    constexpr double maxAngularStep = 0.1;
    const double pi = std::acos(-1.0);
    const double twoPi = 2.0 * pi;
    const double startAngle = std::atan2(previousNormal.y, previousNormal.x);
    const double endAngle = std::atan2(nextNormal.y, nextNormal.x);
    double delta = endAngle - startAngle;

    if (turn > 0.0)
    {
        while (delta <= 0.0)
            delta += twoPi;
        if (delta > pi)
            delta -= twoPi;
    }
    else
    {
        while (delta >= 0.0)
            delta -= twoPi;
        if (delta < -pi)
            delta += twoPi;
    }

    const std::size_t steps = std::max<std::size_t>(
        1, static_cast<std::size_t>(std::ceil(std::abs(delta) / maxAngularStep)));
    for (std::size_t step = 0; step <= steps; ++step)
    {
        const double ratio = static_cast<double>(step) /
                             static_cast<double>(steps);
        const double angle = startAngle + delta * ratio;
        points.push_back({center.x + radius * std::cos(angle),
                          center.y + radius * std::sin(angle)});
    }
}

bool samePolygonCycle(const std::vector<Point2d>& first,
                      const std::vector<Point2d>& second)
{
    if (first.size() != second.size())
        return false;
    if (first.empty())
        return true;

    for (std::size_t start = 0; start < second.size(); ++start)
    {
        if (!(second[start] == first.front()))
            continue;
        bool matches = true;
        for (std::size_t i = 0; i < first.size(); ++i)
        {
            if (!(first[i] == second[(start + i) % second.size()]))
            {
                matches = false;
                break;
            }
        }
        if (matches)
            return true;
    }
    return false;
}

bool preservesModificationMode(const SimplePolygon& original,
                               const SimplePolygon& modified,
                               ShapeModifyMode mode)
{
    if (modified.Area() == original.Area())
    {
        return samePolygonCycle(original.Vertices(), modified.Vertices());
    }

    constexpr double tolerance = 1.0e-8;
    if (mode == ShapeModifyMode::Inflate)
    {
        if (modified.Area() < original.Area())
            return false;
        for (const Point2d& point : original.Vertices())
        {
            if (!modified.Contains(point, tolerance))
                return false;
        }
        return true;
    }

    if (modified.Area() > original.Area())
        return false;
    for (const Point2d& point : modified.Vertices())
    {
        if (!original.Contains(point, tolerance))
            return false;
    }
    return true;
}

Result<SimplePolygon> fallbackOffsetShape(const SimplePolygon& shape,
                                          ShapeModifyMode mode,
                                          double distance)
{
    const double signedDistance =
        mode == ShapeModifyMode::Inflate ? distance : -distance;
    const auto& points = shape.Vertices();
    std::vector<Point2d> offsetPoints;
    offsetPoints.reserve(points.size() * 4);

    for (std::size_t i = 0; i < points.size(); ++i)
    {
        const std::size_t previous = (i + points.size() - 1) % points.size();
        const std::size_t next = (i + 1) % points.size();

        const Point2d previousNormal = outwardNormal(points[previous], points[i]);
        const Point2d nextNormal = outwardNormal(points[i], points[next]);

        const Line2d previousLine{
            {points[previous].x + previousNormal.x * signedDistance,
             points[previous].y + previousNormal.y * signedDistance},
            {points[i].x + previousNormal.x * signedDistance,
             points[i].y + previousNormal.y * signedDistance}};
        const Line2d nextLine{
            {points[i].x + nextNormal.x * signedDistance,
             points[i].y + nextNormal.y * signedDistance},
            {points[next].x + nextNormal.x * signedDistance,
             points[next].y + nextNormal.y * signedDistance}};

        const double turn = turnAt(points[previous], points[i], points[next]);
        const bool roundedJoin =
            (signedDistance > 0.0 && turn > 1.0e-12) ||
            (signedDistance < 0.0 && turn < -1.0e-12);
        if (roundedJoin)
        {
            // The rounded join follows the offset side of the two incident
            // edges.  For an inward offset the signed distance reverses the
            // normals; using the outward normals unchanged would place a
            // concave deflate join on the exterior of the source polygon.
            const double normalSign = signedDistance < 0.0 ? -1.0 : 1.0;
            appendRoundedJoin(
                offsetPoints, points[i],
                {previousNormal.x * normalSign,
                 previousNormal.y * normalSign},
                {nextNormal.x * normalSign,
                 nextNormal.y * normalSign},
                std::abs(signedDistance), turn);
        }
        else
        {
            const auto intersection = intersectLines(previousLine, nextLine);
            if (intersection.has_value())
            {
                offsetPoints.push_back(*intersection);
            }
            else
            {
                offsetPoints.push_back(
                    {points[i].x +
                         (previousNormal.x + nextNormal.x) * signedDistance * 0.5,
                     points[i].y +
                         (previousNormal.y + nextNormal.y) * signedDistance * 0.5});
            }
        }
    }

    const auto result =
        detail::createRustImportedOffsetPolygon(std::move(offsetPoints));
    if (!result.HasValue())
        return result;
    if (!preservesModificationMode(shape, result.Value(), mode))
    {
        return Result<SimplePolygon>::FromError(
            {ErrorCode::OffsetFailed,
             "offset does not preserve the requested inflate/deflate relation"});
    }
    return result;
}

}

JAGUARCORE_API Result<SimplePolygon> OffsetShape(const SimplePolygon& shape,
                                                 ShapeModifyMode mode,
                                                 double distance)
{
    const NormalizedOffset offset = normalizeOffsetRustF32(mode, distance);
    if (!std::isfinite(offset.distance))
    {
        return Result<SimplePolygon>::FromError(
            {ErrorCode::OffsetFailed,
             "Offset resulted in an empty polygon"});
    }
    if (offset.distance == 0.0)
        return reimportOffsetPolygon(shape);

    const auto skeletonOffsets = OffsetShapes(
        shape, offset.mode, offset.distance);
    if (skeletonOffsets.HasValue())
    {
        if (skeletonOffsets.Value().empty())
        {
            if (offset.mode == ShapeModifyMode::Deflate &&
                offset.distance < shape.PointOfInaccessibility().radius &&
                isConvexCounterClockwise(shape, offset.distance))
            {
                // OffsetShapes folds this collapsed fallback failure into an empty component list.
                const auto collapsedFallback =
                    fallbackOffsetShape(shape, offset.mode, offset.distance);
                if (!collapsedFallback.HasValue() &&
                    collapsedFallback.GetError().message ==
                        "simple polygon needs at least three points")
                {
                    return Result<SimplePolygon>::FromError(
                        {ErrorCode::OffsetFailed,
                         "Simple polygon must have at least 3 points"});
                }
            }
            return Result<SimplePolygon>::FromError(
                {ErrorCode::OffsetFailed,
                 "Offset resulted in an empty polygon"});
        }
        return reimportOffsetPolygon(skeletonOffsets.Value().front());
    }

    return fallbackOffsetShape(shape, offset.mode, offset.distance);
}

JAGUARCORE_API Result<std::vector<SimplePolygon>> OffsetShapes(
    const SimplePolygon& shape, ShapeModifyMode mode, double distance)
{
    const NormalizedOffset offset = normalizeOffsetRustF32(mode, distance);
    if (!std::isfinite(offset.distance))
    {
        return Result<std::vector<SimplePolygon>>::FromError(
            {ErrorCode::OffsetFailed,
             "Offset resulted in an empty polygon"});
    }
    if (offset.distance == 0.0)
        return Result<std::vector<SimplePolygon>>::FromValue({shape});

    if (offset.mode == ShapeModifyMode::Deflate &&
        offset.distance >= shape.PointOfInaccessibility().radius)
    {
        return Result<std::vector<SimplePolygon>>::FromValue({});
    }

    const auto simpleOffset = simpleOffsetWithIOverlayOutlineRules(
        shape, offset.mode, offset.distance);
    if (simpleOffset.has_value())
    {
        return Result<std::vector<SimplePolygon>>::FromValue(
            std::move(*simpleOffset));
    }

    auto skeletonOffsets = detail::roundedStraightSkeletonOffsets(
        shape,
        offset.mode == ShapeModifyMode::Inflate
            ? offset.distance
            : -offset.distance);
    if (!skeletonOffsets.HasValue())
        return skeletonOffsets;

    if (skeletonOffsets.Value().empty())
    {
        const auto fallback =
            fallbackOffsetShape(shape, offset.mode, offset.distance);
        if (fallback.HasValue())
        {
            return Result<std::vector<SimplePolygon>>::FromValue(
                {fallback.Value()});
        }
        if (offset.mode == ShapeModifyMode::Inflate)
        {
            return Result<std::vector<SimplePolygon>>::FromError(
                fallback.GetError());
        }
        return skeletonOffsets;
    }

    for (const SimplePolygon& polygon : skeletonOffsets.Value())
    {
        if (!preservesModificationMode(shape, polygon, offset.mode))
        {
            return Result<std::vector<SimplePolygon>>::FromError(
                {ErrorCode::OffsetFailed,
                 "straight-skeleton offset does not preserve the requested relation"});
        }
    }

    if (offset.mode == ShapeModifyMode::Deflate &&
        isConvexCounterClockwise(shape, offset.distance))
    {
        std::vector<SimplePolygon> ordered;
        ordered.reserve(skeletonOffsets.Value().size());
        for (const SimplePolygon& polygon : skeletonOffsets.Value())
        {
            if (samePolygonCycle(shape.Vertices(), polygon.Vertices()))
            {
                ordered.push_back(polygon);
                continue;
            }

            const auto canonical =
                rotateConvexContourToIOverlayStart(
                    polygon, shape, offset.distance);
            ordered.push_back(canonical.value_or(polygon));
        }
        skeletonOffsets = Result<std::vector<SimplePolygon>>::FromValue(
            std::move(ordered));
    }
    return skeletonOffsets;
}
}
