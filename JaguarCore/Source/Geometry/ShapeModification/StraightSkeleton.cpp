/*
 * Private straight-skeleton offset implementation.
 *
 * This is an independent C++17 reimplementation of the event model used by
 * geo-buffer 0.2.0 (Apache-2.0, https://github.com/1011-git/geo-buffer).
 * It does not link Rust or copy Rust runtime code.  The implementation is
 * kept private because JaguarCore exposes a single-polygon offset API while
 * the skeleton naturally produces zero or more polygon components.
 */

#include "StraightSkeleton.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

namespace jaguar::detail
{
namespace
{
constexpr double EPSILON = 1.0e-9;
constexpr double ROUNDED_STEP = 0.1;
constexpr std::size_t INVALID_INDEX = std::numeric_limits<std::size_t>::max();

std::uint32_t orderedFloatBits(float value) noexcept
{
    std::uint32_t bits = 0U;
    std::memcpy(&bits, &value, sizeof(bits));
    constexpr std::uint32_t signBit = std::uint32_t{1} << 31U;
    return (bits & signBit) != 0U ? ~bits : (bits ^ signBit);
}

bool rustApproxEqual(double first, double second) noexcept
{
    const float lhs = static_cast<float>(first);
    const float rhs = static_cast<float>(second);
    if (lhs == rhs)
        return true;
    if (std::abs(lhs - rhs) <= std::numeric_limits<float>::epsilon())
        return true;

    const std::uint32_t lhsBits = orderedFloatBits(lhs);
    const std::uint32_t rhsBits = orderedFloatBits(rhs);
    const std::uint32_t ulpDistance = lhsBits < rhsBits
                                          ? rhsBits - lhsBits
                                          : lhsBits - rhsBits;
    return ulpDistance <= 4U;
}

std::vector<Point2d> eliminateDegenerateOffsetVertices(
    const std::vector<Point2d>& input)
{
    std::vector<bool> remove(input.size(), false);
    for (std::size_t i = 0; i < input.size(); ++i)
    {
        const std::size_t next = (i + 1U) % input.size();
        if (rustApproxEqual(input[i].x, input[next].x) &&
            rustApproxEqual(input[i].y, input[next].y))
        {
            remove[i] = true;
        }
    }

    std::vector<Point2d> points;
    points.reserve(input.size());
    for (std::size_t i = 0; i < input.size(); ++i)
    {
        if (!remove[i])
            points.push_back(input[i]);
    }
    return points;
}

bool eq(double lhs, double rhs) noexcept
{
    return std::abs(lhs - rhs) < EPSILON;
}

bool gt(double lhs, double rhs) noexcept
{
    return !eq(lhs, rhs) && lhs > rhs;
}

bool ge(double lhs, double rhs) noexcept
{
    return eq(lhs, rhs) || lhs > rhs;
}

bool le(double lhs, double rhs) noexcept
{
    return eq(lhs, rhs) || lhs < rhs;
}

Point2d add(Point2d lhs, Point2d rhs) noexcept
{
    return {lhs.x + rhs.x, lhs.y + rhs.y};
}

Point2d subtract(Point2d lhs, Point2d rhs) noexcept
{
    return {lhs.x - rhs.x, lhs.y - rhs.y};
}

Point2d scale(Point2d point, double factor) noexcept
{
    return {point.x * factor, point.y * factor};
}

Point2d divide(Point2d point, double divisor) noexcept
{
    return divisor == 0.0 ? point : scale(point, 1.0 / divisor);
}

double cross(Point2d lhs, Point2d rhs) noexcept
{
    return lhs.x * rhs.y - lhs.y * rhs.x;
}

double norm(Point2d point) noexcept
{
    return std::sqrt(point.x * point.x + point.y * point.y);
}

double distance(Point2d lhs, Point2d rhs) noexcept
{
    return std::sqrt(lhs.SquaredDistanceTo(rhs));
}

struct Ray
{
    Point2d origin{};
    Point2d angle{};

    Point2d point(double ratio) const noexcept
    {
        return add(origin, scale(angle, ratio));
    }

    bool degenerate() const noexcept
    {
        return eq(angle.x, 0.0) && eq(angle.y, 0.0);
    }

    double distanceToLine(Point2d value) const noexcept
    {
        const double length = norm(angle);
        return length == 0.0 ? distance(origin, value)
                             : std::abs(cross(subtract(value, origin), angle)) / length;
    }

    bool Contains(Point2d value) const noexcept
    {
        return degenerate() ? (eq(origin.x, value.x) && eq(origin.y, value.y))
                            : eq(cross(subtract(value, origin), angle), 0.0);
    }

    bool parallel(const Ray& other) const noexcept
    {
        return eq(cross(angle, other.angle), 0.0) && !Contains(other.origin);
    }

    bool Intersects(const Ray& other) const noexcept
    {
        const double denominator = cross(angle, other.angle);
        if (eq(denominator, 0.0))
            return Contains(other.origin) || other.Contains(origin);
        const Point2d delta = subtract(other.origin, origin);
        const double first = cross(delta, other.angle) / denominator;
        const double second = cross(delta, angle) / denominator;
        return ge(first, 0.0) && ge(second, 0.0);
    }

    Point2d Intersection(const Ray& other) const noexcept
    {
        const double denominator = cross(angle, other.angle);
        if (eq(denominator, 0.0))
        {
            if (!Contains(other.origin))
                return divide(add(origin, other.origin), 2.0);
            const Point2d delta = subtract(other.origin, origin);
            const double ratio = std::abs(angle.x) >= std::abs(angle.y)
                                     ? (angle.x == 0.0 ? 0.0 : delta.x / angle.x)
                                     : (angle.y == 0.0 ? 0.0 : delta.y / angle.y);
            return gt(ratio, 0.0) ? other.origin : origin;
        }
        const Point2d delta = subtract(other.origin, origin);
        return add(origin, scale(angle, cross(delta, other.angle) / denominator));
    }

    Ray reverse() const noexcept
    {
        return {origin, scale(angle, -1.0)};
    }

    void normalize() noexcept
    {
        const double length = norm(angle);
        if (length != 0.0)
            angle = divide(angle, length);
    }

    int orientation(Point2d value) const noexcept
    {
        const double valueCross = cross(angle, subtract(value, origin));
        if (eq(valueCross, 0.0))
            return 0;
        return valueCross > 0.0 ? 1 : -1;
    }

    Ray rotate(double radians) const noexcept
    {
        const double cosine = std::cos(radians);
        const double sine = std::sin(radians);
        return {origin,
                {angle.x * cosine - angle.y * sine,
                 angle.x * sine + angle.y * cosine}};
    }

    Ray bisector(const Ray& other, Point2d newOrigin, bool inward) const noexcept
    {
        Point2d value = add(scale(angle, norm(other.angle)),
                            scale(other.angle, norm(angle)));
        if (eq(value.x, 0.0) && eq(value.y, 0.0))
        {
            value = {-angle.y, angle.x};
            if (inward)
                value = scale(value, -1.0);
        }
        else if ((inward && cross(angle, value) > 0.0) ||
                 (!inward && cross(angle, value) < 0.0))
        {
            value = scale(value, -1.0);
        }
        return {newOrigin, value};
    }
};

enum class IndexKind
{
    Pointer,
    Real,
};

struct Index
{
    IndexKind kind = IndexKind::Pointer;
    std::size_t value = 0;

    static Index pointer(std::size_t valueIn) noexcept
    {
        return {IndexKind::Pointer, valueIn};
    }

    static Index real(std::size_t valueIn) noexcept
    {
        return {IndexKind::Real, valueIn};
    }

    friend bool operator==(Index lhs, Index rhs) noexcept
    {
        return lhs.kind == rhs.kind && lhs.value == rhs.value;
    }
};

struct Node
{
    Index index = Index::real(0);
    Index left = Index::pointer(0);
    Index right = Index::pointer(0);
    bool done = false;
};

class VertexQueue
{
public:
    void initialize(std::size_t count)
    {
        m_Nodes.clear();
        m_Start.clear();
        m_Start.push_back(0);
        m_Nodes.reserve(count);
        for (std::size_t i = 0; i < count; ++i)
        {
            m_Nodes.push_back({Index::real(i),
                               Index::pointer((i + count - 1) % count),
                               Index::pointer((i + 1) % count), false});
        }
    }

    std::size_t realIndex(Index pointer) const
    {
        return m_Nodes[pointer.value].index.value;
    }

    Index left(Index pointer) const { return m_Nodes[pointer.value].left; }
    Index right(Index pointer) const { return m_Nodes[pointer.value].right; }
    bool done(Index pointer) const { return m_Nodes[pointer.value].done; }
    void markDone(Index pointer) { m_Nodes[pointer.value].done = true; }

    Index remove(Index pointer)
    {
        const Index leftIndex = left(pointer);
        const Index rightIndex = right(pointer);
        m_Nodes[leftIndex.value].right = rightIndex;
        m_Nodes[rightIndex.value].left = leftIndex;
        m_Nodes[pointer.value].done = true;
        return rightIndex;
    }

    Index removeAndSet(Index pointer, Index realIndexValue)
    {
        const Index result = remove(pointer);
        m_Nodes[result.value].index = realIndexValue;
        return result;
    }

    std::pair<Index, Index> splitAndSet(Index current,
                                       Index split,
                                       Index newFirst,
                                       Index newSecond)
    {
        const Index newNode = Index::pointer(m_Nodes.size());
        m_Nodes.push_back({Index::real(0), split, right(current), false});
        m_Nodes[current.value].index = newFirst;
        m_Nodes[newNode.value].index = newSecond;

        const Index splitRight = right(split);
        const Index currentRight = right(current);
        m_Nodes[currentRight.value].left = newNode;
        m_Nodes[split.value].right = newNode;
        m_Nodes[current.value].right = splitRight;
        m_Nodes[splitRight.value].left = current;
        m_Start.push_back(current.value);
        m_Start.push_back(newNode.value);
        return {current, newNode};
    }

    void cleanup()
    {
        std::size_t startIndex = 0;
        std::vector<bool> visited(m_Nodes.size(), false);
        while (startIndex < m_Start.size())
        {
            std::size_t current = m_Start[startIndex];
            while (m_Nodes[current].done && !visited[current])
            {
                visited[current] = true;
                current = m_Nodes[current].right.value;
            }
            if (visited[current] ||
                m_Nodes[current].left.value == m_Nodes[current].right.value)
            {
                m_Start[startIndex] = m_Start.back();
                m_Start.pop_back();
                continue;
            }
            m_Start[startIndex] = current;
            visited[current] = true;
            current = m_Nodes[current].right.value;
            while (current != m_Start[startIndex])
            {
                visited[current] = true;
                current = m_Nodes[current].right.value;
            }
            ++startIndex;
        }
    }

    struct Entry
    {
        std::size_t component = 0;
        Index pointer = Index::pointer(0);
        std::size_t real = 0;
    };

    std::vector<Entry> entries() const
    {
        std::vector<Entry> result;
        for (std::size_t component = 0; component < m_Start.size(); ++component)
        {
            const std::size_t first = m_Start[component];
            std::size_t current = first;
            do
            {
                const Index pointer = Index::pointer(current);
                result.push_back({component, pointer, realIndex(pointer)});
                current = m_Nodes[current].right.value;
            } while (current != first);
        }
        return result;
    }

private:
    std::vector<Node> m_Nodes;
    std::vector<std::size_t> m_Start;
};

enum class VertexKind
{
    Tree,
    Split,
    Root,
};

struct Vertex
{
    VertexKind kind = VertexKind::Tree;
    Ray axis{};
    Ray leftRay{};
    Ray rightRay{};
    std::size_t parent = INVALID_INDEX;
    Point2d location{};
    double time = 0.0;

    Ray ray() const { return axis; }
    std::pair<Ray, Ray> baseRays() const { return {leftRay, rightRay}; }
};

Vertex makeInitialVertex(Point2d previous,
                         Point2d current,
                         Point2d next,
                         bool inward)
{
    const Ray left{current, subtract(previous, current)};
    const Ray right{current, subtract(next, current)};
    Ray axis = left.bisector(right, current, inward);
    axis.angle = divide(axis.angle, right.distanceToLine(axis.point(1.0)));
    return {VertexKind::Tree, axis, left, right, INVALID_INDEX, {}, 0.0};
}

Vertex makeTreeVertex(Point2d location,
                      Ray left,
                      Ray right,
                      bool inward)
{
    Ray axis = left.bisector(right, location, inward);
    const double scaleValue = std::abs(
        left.distanceToLine(axis.point(1.0)) -
        left.distanceToLine(axis.point(0.0)));
    axis.angle = divide(axis.angle, scaleValue);
    return {VertexKind::Tree, axis, left, right, INVALID_INDEX, {},
            left.distanceToLine(location)};
}

struct Timeline
{
    enum class Kind
    {
        Shrink,
        Split,
    };

    Kind kind = Kind::Shrink;
    double time = 0.0;
    Point2d location{};
    Index leftVertex = Index::pointer(0);
    Index rightVertex = Index::pointer(0);
    std::size_t leftReal = 0;
    std::size_t rightReal = 0;
    double tieBreak = 0.0;
    Index anchorVertex = Index::pointer(0);
    std::size_t anchorReal = 0;
};

bool timelineLess(const Timeline& lhs, const Timeline& rhs) noexcept
{
    if (!eq(lhs.time, rhs.time))
        return lhs.time < rhs.time;
    if (lhs.kind != rhs.kind)
        return lhs.kind == Timeline::Kind::Split;
    if (lhs.kind == Timeline::Kind::Split)
    {
        if (lhs.location.x != rhs.location.x)
            return lhs.location.x < rhs.location.x;
        if (lhs.location.y != rhs.location.y)
            return lhs.location.y < rhs.location.y;
        return lhs.anchorReal < rhs.anchorReal;
    }
    if (lhs.tieBreak != rhs.tieBreak)
        return lhs.tieBreak < rhs.tieBreak;
    if (lhs.location.x != rhs.location.x)
        return lhs.location.x < rhs.location.x;
    if (lhs.location.y != rhs.location.y)
        return lhs.location.y < rhs.location.y;
    if (lhs.leftReal != rhs.leftReal)
        return lhs.leftReal < rhs.leftReal;
    return lhs.rightReal < rhs.rightReal;
}

struct Event
{
    Timeline::Kind kind = Timeline::Kind::Shrink;
    double time = 0.0;
    std::size_t mergeFrom = 0;
    std::size_t mergeTo = 0;
    std::size_t splitFrom = 0;
    std::size_t splitInto = 0;
    std::size_t splitToLeft = 0;
    std::size_t splitToRight = 0;
};

struct SplitCandidate
{
    double time = 0.0;
    Point2d location{};
    Index split = Index::pointer(0);
    std::size_t splitReal = 0;
};

class Skeleton
{
public:
    Skeleton(const SimplePolygon& polygon, bool inward)
        : m_Inward(inward)
    {
        const auto& points = polygon.Vertices();
        m_Vertices.reserve(points.size() + 32);
        for (std::size_t i = 0; i < points.size(); ++i)
        {
            const std::size_t previous = (i + points.size() - 1) % points.size();
            const std::size_t next = (i + 1) % points.size();
            m_Vertices.push_back(makeInitialVertex(
                points[previous], points[i], points[next], inward));
        }
        m_Queue.initialize(points.size());
        m_InitialQueue = m_Queue;
        for (const auto& entry : m_Queue.entries())
        {
            makeShrinkEvents(entry.pointer, true);
            makeSplitEvents(entry.pointer);
        }
        buildEvents();
    }

    std::vector<std::vector<Point2d>> loops(double offsetDistance) const
    {
        VertexQueue queue = m_InitialQueue;
        for (const Event& event : m_Events)
        {
            if (gt(event.time, offsetDistance))
                break;
            applyEvent(queue, event);
            queue.cleanup();
        }

        std::vector<std::vector<Point2d>> result;
        std::size_t currentComponent = INVALID_INDEX;
        for (const auto& entry : queue.entries())
        {
            if (entry.component != currentComponent)
            {
                currentComponent = entry.component;
                result.emplace_back();
            }
            const Vertex& vertex = m_Vertices[entry.real];
            const double remaining = offsetDistance - vertex.time;
            const auto [left, right] = vertex.baseRays();
            const Ray axis = vertex.ray();
            if (norm(add(left.angle, axis.angle)) >
                norm(subtract(left.angle, axis.angle)))
            {
                result.back().push_back(axis.point(remaining));
                continue;
            }

            Ray leftNormal;
            Ray rightNormal;
            if (m_Inward)
            {
                leftNormal = {axis.origin, {-left.angle.y, left.angle.x}};
                rightNormal = {axis.origin, {right.angle.y, -right.angle.x}};
            }
            else
            {
                leftNormal = {axis.origin, {left.angle.y, -left.angle.x}};
                rightNormal = {axis.origin, {-right.angle.y, right.angle.x}};
            }
            leftNormal.normalize();
            rightNormal.normalize();
            while (true)
            {
                result.back().push_back(leftNormal.point(remaining));
                leftNormal = leftNormal.rotate(m_Inward ? ROUNDED_STEP
                                                         : -ROUNDED_STEP);
                const int orientation = leftNormal.orientation(rightNormal.point(1.0));
                if ((m_Inward && orientation == -1) ||
                    (!m_Inward && orientation == 1))
                    break;
            }
            result.back().push_back(rightNormal.point(remaining));
        }
        return result;
    }

private:
    void addTimeline(Timeline timeline)
    {
        m_Timelines.push_back(std::move(timeline));
    }

    void makeShrinkEvents(Index current, bool initial)
    {
        if (m_Queue.right(current) == m_Queue.left(current))
            return;
        Index left = current;
        for (std::size_t iteration = 0; iteration < 2; ++iteration)
        {
            const Index right = m_Queue.right(left);
            const std::size_t leftReal = m_Queue.realIndex(left);
            const std::size_t rightReal = m_Queue.realIndex(right);
            const Ray leftRay = m_Vertices[leftReal].ray();
            const Ray rightRay = m_Vertices[rightReal].ray();
            if (leftRay.Intersects(rightRay))
            {
                const Point2d location = leftRay.Intersection(rightRay);
                addTimeline({Timeline::Kind::Shrink,
                             m_Vertices[leftReal].leftRay.distanceToLine(location),
                             location, left, right, leftReal, rightReal,
                             distance(leftRay.origin, rightRay.origin),
                             Index::pointer(0), 0});
            }
            if (initial)
                break;
            left = m_Queue.left(current);
        }
    }

    std::vector<SplitCandidate> findSplit(Index current, bool initial) const
    {
        std::vector<SplitCandidate> result;
        const std::size_t currentReal = m_Queue.realIndex(current);
        const Ray leftRay = m_Vertices[currentReal].leftRay;
        const Ray rightRay = m_Vertices[currentReal].rightRay;
        const double product = cross(leftRay.angle, rightRay.angle);
        if ((m_Inward && le(product, 0.0)) ||
            (!m_Inward && ge(product, 0.0)))
            return result;

        const Index currentLeft = m_Queue.left(current);
        const Index currentRight = m_Queue.right(current);
        for (const auto& entry : m_Queue.entries())
        {
            const Index split = entry.pointer;
            const Index splitRight = m_Queue.right(split);
            const std::size_t splitReal = entry.real;
            const std::size_t splitRightReal = m_Queue.realIndex(splitRight);
            if (split == current || split == currentRight ||
                splitRight == current || splitRight == currentLeft)
                continue;

            const Ray baseRay = m_Vertices[splitReal].rightRay;
            const bool leftParallel = leftRay.parallel(baseRay);
            const Point2d leftIntersection = leftParallel
                                                  ? Point2d{}
                                                  : leftRay.Intersection(baseRay);
            const Point2d rightIntersection = rightRay.parallel(baseRay)
                                                   ? Point2d{}
                                                   : rightRay.Intersection(baseRay);
            Point2d realIntersection{};
            if (leftParallel)
            {
                const Ray ray = rightRay.bisector(
                    baseRay.reverse(), rightIntersection, !m_Inward);
                if (!ray.Intersects(m_Vertices[currentReal].ray()))
                    continue;
                realIntersection = ray.Intersection(m_Vertices[currentReal].ray());
            }
            else
            {
                const Ray ray = leftRay.bisector(
                    baseRay, leftIntersection, m_Inward);
                if (!ray.Intersects(m_Vertices[currentReal].ray()))
                    continue;
                realIntersection = ray.Intersection(m_Vertices[currentReal].ray());
            }

            if (initial)
            {
                if ((m_Inward && baseRay.orientation(realIntersection) < 0) ||
                    (!m_Inward && baseRay.orientation(realIntersection) > 0))
                    continue;
            }
            else if (m_Inward)
            {
                if (m_Vertices[splitReal].ray().orientation(realIntersection) >= 0 ||
                    baseRay.orientation(realIntersection) < 0 ||
                    m_Vertices[splitRightReal].ray().orientation(realIntersection) < 0)
                    continue;
            }
            else
            {
                if (m_Vertices[splitReal].ray().orientation(realIntersection) <= 0 ||
                    baseRay.orientation(realIntersection) > 0 ||
                    m_Vertices[splitRightReal].ray().orientation(realIntersection) > 0)
                    continue;
            }

            result.push_back({rightRay.distanceToLine(realIntersection),
                              realIntersection, split, splitReal});
        }

        std::sort(result.begin(), result.end(), [](const SplitCandidate& lhs,
                                                   const SplitCandidate& rhs) {
            if (!eq(lhs.time, rhs.time))
                return lhs.time < rhs.time;
            if (lhs.location.x != rhs.location.x)
                return lhs.location.x < rhs.location.x;
            if (lhs.location.y != rhs.location.y)
                return lhs.location.y < rhs.location.y;
            if (lhs.split.value != rhs.split.value)
                return lhs.split.value < rhs.split.value;
            return lhs.splitReal < rhs.splitReal;
        });
        if (!initial && !result.empty())
            result.resize(1);
        return result;
    }

    void makeSplitEvents(Index current)
    {
        for (const auto& candidate : findSplit(current, true))
        {
            addTimeline({Timeline::Kind::Split, candidate.time,
                         candidate.location, Index::pointer(0),
                         Index::pointer(0), 0, 0, 0.0, current,
                         m_Queue.realIndex(current)});
        }
    }

    void buildEvents()
    {
        while (!m_Timelines.empty())
        {
            const auto it = std::min_element(
                m_Timelines.begin(), m_Timelines.end(), timelineLess);
            const Timeline timeline = *it;
            m_Timelines.erase(it);
            if (timeline.kind == Timeline::Kind::Shrink)
            {
                if (m_Queue.done(timeline.leftVertex) ||
                    m_Queue.done(timeline.rightVertex) ||
                    m_Queue.realIndex(timeline.leftVertex) != timeline.leftReal ||
                    m_Queue.realIndex(timeline.rightVertex) != timeline.rightReal)
                    continue;

                const std::size_t newIndex = m_Vertices.size();
                const Ray leftRay = m_Vertices[timeline.leftReal].leftRay;
                const Ray rightRay = m_Vertices[timeline.rightReal].rightRay;
                m_Vertices[timeline.leftReal].parent = newIndex;
                m_Vertices[timeline.rightReal].parent = newIndex;
                m_Vertices.push_back(makeTreeVertex(
                    timeline.location, leftRay, rightRay, m_Inward));
                const Event event{Timeline::Kind::Shrink,
                                  timeline.time,
                                  timeline.leftVertex.value,
                                  newIndex,
                                  0, 0, 0, 0};
                const Index result = m_Queue.removeAndSet(
                    timeline.leftVertex, Index::real(newIndex));
                if (m_Queue.left(result) == m_Queue.right(result))
                {
                    m_Queue.markDone(m_Queue.left(result));
                    m_Queue.markDone(result);
                    m_Vertices[newIndex].kind = VertexKind::Root;
                    m_Vertices[newIndex].location = timeline.location;
                }
                else
                {
                    makeShrinkEvents(result, false);
                }
                m_Events.push_back(event);
                continue;
            }

            if (m_Queue.done(timeline.anchorVertex) ||
                m_Queue.realIndex(timeline.anchorVertex) != timeline.anchorReal)
                continue;
            m_Queue.cleanup();
            const auto candidates = findSplit(timeline.anchorVertex, false);
            if (candidates.size() != 1 ||
                !eq(candidates.front().time, timeline.time) ||
                !eq(candidates.front().location.x, timeline.location.x) ||
                !eq(candidates.front().location.y, timeline.location.y))
                continue;

            const SplitCandidate& candidate = candidates.front();
            const std::size_t firstTree = m_Vertices.size();
            const std::size_t secondTree = firstTree + 1;
            const std::size_t splitVertex = firstTree + 2;
            const Ray anchorLeft = m_Vertices[timeline.anchorReal].leftRay;
            const Ray anchorRight = m_Vertices[timeline.anchorReal].rightRay;
            const Ray splitRight = m_Vertices[candidate.splitReal].rightRay;
            m_Vertices.push_back(makeTreeVertex(
                candidate.location, anchorLeft, splitRight, m_Inward));
            m_Vertices.push_back(makeTreeVertex(
                candidate.location, splitRight.reverse(), anchorRight, m_Inward));
            m_Vertices.push_back({VertexKind::Split, {}, {}, {}, INVALID_INDEX,
                                  candidate.location,
                                  m_Vertices[timeline.anchorReal].time});
            m_Vertices[timeline.anchorReal].parent = splitVertex;

            const auto children = m_Queue.splitAndSet(
                timeline.anchorVertex, candidate.split,
                Index::real(firstTree), Index::real(secondTree));
            const Event event{Timeline::Kind::Split, timeline.time,
                              0, 0, timeline.anchorVertex.value,
                              candidate.split.value, firstTree, secondTree};
            makeShrinkEvents(children.first, false);
            makeShrinkEvents(children.second, false);
            m_Events.push_back(event);
        }
    }

    static void applyEvent(VertexQueue& queue, const Event& event)
    {
        if (event.kind == Timeline::Kind::Shrink)
        {
            const Index result = queue.removeAndSet(
                Index::pointer(event.mergeFrom), Index::real(event.mergeTo));
            if (queue.left(result) == queue.right(result))
            {
                queue.markDone(queue.left(result));
                queue.markDone(result);
            }
            return;
        }
        queue.splitAndSet(Index::pointer(event.splitFrom),
                          Index::pointer(event.splitInto),
                          Index::real(event.splitToLeft),
                          Index::real(event.splitToRight));
    }

    std::vector<Vertex> m_Vertices;
    VertexQueue m_Queue;
    VertexQueue m_InitialQueue;
    std::vector<Timeline> m_Timelines;
    std::vector<Event> m_Events;
    bool m_Inward = false;
};

double signedArea(const std::vector<Point2d>& points) noexcept
{
    double result = 0.0;
    for (std::size_t i = 0; i < points.size(); ++i)
    {
        const std::size_t next = (i + 1) % points.size();
        result += points[i].x * points[next].y -
                  points[next].x * points[i].y;
    }
    return result * 0.5;
}

void cleanLoop(std::vector<Point2d>& points)
{
    std::vector<Point2d> result;
    result.reserve(points.size());
    for (const Point2d& point : points)
    {
        if (result.empty() || distance(result.back(), point) > EPSILON)
            result.push_back(point);
    }
    if (result.size() > 1 &&
        distance(result.front(), result.back()) <= EPSILON)
        result.pop_back();
    points = std::move(result);
}
}

Result<SimplePolygon> createRustImportedOffsetPolygon(
    std::vector<Point2d> points)
{
    points = eliminateDegenerateOffsetVertices(points);
    const auto polygon = SimplePolygon::Create(std::move(points));
    if (!polygon.HasValue())
    {
        return Result<SimplePolygon>::FromError(
            {ErrorCode::OffsetFailed, polygon.GetError().message});
    }
    return polygon;
}

Result<std::vector<SimplePolygon>> roundedStraightSkeletonOffsets(
    const SimplePolygon& shape, double signedDistance)
{
    if (!std::isfinite(signedDistance))
        return Result<std::vector<SimplePolygon>>::FromError(
            {ErrorCode::InvalidInput, "invalid signed offset distance"});
    if (signedDistance == 0.0)
        return Result<std::vector<SimplePolygon>>::FromValue({shape});

    const Skeleton skeleton(shape, signedDistance < 0.0);
    std::vector<SimplePolygon> polygons;
    for (auto loop : skeleton.loops(std::abs(signedDistance)))
    {
        cleanLoop(loop);
        if (loop.size() < 3 || signedArea(loop) <= EPSILON)
            continue;
        const auto polygon = createRustImportedOffsetPolygon(std::move(loop));
        if (polygon.HasValue())
            polygons.push_back(polygon.Value());
    }
    return Result<std::vector<SimplePolygon>>::FromValue(std::move(polygons));
}
}
