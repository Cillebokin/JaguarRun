// OverlayExtract.cpp
//
// Line-by-line C++17 port of the i_overlay 4.5.2 build + extract chain (see OverlayExtract.h
// for the source map). Every function carries a "rs <file>:<line>" comment pointing at the
// Rust original it mirrors; control flow, comparators, bit ops and branch order are 1:1.
//
// Rust pieces intentionally NOT ported (all outside the required chain or debug-only):
//   - core/extract.rs:50-62 extract_shapes / 87-179 extract  (nested IntShapes with hole
//     joining; depends on bind/segment.rs + bind/solver.rs which are outside the task's file
//     list — the offset use case only needs the flat extract_contours path)
//   - core/extract_ogc.rs (ogc == false on this path)
//   - build/string.rs (skipped per task)
//   - core/graph.rs:40-47 OverlayGraph::validate (debug_assert only)
//   - core/solver.rs Precision / MultithreadOptions knobs (only used by the upstream split
//     solver; is_list_fill / is_parallel_sort_allowed are ported)
//   - i_tree KeyExpCollection::get_value / first_less / first_less_or_equal (not called by
//     build/sweep.rs; only insert + first_less_or_equal_by are ported, for both structures)
//   - SweepRunner's list/tree allocation cache (rs build/sweep.rs:80-83, 140-159): pure
//     allocation reuse across runs; this entry runs the sweep once per call, so the scan
//     structure is created locally — no behavioral difference.

#include "JaguarCore/Geometry/ShapeModification/OverlayExtract.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace jaguar
{
namespace overlay
{
namespace detail
{

// ============================================================================
// Integer primitives (Rust release-mode wrapping semantics via unsigned ops)
// ============================================================================

// Rust i64/i32 arithmetic wraps in release builds; mirror it without C++ UB.
static inline int64_t wrapMul64(int64_t a, int64_t b) noexcept
{
    return static_cast<int64_t>(static_cast<uint64_t>(a) * static_cast<uint64_t>(b));
}

static inline int64_t wrapSub64(int64_t a, int64_t b) noexcept
{
    return static_cast<int64_t>(static_cast<uint64_t>(a) - static_cast<uint64_t>(b));
}

static inline int64_t wrapAdd64(int64_t a, int64_t b) noexcept
{
    return static_cast<int64_t>(static_cast<uint64_t>(a) + static_cast<uint64_t>(b));
}

// rs segm/winding.rs + segm/boolean.rs:37-42 (ShapeCountBoolean::add — i32 add, wraps in release)
static inline int32_t wrapAdd32(int32_t a, int32_t b) noexcept
{
    return static_cast<int32_t>(static_cast<uint32_t>(a) + static_cast<uint32_t>(b));
}

// rs i_float-1.16.0/src/int/point.rs:86-98 IntPoint::cmp — lexicographic (x, then y)
static inline int pointCmp(IntPoint a, IntPoint b) noexcept
{
    if (a.x == b.x)
    {
        if (a.y == b.y) return 0;
        return a.y < b.y ? -1 : 1;
    }
    return a.x < b.x ? -1 : 1;
}

static inline bool pointEqual(IntPoint a, IntPoint b) noexcept
{
    return a.x == b.x && a.y == b.y;
}

// rs i_float-1.16.0/src/int/point.rs (PartialOrd/Ord derived usage `a < b`)
static inline bool pointLess(IntPoint a, IntPoint b) noexcept
{
    return pointCmp(a, b) < 0;
}

// rs i_float-1.16.0/src/fix_vec.rs:9-12 FixVec (i64 components)
struct FixVec
{
    int64_t x;
    int64_t y;
};

// rs i_float-1.16.0/src/int/point.rs:40-44 IntPoint::subtract -> FixVec
static inline FixVec pointSubtract(IntPoint a, IntPoint b) noexcept
{
    FixVec v;
    v.x = static_cast<int64_t>(a.x) - static_cast<int64_t>(b.x);
    v.y = static_cast<int64_t>(a.y) - static_cast<int64_t>(b.y);
    return v;
}

// rs i_float-1.16.0/src/fix_vec.rs:133-138 FixVec::cross_product
static inline int64_t fixVecCross(FixVec a, FixVec b) noexcept
{
    return wrapSub64(wrapMul64(a.x, b.y), wrapMul64(a.y, b.x));
}

// rs i_float-1.16.0/src/triangle.rs:74-82 Triangle::area_two_point
static int64_t areaTwoPoint(IntPoint p0, IntPoint p1, IntPoint p2) noexcept
{
    const int64_t x0 = static_cast<int64_t>(p1.x) - static_cast<int64_t>(p0.x);
    const int64_t y0 = static_cast<int64_t>(p1.y) - static_cast<int64_t>(p0.y);

    const int64_t x1 = static_cast<int64_t>(p1.x) - static_cast<int64_t>(p2.x);
    const int64_t y1 = static_cast<int64_t>(p1.y) - static_cast<int64_t>(p2.y);

    return wrapSub64(wrapMul64(x0, y1), wrapMul64(x1, y0));
}

// rs i_float-1.16.0/src/triangle.rs:85-87 Triangle::is_clockwise_point
static inline bool isClockwisePoint(IntPoint p0, IntPoint p1, IntPoint p2) noexcept
{
    return areaTwoPoint(p0, p1, p2) > 0;
}

// rs i_float-1.16.0/src/triangle.rs:161-163 Triangle::clock_order_point = 0.cmp(&area)
// -> Less (-1) when area > 0, Greater (+1) when area < 0, Equal (0) when area == 0.
static inline int clockOrderPoint(IntPoint p0, IntPoint p1, IntPoint p2) noexcept
{
    const int64_t area = areaTwoPoint(p0, p1, p2);
    if (area > 0) return -1;
    if (area < 0) return 1;
    return 0;
}

// rs i_overlay-4.5.2/src/util/log.rs:5-13 usize::log2_sqrt
static size_t log2Sqrt(size_t v) noexcept
{
    unsigned i = 0;
    while (v > 0)
    {
        v >>= 1;
        ++i;
    }
    const unsigned n = (i + 1) >> 1;
    return static_cast<size_t>(1) << n;
}

// ============================================================================
// Segment fill flags — rs segm/segment.rs:6-20
// ============================================================================

typedef uint8_t SegmentFill;

static const SegmentFill kNone = 0;                    // rs NONE
static const SegmentFill kSubjTop = 0b0001;            // rs SUBJ_TOP
static const SegmentFill kSubjBottom = 0b0010;         // rs SUBJ_BOTTOM
static const SegmentFill kClipTop = 0b0100;            // rs CLIP_TOP
static const SegmentFill kClipBottom = 0b1000;         // rs CLIP_BOTTOM

static const SegmentFill kSubjBoth = kSubjTop | kSubjBottom;     // rs SUBJ_BOTH
static const SegmentFill kClipBoth = kClipTop | kClipBottom;     // rs CLIP_BOTH
static const SegmentFill kBothTop = kSubjTop | kClipTop;         // rs BOTH_TOP
static const SegmentFill kBothBottom = kSubjBottom | kClipBottom; // rs BOTH_BOTTOM
static const SegmentFill kAll = kSubjBoth | kClipBoth;           // rs ALL

// ============================================================================
// ShapeCountBoolean — rs segm/boolean.rs:4-57 (WindingCount impl, rs segm/winding.rs)
// Stored packed inside OverlaySegment::fill (see OverlayExtract.h); unpacked to full i32
// pairs for all arithmetic so the sweep adds in i32 exactly as in Rust.
// ============================================================================

struct ShapeCount
{
    int32_t subj;
    int32_t clip;
};

// rs segm/boolean.rs:24-26 WindingCount::new
static inline ShapeCount shapeCountNew(int32_t subj, int32_t clip) noexcept
{
    return ShapeCount{ subj, clip };
}

// rs segm/boolean.rs:37-42 ShapeCountBoolean::add
static inline ShapeCount shapeCountAdd(ShapeCount self, ShapeCount count) noexcept
{
    return ShapeCount{ wrapAdd32(self.subj, count.subj), wrapAdd32(self.clip, count.clip) };
}

// rs segm/boolean.rs:19-21 is_not_empty (kept for winding.rs coverage)
static inline bool shapeCountIsNotEmpty(ShapeCount c) noexcept
{
    return c.subj != 0 || c.clip != 0;
}

// rs segm/boolean.rs:45-48 apply (kept for winding.rs coverage)
static inline void shapeCountApply(ShapeCount& self, ShapeCount count) noexcept
{
    self.subj = wrapAdd32(self.subj, count.subj);
    self.clip = wrapAdd32(self.clip, count.clip);
}

// rs segm/boolean.rs:51-56 invert — negate both packed counters
static inline int32_t negatePackedFill(int32_t fill) noexcept
{
    return packShapeCount(-shapeCountSubj(fill), -shapeCountClip(fill));
}

// unpack OverlaySegment::fill into the full-precision pair used by the sweep
static inline ShapeCount unpackCount(int32_t fill) noexcept
{
    return ShapeCount{ shapeCountSubj(fill), shapeCountClip(fill) };
}

// ============================================================================
// VSegment — rs geom/v_segment.rs:8-11 (+ the Ord / ExiredKey impls used by the scan line)
// ============================================================================

struct VSegment
{
    IntPoint a;
    IntPoint b;
};

// rs geom/v_segment.rs:72-77 ExpiredKey::expiration
static inline int32_t vSegmentExpiration(const VSegment& s) noexcept
{
    return s.b.x;
}

// rs geom/v_segment.rs:15-21 is_under_segment_order (used as Ord::cmp, rs v_segment.rs:65-69)
static int vSegmentOrder(const VSegment& self, const VSegment& other) noexcept
{
    const int c = pointCmp(self.a, other.a);
    if (c < 0) return clockOrderPoint(self.a, other.a, self.b);
    if (c == 0) return clockOrderPoint(self.a, other.b, self.b);
    return clockOrderPoint(other.a, other.b, self.a);
}

// rs geom/v_segment.rs:24-29 is_under_point_order
static int vSegmentIsUnderPointOrder(const VSegment& s, IntPoint p) noexcept
{
    return clockOrderPoint(s.a, p, s.b);
}

// ============================================================================
// Scan structures (sweep-line status). Key = VSegment (Ord above), value = ShapeCount,
// expiration = i32 (b.x), lazy expiration at `time`.
// ============================================================================

// Rust binary_search_by as implemented by core (the algorithm i_tree list.rs relies on):
// probes mid = base + size/2, moves right on Less/Equal, left on Greater; returns
// Ok(equal index) / Err(insertion point). Replicated 1:1 so duplicate-key resolution
// (identical segments) behaves identically.
struct BinarySearchResult
{
    bool found;
    size_t index;
};

template <typename E, typename C>
static BinarySearchResult rustBinarySearchBy(const std::vector<E>& v, C cmp)
{
    size_t size = v.size();
    if (size == 0)
    {
        return BinarySearchResult{ false, 0 };
    }
    size_t base = 0;
    while (size > 1)
    {
        const size_t half = size / 2;
        const size_t mid = base + half;
        const int c = cmp(v[mid]);
        base = (c > 0) ? base : mid; // Ordering::Greater -> stay left, else move right
        size -= half;
    }
    const int c = cmp(v[base]);
    if (c == 0)
    {
        return BinarySearchResult{ true, base };
    }
    return BinarySearchResult{ false, base + (c < 0 ? size_t(1) : size_t(0)) };
}

// rs i_tree-0.18.0/src/key/list.rs:8-11 KeyExpList<VSegment, i32, ShapeCount>
struct ScanEntity
{
    VSegment key;
    ShapeCount val;
};

struct ScanList
{
    std::vector<ScanEntity> buffer;
    int32_t min_exp;

    // rs list.rs:15-19 KeyExpList::new
    explicit ScanList(size_t capacity)
        : min_exp(INT32_MAX) // rs Expiration for i32 (i_tree lib.rs:56-60)
    {
        buffer.reserve(capacity);
    }

    // rs list.rs:23-28 reserve_capacity
    void reserveCapacity(size_t capacity)
    {
        if (buffer.capacity() < capacity)
        {
            buffer.reserve(capacity);
        }
    }

    // rs list.rs:133-147 clear_expired
    void clearExpired(int32_t time)
    {
        if (min_exp > time)
        {
            return;
        }
        int32_t new_min_exp = INT32_MAX;
        size_t keep = 0;
        for (size_t i = 0; i < buffer.size(); ++i)
        {
            const int32_t exp = vSegmentExpiration(buffer[i].key);
            if (exp > time)
            {
                if (keep != i)
                {
                    buffer[keep] = buffer[i];
                }
                ++keep;
                if (exp < new_min_exp)
                {
                    new_min_exp = exp;
                }
            }
        }
        buffer.resize(keep);
        min_exp = new_min_exp;
    }

    // rs list.rs:38-46 insert
    void insert(const VSegment& key, ShapeCount val, int32_t time)
    {
        clearExpired(time);
        if (vSegmentExpiration(key) < min_exp)
        {
            min_exp = vSegmentExpiration(key);
        }
        // rs: binary_search_by_key(&key, |e| e.key).unwrap_or_else(|index| index)
        const BinarySearchResult r =
            rustBinarySearchBy(buffer, [&key](const ScanEntity& e) { return vSegmentOrder(e.key, key); });
        buffer.insert(buffer.begin() + r.index, ScanEntity{ key, val });
    }

    // rs list.rs:107-122 first_less_or_equal_by
    template <typename F>
    ShapeCount firstLessOrFailBy(int32_t time, ShapeCount default_value, F f)
    {
        clearExpired(time);
        const BinarySearchResult r =
            rustBinarySearchBy(buffer, [&f](const ScanEntity& e) { return f(e.key); });
        if (r.found)
        {
            return buffer[r.index].val;
        }
        if (r.index > 0)
        {
            return buffer[r.index - 1].val;
        }
        return default_value;
    }

    // rs list.rs:125-128 clear
    void clear()
    {
        min_exp = INT32_MAX;
        buffer.clear();
    }
};

// rs i_tree-0.18.0/src/key/tree.rs:9-13 KeyExpTree<VSegment, i32, ShapeCount>
// (red-black tree with lazy expiration; full 1:1 port of tree.rs + node.rs + pool.rs)

static const uint32_t kEmptyRef = 0xFFFFFFFFu; // rs i_tree lib.rs:8 EMPTY_REF
static const uint32_t kNilIndex = 0;           // rs tree.rs:15 NIL_INDEX
static const size_t kSizeMax = static_cast<size_t>(-1); // rs usize::MAX

// rs i_tree key/node.rs:5-8 enum Color { Red, Black }
enum NodeColor : uint8_t
{
    NodeRed = 0,
    NodeBlack = 1
};

// rs i_tree key/node.rs:11-16 Node<K, E, V> (entity inlined: key + val)
struct TreeNode
{
    uint32_t parent;
    uint32_t left;
    uint32_t right;
    NodeColor color;
    VSegment key;
    ShapeCount val;
};

struct ScanTree
{
    std::vector<TreeNode> buffer; // rs Pool::buffer (pool.rs:5-8)
    std::vector<uint32_t> unused; // rs Pool::unused
    uint32_t root;

    // rs tree.rs:19-27 KeyExpTree::new -> Pool::new (pool.rs:12-20: capacity.max(8))
    explicit ScanTree(size_t capacity)
        : root(kEmptyRef)
    {
        const size_t cap = capacity > 8 ? capacity : 8;
        buffer.reserve(cap);
        unused.reserve(cap);
        poolReserve(cap);
        const uint32_t nil_index = getFreeIndex(); // rs tree.rs:20-22 (asserted == NIL_INDEX)
        (void)nil_index;
    }

    // rs pool.rs:23-32 Pool::reserve
    void poolReserve(size_t additional)
    {
        const uint32_t n = static_cast<uint32_t>(buffer.size());
        const uint32_t l = static_cast<uint32_t>(additional);
        const TreeNode default_node = TreeNode{ 0, 0, 0, NodeRed, VSegment{ IntPoint{ 0, 0 }, IntPoint{ 0, 0 } }, ShapeCount{ 0, 0 } };
        buffer.resize(buffer.size() + additional, default_node);
        unused.reserve(unused.size() + additional);
        // rs: unused.extend((n..n + l).rev()) — descending, so pop() yields n first
        for (uint32_t idx = n + l; idx-- > n;)
        {
            unused.push_back(idx);
        }
    }

    // rs pool.rs:34-39 get_free_index
    uint32_t getFreeIndex()
    {
        if (unused.empty())
        {
            poolReserve(unused.capacity());
        }
        const uint32_t index = unused.back();
        unused.pop_back();
        return index;
    }

    // rs pool.rs:41-45 put_back
    void putBack(uint32_t index)
    {
        unused.push_back(index);
    }

    // rs node.rs:21-23 is_not_expired
    bool isNotExpired(uint32_t index, int32_t time) const
    {
        return vSegmentExpiration(buffer[index].key) > time;
    }

    // rs tree.rs:112-114 is_black
    bool isBlack(uint32_t index) const
    {
        return index == kEmptyRef || buffer[index].color == NodeBlack;
    }

    // rs tree.rs:127-139 expire_root
    uint32_t expireRoot(int32_t time)
    {
        uint32_t index = root;
        while (index != kEmptyRef)
        {
            if (isNotExpired(index, time))
            {
                return index;
            }
            deleteIndex(index);
            index = root;
        }
        return index;
    }

    // rs tree.rs:141-154 expire_left
    uint32_t expireLeft(uint32_t n_index, int32_t time)
    {
        uint32_t index = buffer[n_index].left;
        while (index != kEmptyRef)
        {
            if (isNotExpired(index, time))
            {
                return index;
            }
            deleteIndex(index);
            index = buffer[n_index].left;
        }
        return index;
    }

    // rs tree.rs:156-169 expire_right
    uint32_t expireRight(uint32_t n_index, int32_t time)
    {
        uint32_t index = buffer[n_index].right;
        while (index != kEmptyRef)
        {
            if (isNotExpired(index, time))
            {
                return index;
            }
            deleteIndex(index);
            index = buffer[n_index].right;
        }
        return index;
    }

    // rs tree.rs:172-178 create_nil_node
    void createNilNode(uint32_t parent)
    {
        buffer[kNilIndex].parent = parent;
        buffer[kNilIndex].left = kEmptyRef;
        buffer[kNilIndex].right = kEmptyRef;
        buffer[kNilIndex].color = NodeRed;
    }

    // rs tree.rs:46-49 insert
    void insert(const VSegment& key, ShapeCount val, int32_t time)
    {
        // rs: debug_assert!(key.expiration() >= time)
        insertEntity(key, val, time);
    }

    // rs tree.rs:181-190 insert_root
    void insertRoot(const VSegment& key, ShapeCount val)
    {
        const uint32_t new_index = getFreeIndex();
        buffer[new_index].parent = kEmptyRef;
        buffer[new_index].left = kEmptyRef;
        buffer[new_index].right = kEmptyRef;
        buffer[new_index].color = NodeBlack;
        buffer[new_index].key = key;
        buffer[new_index].val = val;
        root = new_index;
    }

    // rs tree.rs:289-314 insert_entity
    void insertEntity(const VSegment& key, ShapeCount val, int32_t time)
    {
        uint32_t index = expireRoot(time);
        if (index == kEmptyRef)
        {
            insertRoot(key, val);
            return;
        }

        for (;;)
        {
            const uint32_t p_index = index;
            if (vSegmentOrder(key, buffer[index].key) < 0) // rs: key < node.key
            {
                index = expireLeft(index, time);
                if (index == kEmptyRef)
                {
                    insertAsLeft(key, val, p_index);
                    return;
                }
            }
            else
            {
                index = expireRight(index, time);
                if (index == kEmptyRef)
                {
                    insertAsRight(key, val, p_index);
                    return;
                }
            }
        }
    }

    // rs tree.rs:317-327 insert_new
    uint32_t insertNew(const VSegment& key, ShapeCount val, uint32_t p_index)
    {
        const uint32_t new_index = getFreeIndex();
        buffer[new_index].parent = p_index;
        buffer[new_index].left = kEmptyRef;
        buffer[new_index].right = kEmptyRef;
        buffer[new_index].color = NodeRed;
        buffer[new_index].key = key;
        buffer[new_index].val = val;
        return new_index;
    }

    // rs tree.rs:330-339 insert_as_left
    void insertAsLeft(const VSegment& key, ShapeCount val, uint32_t p_index)
    {
        const uint32_t new_index = insertNew(key, val, p_index);
        buffer[p_index].left = new_index;
        if (buffer[p_index].color == NodeRed)
        {
            fixRedBlackAfterInsert(new_index, p_index);
        }
    }

    // rs tree.rs:342-351 insert_as_right
    void insertAsRight(const VSegment& key, ShapeCount val, uint32_t p_index)
    {
        const uint32_t new_index = insertNew(key, val, p_index);
        buffer[p_index].right = new_index;
        if (buffer[p_index].color == NodeRed)
        {
            fixRedBlackAfterInsert(new_index, p_index);
        }
    }

    // rs tree.rs:353-417 fix_red_black_properties_after_insert
    void fixRedBlackAfterInsert(uint32_t n_index, uint32_t p_origin)
    {
        // parent is red!
        uint32_t p_index = p_origin;
        const uint32_t g_index = buffer[p_index].parent;
        if (g_index == kEmptyRef)
        {
            // Case 2: parent is root -> recolor root black
            buffer[p_index].color = NodeBlack;
            return;
        }

        // Case 3: Uncle is red -> recolor parent, grandparent and uncle
        const uint32_t u_index = getUncle(p_index);

        if (u_index != kEmptyRef && buffer[u_index].color == NodeRed)
        {
            buffer[p_index].color = NodeBlack;
            buffer[g_index].color = NodeRed;
            buffer[u_index].color = NodeBlack;

            const uint32_t gg_index = buffer[g_index].parent;
            if (gg_index != kEmptyRef && buffer[gg_index].color == NodeRed)
            {
                fixRedBlackAfterInsert(g_index, gg_index);
            }
        }
        else if (p_index == buffer[g_index].left)
        {
            // Parent is left child of grandparent
            // Case 4a: node is left->right "inner child"
            if (n_index == buffer[p_index].right)
            {
                rotateLeft(p_index);
                p_index = n_index;
            }

            // Case 5a: node is left->left "outer child"
            rotateRight(g_index);

            buffer[p_index].color = NodeBlack;
            buffer[g_index].color = NodeRed;
        }
        else
        {
            // Parent is right child of grandparent
            // Case 4b: node is right->left "inner child"
            if (n_index == buffer[p_index].left)
            {
                rotateRight(p_index);
                p_index = n_index;
            }

            // Case 5b: node is right->right "outer child"
            rotateLeft(g_index);

            buffer[p_index].color = NodeBlack;
            buffer[g_index].color = NodeRed;
        }
    }

    // rs tree.rs:419-437 rotate_right
    void rotateRight(uint32_t index)
    {
        const uint32_t p = buffer[index].parent;
        const uint32_t lt_index = buffer[index].left;

        const uint32_t lt_right = buffer[lt_index].right;
        buffer[lt_index].right = index;

        if (lt_right != kEmptyRef)
        {
            buffer[lt_right].parent = index;
        }

        buffer[index].left = lt_right;
        buffer[index].parent = lt_index;

        replaceParentsChild(p, index, lt_index);
    }

    // rs tree.rs:439-456 rotate_left
    void rotateLeft(uint32_t index)
    {
        const uint32_t p = buffer[index].parent;
        const uint32_t rt_index = buffer[index].right;

        const uint32_t rt_left = buffer[rt_index].left;
        buffer[rt_index].left = index;

        if (rt_left != kEmptyRef)
        {
            buffer[rt_left].parent = index;
        }
        buffer[index].right = rt_left;
        buffer[index].parent = rt_index;

        replaceParentsChild(p, index, rt_index);
    }

    // rs tree.rs:458-477 replace_parents_child
    void replaceParentsChild(uint32_t parent, uint32_t old_child, uint32_t new_child)
    {
        buffer[new_child].parent = parent;
        if (parent == kEmptyRef)
        {
            root = new_child;
            return;
        }

        if (buffer[parent].left == old_child)
        {
            buffer[parent].left = new_child;
        }
        else
        {
            buffer[parent].right = new_child;
        }
    }

    // rs tree.rs:479-485 find_left_minimum
    uint32_t findLeftMinimum(uint32_t i) const
    {
        while (buffer[i].left != kEmptyRef)
        {
            i = buffer[i].left;
        }
        return i;
    }

    // rs tree.rs:267-286 search_first_less_or_equal_by (first_less_or_equal_by)
    template <typename F>
    ShapeCount firstLessOrFailBy(int32_t time, ShapeCount default_value, F f)
    {
        uint32_t index = expireRoot(time);
        ShapeCount result = default_value;
        while (index != kEmptyRef)
        {
            const VSegment key = buffer[index].key;
            const ShapeCount val = buffer[index].val;
            const int c = f(key);
            if (c == 0)
            {
                return val;
            }
            if (c < 0) // Ordering::Less
            {
                result = val;
                index = expireRight(index, time);
            }
            else // Ordering::Greater
            {
                index = expireLeft(index, time);
            }
        }
        return result;
    }

    // rs tree.rs:487-537 delete_index
    void deleteIndex(uint32_t index)
    {
        // Node has zero or one child
        uint32_t del_index = index;

        uint32_t nd_left = buffer[index].left;
        uint32_t nd_right = buffer[index].right;
        uint32_t nd_parent = buffer[index].parent;
        NodeColor nd_color = buffer[index].color;

        // if two children replace node with its left minimum
        if (nd_left != kEmptyRef && nd_right != kEmptyRef)
        {
            const uint32_t successor_index = findLeftMinimum(nd_right);
            const VSegment s_key = buffer[successor_index].key;
            const ShapeCount s_val = buffer[successor_index].val;
            nd_parent = buffer[successor_index].parent;
            nd_left = buffer[successor_index].left;
            nd_right = buffer[successor_index].right;
            nd_color = buffer[successor_index].color;

            buffer[index].key = s_key;
            buffer[index].val = s_val;

            del_index = successor_index;
        }

        // only one child can be!
        if (nd_left != kEmptyRef)
        {
            replaceParentsChild(nd_parent, del_index, nd_left);
            fixRedBlackAfterDelete(nd_left);
        }
        else if (nd_right != kEmptyRef)
        {
            replaceParentsChild(nd_parent, del_index, nd_right);
            fixRedBlackAfterDelete(nd_right);
        }
        else if (nd_parent == kEmptyRef)
        {
            root = kEmptyRef;
        }
        else
        {
            // Node has no children -->
            // * node is red --> just remove it
            // * node is black --> replace it by a temporary NIL node (needed to fix the R-B rules)
            if (nd_color == NodeBlack)
            {
                createNilNode(nd_parent);
                setNilParentsChild(nd_parent, del_index);
                fixRedBlackAfterDelete(kNilIndex);
                fixParentsNilChild();
            }
            else
            {
                removeParentsChild(nd_parent, del_index);
            }
        }

        putBack(del_index);
    }

    // rs tree.rs:539-573 fix_red_black_properties_after_delete
    void fixRedBlackAfterDelete(uint32_t n_index)
    {
        // Case 1: Examined node is root, end of recursion
        if (n_index == root)
        {
            // do not color root to black
            return;
        }

        uint32_t s_index = getSibling(n_index);

        // Case 2: Red sibling
        if (buffer[s_index].color == NodeRed)
        {
            handleRedSibling(n_index, s_index);
            s_index = getSibling(n_index); // Get new sibling for fall-through to cases 3-6
        }

        // Cases 3+4: Black sibling with two black children
        if (isBlack(buffer[s_index].left) && isBlack(buffer[s_index].right))
        {
            buffer[s_index].color = NodeRed;
            const uint32_t p_index = buffer[n_index].parent;

            // Case 3: Black sibling with two black children + red parent
            if (buffer[p_index].color == NodeRed)
            {
                buffer[p_index].color = NodeBlack;
            }
            else
            {
                // Case 4: Black sibling with two black children + black parent
                fixRedBlackAfterDelete(p_index);
            }
        }
        else
        {
            // Case 5+6: Black sibling with at least one red child
            handleBlackSiblingWithAtLeastOneRedChild(n_index, s_index);
        }
    }

    // rs tree.rs:575-629 handle_black_sibling_with_at_least_one_red_child
    void handleBlackSiblingWithAtLeastOneRedChild(uint32_t n_index, uint32_t s_origin)
    {
        const uint32_t p_index = buffer[n_index].parent;

        uint32_t s_index = s_origin;
        uint32_t sibling_left = buffer[s_origin].left;
        uint32_t sibling_right = buffer[s_origin].right;

        const bool node_is_left_child = n_index == buffer[p_index].left;

        // Case 5: "outer nephew" is black -> recolor sibling and its child, rotate around sibling
        if (node_is_left_child && isBlack(sibling_right))
        {
            if (sibling_left != kEmptyRef)
            {
                buffer[sibling_left].color = NodeBlack;
            }
            buffer[s_index].color = NodeRed;
            rotateRight(s_index);
            s_index = buffer[p_index].right;

            sibling_left = buffer[s_index].left;
            sibling_right = buffer[s_index].right;
        }
        else if (!node_is_left_child && isBlack(sibling_left))
        {
            if (sibling_right != kEmptyRef)
            {
                buffer[sibling_right].color = NodeBlack;
            }
            buffer[s_index].color = NodeRed;
            rotateLeft(s_index);
            s_index = buffer[p_index].left;

            sibling_left = buffer[s_index].left;
            sibling_right = buffer[s_index].right;
        }

        // Fall-through to case 6...

        // Case 6: "outer nephew" is red -> recolor sibling + parent + sibling's child,
        // rotate around parent
        buffer[s_index].color = buffer[p_index].color;
        buffer[p_index].color = NodeBlack;
        if (node_is_left_child)
        {
            if (sibling_right != kEmptyRef)
            {
                buffer[sibling_right].color = NodeBlack;
            }
            rotateLeft(p_index);
        }
        else
        {
            if (sibling_left != kEmptyRef)
            {
                buffer[sibling_left].color = NodeBlack;
            }
            rotateRight(p_index);
        }
    }

    // rs tree.rs:631-646 handle_red_sibling
    void handleRedSibling(uint32_t n_index, uint32_t s_index)
    {
        // Recolor...
        buffer[s_index].color = NodeBlack;
        const uint32_t p_index = buffer[n_index].parent;
        buffer[p_index].color = NodeRed;

        // ... and rotate
        if (n_index == buffer[p_index].left)
        {
            rotateLeft(p_index);
        }
        else
        {
            rotateRight(p_index);
        }
    }

    // rs tree.rs:648-664 get_uncle
    uint32_t getUncle(uint32_t p_index) const
    {
        const uint32_t grandparent_index = buffer[p_index].parent;

        if (buffer[grandparent_index].left == p_index)
        {
            return buffer[grandparent_index].right;
        }
        return buffer[grandparent_index].left;
    }

    // rs tree.rs:666-676 get_sibling
    uint32_t getSibling(uint32_t n_index) const
    {
        const uint32_t p_index = buffer[n_index].parent;
        if (n_index == buffer[p_index].left)
        {
            return buffer[p_index].right;
        }
        return buffer[p_index].left;
    }

    // rs tree.rs:678-691 remove_parents_child
    void removeParentsChild(uint32_t parent, uint32_t old_child)
    {
        if (buffer[parent].left == old_child)
        {
            buffer[parent].left = kEmptyRef;
        }
        else
        {
            buffer[parent].right = kEmptyRef;
        }
    }

    // rs tree.rs:693-706 set_nil_parents_child
    void setNilParentsChild(uint32_t parent, uint32_t old_child)
    {
        if (buffer[parent].left == old_child)
        {
            buffer[parent].left = kNilIndex;
        }
        else
        {
            buffer[parent].right = kNilIndex;
        }
    }

    // rs tree.rs:708-722 fix_parents_nil_child
    void fixParentsNilChild()
    {
        const uint32_t p_index = buffer[kNilIndex].parent;
        if (buffer[p_index].left == kNilIndex)
        {
            buffer[p_index].left = kEmptyRef;
        }
        else
        {
            buffer[p_index].right = kEmptyRef;
        }
    }

    // rs tree.rs:81-107 clear
    void clear()
    {
        if (root == kEmptyRef)
        {
            return;
        }
        putBack(root);
        root = kEmptyRef;

        size_t n = 1;
        while (n > 0)
        {
            const size_t i0 = unused.size() - n;
            n = 0;
            const size_t end = unused.size(); // rs: range end captured once (BFS waves)
            for (size_t i = i0; i < end; ++i)
            {
                const uint32_t index = unused[i];
                const uint32_t left = buffer[index].left;
                const uint32_t right = buffer[index].right;
                if (left != kEmptyRef)
                {
                    putBack(left);
                    ++n;
                }
                if (right != kEmptyRef)
                {
                    putBack(right);
                    ++n;
                }
            }
        }
    }
};

// ============================================================================
// Solver — rs core/solver.rs (only the parts referenced by the build chain)
// ============================================================================

// rs core/solver.rs:13-18 enum Strategy
enum class Strategy
{
    List,
    Tree,
    Frag,
    Auto
};

struct Solver
{
    Strategy strategy;
    bool multithreading; // rs Option<MultithreadOptions>: default Some (solver.rs:86-92)

    static constexpr size_t MAX_SPLIT_LIST_COUNT = 4000;  // rs solver.rs:140
    static constexpr size_t MIN_FRAGMENT_COUNT = 16000;   // rs solver.rs:141
    static constexpr size_t MAX_FILL_LIST_COUNT = 8000;   // rs solver.rs:142

    // rs solver.rs:132-138 Solver::AUTO (precision is only consumed by the split solver)
    static Solver AUTO()
    {
        return Solver{ Strategy::Auto, true };
    }

    // rs solver.rs:176-182 is_list_fill
    bool isListFill(size_t segments_len) const
    {
        switch (strategy)
        {
        case Strategy::List:
            return true;
        case Strategy::Tree:
        case Strategy::Frag:
            return false;
        default: // Auto
            return segments_len < MAX_FILL_LIST_COUNT;
        }
    }

    // rs solver.rs:184-187 is_parallel_sort_allowed
    bool isParallelSortAllowed() const
    {
        return multithreading;
    }
};

// ============================================================================
// Fill strategies — rs build/boolean.rs:85-143 (the four FillStrategy impls)
// ============================================================================

enum class FillStrategyKind
{
    EvenOdd,
    NonZero,
    Positive,
    Negative
};

// rs build/boolean.rs:87-97 / 102-112 / 117-127 / 132-142 add_and_fill
static ShapeCount addAndFill(FillStrategyKind kind, ShapeCount this_, ShapeCount bot, SegmentFill* out_fill)
{
    const ShapeCount top = shapeCountAdd(bot, this_); // rs: let top = bot.add(this);

    uint8_t subj_top;
    uint8_t subj_bot;
    uint8_t clip_top;
    uint8_t clip_bot;

    switch (kind)
    {
    case FillStrategyKind::EvenOdd: // rs boolean.rs:88-94
        subj_top = static_cast<uint8_t>(1 & top.subj);
        subj_bot = static_cast<uint8_t>(1 & bot.subj);
        clip_top = static_cast<uint8_t>(1 & top.clip);
        clip_bot = static_cast<uint8_t>(1 & bot.clip);
        break;
    case FillStrategyKind::NonZero: // rs boolean.rs:103-108
        subj_top = top.subj != 0 ? 1 : 0;
        subj_bot = bot.subj != 0 ? 1 : 0;
        clip_top = top.clip != 0 ? 1 : 0;
        clip_bot = bot.clip != 0 ? 1 : 0;
        break;
    case FillStrategyKind::Positive: // rs boolean.rs:118-123
        subj_top = top.subj > 0 ? 1 : 0;
        subj_bot = bot.subj > 0 ? 1 : 0;
        clip_top = top.clip > 0 ? 1 : 0;
        clip_bot = bot.clip > 0 ? 1 : 0;
        break;
    default: // FillStrategyKind::Negative — rs boolean.rs:133-138
        subj_top = top.subj < 0 ? 1 : 0;
        subj_bot = bot.subj < 0 ? 1 : 0;
        clip_top = top.clip < 0 ? 1 : 0;
        clip_bot = bot.clip < 0 ? 1 : 0;
        break;
    }

    *out_fill = static_cast<SegmentFill>(subj_top | (subj_bot << 1) | (clip_top << 2) | (clip_bot << 3));
    return top;
}

// rs core/fill_rule.rs:9-15 enum FillRule
enum class FillRule
{
    EvenOdd,
    NonZero,
    Positive,
    Negative
};

// rs build/boolean.rs:66-71 build_boolean_fills dispatch
static FillStrategyKind fillStrategyFor(FillRule fill_rule)
{
    switch (fill_rule)
    {
    case FillRule::EvenOdd:
        return FillStrategyKind::EvenOdd;
    case FillRule::NonZero:
        return FillStrategyKind::NonZero;
    case FillRule::Positive:
        return FillStrategyKind::Positive;
    default:
        return FillStrategyKind::Negative;
    }
}

// ============================================================================
// Sweep — rs build/sweep.rs
// ============================================================================

// rs geom/end.rs:4-7 End
struct End
{
    size_t index;
    IntPoint point;
};

// rs build/sweep.rs:26-78 sweep_with_handler
// Handler: rs build/builder.rs:16-40 StoreFillsHandler (fills[index] = fill, rs builder.rs:31-36;
// fills is pre-allocated to segments.len() by build_fills_with_strategy).
template <typename Scan>
static void sweepWithHandler(FillStrategyKind strategy, Scan& scan, const std::vector<OverlaySegment>& segments, std::vector<SegmentFill>& fills)
{
    std::vector<End> node;
    node.reserve(4);
    const size_t n = segments.size();
    size_t i = 0;

    while (i < n)
    {
        const IntPoint p = segments[i].a;

        node.push_back(End{ i, segments[i].b });
        i += 1;

        while (i < n && pointEqual(segments[i].a, p))
        {
            node.push_back(End{ i, segments[i].b });
            i += 1;
        }

        if (node.size() > 1)
        {
            // rs: node.sort_by(|s0, s1| Triangle::clock_order_point(p, s1.point, s0.point));
            // Rust sort_by is STABLE -> std::stable_sort.
            std::stable_sort(node.begin(), node.end(), [p](const End& s0, const End& s1) {
                return clockOrderPoint(p, s1.point, s0.point) < 0;
            });
        }

        ShapeCount sum_count = scan.firstLessOrFailBy(
            p.x, shapeCountNew(0, 0), [p](const VSegment& s) { return vSegmentIsUnderPointOrder(s, p); });

        for (size_t k = 0; k < node.size(); ++k)
        {
            const End& se = node[k];
            const OverlaySegment& sid = segments[se.index];

            SegmentFill fill = kNone;
            const ShapeCount new_sum = addAndFill(strategy, unpackCount(sid.fill), sum_count, &fill);
            sum_count = new_sum;

            // rs builder.rs:31-36: StoreFillsHandler stores the fill by segment index
            fills[se.index] = fill;

            // rs geom/x_segment.rs:27-30 is_not_vertical
            if (sid.a.x != sid.b.x)
            {
                scan.insert(VSegment{ sid.a, sid.b }, sum_count, p.x); // rs: x_segment.into() = VSegment{a,b}
            }
        }

        node.clear();
    }
}

// rs build/sweep.rs:95-114 SweepRunner::run
// (rs caches the scan structure across runs purely for allocation reuse —
// take_scan_list/take_scan_tree, rs sweep.rs:80-83/139-159 — this entry runs the sweep once
// per call, so the scan is created and destroyed locally; identical behavior.)
static void sweepRun(FillStrategyKind strategy, const Solver& solver, const std::vector<OverlaySegment>& segments, std::vector<SegmentFill>& fills)
{
    const size_t count = segments.size();
    if (solver.isListFill(count))
    {
        const size_t capacity = std::max(log2Sqrt(count), size_t(4)) * 2; // rs sweep.rs:102
        ScanList list(capacity);
        sweepWithHandler(strategy, list, segments, fills);
    }
    else
    {
        const size_t capacity = std::max(log2Sqrt(count), size_t(8)); // rs sweep.rs:108
        ScanTree tree(capacity);
        sweepWithHandler(strategy, tree, segments, fills);
    }
}

// ============================================================================
// Graph data — rs core/link.rs, core/graph.rs, geom/id_point.rs
// ============================================================================

// rs geom/id_point.rs:4-7 IdPoint
struct IdPoint
{
    size_t id;
    IntPoint point;
};

// rs core/link.rs:8-12 OverlayLink
struct OverlayLink
{
    IdPoint a;
    IdPoint b;
    SegmentFill fill;
};

// rs core/link.rs:21-23 OverlayLink::other
static IdPoint linkOther(const OverlayLink& link, size_t node_id) noexcept
{
    if (link.a.id == node_id) return link.b;
    return link.a;
}

// rs core/link.rs:26-28 OverlayLink::is_direct
static bool linkIsDirect(const OverlayLink& link) noexcept
{
    return pointLess(link.a.point, link.b.point);
}

// rs core/graph.rs:23-26 OverlayNode:: Bridge([usize; 2]) | Cross(Vec<usize>).
// A node is a Bridge exactly when it was built from 2 indices (rs core/graph.rs:30-36),
// so the variant is represented by indices.size() == 2.
struct OverlayNode
{
    std::vector<size_t> indices;
};

// rs core/graph.rs:29-36 OverlayNode::with_indices (GraphNode impl)
static OverlayNode overlayNodeWithIndices(const std::vector<size_t>& indices)
{
    OverlayNode node;
    node.indices = indices;
    return node;
}

// ============================================================================
// GraphBuilder — rs build/builder.rs:46-107 + build/graph.rs + build/util.rs
// ============================================================================

struct GraphBuilder
{
    std::vector<OverlayLink> links;    // rs builder.rs:48
    std::vector<OverlayNode> nodes;    // rs builder.rs:49
    std::vector<SegmentFill> fills;    // rs builder.rs:50
    std::vector<End> ends;             // rs builder.rs:51

    // rs build/builder.rs:67-75 build_fills_with_strategy
    void buildFillsWithStrategy(FillStrategyKind strategy, const Solver& solver, const std::vector<OverlaySegment>& segments)
    {
        fills.resize(segments.size(), kNone);
        sweepRun(strategy, solver, segments, fills);
    }

    // rs build/builder.rs:78-92 build_links_by_filter (F::is_included passed as a function)
    void buildLinksByFilter(bool (*is_included)(SegmentFill), const std::vector<OverlaySegment>& segments)
    {
        links.clear();
        links.reserve(segments.size());

        // rs: for (segment, &fill) in segments.iter().zip(&self.fills) — zip stops at the shorter
        const size_t count = segments.size() < fills.size() ? segments.size() : fills.size();
        for (size_t i = 0; i < count; ++i)
        {
            const OverlaySegment& segment = segments[i];
            const SegmentFill fill = fills[i];
            if (!is_included(fill))
            {
                continue;
            }
            links.push_back(OverlayLink{ IdPoint{ 0, segment.a }, IdPoint{ 0, segment.b }, fill });
        }
    }

    // rs build/builder.rs:95-106 build_links_all
    void buildLinksAll(const std::vector<OverlaySegment>& segments)
    {
        links.clear();
        links.reserve(segments.size());

        const size_t count = segments.size() < fills.size() ? segments.size() : fills.size();
        for (size_t i = 0; i < count; ++i)
        {
            const OverlaySegment& segment = segments[i];
            const SegmentFill fill = fills[i];
            links.push_back(OverlayLink{ IdPoint{ 0, segment.a }, IdPoint{ 0, segment.b }, fill });
        }
    }

    // rs build/graph.rs:86-97 build_ends
    void buildEnds(const Solver& solver)
    {
        ends.clear();
        ends.reserve(links.size());
        for (size_t i = 0; i < links.size(); ++i)
        {
            ends.push_back(End{ i, links[i].b.point });
        }
        // rs: ends.sort_by_two_keys(solver.is_parallel_sort_allowed(), |e| e.point.x, |e| e.point.y)
        // (i_key_sort two_keys.rs:64-99 — unstable bin sort by (x, y); relative order of fully
        // equal keys is unspecified in Rust as well). C++ port: serial unstable sort by (x, y).
        (void)solver;
        std::sort(ends.begin(), ends.end(), [](const End& e0, const End& e1) {
            if (e0.point.x != e1.point.x) return e0.point.x < e1.point.x;
            return e0.point.y < e1.point.y;
        });
    }

    // rs build/graph.rs:9-83 build_nodes_and_connect_links
    void buildNodesAndConnectLinks(const Solver& solver)
    {
        const size_t n = links.size();
        if (n == 0)
        {
            return;
        }

        buildEnds(solver);

        nodes.clear();
        nodes.reserve(n);

        size_t ai = 0;
        size_t bi = 0;
        std::vector<size_t> indices;
        indices.reserve(4);

        while (ai < n || bi < n)
        {
            // rs build/graph.rs:25-34 — group of equal `a` points starting at ai
            size_t a_cnt = 0;
            size_t next_ai = ai;
            bool has_a = false;
            IntPoint a_point = IntPoint{ 0, 0 };
            if (ai < n)
            {
                const IntPoint point = links[ai].a.point;
                size_t end = ai + 1;
                while (end < n && pointEqual(links[end].a.point, point))
                {
                    end += 1;
                }
                a_cnt = end - ai;
                next_ai = end;
                has_a = true;
                a_point = point;
            }

            // rs build/graph.rs:36-45 — group of equal `b` points starting at bi
            size_t b_cnt = 0;
            size_t next_bi = bi;
            bool has_b = false;
            IntPoint b_point = IntPoint{ 0, 0 };
            if (bi < n)
            {
                const IntPoint point = ends[bi].point;
                size_t end = bi + 1;
                while (end < n && pointEqual(ends[end].point, point))
                {
                    end += 1;
                }
                b_cnt = end - bi;
                next_bi = end;
                has_b = true;
                b_point = point;
            }

            // rs build/graph.rs:47-54
            size_t consume_a;
            size_t consume_b;
            if (has_a && has_b)
            {
                if (pointEqual(a_point, b_point)) // (Some(a), Some(b)) if a == b
                {
                    consume_a = a_cnt;
                    consume_b = b_cnt;
                }
                else if (pointLess(a_point, b_point)) // (Some(a), Some(b)) if a < b
                {
                    consume_a = a_cnt;
                    consume_b = 0;
                }
                else // (Some(_), Some(_))
                {
                    consume_a = 0;
                    consume_b = b_cnt;
                }
            }
            else if (has_a) // (Some(_), None)
            {
                consume_a = a_cnt;
                consume_b = 0;
            }
            else if (has_b) // (None, Some(_))
            {
                consume_a = 0;
                consume_b = b_cnt;
            }
            else // (None, None) -> break
            {
                break;
            }

            const size_t node_id = nodes.size();

            if (consume_a > 0) // rs build/graph.rs:58-66
            {
                const size_t start = ai;
                const size_t end = ai + consume_a;
                for (size_t idx = start; idx < end; ++idx)
                {
                    links[idx].a.id = node_id;
                    indices.push_back(idx);
                }
                ai = next_ai;
            }

            if (consume_b > 0) // rs build/graph.rs:68-77
            {
                const size_t start = bi;
                const size_t end = bi + consume_b;
                for (size_t idx = start; idx < end; ++idx)
                {
                    const size_t link_idx = ends[idx].index;
                    indices.push_back(link_idx);
                    links[link_idx].b.id = node_id;
                }
                bi = next_bi;
            }

            // rs: debug_assert!(!indices.is_empty());
            nodes.push_back(overlayNodeWithIndices(indices));
            indices.clear();
        }

        // Rust's OverlayGraph::validate checks these connection invariants in
        // debug builds. Keep the guard active here as this graph is fed by
        // offset-generated segments that can expose split/rounding defects;
        // a singleton Cross node can send contour traversal to an unrelated
        // default link and leave the outer extraction cursor unchanged.
        for (size_t node_id = 0; node_id < nodes.size(); ++node_id)
        {
            const OverlayNode& node = nodes[node_id];
            if (node.indices.size() < 2U)
            {
                throw std::runtime_error(
                    "overlay graph: node has fewer than two incident links");
            }
        }
        if (nodes.size() > links.size())
        {
            throw std::runtime_error(
                "overlay graph: node count exceeds link count");
        }
    }
};

// rs build/util.rs:8-29 GraphBuilder::test_contour_for_loops (uses no builder fields)
static bool testContourForLoops(const std::vector<IntPoint>& contour, std::vector<IntPoint>& buffer)
{
    const size_t n = contour.size();
    if (n < 64)
    {
        // rs: contour[..n.saturating_sub(1)] — empty when n == 0
        const size_t upto = n > 0 ? n - 1 : 0;
        for (size_t i = 0; i < upto; ++i)
        {
            // rs: contour[i + 1..].contains(&contour[i])
            for (size_t j = i + 1; j < n; ++j)
            {
                if (pointEqual(contour[j], contour[i]))
                {
                    return true;
                }
            }
        }
        return false;
    }

    buffer.clear();
    buffer.insert(buffer.end(), contour.begin(), contour.end());
    // rs: buffer.sort_by_two_keys(false, |p| p.x, |p| p.y)
    std::sort(buffer.begin(), buffer.end(), [](const IntPoint& p0, const IntPoint& p1) {
        if (p0.x != p1.x) return p0.x < p1.x;
        return p0.y < p1.y;
    });

    // rs: buffer.windows(2).any(|w| w[0] == w[1])
    for (size_t i = 0; i + 1 < buffer.size(); ++i)
    {
        if (pointEqual(buffer[i], buffer[i + 1]))
        {
            return true;
        }
    }
    return false;
}

// ============================================================================
// Boolean fill filters — rs build/boolean.rs:202-274
// ============================================================================

// rs build/boolean.rs:213-218 is_subject
static bool fillIsSubject(SegmentFill fill)
{
    const SegmentFill subj = static_cast<SegmentFill>(fill & kSubjBoth);
    return subj == kSubjTop || subj == kSubjBottom;
}

// rs build/boolean.rs:220-225 is_clip
static bool fillIsClip(SegmentFill fill)
{
    const SegmentFill clip = static_cast<SegmentFill>(fill & kClipBoth);
    return clip == kClipTop || clip == kClipBottom;
}

// rs build/boolean.rs:227-234 is_intersect
static bool fillIsIntersect(SegmentFill fill)
{
    const SegmentFill top = static_cast<SegmentFill>(fill & kBothTop);
    const SegmentFill bottom = static_cast<SegmentFill>(fill & kBothBottom);

    return (top == kBothTop || bottom == kBothBottom) && fill != kAll;
}

// rs build/boolean.rs:236-242 is_union
static bool fillIsUnion(SegmentFill fill)
{
    const SegmentFill top = static_cast<SegmentFill>(fill & kBothTop);
    const SegmentFill bottom = static_cast<SegmentFill>(fill & kBothBottom);
    return (top == kNone || bottom == kNone) && fill != kNone;
}

// rs build/boolean.rs:244-251 is_difference
static bool fillIsDifference(SegmentFill fill)
{
    const SegmentFill top = static_cast<SegmentFill>(fill & kBothTop);
    const SegmentFill bottom = static_cast<SegmentFill>(fill & kBothBottom);
    const bool is_not_inner_subj = fill != kSubjBoth;
    return (top == kSubjTop || bottom == kSubjBottom) && is_not_inner_subj;
}

// rs build/boolean.rs:253-260 is_inverse_difference
static bool fillIsInverseDifference(SegmentFill fill)
{
    const SegmentFill top = static_cast<SegmentFill>(fill & kBothTop);
    const SegmentFill bottom = static_cast<SegmentFill>(fill & kBothBottom);
    const bool is_not_inner_clip = fill != kClipBoth;
    return (top == kClipTop || bottom == kClipBottom) && is_not_inner_clip;
}

// rs build/boolean.rs:262-273 is_xor
static bool fillIsXor(SegmentFill fill)
{
    const SegmentFill top = static_cast<SegmentFill>(fill & kBothTop);
    const SegmentFill bottom = static_cast<SegmentFill>(fill & kBothBottom);

    const bool is_any_top = top == kSubjTop || top == kClipTop;
    const bool is_any_bottom = bottom == kSubjBottom || bottom == kClipBottom;

    // only one of it must be true
    return is_any_top != is_any_bottom;
}

// ============================================================================
// OverlayRule — rs core/overlay_rule.rs
// ============================================================================

// rs core/overlay_rule.rs:13-21 enum OverlayRule
enum class OverlayRule
{
    Subject,
    Clip,
    Intersect,
    Union,
    Difference,
    InverseDifference,
    Xor
};

// rs core/overlay_rule.rs:24-39 OverlayRule::is_fill_top
static bool isFillTop(OverlayRule rule, SegmentFill fill)
{
    switch (rule)
    {
    case OverlayRule::Subject:
        return (fill & kSubjTop) == kSubjTop;
    case OverlayRule::Clip:
        return (fill & kClipTop) == kClipTop;
    case OverlayRule::Intersect:
        return (fill & kBothTop) == kBothTop;
    case OverlayRule::Union:
        return (fill & kBothBottom) == kNone;
    case OverlayRule::Difference:
        return (fill & kBothTop) == kSubjTop;
    case OverlayRule::InverseDifference:
        return (fill & kBothTop) == kClipTop;
    default:
    { // Xor — rs overlay_rule.rs:33-37
        const bool is_subject = (fill & kBothTop) == kSubjTop;
        const bool is_clip = (fill & kBothTop) == kClipTop;
        return is_subject || is_clip;
    }
    }
}

// ============================================================================
// Extraction — rs core/extract.rs
// ============================================================================

// rs core/extract.rs:20-28 enum VisitState (repr u8)
enum VisitStateValue : uint8_t
{
    Unvisited = 0,
    Skipped = 1,
    HoleVisited = 2,
    HullVisited = 3
};

// rs core/extract.rs:336-343 VisitState::new
static inline uint8_t visitStateNew(bool skipped) noexcept
{
    return skipped ? Skipped : Unvisited;
}

// rs core/extract.rs:355-378 Visit for [VisitState]
static inline bool isVisited(const std::vector<uint8_t>& visited, size_t index) noexcept
{
    return visited[index] != Unvisited;
}

static inline bool isNotVisited(const std::vector<uint8_t>& visited, size_t index) noexcept
{
    return visited[index] == Unvisited;
}

static inline void visitEdge(std::vector<uint8_t>& visited, size_t index, uint8_t state) noexcept
{
    visited[index] = state;
}

// rs core/extract.rs:30-35 BooleanExtractionBuffer (contour_visited is ogc-path only)
struct BooleanExtractionBuffer
{
    std::vector<IntPoint> points;
    std::vector<uint8_t> visited;
};

// rs build/boolean.rs:292-298 filter_subject_into
static void filterSubjectInto(const std::vector<OverlayLink>& links, std::vector<uint8_t>& buffer)
{
    buffer.clear();
    buffer.reserve(links.size());
    for (size_t i = 0; i < links.size(); ++i)
    {
        buffer.push_back(visitStateNew(!fillIsSubject(links[i].fill)));
    }
}

// rs build/boolean.rs:301-307 filter_clip_into
static void filterClipInto(const std::vector<OverlayLink>& links, std::vector<uint8_t>& buffer)
{
    buffer.clear();
    buffer.reserve(links.size());
    for (size_t i = 0; i < links.size(); ++i)
    {
        buffer.push_back(visitStateNew(!fillIsClip(links[i].fill)));
    }
}

// rs build/boolean.rs:310-316 filter_intersect_into
static void filterIntersectInto(const std::vector<OverlayLink>& links, std::vector<uint8_t>& buffer)
{
    buffer.clear();
    buffer.reserve(links.size());
    for (size_t i = 0; i < links.size(); ++i)
    {
        buffer.push_back(visitStateNew(!fillIsIntersect(links[i].fill)));
    }
}

// rs build/boolean.rs:319-325 filter_union_into
static void filterUnionInto(const std::vector<OverlayLink>& links, std::vector<uint8_t>& buffer)
{
    buffer.clear();
    buffer.reserve(links.size());
    for (size_t i = 0; i < links.size(); ++i)
    {
        buffer.push_back(visitStateNew(!fillIsUnion(links[i].fill)));
    }
}

// rs build/boolean.rs:328-334 filter_difference_into
static void filterDifferenceInto(const std::vector<OverlayLink>& links, std::vector<uint8_t>& buffer)
{
    buffer.clear();
    buffer.reserve(links.size());
    for (size_t i = 0; i < links.size(); ++i)
    {
        buffer.push_back(visitStateNew(!fillIsDifference(links[i].fill)));
    }
}

// rs build/boolean.rs:337-343 filter_inverse_difference_into
static void filterInverseDifferenceInto(const std::vector<OverlayLink>& links, std::vector<uint8_t>& buffer)
{
    buffer.clear();
    buffer.reserve(links.size());
    for (size_t i = 0; i < links.size(); ++i)
    {
        buffer.push_back(visitStateNew(!fillIsInverseDifference(links[i].fill)));
    }
}

// rs build/boolean.rs:346-352 filter_xor_into
static void filterXorInto(const std::vector<OverlayLink>& links, std::vector<uint8_t>& buffer)
{
    buffer.clear();
    buffer.reserve(links.size());
    for (size_t i = 0; i < links.size(); ++i)
    {
        buffer.push_back(visitStateNew(!fillIsXor(links[i].fill)));
    }
}

// rs build/boolean.rs:276-289 [OverlayLink]::filter_by_overlay_into
static void filterByOverlayInto(const std::vector<OverlayLink>& links, OverlayRule overlay_rule, std::vector<uint8_t>& buffer)
{
    switch (overlay_rule)
    {
    case OverlayRule::Subject:
        filterSubjectInto(links, buffer);
        break;
    case OverlayRule::Clip:
        filterClipInto(links, buffer);
        break;
    case OverlayRule::Intersect:
        filterIntersectInto(links, buffer);
        break;
    case OverlayRule::Union:
        filterUnionInto(links, buffer);
        break;
    case OverlayRule::Difference:
        filterDifferenceInto(links, buffer);
        break;
    case OverlayRule::InverseDifference:
        filterInverseDifferenceInto(links, buffer);
        break;
    case OverlayRule::Xor:
        filterXorInto(links, buffer);
        break;
    }
}

// rs core/extract.rs:268-273 StartPathData
struct StartPathData
{
    IntPoint begin;
    size_t node_id;
    size_t link_id;
    size_t last_node_id;
};

// rs core/extract.rs:275-294 StartPathData::new
static StartPathData startPathDataNew(bool direction, const OverlayLink& link, size_t link_id) noexcept
{
    if (direction)
    {
        return StartPathData{ link.b.point, link.a.id, link_id, link.b.id };
    }
    return StartPathData{ link.a.point, link.b.id, link_id, link.a.id };
}

// ---- contour helpers (i_shape) ----

// rs i_shape-1.18.0/src/int/path.rs:19-32 [IntPoint]::unsafe_area
static int64_t unsafeArea(const std::vector<IntPoint>& self)
{
    const size_t n = self.size();
    IntPoint p0 = self[n - 1];
    int64_t area = 0;

    for (size_t i = 0; i < n; ++i)
    {
        const IntPoint p1 = self[i];
        const int64_t a = wrapMul64(static_cast<int64_t>(p1.x), static_cast<int64_t>(p0.y));
        const int64_t b = wrapMul64(static_cast<int64_t>(p1.y), static_cast<int64_t>(p0.x));
        area = wrapSub64(wrapAdd64(area, a), b);
        p0 = p1;
    }

    return area;
}

// rs i_shape-1.18.0/src/int/simple.rs:203-226 [IntPoint]::is_simple
static bool isSimpleContour(const std::vector<IntPoint>& self)
{
    const size_t count = self.size();

    if (count < 3)
    {
        return false;
    }

    IntPoint p0 = self[count - 2];
    const IntPoint p1 = self[count - 1];

    FixVec v0 = pointSubtract(p1, p0);
    p0 = p1;

    for (size_t i = 0; i < count; ++i)
    {
        const IntPoint pi = self[i];
        const FixVec vi = pointSubtract(pi, p0);
        const int64_t prod = fixVecCross(vi, v0);
        if (prod == 0)
        {
            return false;
        }
        v0 = vi;
        p0 = pi;
    }

    return true;
}

// rs i_shape-1.18.0/src/int/simple.rs:329-334 Node
struct SimplifierNode
{
    size_t next;
    size_t index;
    size_t prev;
};

// rs i_shape-1.18.0/src/int/simple.rs:241-327 ContourSimplifier::simplify_contour
// (rs: returns None -> false; Some(contour) -> true + out)
static bool simplifiedContour(const std::vector<IntPoint>& contour, std::vector<IntPoint>& out)
{
    size_t n = contour.size();

    if (n < 3)
    {
        return false;
    }

    std::vector<bool> validated(n, false);

    std::vector<SimplifierNode> nodes;
    nodes.reserve(n);

    size_t prev = n - 1;
    size_t next = 1;
    const size_t last = n - 1;
    for (size_t index = 0; index < last; ++index)
    {
        nodes.push_back(SimplifierNode{ next, index, prev });
        prev = index;
        next += 1;
    }
    nodes.push_back(SimplifierNode{ 0, last, prev });

    size_t first = 0;
    SimplifierNode node = nodes[first];
    size_t i = 0;
    while (i < n)
    {
        if (validated[node.index])
        {
            node = nodes[node.next];
            continue;
        }

        const IntPoint p0 = contour[node.prev];
        const IntPoint p1 = contour[node.index];
        const IntPoint p2 = contour[node.next];

        if (fixVecCross(pointSubtract(p1, p0), pointSubtract(p2, p1)) == 0)
        {
            n -= 1;
            if (n < 3)
            {
                return false;
            }

            // remove node
            nodes[node.prev].next = node.next;
            nodes[node.next].prev = node.prev;

            if (node.index == first)
            {
                first = node.next;
            }

            node = nodes[node.prev];

            if (validated[node.prev])
            {
                i -= 1;
                validated[node.prev] = false;
            }

            if (validated[node.next])
            {
                i -= 1;
                validated[node.next] = false;
            }

            if (validated[node.index])
            {
                i -= 1;
                validated[node.index] = false;
            }
        }
        else
        {
            validated[node.index] = true;
            i += 1;
            node = nodes[node.next];
        }
    }

    out.assign(n, IntPoint{ 0, 0 });
    node = nodes[first];

    for (size_t k = 0; k < n; ++k)
    {
        out[k] = contour[node.index];
        node = nodes[node.next];
    }

    return true;
}

// rs i_shape-1.18.0/src/int/simple.rs:76-90 IntContour::simplify_contour
static bool simplifyContour(std::vector<IntPoint>& self)
{
    if (isSimpleContour(self))
    {
        return false;
    }
    std::vector<IntPoint> simplified;
    if (simplifiedContour(self, simplified))
    {
        // rs: self.clear(); self.extend(contour) — replace contents
        self = simplified;
    }
    else
    {
        self.clear();
    }
    return true;
}

// rs core/extract.rs:296-299 GraphContour::validate result
struct Validity
{
    bool is_valid;
    bool is_modified;
};

// rs core/extract.rs:302-322 IntContour::validate
static Validity validateContour(std::vector<IntPoint>& self, uint64_t min_output_area, bool preserve_output_collinear)
{
    const bool is_modified = !preserve_output_collinear ? simplifyContour(self) : false;

    if (self.size() < 3)
    {
        return Validity{ false, is_modified };
    }

    if (min_output_area == 0)
    {
        return Validity{ true, is_modified };
    }
    const int64_t area = unsafeArea(self);
    // rs: area.unsigned_abs() >> 1
    const uint64_t abs_area = area < 0 ? (static_cast<uint64_t>(0) - static_cast<uint64_t>(area)) : static_cast<uint64_t>(area);
    const bool is_valid = (abs_area >> 1) >= min_output_area;

    return Validity{ is_valid, is_modified };
}

// rs core/extract.rs:324-333 IntContour::push_node_and_get_other
static size_t pushNodeAndGetOther(std::vector<IntPoint>& points, const OverlayLink& link, size_t node_id)
{
    if (link.a.id == node_id)
    {
        points.push_back(link.a.point);
        return link.b.id;
    }
    points.push_back(link.b.point);
    return link.a.id;
}

// rs core/nearest_vector.rs:4-11 NearestVector
struct NearestVector
{
    IntPoint c;             // center
    FixVec va;              // our target vector
    FixVec vb;              // nearest vector to Va by specified rotation
    bool ab_more_180;       // is angle between Va and Vb more than 180 degrees
    size_t best_id;
    int64_t rotation_factor; // +1 for clockwise, -1 for counterclockwise

    // rs core/nearest_vector.rs:15-31 NearestVector::new
    NearestVector(IntPoint c_, IntPoint a, IntPoint b, size_t best_id_, bool clockwise)
        : c(c_)
        , va(pointSubtract(a, c_))
        , vb(pointSubtract(b, c_))
        , best_id(best_id_)
    {
        if (clockwise)
        {
            ab_more_180 = fixVecCross(va, vb) >= 0;
            rotation_factor = 1;
        }
        else
        {
            ab_more_180 = fixVecCross(va, vb) <= 0;
            rotation_factor = -1;
        }
    }

    // rs core/nearest_vector.rs:33-48 NearestVector::add
    void add(IntPoint p, size_t id)
    {
        const FixVec vp = pointSubtract(p, c);
        const bool ap_more_180 = wrapMul64(fixVecCross(va, vp), rotation_factor) >= 0;

        if (ab_more_180 == ap_more_180)
        {
            if (wrapMul64(fixVecCross(vp, vb), rotation_factor) < 0)
            {
                vb = vp;
                best_id = id;
            }
        }
        else if (ab_more_180)
        {
            ab_more_180 = false;
            vb = vp;
            best_id = id;
        }
    }
};

// rs core/extract.rs:380-570 GraphUtil
struct GraphUtil
{
    // rs core/extract.rs:389-414 find_left_top_link
    static size_t findLeftTopLink(const std::vector<OverlayLink>& links, const std::vector<OverlayNode>& nodes, size_t link_index, const std::vector<uint8_t>& visited)
    {
        const OverlayLink& top = links[link_index];
        const OverlayNode& node = nodes[top.a.id];

        // rs: debug_assert!(top.is_direct());

        if (node.indices.size() == 2) // OverlayNode::Bridge(bridge)
        {
            return findLeftTopLinkOnBridge(links, node.indices);
        }
        // OverlayNode::Cross(indices)
        return findLeftTopLinkOnIndices(links, top, link_index, node.indices, visited);
    }

    // rs core/extract.rs:417-450 find_left_top_link_on_indices
    static size_t findLeftTopLinkOnIndices(const std::vector<OverlayLink>& links, const OverlayLink& link, size_t link_index, const std::vector<size_t>& indices, const std::vector<uint8_t>& visited)
    {
        size_t top_index = link_index;
        const OverlayLink* top = &link;

        // find most top link

        for (size_t k = 0; k < indices.size(); ++k)
        {
            const size_t i = indices[k];
            if (i == link_index)
            {
                continue;
            }
            const OverlayLink& candidate = links[i];
            if (!linkIsDirect(candidate) || isClockwisePoint(top->a.point, top->b.point, candidate.b.point))
            {
                continue;
            }

            if (isVisited(visited, i))
            {
                continue;
            }

            top_index = i;
            top = &candidate;
        }

        return top_index;
    }

    // rs core/extract.rs:453-462 find_left_top_link_on_bridge
    static size_t findLeftTopLinkOnBridge(const std::vector<OverlayLink>& links, const std::vector<size_t>& bridge)
    {
        const OverlayLink& l0 = links[bridge[0]];
        const OverlayLink& l1 = links[bridge[1]];
        if (isClockwisePoint(l0.a.point, l0.b.point, l1.b.point))
        {
            return bridge[0];
        }
        return bridge[1];
    }

    // rs core/extract.rs:465-490 next_link
    static size_t nextLink(const std::vector<OverlayLink>& links, const std::vector<OverlayNode>& nodes, size_t link_id, size_t node_id, bool clockwise, const std::vector<uint8_t>& visited)
    {
        const OverlayNode& node = nodes[node_id];

        if (node.indices.size() == 2) // OverlayNode::Bridge(bridge)
        {
            if (node.indices[0] == link_id)
            {
                return node.indices[1];
            }
            return node.indices[0];
        }

        // OverlayNode::Cross(indices)
        return findNearestLinkTo(links, link_id, node_id, clockwise, node.indices, visited);
    }

    // rs core/extract.rs:496-569 find_nearest_link_to
    static size_t findNearestLinkTo(const std::vector<OverlayLink>& links, size_t target_index, size_t node_id, bool clockwise, const std::vector<size_t>& indices, const std::vector<uint8_t>& visited)
    {
        bool is_first = true;
        size_t first_index = 0;
        size_t second_index = kSizeMax; // rs usize::MAX
        size_t pos = 0;
        for (size_t i = 0; i < indices.size(); ++i)
        {
            const size_t link_index = indices[i];
            if (isNotVisited(visited, link_index))
            {
                if (is_first)
                {
                    first_index = link_index;
                    is_first = false;
                }
                else
                {
                    second_index = link_index;
                    pos = i;
                    break;
                }
            }
        }

        if (is_first)
        {
            // The Rust implementation relies on the graph invariant that a
            // Cross node always has an unvisited outgoing link. A malformed
            // graph must not fall through to the default index 0: that can
            // teleport the contour to an unrelated link and break traversal.
            throw std::runtime_error(
                "overlay extract: cross node has no unvisited link");
        }

        if (second_index == kSizeMax)
        {
            return first_index;
        }

        const OverlayLink& target = links[target_index];
        IntPoint c;
        IntPoint a;
        if (target.a.id == node_id)
        {
            c = target.a.point;
            a = target.b.point;
        }
        else
        {
            c = target.b.point;
            a = target.a.point;
        }

        // more the one vectors
        const IntPoint b = linkOther(links[first_index], node_id).point;
        NearestVector vector_solver(c, a, b, first_index, clockwise);

        // add second vector
        vector_solver.add(linkOther(links[second_index], node_id).point, second_index);

        // check the rest vectors
        for (size_t k = pos + 1; k < indices.size(); ++k)
        {
            const size_t link_index = indices[k];
            if (isNotVisited(visited, link_index))
            {
                const IntPoint p = linkOther(links[link_index], node_id).point;
                vector_solver.add(p, link_index);
            }
        }

        return vector_solver.best_id;
    }
};

// rs core/overlay.rs:28-44 IntOverlayOptions (defaults rs core/overlay.rs:403-413)
struct IntOverlayOptions
{
    bool preserve_input_collinear = false;
    bool output_clockwise = false; // ContourDirection::CounterClockwise default (rs overlay.rs:58-61)
    bool preserve_output_collinear = false;
    uint64_t min_output_area = 0;
    bool ogc = false;
};

// rs core/graph.rs:16-20 OverlayGraph (borrows nodes/links from the GraphBuilder)
struct OverlayGraph
{
    const std::vector<OverlayNode>* nodes;
    const std::vector<OverlayLink>* links;
    IntOverlayOptions options;

    // rs core/extract.rs:76-85 extract_contours_into
    // (output: rs FlatContoursBuffer — i_shape flat/buffer.rs:103-108 add_contour — adapted to
    // vector<ExtractedContour>; the is_hole flag is the value computed at rs extract.rs:240)
    void extractContoursInto(OverlayRule overlay_rule, BooleanExtractionBuffer& buffer, std::vector<ExtractedContour>& output) const
    {
        filterByOverlayInto(*links, overlay_rule, buffer.visited);
        extractContours(overlay_rule, buffer, output);
    }

    // rs core/extract.rs:181-210 find_contour
    void findContour(const StartPathData& start_data, bool clockwise, uint8_t visited_state, std::vector<uint8_t>& visited, std::vector<IntPoint>& points) const
    {
        size_t link_id = start_data.link_id;
        size_t node_id = start_data.node_id;
        const size_t last_node_id = start_data.last_node_id;

        visitEdge(visited, link_id, visited_state);
        points.clear();
        points.push_back(start_data.begin);

        // Bring-up guard: a closed tour can not visit more links than the
        // graph holds. If nextLink ever cycles (translation bug under
        // audit), abort the whole extraction instead of growing `points`
        // without bound (the runaway previously surfaced as std::bad_alloc).
        // Throwing (rather than returning a partial contour) makes the
        // caller fall back to the established ring resolver, so no partial
        // result can leak out.
        const size_t maxTourLength = links->size() + 1U;

        // Find a closed tour
        while (node_id != last_node_id)
        {
            if (points.size() >= maxTourLength)
            {
                throw std::runtime_error("overlay extract: tour did not close");
            }
            link_id = GraphUtil::nextLink(*links, *nodes, link_id, node_id, clockwise, visited);

            const OverlayLink& link = (*links)[link_id];
            node_id = pushNodeAndGetOther(points, link, node_id);

            visitEdge(visited, link_id, visited_state);
        }
    }

    // rs core/extract.rs:212-265 extract_contours
    void extractContours(OverlayRule overlay_rule, BooleanExtractionBuffer& buffer, std::vector<ExtractedContour>& output) const
    {
        const bool clockwise = options.output_clockwise; // rs: output_direction == Clockwise
        const size_t len = buffer.visited.size();
        // rs: buffer.points.reserve_capacity(len) (i_shape util/reserve.rs:7-16)
        if (buffer.points.capacity() < len)
        {
            buffer.points.reserve(len);
        }
        // rs: output.clear_and_reserve(len, 4) (i_shape flat/buffer.rs:94-100) — points cap len,
        // ranges cap 4; the C++ output vector reserves the contour-count hint (4).
        output.clear();
        output.reserve(4);

        size_t link_index = 0;
        while (link_index < len)
        {
            if (isVisited(buffer.visited, link_index))
            {
                link_index += 1;
                continue;
            }

            const size_t left_top_link = GraphUtil::findLeftTopLink(*links, *nodes, link_index, buffer.visited);

            const OverlayLink& link = (*links)[left_top_link];
            const bool is_hole = isFillTop(overlay_rule, link.fill);
            // rs: [VisitState::HullVisited, VisitState::HoleVisited][is_hole as usize]
            const uint8_t visited_state = is_hole ? HoleVisited : HullVisited;

            const bool direction = is_hole == clockwise;
            const StartPathData start_data = startPathDataNew(direction, link, left_top_link);

            findContour(start_data, direction, visited_state, buffer.visited, buffer.points);
            const Validity validity = validateContour(buffer.points, options.min_output_area, options.preserve_output_collinear);

            if (!validity.is_valid)
            {
                link_index += 1;
                continue;
            }

            // A valid contour must consume the cursor link as well as close
            // the selected tour. Otherwise link_index can stay fixed while
            // the same already-visited contour is appended forever.
            if (!isVisited(buffer.visited, link_index))
            {
                throw std::runtime_error(
                    "overlay extract: contour made no progress");
            }

            // rs extract.rs:263: output.add_contour(buffer.points.as_slice()) — flat contour,
            // no nesting/hole-joining; is_hole surfaced from rs extract.rs:240.
            output.emplace_back();
            ExtractedContour& contour = output.back();
            contour.points.assign(buffer.points.begin(), buffer.points.end());
            contour.is_hole = is_hole;
        }
    }
};

// ============================================================================
// Boolean graph build — rs build/boolean.rs
// ============================================================================

// rs build/boolean.rs:153-158 InclusionFilterStrategy for SubjectFilter (and siblings 160-200)
static bool subjectFilterIncluded(SegmentFill fill)
{
    return fillIsSubject(fill);
}

static bool clipFilterIncluded(SegmentFill fill) // rs boolean.rs:160-165
{
    return fillIsClip(fill);
}

static bool intersectFilterIncluded(SegmentFill fill) // rs boolean.rs:167-172
{
    return fillIsIntersect(fill);
}

static bool unionFilterIncluded(SegmentFill fill) // rs boolean.rs:174-179
{
    return fillIsUnion(fill);
}

static bool differenceFilterIncluded(SegmentFill fill) // rs boolean.rs:181-186
{
    return fillIsDifference(fill);
}

static bool inverseDifferenceFilterIncluded(SegmentFill fill) // rs boolean.rs:188-193
{
    return fillIsInverseDifference(fill);
}

static bool xorFilterIncluded(SegmentFill fill) // rs boolean.rs:195-200
{
    return fillIsXor(fill);
}

// rs build/boolean.rs:74-82 boolean_graph
static OverlayGraph booleanGraph(GraphBuilder& builder, const IntOverlayOptions& options, const Solver& solver)
{
    builder.buildNodesAndConnectLinks(solver);
    OverlayGraph graph;
    graph.nodes = &builder.nodes;
    graph.links = &builder.links;
    graph.options = options;
    return graph;
}

// rs build/boolean.rs:60-72 build_boolean_fills
static void buildBooleanFills(GraphBuilder& builder, FillRule fill_rule, const Solver& solver, const std::vector<OverlaySegment>& segments)
{
    builder.buildFillsWithStrategy(fillStrategyFor(fill_rule), solver, segments);
}

// rs build/boolean.rs:38-57 build_boolean_overlay
static OverlayGraph buildBooleanOverlay(GraphBuilder& builder, FillRule fill_rule, OverlayRule overlay_rule, const IntOverlayOptions& options, const Solver& solver, const std::vector<OverlaySegment>& segments)
{
    buildBooleanFills(builder, fill_rule, solver, segments);
    switch (overlay_rule)
    {
    case OverlayRule::Subject:
        builder.buildLinksByFilter(subjectFilterIncluded, segments);
        break;
    case OverlayRule::Clip:
        builder.buildLinksByFilter(clipFilterIncluded, segments);
        break;
    case OverlayRule::Intersect:
        builder.buildLinksByFilter(intersectFilterIncluded, segments);
        break;
    case OverlayRule::Union:
        builder.buildLinksByFilter(unionFilterIncluded, segments);
        break;
    case OverlayRule::Difference:
        builder.buildLinksByFilter(differenceFilterIncluded, segments);
        break;
    case OverlayRule::InverseDifference:
        builder.buildLinksByFilter(inverseDifferenceFilterIncluded, segments);
        break;
    case OverlayRule::Xor:
        builder.buildLinksByFilter(xorFilterIncluded, segments);
        break;
    }
    return booleanGraph(builder, options, solver);
}

// rs build/boolean.rs:25-35 build_boolean_all (provided for completeness; not used by the entry)
static OverlayGraph buildBooleanAll(GraphBuilder& builder, FillRule fill_rule, const IntOverlayOptions& options, const Solver& solver, const std::vector<OverlaySegment>& segments)
{
    buildBooleanFills(builder, fill_rule, solver, segments);
    builder.buildLinksAll(segments);
    return booleanGraph(builder, options, solver);
}

// ============================================================================
// Segment helpers — rs segm/segment.rs + segm/sort.rs
// ============================================================================

} // namespace detail

// rs i_overlay-4.5.2/src/segm/segment.rs:30-42 Segment::create_and_validate
OverlaySegment makeOverlaySegment(IntPoint p0, IntPoint p1, int32_t packedFill) noexcept
{
    using detail::pointLess;

    if (pointLess(p0, p1))
    {
        return OverlaySegment{ p0, p1, packedFill };
    }
    // rs: count.invert() — negate both packed counters (rs segm/boolean.rs:51-56)
    return OverlaySegment{ p1, p0, detail::negatePackedFill(packedFill) };
}

namespace detail
{

// rs i_overlay-4.5.2/src/segm/sort.rs:8-18 ShapeSegmentsSort::sort_by_ab
// (used by the upstream split solver before the build chain; exposed so callers can
// normalize input order: sort by (a.x, a.y), then b lexicographically)
void sortSegmentsByAb(std::vector<OverlaySegment>& segments)
{
    // rs: sort_by_two_keys_then_by(parallel, a.x, a.y, b.cmp) — total order on (a.x, a.y, b)
    std::sort(segments.begin(), segments.end(), [](const OverlaySegment& s0, const OverlaySegment& s1) {
        if (s0.a.x != s1.a.x) return s0.a.x < s1.a.x;
        if (s0.a.y != s1.a.y) return s0.a.y < s1.a.y;
        return pointLess(s0.b, s1.b);
    });
}

} // namespace detail

// ============================================================================
// Public entry — rs core/overlay.rs:379-400 Overlay::overlay_into(Subject, fill_rule)
// (the split solver step at rs overlay.rs:385 is upstream of this port: segments must
// arrive pre-split and sorted — see OverlayExtract.h preconditions)
// ============================================================================

std::vector<ExtractedContour> extractSubjectContours(
    std::vector<OverlaySegment> segments,
    bool fillRulePositive,
    bool preserveOutputCollinear,
    bool outputClockwise,
    std::uint64_t minOutputArea)
{
    using namespace detail;

    // rs core/overlay.rs:386-388
    if (segments.empty())
    {
        return std::vector<ExtractedContour>();
    }

    // rs core/overlay.rs:403-413 IntOverlayOptions::default, with the caller's overrides
    IntOverlayOptions options;
    options.preserve_output_collinear = preserveOutputCollinear;
    options.output_clockwise = outputClockwise;
    options.min_output_area = minOutputArea;

    // rs mesh/outline/offset.rs:280 + core/overlay.rs:77-86: default solver
    const Solver solver = Solver::AUTO();

    // rs core/overlay.rs:390-398: graph_builder.build_boolean_overlay(...).extract_contours_into(...)
    GraphBuilder builder;
    const OverlayGraph graph = buildBooleanOverlay(
        builder,
        fillRulePositive ? FillRule::Positive : FillRule::Negative,
        OverlayRule::Subject,
        options,
        solver,
        segments);

    BooleanExtractionBuffer buffer;
    std::vector<ExtractedContour> output;
    graph.extractContoursInto(OverlayRule::Subject, buffer, output);
    return output;
}

} // namespace overlay
} // namespace jaguar
