// ============================================================================
// OverlaySplit.cpp
//
// Line-by-line C++17 port of the i_overlay 4.5.2 split chain. All integer /
// bit arithmetic; no floating point, no libm. Every function carries the
// source citation "rs <file>:<line>" against the read-only originals under
// CodeResource/i_overlay-4.5.2 and CodeResource/i_float-1.16.0.
//
// PLACEHOLDER notes (performance structures intentionally skipped):
//   1. grid_layout.rs (grid index) - skipped. The fragment path uses a
//      single-bin layout so every segment lands whole in one group and
//      bin_split degenerates to full pairwise detection (O(n^2)).
//   2. i_tree SegExpTree (y-range segment tree with lazy x-expiration) -
//      skipped. tree_split scans previously inserted segments directly
//      (O(n^2)); pair semantics are identical (see TreeSplit below).
// ============================================================================

#include "JaguarCore/Geometry/ShapeModification/OverlaySplit.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace jaguar
{
namespace overlay
{
namespace
{

// ============================================================================
// Small helpers
// ============================================================================

// Rust i64/i32::signum semantics: -1 / 0 / +1.
inline int SignI64(std::int64_t v) noexcept
{
    return (v > 0) - (v < 0);
}

// Rust u64::leading_zeros (returns 64 for 0). Plain shift loop, no intrinsics.
inline unsigned Clz64(std::uint64_t v) noexcept
{
    if (v == 0)
    {
        return 64;
    }
    unsigned n = 0;
    while ((v & 0x8000000000000000ULL) == 0)
    {
        v <<= 1;
        ++n;
    }
    return n;
}

// Rust i64::unsigned_abs: |v| as u64 (well defined for INT64_MIN).
inline std::uint64_t UnsignedAbs64(std::int64_t v) noexcept
{
    return v < 0 ? -static_cast<std::uint64_t>(v) : static_cast<std::uint64_t>(v);
}

// Rust u64::ilog2 for v != 0 (used only on non-zero values, rs grid_layout.rs:115).
inline unsigned ILog2U64(std::uint64_t v) noexcept
{
    unsigned r = 0;
    while (v > 1)
    {
        v >>= 1;
        ++r;
    }
    return r;
}

// ============================================================================
// IntPoint primitives - rs i_float-1.16.0/src/int/point.rs
// ============================================================================

// rs int/point.rs:86-98 IntPoint::cmp - lexicographic (x, then y).
// Returns -1 / 0 / +1.
inline int CmpPoint(const IntPoint& lhs, const IntPoint& rhs) noexcept
{
    if (lhs.x != rhs.x)
    {
        return lhs.x < rhs.x ? -1 : 1;
    }
    if (lhs.y != rhs.y)
    {
        return lhs.y < rhs.y ? -1 : 1;
    }
    return 0;
}

// rs int/point.rs:47-56 sqr_length / sqr_distance (i64 math).
inline std::int64_t SqrDistance(const IntPoint& p, const IntPoint& other) noexcept
{
    const std::int64_t x = static_cast<std::int64_t>(p.x) - other.x;
    const std::int64_t y = static_cast<std::int64_t>(p.y) - other.y;
    return x * x + y * y;
}

// ============================================================================
// LineRange - rs i_overlay-4.5.2/src/geom/line_range.rs:1-5
// ============================================================================

struct LineRange
{
    std::int32_t min;
    std::int32_t max;
};

// ============================================================================
// IntRect (used subset) - rs i_float-1.16.0/src/int/rect.rs:5-10
// ============================================================================

struct IntRect
{
    std::int32_t min_x;
    std::int32_t max_x;
    std::int32_t min_y;
    std::int32_t max_y;
};

// rs int/rect.rs:137-143 contains_with_radius. Radius is i32 in the source;
// the additions run in i64 here to keep C++ overflow defined (identical to
// the Rust result whenever the i32 arithmetic does not wrap).
inline bool ContainsWithRadius(const IntRect& r, const IntPoint& point, std::int64_t radius) noexcept
{
    return static_cast<std::int64_t>(r.min_x) - radius <= point.x
        && point.x <= static_cast<std::int64_t>(r.max_x) + radius
        && static_cast<std::int64_t>(r.min_y) - radius <= point.y
        && point.y <= static_cast<std::int64_t>(r.max_y) + radius;
}

// rs int/rect.rs:146-150 is_intersect_border_include
inline bool IsIntersectBorderInclude(const IntRect& r, const IntRect& other) noexcept
{
    const bool x = r.min_x <= other.max_x && r.max_x >= other.min_x;
    const bool y = r.min_y <= other.max_y && r.max_y >= other.min_y;
    return x && y;
}

// ============================================================================
// Triangle (used subset) - rs i_float-1.16.0/src/triangle.rs
// ============================================================================

// rs triangle.rs:73-82 area_two_point
inline std::int64_t AreaTwoPoint(const IntPoint& p0, const IntPoint& p1, const IntPoint& p2) noexcept
{
    const std::int64_t x0 = static_cast<std::int64_t>(p1.x) - p0.x;
    const std::int64_t y0 = static_cast<std::int64_t>(p1.y) - p0.y;
    const std::int64_t x1 = static_cast<std::int64_t>(p1.x) - p2.x;
    const std::int64_t y1 = static_cast<std::int64_t>(p1.y) - p2.y;
    return x0 * y1 - x1 * y0;
}

// rs triangle.rs:95-97 is_line_point
inline bool IsLinePoint(const IntPoint& p0, const IntPoint& p1, const IntPoint& p2) noexcept
{
    return AreaTwoPoint(p0, p1, p2) == 0;
}

// rs triangle.rs:100-102 is_not_line_point
inline bool IsNotLinePoint(const IntPoint& p0, const IntPoint& p1, const IntPoint& p2) noexcept
{
    return AreaTwoPoint(p0, p1, p2) != 0;
}

// rs triangle.rs:105-107 clock_direction_point (signum of the twice-area)
inline std::int64_t ClockDirectionPoint(const IntPoint& p0, const IntPoint& p1, const IntPoint& p2) noexcept
{
    return static_cast<std::int64_t>(SignI64(AreaTwoPoint(p0, p1, p2)));
}

// ============================================================================
// XSegment - rs i_overlay-4.5.2/src/geom/x_segment.rs:5-9
// ============================================================================

struct XSegment
{
    IntPoint a;
    IntPoint b;

    // rs x_segment.rs:13-25 y_range
    LineRange YRange() const noexcept
    {
        if (a.y < b.y)
        {
            return LineRange{ a.y, b.y };
        }
        return LineRange{ b.y, a.y };
    }

    // rs x_segment.rs:33-35 is_not_intersect_y_range
    bool IsNotIntersectYRange(const LineRange& range) const noexcept
    {
        return (range.min > a.y && range.min > b.y) || (range.max < a.y && range.max < b.y);
    }
};

// rs x_segment.rs:45-55 XSegment::cmp - a, then b (both IntPoint::cmp).
inline int CmpXSegment(const XSegment& lhs, const XSegment& rhs) noexcept
{
    const int a = CmpPoint(lhs.a, rhs.a);
    if (a != 0)
    {
        return a;
    }
    return CmpPoint(lhs.b, rhs.b);
}

// rs x_segment.rs:45-46 PartialEq (a == a && b == b)
inline bool EqXSegment(const XSegment& lhs, const XSegment& rhs) noexcept
{
    return lhs.a.x == rhs.a.x && lhs.a.y == rhs.a.y && lhs.b.x == rhs.b.x && lhs.b.y == rhs.b.y;
}

// OverlaySegment -> XSegment view (rs segm/segment.rs:24 - Segment.x_segment).
inline XSegment XSegOf(const OverlaySegment& s) noexcept
{
    return XSegment{ s.a, s.b };
}

// Exact y-range overlap (used by the tree placeholder below).
inline bool YRangesOverlap(const LineRange& r0, const LineRange& r1) noexcept
{
    return !(r0.max < r1.min || r1.max < r0.min);
}

// ============================================================================
// UInt128 / mul64 - rs i_float-1.16.0/src/u128.rs:3-7, 16-43
//
// 128-bit unsigned multiply implemented from two u64 halves ("mul64"
// extension). Used by the fixed-point intersection in cross_point
// (rs split/cross_solver.rs:285-289).
// ============================================================================

struct UInt128
{
    std::uint64_t high; // rs u128.rs:5
    std::uint64_t low;  // rs u128.rs:6
};

// rs u128.rs:16-23 sum(a, b, c) -> (sum, carry)
inline void U128Sum(std::uint64_t a, std::uint64_t b, std::uint64_t c,
                    std::uint64_t& out_sum, std::uint64_t& out_high) noexcept
{
    const std::uint64_t s0 = a + b; // unsigned wrap == Rust overflowing_add
    std::uint64_t high = s0 < a ? 1 : 0;
    const std::uint64_t s1 = s0 + c;
    if (s1 < s0)
    {
        high += 1;
    }
    out_sum = s1;
    out_high = high;
}

// rs u128.rs:26-43 UInt128::multiply (u64 x u64 -> u128)
inline UInt128 U128Multiply(std::uint64_t a, std::uint64_t b) noexcept
{
    if (Clz64(a) + Clz64(b) >= 64) // rs:27-29 - product fits u64
    {
        return UInt128{ 0, a * b };
    }

    const std::uint64_t a1 = a >> 32; // rs:31
    const std::uint64_t a0 = a & 0xFFFFFFFFULL; // rs:32 (0xFFFF_FFFF)
    const std::uint64_t b1 = b >> 32; // rs:33
    const std::uint64_t b0 = b & 0xFFFFFFFFULL; // rs:34 (0xFFFF_FFFF)

    const std::uint64_t ab00 = a0 * b0; // rs:36
    std::uint64_t m_partial = 0;
    std::uint64_t m_high = 0;
    U128Sum(a0 * b1, a1 * b0, ab00 >> 32, m_partial, m_high); // rs:37
    const std::uint64_t high = a1 * b1 + (m_partial >> 32) + (m_high << 32); // rs:38
    const std::uint64_t low = (m_partial << 32) | (ab00 & 0xFFFFFFFFULL); // rs:40

    return UInt128{ high, low };
}

// rs split/cross_solver.rs:303
constexpr unsigned kLastBitIndex = 63;

// Rust release masks shift amounts >= 64 down to amount & 63 (relevant sites:
// rs split/cross_solver.rs:323, 341). dn == 0 (divisor >= 2^63) is unreachable
// under the source precondition |coords| < 2^30 (rs cross_solver.rs:208-210);
// masking keeps the shift defined in C++.
inline std::uint64_t MaskedShl64(std::uint64_t v, unsigned n) noexcept
{
    return v << (n & 63u);
}

inline std::uint64_t MaskedShr64(std::uint64_t v, unsigned n) noexcept
{
    return v >> (n & 63u);
}

// rs split/cross_solver.rs:305-348 RoundDivide::divide_with_rounding:
// u128 / u64 with round-half-up.
inline std::uint64_t DivideWithRounding(const UInt128& v, std::uint64_t divisor) noexcept
{
    if (v.high == 0) // rs:311-319
    {
        const std::uint64_t result = v.low / divisor;
        const std::uint64_t remainder = v.low - result * divisor;
        // (divisor + 1) >> 1 (rs:314) rewritten overflow-safe: ceil(divisor/2).
        const std::uint64_t half = (divisor >> 1) + (divisor & 1);
        return remainder >= half ? result + 1 : result;
    }

    const unsigned dn = Clz64(divisor); // rs:321
    const std::uint64_t norm_divisor = divisor << dn; // rs:322
    std::uint64_t norm_dividend_high =
        MaskedShl64(v.high, dn) | MaskedShr64(v.low, 64 - dn); // rs:323
    std::uint64_t norm_dividend_low = v.low << dn; // rs:324

    std::uint64_t quotient = 0; // rs:326
    const std::uint64_t one = 1ULL << kLastBitIndex; // rs:327

    for (unsigned it = 0; it < 64; ++it) // rs:329 - u64::BITS iterations
    {
        const bool bit = (norm_dividend_high & one) != 0; // rs:330
        norm_dividend_high = (norm_dividend_high << 1) | (norm_dividend_low >> kLastBitIndex); // rs:331
        norm_dividend_low <<= 1; // rs:332
        quotient <<= 1; // rs:333
        if (norm_dividend_high >= norm_divisor || bit) // rs:334
        {
            norm_dividend_high = norm_dividend_high - norm_divisor; // rs:335 wrapping_sub
            quotient |= 1; // rs:336
        }
    }

    // rs:340-344 - check remainder for rounding
    const std::uint64_t remainder =
        MaskedShl64(norm_dividend_high, 64 - dn) | MaskedShr64(norm_dividend_low, dn); // rs:341
    const std::uint64_t half = (divisor >> 1) + (divisor & 1); // (divisor + 1) >> 1, rs:342
    if (remainder >= half)
    {
        quotient += 1; // rs:343
    }

    return quotient;
}

// ============================================================================
// CrossSolver - rs i_overlay-4.5.2/src/split/cross_solver.rs:7-301
// ============================================================================

// rs split/cross_solver.rs:18-21 - collinear end mask bits
constexpr std::uint8_t kTargetA = 0b0001;
constexpr std::uint8_t kTargetB = 0b0010;
constexpr std::uint8_t kOtherA = 0b0100;
constexpr std::uint8_t kOtherB = 0b1000;

// rs split/cross_solver.rs:45-52 EndMask::new
inline std::uint8_t MakeEndMask(bool target_a, bool target_b, bool other_a, bool other_b) noexcept
{
    const std::uint8_t a0 = target_a ? 1 : 0;
    const std::uint8_t b0 = static_cast<std::uint8_t>(target_b ? 1 : 0) << 1;
    const std::uint8_t a1 = static_cast<std::uint8_t>(other_a ? 1 : 0) << 2;
    const std::uint8_t b1 = static_cast<std::uint8_t>(other_b ? 1 : 0) << 3;
    return static_cast<std::uint8_t>(a0 | b0 | a1 | b1);
}

// rs split/cross_solver.rs:61-66
enum class CrossType
{
    Pure,
    TargetEnd,
    OtherEnd,
    Overlay,
};

// rs split/cross_solver.rs:55-59
struct CrossResult
{
    IntPoint point;
    CrossType crossType;
    bool isRound;
};

// rs split/cross_solver.rs:207-300 CrossSolver::cross_point.
// Fixed-point line/line intersection. Precondition from the source
// (rs:208-210): edges are not parallel and |x|, |y| < 2^30, result < 2^30.
// The i64 products below rely on that precondition exactly like the source.
inline IntPoint CrossPoint(const XSegment& target, const XSegment& other) noexcept
{
    // offset approach (rs:235-236): move all points by -target.a so a0 == (0,0)
    const std::int64_t a0x = target.a.x; // rs:240
    const std::int64_t a0y = target.a.y; // rs:241

    const std::int64_t a1x = static_cast<std::int64_t>(target.b.x) - a0x; // rs:243
    const std::int64_t b0x = static_cast<std::int64_t>(other.a.x) - a0x; // rs:244
    const std::int64_t b1x = static_cast<std::int64_t>(other.b.x) - a0x; // rs:245

    const std::int64_t a1y = static_cast<std::int64_t>(target.b.y) - a0y; // rs:249
    const std::int64_t b0y = static_cast<std::int64_t>(other.a.y) - a0y; // rs:250
    const std::int64_t b1y = static_cast<std::int64_t>(other.b.y) - a0y; // rs:251

    const std::int64_t dy_b = b0y - b1y; // rs:253
    const std::int64_t dx_b = b0x - b1x; // rs:254

    // let xyA = 0 (a0 is the origin) - rs:256
    const std::int64_t xy_b = b0x * b1y - b0y * b1x; // rs:257

    std::int64_t x0 = 0;
    std::int64_t y0 = 0;

    // a1y and a1x cannot be zero simultaneously (rs:262)
    if (a1x == 0)
    {
        // dxB != 0, otherwise the edges would be parallel (rs:265)
        x0 = 0; // rs:266
        y0 = xy_b / dx_b; // rs:267 - i64 truncating division (Rust semantics)
    }
    else if (a1y == 0)
    {
        // dyB != 0, otherwise the edges would be parallel (rs:269)
        y0 = 0; // rs:270
        x0 = -xy_b / dy_b; // rs:271
    }
    else
    {
        // divider - rs:274
        const std::int64_t div = a1y * dx_b - a1x * dy_b;

        // result sign - rs:276-279
        const std::int64_t s = SignI64(div) * SignI64(xy_b);
        const std::int64_t sx = SignI64(a1x) * s;
        const std::int64_t sy = SignI64(a1y) * s;

        // custom u128 bit math with rounding - rs:281-282
        const std::uint64_t uxy_b = UnsignedAbs64(xy_b);
        const std::uint64_t udiv = UnsignedAbs64(div);

        const UInt128 kx = U128Multiply(UnsignedAbs64(a1x), uxy_b); // rs:285
        const UInt128 ky = U128Multiply(UnsignedAbs64(a1y), uxy_b); // rs:286

        const std::uint64_t ux = DivideWithRounding(kx, udiv); // rs:288
        const std::uint64_t uy = DivideWithRounding(ky, udiv); // rs:289

        x0 = sx * static_cast<std::int64_t>(ux); // rs:292
        y0 = sy * static_cast<std::int64_t>(uy); // rs:293
    }

    const std::int32_t x = static_cast<std::int32_t>(x0 + a0x); // rs:296 - `as i32` truncation
    const std::int32_t y = static_cast<std::int32_t>(y0 + a0y); // rs:297

    return IntPoint{ x, y }; // rs:299
}

// rs split/cross_solver.rs:152-205 CrossSolver::middle_cross.
// Returns true (Some) in every path, mirroring the source.
inline bool MiddleCross(const XSegment& target, const XSegment& other, std::int64_t radius,
                        CrossResult& out) noexcept
{
    const IntPoint p = CrossPoint(target, other); // rs:153

    if (IsLinePoint(target.a, p, target.b) && IsLinePoint(other.a, p, other.b)) // rs:155
    {
        out.point = p; // rs:156-160
        out.crossType = CrossType::Pure;
        out.isRound = false;
        return true;
    }

    // still can be common ends because of rounding - rs:163-164
    // snap to nearest end with r (1^2 + 1^2 == 2)

    const std::int64_t ra0 = SqrDistance(target.a, p); // rs:166
    const std::int64_t rb0 = SqrDistance(target.b, p); // rs:167
    const std::int64_t ra1 = SqrDistance(other.a, p); // rs:169
    const std::int64_t rb1 = SqrDistance(other.b, p); // rs:170

    if (ra0 <= radius || ra1 <= radius || rb0 <= radius || rb1 <= radius) // rs:172
    {
        const std::int64_t r0 = std::min(ra0, rb0); // rs:173
        const std::int64_t r1 = std::min(ra1, rb1); // rs:174

        if (r0 <= r1) // rs:176
        {
            const IntPoint snap = ra0 < rb0 ? target.a : target.b; // rs:177
            // ignore if it's a clean point - rs:179
            if (IsNotLinePoint(other.a, snap, other.b))
            {
                out.point = snap; // rs:180-184
                out.crossType = CrossType::TargetEnd;
                out.isRound = true;
                return true;
            }
        }
        else
        {
            const IntPoint snap = ra1 < rb1 ? other.a : other.b; // rs:187

            // ignore if it's a clean point - rs:190
            if (IsNotLinePoint(target.a, snap, target.b))
            {
                out.point = snap; // rs:191-195
                out.crossType = CrossType::OtherEnd;
                out.isRound = true;
                return true;
            }
        }
    }

    out.point = p; // rs:200-204
    out.crossType = CrossType::Pure;
    out.isRound = true;
    return true;
}

// rs split/cross_solver.rs:71-123 CrossSolver::cross.
// Returns false for None, true plus `out` for Some.
inline bool CrossSolverCross(const XSegment& target, const XSegment& other, std::int64_t radius,
                             CrossResult& out) noexcept
{
    const std::int64_t a0b0a1 = ClockDirectionPoint(target.a, target.b, other.a); // rs:72
    const std::int64_t a0b0b1 = ClockDirectionPoint(target.a, target.b, other.b); // rs:73

    const std::int64_t a1b1a0 = ClockDirectionPoint(other.a, other.b, target.a); // rs:75
    const std::int64_t a1b1b0 = ClockDirectionPoint(other.a, other.b, target.b); // rs:76

    // each term is 1 iff the orientation is 0 (collinear) - rs:78
    const std::int64_t s =
        (1 & (a0b0a1 + 1)) + (1 & (a0b0b1 + 1)) + (1 & (a1b1a0 + 1)) + (1 & (a1b1b0 + 1));

    if (s == 4) // rs:80-86
    {
        out.point = IntPoint{ 0, 0 };
        out.crossType = CrossType::Overlay;
        out.isRound = false;
        return true;
    }

    const bool is_not_cross = a0b0a1 == a0b0b1 || a1b1a0 == a1b1b0; // rs:88

    if (s > 1 || is_not_cross) // rs:90-92
    {
        return false;
    }

    if (s != 0) // rs:94-120 - exactly one collinear end: touch at an endpoint
    {
        if (a0b0a1 == 0) // rs:95-100
        {
            out.point = other.a;
            out.crossType = CrossType::OtherEnd;
            out.isRound = false;
            return true;
        }
        if (a0b0b1 == 0) // rs:101-106
        {
            out.point = other.b;
            out.crossType = CrossType::OtherEnd;
            out.isRound = false;
            return true;
        }
        if (a1b1a0 == 0) // rs:107-112
        {
            out.point = target.a;
            out.crossType = CrossType::TargetEnd;
            out.isRound = false;
            return true;
        }
        out.point = target.b; // rs:113-118
        out.crossType = CrossType::TargetEnd;
        out.isRound = false;
        return true;
    }

    return MiddleCross(target, other, radius, out); // rs:122
}

// rs split/cross_solver.rs:125-150 CrossSolver::collinear.
// FixVec is an i64 vector (rs i_float-1.16.0/src/fix_vec.rs:9-12, 43-45);
// dot_product is plain i64 math (rs fix_vec.rs:135-139), signum on i64.
inline std::uint8_t Collinear(const XSegment& target, const XSegment& other) noexcept
{
    const std::int64_t a0x = target.a.x; // rs:126 - FixVec::new_point
    const std::int64_t a0y = target.a.y;
    const std::int64_t b0x = target.b.x; // rs:127
    const std::int64_t b0y = target.b.y;
    const std::int64_t a1x = other.a.x; // rs:128
    const std::int64_t a1y = other.a.y;
    const std::int64_t b1x = other.b.x; // rs:129
    const std::int64_t b1y = other.b.y;

    const std::int64_t v1x = b1x - a1x; // rs:131 - v1 = b1 - a1
    const std::int64_t v1y = b1y - a1y;

    const int aa0 = SignI64((a0x - a1x) * v1x + (a0y - a1y) * v1y); // rs:133
    const int ab0 = SignI64((a0x - b1x) * v1x + (a0y - b1y) * v1y); // rs:134
    const int ba0 = SignI64((b0x - a1x) * v1x + (b0y - a1y) * v1y); // rs:135
    const int bb0 = SignI64((b0x - b1x) * v1x + (b0y - b1y) * v1y); // rs:136

    const int aa1 = -aa0; // rs:138
    const int ab1 = -ba0; // rs:139
    const int ba1 = -ab0; // rs:140
    const int bb1 = -bb0; // rs:141

    const bool is_target_a = aa0 == -ab0 && aa0 != 0; // rs:143
    const bool is_target_b = ba0 == -bb0 && ba0 != 0; // rs:144

    const bool is_other_a = aa1 == -ab1 && aa1 != 0; // rs:146
    const bool is_other_b = ba1 == -bb1 && ba1 != 0; // rs:147

    return MakeEndMask(is_target_a, is_target_b, is_other_a, is_other_b); // rs:149
}

// ============================================================================
// SnapRadius - rs i_overlay-4.5.2/src/split/snap_radius.rs:3-16
// ============================================================================

struct SnapRadius
{
    std::size_t current; // rs:4
    std::size_t step;    // rs:5

    // rs:9-11
    void Increment() noexcept
    {
        current = std::min<std::size_t>(60, current + step);
    }

    // rs:13-15
    std::int64_t Radius() const noexcept
    {
        return static_cast<std::int64_t>(1) << current;
    }
};

// rs snap_radius.rs:19-24 + Precision::HIGH { start: 0, progression: 1 }
// (rs core/solver.rs:46-50; Solver::AUTO uses Precision::HIGH,
//  rs core/solver.rs:132-138).
inline SnapRadius MakeSnapRadius() noexcept
{
    return SnapRadius{ 0, 1 };
}

// ============================================================================
// LineMark - rs i_overlay-4.5.2/src/split/line_mark.rs:5-9
// ============================================================================

struct LineMark
{
    std::size_t index;
    IntPoint point;
};

// rs line_mark.rs:15-25 - sort by index, then IntPoint::cmp.
// The i_key_sort bin sort (i_key_sort-0.10.3/src/sort/one_key_cmp.rs:152-159)
// is an exact sort by this total order; std::sort produces the same ordering
// (elements comparing equal are value-identical).
inline void SortMarksByIndexAndPoint(std::vector<LineMark>& marks) noexcept
{
    std::sort(marks.begin(), marks.end(), [](const LineMark& m0, const LineMark& m1) {
        if (m0.index != m1.index)
        {
            return m0.index < m1.index;
        }
        return CmpPoint(m0.point, m1.point) < 0;
    });
}

// ============================================================================
// Segment helpers - rs i_overlay-4.5.2/src/segm/segment.rs
// ============================================================================

// rs segm/segment.rs:30-43 create_and_validate. count.invert() negates both
// components of ShapeCountBoolean independently (rs segm/boolean.rs:51-56).
// The fill is a packed pair of 16-bit counters (subject in bits 0..15, clip
// in bits 16..31 — same layout as OverlayExtract.h packShapeCount), so a raw
// negation of the packed int32 is WRONG: -pack(1,0) = 0xFFFF0000 unpacks to
// (subj=-1, clip=-1) instead of (subj=-1, clip=0) — the borrow leaks into
// the clip field and corrupts every winding computation downstream.
inline std::int32_t PackShapeCount(std::int32_t subjCount,
                                   std::int32_t clipCount) noexcept
{
    return static_cast<std::int32_t>(
        (static_cast<std::uint32_t>(static_cast<std::uint16_t>(subjCount)) << 0)
        | (static_cast<std::uint32_t>(static_cast<std::uint16_t>(clipCount))
           << 16));
}

inline std::int32_t ShapeCountSubj(std::int32_t fill) noexcept
{
    return static_cast<std::int16_t>(static_cast<std::uint32_t>(fill)
                                     & 0xFFFFu);
}

inline std::int32_t ShapeCountClip(std::int32_t fill) noexcept
{
    return static_cast<std::int16_t>(static_cast<std::uint32_t>(fill) >> 16);
}

inline std::int32_t NegatePackedCount(std::int32_t fill) noexcept
{
    return PackShapeCount(-ShapeCountSubj(fill), -ShapeCountClip(fill));
}

// rs segm/boolean.rs:38-43 ShapeCountBoolean::add/apply — component-wise
// addition; a raw packed += leaks carries between the two 16-bit fields.
inline std::int32_t AddPackedCount(std::int32_t left,
                                   std::int32_t right) noexcept
{
    return PackShapeCount(ShapeCountSubj(left) + ShapeCountSubj(right),
                          ShapeCountClip(left) + ShapeCountClip(right));
}

inline OverlaySegment CreateAndValidate(const IntPoint& a, const IntPoint& b,
                                        std::int32_t count) noexcept
{
    if (CmpPoint(a, b) < 0)
    {
        return OverlaySegment{ a, b, count };
    }
    return OverlaySegment{ b, a, NegatePackedCount(count) };
}

// ============================================================================
// ShapeSegmentsSort - rs i_overlay-4.5.2/src/segm/sort.rs:4-18
// ============================================================================

// rs segm/sort.rs:10-17 - keys (a.x, a.y) then full IntPoint::cmp of b.
// The parallel flag only selects the i_key_sort parallel implementation
// (two_keys_cmp.rs:200-219); both produce the exact same total order, so the
// serial std::sort here is equivalent.
inline void SortByAb(std::vector<OverlaySegment>& segments) noexcept
{
    std::sort(segments.begin(), segments.end(),
              [](const OverlaySegment& s0, const OverlaySegment& s1) {
                  if (s0.a.x != s1.a.x)
                  {
                      return s0.a.x < s1.a.x;
                  }
                  if (s0.a.y != s1.a.y)
                  {
                      return s0.a.y < s1.a.y;
                  }
                  return CmpPoint(s0.b, s1.b) < 0;
              });
}

// ============================================================================
// ShapeSegmentsMerge - rs i_overlay-4.5.2/src/segm/merge.rs:9-54
// ============================================================================

// rs segm/merge.rs:30-54
inline std::size_t MergeEqual(std::vector<OverlaySegment>& segments, std::size_t after) noexcept
{
    std::size_t i = after;
    std::size_t j = i - 1;
    OverlaySegment prev = segments[j]; // Segment is Copy (rs segm/segment.rs:22)

    while (i < segments.size()) // rs:35
    {
        if (EqXSegment(XSegOf(prev), XSegOf(segments[i]))) // rs:36
        {
            prev.fill = AddPackedCount(prev.fill, segments[i].fill); // rs:37 - count.apply (boolean.rs:45-48)
        }
        else
        {
            if (prev.fill != 0) // rs:39 - count.is_not_empty (boolean.rs:19-21)
            {
                segments[j] = prev; // rs:40
                j += 1; // rs:41
            }
            prev = segments[i]; // rs:43
        }
        i += 1; // rs:45
    }

    if (prev.fill != 0) // rs:48
    {
        segments[j] = prev; // rs:49
        j += 1; // rs:50
    }

    return j; // rs:53
}

// rs segm/merge.rs:10-28 merge_if_needed
inline bool MergeIfNeeded(std::vector<OverlaySegment>& segments) noexcept
{
    if (segments.size() < 2) // rs:11-13
    {
        return false;
    }

    XSegment prev = XSegOf(segments[0]); // rs:15
    for (std::size_t i = 1; i < segments.size(); ++i) // rs:16
    {
        const XSegment cur = XSegOf(segments[i]); // rs:17
        if (EqXSegment(prev, cur)) // rs:18 - Segment::eq compares x_segment (segment.rs:45-50)
        {
            const std::size_t new_len = MergeEqual(segments, i); // rs:19
            segments.resize(new_len); // rs:20 - truncate
            return true; // rs:21
        }
        prev = cur; // rs:23
    }

    return false; // rs:26
}

// ============================================================================
// Solver selection - rs i_overlay-4.5.2/src/core/solver.rs
// Fixed to Strategy::Auto (Solver::AUTO, rs core/solver.rs:132-138).
// ============================================================================

// rs core/solver.rs:140
constexpr std::size_t kMaxSplitListCount = 4000;
// rs core/solver.rs:141
constexpr std::size_t kMinFragmentCount = 16000;

// rs core/solver.rs:164-170 is_list_split (Auto: len < MAX_SPLIT_LIST_COUNT)
inline bool IsListSplit(const std::vector<OverlaySegment>& segments) noexcept
{
    return segments.size() < kMaxSplitListCount;
}

// rs core/solver.rs:172-174 is_fragmentation_required
// (Auto: len > MIN_FRAGMENT_COUNT; the explicit Frag strategy is not exposed)
inline bool IsFragmentationRequired(const std::vector<OverlaySegment>& segments) noexcept
{
    return segments.size() > kMinFragmentCount;
}

// ============================================================================
// Fragment - rs i_overlay-4.5.2/src/split/fragment.rs:5-33
// ============================================================================

struct Fragment
{
    std::size_t index;  // rs:6
    IntRect rect;       // rs:7
    XSegment x_segment; // rs:8
};

// rs split/fragment.rs:13-32 with_index_and_segment
inline Fragment FragmentWithIndexAndSegment(std::size_t index, const XSegment& x_segment) noexcept
{
    const std::int32_t min_y = x_segment.a.y < x_segment.b.y ? x_segment.a.y : x_segment.b.y; // rs:14-18
    const std::int32_t max_y = x_segment.a.y < x_segment.b.y ? x_segment.b.y : x_segment.a.y;

    const IntRect rect{ x_segment.a.x, x_segment.b.x, min_y, max_y }; // rs:20-25

    return Fragment{ index, rect, x_segment }; // rs:27-31
}

// ============================================================================
// BorderVSegment / GridLayout / FragmentBuffer -
// rs i_overlay-4.5.2/src/split/grid_layout.rs
//
// PLACEHOLDER: grid index skipped - O(n^2) path.
// The original GridLayout partitioned the x extent into ~2^max_power power-of-
// two bins (rs grid_layout.rs:247-293) and FragmentBuffer::add_segment cut
// each segment into per-bin pieces (rs grid_layout.rs:86-190) so that
// bin_split only compared fragments sharing a bin. The port keeps the same
// interface but collapses the layout to a SINGLE bin: every segment is added
// whole to that one group, so bin_split performs full pairwise detection over
// all segments. The per-bin piece math (rs grid_layout.rs:104-190) is ported
// verbatim but is unreachable with a single bin (i0 == i1 == 0 always).
// ============================================================================

// rs split/grid_layout.rs:9-13
struct BorderVSegment
{
    std::size_t id;
    std::int32_t x;
    LineRange y_range;
};

// rs split/grid_layout.rs:247-251
struct GridLayout
{
    std::int32_t min_x;
    std::int32_t max_x;
    unsigned power; // rs:250 - u32

    // rs grid_layout.rs:255-257 index. The source shifts an i32; the port
    // shifts in i64 so the full i32 span cannot overflow (same result for
    // every in-range input).
    std::size_t Index(std::int32_t x) const noexcept
    {
        return static_cast<std::size_t>(
            static_cast<std::uint64_t>(static_cast<std::int64_t>(x) - min_x) >> power);
    }

    // rs grid_layout.rs:260-262 pos:
    //   (index << power) as i32 + min_x  (usize = 64-bit shift, i32 truncation)
    std::int32_t Pos(std::size_t index) const noexcept
    {
        return static_cast<std::int32_t>(static_cast<std::uint64_t>(index) << power) + min_x;
    }
};

// rs grid_layout.rs:264-292 GridLayout::new (over x_segments of `segments`,
// count = segments.len()).
//
// PLACEHOLDER: grid index skipped - O(n^2) path. The original derived
// max_power from ilog2(count) (rs:277-280) and the bin width from
// ilog2(dx) - max_power (rs:288-289). The placeholder uses power = 32, i.e.
// exactly one bin covers [min_x, max_x] (Index() == 0 for the whole i32
// span), so every segment lands whole in that single group.
// Returns false where the source returns None (dx < 4, rs:284-287).
bool GridLayoutTryNew(const std::vector<OverlaySegment>& segments, GridLayout& out) noexcept
{
    if (segments.empty()) // rs:269 - iter.peek() == None
    {
        return false;
    }
    const std::int32_t min_x = segments[0].a.x; // rs:270 (sorted by a.x, so the first is the min)
    std::int32_t max_x = segments[0].b.x;       // rs:271

    for (const OverlaySegment& s : segments) // rs:273-275
    {
        max_x = std::max(max_x, s.b.x);
    }

    const std::int32_t dx = max_x - min_x; // rs:284
    if (dx < 4) // rs:285-287
    {
        return false;
    }

    out = GridLayout{ min_x, max_x, 32 }; // PLACEHOLDER single bin (rs:288-289 skipped)
    return true;
}

// rs split/grid_layout.rs:15-19
struct FragmentBuffer
{
    GridLayout layout;                        // rs:16
    std::vector<std::vector<Fragment>> groups; // rs:17
    std::vector<BorderVSegment> on_border;    // rs:18

    // rs grid_layout.rs:23-30 new
    explicit FragmentBuffer(const GridLayout& grid)
        : layout(grid)
    {
        const std::size_t n = layout.Index(layout.max_x) + 1; // rs:24
        groups.resize(n);                                     // rs:26
        on_border.reserve(64);                                // rs:28
    }

    // rs grid_layout.rs:32-52 init_fragment_buffer - per-bin capacity counts.
    void InitFragmentBuffer(const std::vector<OverlaySegment>& segments)
    {
        std::vector<std::size_t> counts(groups.size(), 0); // rs:36
        for (const OverlaySegment& s : segments) // rs:37
        {
            const XSegment seg = XSegOf(s);
            const std::size_t i0 = layout.Index(seg.a.x); // rs:38
            if (seg.a.x < seg.b.x) // rs:39
            {
                const std::size_t i1 = layout.Index(seg.b.x - 1); // rs:40
                for (std::size_t c = i0; c < i1; ++c) // rs:41 - take(i1).skip(i0)
                {
                    counts[c] += 1;
                }
            }
            else // rs:44-46
            {
                counts[i0] += 1;
            }
        }

        for (std::size_t i = 0; i < groups.size(); ++i) // rs:49-51
        {
            groups[i].reserve(counts[i]);
        }
    }

    // rs grid_layout.rs:55-62 insert (checked instead of get_unchecked)
    void Insert(const Fragment& fragment, std::size_t bin_index)
    {
        groups[bin_index].push_back(fragment);
    }

    // rs grid_layout.rs:65-75 insert_vertical
    void InsertVertical(const Fragment& fragment, std::size_t bin_index)
    {
        const std::int32_t x = fragment.x_segment.a.x; // rs:66
        if (bin_index > 0 && x == layout.Pos(bin_index)) // rs:67
        {
            // BorderVSegment (rs grid_layout.rs:9-13): id, x, y_range -
            // the C++ struct keeps the same field order.
            on_border.push_back(
                BorderVSegment{ fragment.index, x, fragment.x_segment.YRange() }); // rs:68-72
        }
        Insert(fragment, bin_index); // rs:74
    }

    // rs grid_layout.rs:78-83 clear
    void Clear()
    {
        for (auto& group : groups)
        {
            group.clear();
        }
        on_border.clear();
    }

    // rs grid_layout.rs:86-190 add_segment
    void AddSegment(std::size_t segment_index, const XSegment& s)
    {
        if (s.a.y == s.b.y) // rs:87-90
        {
            AddHorizontal(segment_index, s);
            return;
        }

        const std::size_t i0 = layout.Index(s.a.x); // rs:92
        if (s.a.x == s.b.x) // rs:93-95
        {
            InsertVertical(FragmentWithIndexAndSegment(segment_index, s), i0);
            return;
        }

        const std::size_t i1 = layout.Index(s.b.x - 1); // rs:98
        if (i0 >= i1) // rs:99-102
        {
            Insert(FragmentWithIndexAndSegment(segment_index, s), i0);
            return;
        }

        // rs:104-190 - per-bin piece fragmentation. Unreachable under the
        // single-bin placeholder (i0 == i1 == 0); ported verbatim.
        const std::int32_t x0 = s.a.x; // rs:104
        const std::int32_t y0 = s.a.y; // rs:105

        std::int32_t prev_x = x0; // rs:107
        std::int32_t prev_y = y0; // rs:108

        const bool is_inc = s.a.y <= s.b.y; // rs:110

        const std::uint64_t width =
            static_cast<std::uint64_t>(static_cast<std::int64_t>(s.b.x) - s.a.x); // rs:112
        const std::uint64_t height = static_cast<std::uint64_t>( // rs:113 - unsigned_abs
            s.a.y <= s.b.y ? static_cast<std::int64_t>(s.b.y) - s.a.y
                           : static_cast<std::int64_t>(s.a.y) - s.b.y);

        const unsigned lg = ILog2U64(width * height); // rs:115
        const unsigned p = 63 - lg;                   // rs:116
        const std::uint64_t k = (height << p) / width; // rs:117

        std::uint64_t w =
            static_cast<std::uint64_t>(static_cast<std::int64_t>(layout.Pos(i0 + 1)) - s.a.x); // rs:119
        const std::uint64_t dw = 1ULL << layout.power; // rs:120

        for (std::size_t i = i0; i < i1; ++i) // rs:122
        {
            const std::uint64_t h_min = (w * k) >> p; // rs:123
            std::uint64_t h_max = h_min; // rs:124
            while (h_max * width < height * w) // rs:125-127
            {
                h_max += 1;
            }

            const std::int32_t max_x = x0 + static_cast<std::int32_t>(w); // rs:129

            IntRect rect;
            if (is_inc) // rs:131-140
            {
                const std::int32_t max_y = y0 + static_cast<std::int32_t>(h_max); // rs:132
                rect = IntRect{ prev_x, max_x, prev_y, max_y }; // rs:133-138
                prev_y = y0 + static_cast<std::int32_t>(h_min); // rs:139
            }
            else // rs:141-150
            {
                const std::int32_t min_y = y0 - static_cast<std::int32_t>(h_max); // rs:142
                rect = IntRect{ prev_x, max_x, min_y, prev_y }; // rs:143-148
                prev_y = y0 - static_cast<std::int32_t>(h_min); // rs:149
            }

            prev_x = max_x; // rs:153
            w += dw; // rs:154

            Insert(Fragment{ segment_index, rect, s }, i); // rs:156-163
        }

        const IntRect rect = is_inc // rs:166-180
            ? IntRect{ prev_x, s.b.x, prev_y, s.b.y }
            : IntRect{ prev_x, s.b.x, s.b.y, prev_y };

        Insert(Fragment{ segment_index, rect, s }, i1); // rs:182-189
    }

    // rs grid_layout.rs:192-234 add_horizontal
    void AddHorizontal(std::size_t segment_index, const XSegment& s)
    {
        const std::size_t i0 = layout.Index(s.a.x); // rs:193
        const std::size_t i1 = layout.Index(s.b.x - 1); // rs:194

        const std::int32_t y = s.a.y; // rs:196
        std::int32_t x0 = s.a.x; // rs:197
        std::size_t i = i0; // rs:198
        while (i < i1) // rs:199
        {
            const std::int32_t x = layout.Pos(i + 1); // rs:200
            const IntRect rect{ x0, x, y, y }; // rs:201-206
            Insert(Fragment{ segment_index, rect, s }, i); // rs:207-214

            x0 = x; // rs:216
            i += 1; // rs:217
        }

        const IntRect rect{ x0, s.b.x, y, y }; // rs:220-225
        Insert(Fragment{ segment_index, rect, s }, i1); // rs:226-233
    }
};

// ============================================================================
// SplitSolver - rs i_overlay-4.5.2/src/split/solver.rs:12-246
// ============================================================================

class SplitSolver
{
public:
    // rs split/solver.rs:13
    std::vector<LineMark> marks;

    // rs split/solver.rs:17-20 new
    SplitSolver() = default;

    // rs split/solver.rs:39-54 split (Solver fixed to Auto, see file header)
    bool Split(std::vector<OverlaySegment>& segments)
    {
        const bool is_list = IsListSplit(segments); // rs:41
        const SnapRadius snap_radius = MakeSnapRadius(); // rs:42
        if (is_list) // rs:43-45
        {
            return ListSplit(snap_radius, segments);
        }

        const bool is_fragmentation = IsFragmentationRequired(segments); // rs:47

        if (is_fragmentation) // rs:49-50
        {
            return FragmentSplit(snap_radius, segments);
        }
        return TreeSplit(snap_radius, segments); // rs:52
    }

    // rs split/solver.rs:56-130 cross - records split marks for one pair.
    static bool Cross(std::size_t i, std::size_t j, const XSegment& ei, const XSegment& ej,
                      std::vector<LineMark>& out_marks, std::int64_t radius)
    {
        CrossResult cross;
        if (!CrossSolverCross(ei, ej, radius, cross)) // rs:64-68
        {
            return false;
        }

        switch (cross.crossType)
        {
        case CrossType::Pure: // rs:71-80
            out_marks.push_back(LineMark{ i, cross.point });
            out_marks.push_back(LineMark{ j, cross.point });
            break;
        case CrossType::TargetEnd: // rs:81-86
            out_marks.push_back(LineMark{ j, cross.point });
            break;
        case CrossType::OtherEnd: // rs:87-92
            out_marks.push_back(LineMark{ i, cross.point });
            break;
        case CrossType::Overlay: // rs:93-127
        {
            const std::uint8_t mask = Collinear(ei, ej); // rs:94
            if (mask == 0) // rs:95-97
            {
                return false;
            }

            // EndMask bit tests - rs cross_solver.rs:23-42
            if (mask & kTargetA) // rs:99-104
            {
                out_marks.push_back(LineMark{ j, ei.a });
            }
            if (mask & kTargetB) // rs:105-111
            {
                out_marks.push_back(LineMark{ j, ei.b });
            }
            if (mask & kOtherA) // rs:113-118
            {
                out_marks.push_back(LineMark{ i, ej.a });
            }
            if (mask & kOtherB) // rs:119-125
            {
                out_marks.push_back(LineMark{ i, ej.b });
            }
            break;
        }
        }

        return cross.isRound; // rs:129
    }

    // rs split/solver.rs:132-194 apply
    void Apply(std::vector<OverlaySegment>& segments,
               std::vector<LineMark>& /*reusable_buffer*/)
    {
        // rs:138-139 - sort by index then point (parallel flag irrelevant:
        // serial port; the buffer is the parallel-sort scratch of
        // rs line_mark.rs:17-25 and is unused here)
        SortMarksByIndexAndPoint(marks);

        // rs:140 - Vec::dedup removes *consecutive* equal marks; the sort
        // above makes all equal (index, point) pairs adjacent.
        marks.erase(std::unique(marks.begin(), marks.end(),
                                [](const LineMark& l, const LineMark& r) {
                                    return l.index == r.index && l.point.x == r.point.x
                                        && l.point.y == r.point.y;
                                }),
                    marks.end());

        segments.reserve(segments.size() + marks.size()); // rs:142 - reserve(additional)

        // split segments - rs:144

        std::size_t i = 0; // rs:146
        while (i < marks.size()) // rs:147
        {
            const std::size_t start = i; // rs:148
            const LineMark m0 = marks[i]; // rs:149

            i += 1; // rs:151
            while (i < marks.size() && marks[i].index == m0.index) // rs:152-154
            {
                i += 1;
            }

            // rs:156-164 - marks reference valid original segment indices;
            // the reference is written before any push below, so later
            // reallocation cannot invalidate it (same discipline as the
            // unsafe get_unchecked_mut in the source).
            OverlaySegment& s0 = segments[m0.index];

            const std::int32_t count = s0.fill; // rs:163
            const XSegment x_seg = XSegOf(s0); // rs:164

            if (start + 1 == i) // rs:166-173 - single split
            {
                s0 = CreateAndValidate(x_seg.a, m0.point, count); // rs:168
                const OverlaySegment s1 = CreateAndValidate(m0.point, x_seg.b, count); // rs:169
                segments.push_back(s1); // rs:170

                continue; // rs:172
            }

            // we have servral points - rs:175 (source typo kept in comment)
            LineMark* sub_marks = marks.data() + start; // rs:176 - marks[start..i]
            const std::size_t sub_len = i - start;
            SortSubMarks(sub_marks, sub_len, x_seg); // rs:177

            const LineMark first = sub_marks[0]; // rs:179
            s0 = CreateAndValidate(x_seg.a, first.point, count); // rs:180

            IntPoint p0 = first.point; // rs:182

            for (std::size_t k = 1; k < sub_len; ++k) // rs:184 - iter().skip(1)
            {
                segments.push_back(CreateAndValidate(p0, sub_marks[k].point, count)); // rs:185
                p0 = sub_marks[k].point; // rs:186
            }

            segments.push_back(CreateAndValidate(p0, x_seg.b, count)); // rs:189
        }

        SortByAb(segments); // rs:192
        MergeIfNeeded(segments); // rs:193
    }

    // rs split/solver.rs:196-224 sort_sub_marks
    static void SortSubMarks(LineMark* sub_marks, std::size_t len, const XSegment& x_seg)
    {
        std::size_t j0 = 0; // rs:198
        std::size_t j = 1; // rs:199

        const LineMark m0 = sub_marks[0]; // rs:201
        std::int32_t x0 = m0.point.x; // rs:202
        while (j < len) // rs:203
        {
            const std::int32_t xi = sub_marks[j].point.x; // rs:204
            if (x0 == xi) // rs:205-208
            {
                j += 1;
                continue;
            }

            if (j0 + 1 < j) // rs:210-213
            {
                const YPair y01 = YRangeOf(j0, j, x_seg, sub_marks, len); // rs:211
                SortSubMarksByY(y01.y0, y01.y1, sub_marks + j0, j - j0); // rs:212
            }

            x0 = xi; // rs:215
            j0 = j; // rs:216
            j += 1; // rs:217
        }

        if (j0 + 1 < j) // rs:220-223
        {
            const YPair y01 = YRangeOf(j0, j, x_seg, sub_marks, len); // rs:221
            SortSubMarksByY(y01.y0, y01.y1, sub_marks + j0, j - j0); // rs:222
        }
    }

    // local substitute for the (i32, i32) tuple returned by the source
    struct YPair
    {
        std::int32_t y0;
        std::int32_t y1;
    };

    // rs split/solver.rs:226-235 y_range
    static YPair YRangeOf(std::size_t j0, std::size_t j1, const XSegment& s, const LineMark* m,
                          std::size_t len)
    {
        const std::int32_t y0 = j0 == 0 ? s.a.y : m[j0 - 1].point.y; // rs:228
        const std::int32_t y1 = j1 == len ? s.b.y : m[j1].point.y; // rs:229-233
        return YPair{ y0, y1 };
    }

    // rs split/solver.rs:237-245 sort_sub_marks_by_y
    static void SortSubMarksByY(std::int32_t y0, std::int32_t y1, LineMark* m, std::size_t len)
    {
        // The x-coordinate is the same for every point (rs:239-240)
        if (y0 > y1) // rs:241-244 - reverse to sort descending by y
        {
            std::reverse(m, m + len); // rs:243
        }
    }

    // -----------------------------------------------------------------------
    // list split - rs i_overlay-4.5.2/src/split/solver_list.rs:9-61
    // -----------------------------------------------------------------------

    bool ListSplit(SnapRadius snap_radius, std::vector<OverlaySegment>& segments)
    {
        bool need_to_fix = true; // rs:15

        // Bring-up guard: the fix-up loop terminates in Rust because a pass
        // without new marks breaks it; a translation defect in the mark
        // logic would spin here while pushing split segments without bound
        // (the failure mode that exhausted system memory). Real inputs
        // settle in a handful of rounds and a few thousand segments, so 64
        // rounds / 2^20 segments are orders of magnitude above any valid
        // offset case. Tripping the guard aborts the whole split; the
        // caller catches and falls back to the ring resolver.
        std::size_t splitRoundGuard = 0;

        // rs:17 - snap_radius moved (by value here)
        bool any_intersection = false; // rs:18
        std::vector<LineMark> reusable_buffer; // rs:19 (parallel-sort scratch, unused)

        while (need_to_fix && segments.size() > 1) // rs:21
        {
            if (++splitRoundGuard > 64U || segments.size() > (1U << 20))
            {
                throw std::runtime_error("overlay split: guard tripped");
            }
            need_to_fix = false; // rs:22
            marks.clear(); // rs:23

            const std::int64_t radius = snap_radius.Radius(); // rs:25

            for (std::size_t i = 0; i + 1 < segments.size(); ++i) // rs:27 - 0..len-1
            {
                const XSegment ei = XSegOf(segments[i]); // rs:28
                const LineRange ri = ei.YRange(); // rs:29
                for (std::size_t j = i + 1; j < segments.size(); ++j) // rs:30 - skip(i+1)
                {
                    const XSegment ej = XSegOf(segments[j]); // rs:31
                    if (ei.b.x < ej.a.x) // rs:32-34
                    {
                        break; // sorted by a.x, nothing later can overlap
                    }

                    if (ej.IsNotIntersectYRange(ri)) // rs:36-38
                    {
                        continue;
                    }

                    const bool is_round = Cross(i, j, ei, ej, marks, radius); // rs:40
                    need_to_fix = need_to_fix || is_round; // rs:41
                }
            }

            if (marks.empty()) // rs:45-47
            {
                return any_intersection;
            }
            any_intersection = true; // rs:48
            Apply(segments, reusable_buffer); // rs:49

            snap_radius.Increment(); // rs:51

            if (need_to_fix && !IsListSplit(segments)) // rs:53
            {
                // finish with tree solver if edges is become large - rs:54
                TreeSplit(snap_radius, segments); // rs:55
                return true; // rs:56
            }
        }

        return any_intersection; // rs:60
    }

    // -----------------------------------------------------------------------
    // tree split - rs i_overlay-4.5.2/src/split/solver_tree.rs:27-85
    //
    // PLACEHOLDER: grid-like index skipped - O(n^2) path.
    // The source drives this pass with an i_tree SegExpTree
    // (i_tree-0.18.0 src/seg/tree.rs): a segment tree keyed by y-range with
    // lazy expiration by x (expiration == segment.b.x, rs solver_tree.rs:19-24).
    // iter_by_range(si_range, time) yields each previously inserted segment
    // exactly once iff (a) its y-range bucket mask intersects the query's
    // (a superset of exact y-overlap, since Layout quantizes to
    // 2^scale buckets, i_tree src/seg/layout.rs:28-52) and (b) it is not
    // expired (sj.b.x >= si.a.x, i_tree src/seg/tree.rs:135). Pairs found
    // only via the bucket superset cannot produce marks: CrossSolver::cross
    // returns None for y-disjoint segments (is_not_cross,
    // rs split/cross_solver.rs:88-92). The port therefore scans all j < i
    // with the exact y-overlap test - identical mark output, O(n^2) instead
    // of O(n log n).
    // -----------------------------------------------------------------------

    bool TreeSplit(SnapRadius snap_radius, std::vector<OverlaySegment>& segments)
    {
        if (segments.empty()) // defensive; unreachable (rs split/solver.rs:28-30)
        {
            return false;
        }

        // rs solver_tree.rs:104-118 ver_range - vertical extent of all segments
        std::int32_t min_y = segments[0].a.y; // rs:104
        std::int32_t max_y = min_y; // rs:105
        for (const OverlaySegment& edge : segments) // rs:107-112
        {
            min_y = std::min(min_y, edge.a.y);
            max_y = std::max(max_y, edge.a.y);
            min_y = std::min(min_y, edge.b.y);
            max_y = std::max(max_y, edge.b.y);
        }

        // rs:33-38 - SegExpTree::new(range) or fallback to list_split.
        // i_tree Layout::new succeeds iff (max - min + 1) >= 17
        // (i_tree-0.18.0 src/seg/layout.rs:11-25: len >= 2^POWER with
        // POWER = 5 gives len >= 32; and p = ilog2(len-1)+1 >= 5 gives
        // len >= 17, the binding constraint).
        // TODO(i_tree layout.rs:14): the source computes (max - min + 1) in
        // i32 (wraps/panics for extreme ranges); the port computes in i64,
        // which only differs for ranges spanning >= 2^31 - out of contract.
        const std::int64_t range_len =
            static_cast<std::int64_t>(max_y) - static_cast<std::int64_t>(min_y) + 1;
        if (range_len < 17)
        {
            return ListSplit(snap_radius, segments); // rs:36-38
        }

        std::vector<LineMark> reusable_buffer; // rs:40 (unused scratch)

        bool need_to_fix = true; // rs:42
        bool any_intersection = false; // rs:43

        // rs:45 - snap_radius moved (by value here)
        std::size_t splitRoundGuard = 0; // same bring-up guard as list_split

        while (need_to_fix && segments.size() > 2) // rs:47
        {
            if (++splitRoundGuard > 64U || segments.size() > (1U << 20))
            {
                throw std::runtime_error("overlay split: guard tripped");
            }
            need_to_fix = false; // rs:48
            marks.clear(); // rs:49

            const std::int64_t radius = snap_radius.Radius(); // rs:51

            // rs:53-70 - sweep in a.x order (the vector is sorted by a.x on
            // entry and re-sorted by apply). For each i, test every
            // previously inserted j (insertion happens after the scan, rs:69).
            for (std::size_t i = 0; i < segments.size(); ++i) // rs:53
            {
                const std::int32_t time = segments[i].a.x; // rs:54
                const XSegment si = XSegOf(segments[i]); // rs:53 (si)
                const LineRange si_range = si.YRange(); // rs:55
                for (std::size_t j = 0; j < i; ++j) // PLACEHOLDER scan (see note above)
                {
                    // i_tree src/seg/tree.rs:135 - expired: expiration < time
                    if (static_cast<std::int64_t>(segments[j].b.x) < time)
                    {
                        continue;
                    }
                    const XSegment sj = XSegOf(segments[j]);
                    if (!YRangesOverlap(si_range, sj.YRange())) // exact-overlap filter
                    {
                        continue;
                    }

                    std::size_t this_index; // rs:57-61
                    std::size_t scan_index;
                    const XSegment* this_seg;
                    const XSegment* scan_seg;
                    if (CmpXSegment(si, sj) < 0) // si.x_segment < sj.x_segment
                    {
                        this_index = i;
                        scan_index = j;
                        this_seg = &si;
                        scan_seg = &sj;
                    }
                    else
                    {
                        this_index = j;
                        scan_index = i;
                        this_seg = &sj;
                        scan_seg = &si;
                    }

                    const bool is_round =
                        Cross(this_index, scan_index, *this_seg, *scan_seg, marks, radius); // rs:63-64

                    need_to_fix = is_round || need_to_fix; // rs:66
                }

                // rs:69 - tree.insert_by_range(si_range, si.id_segment(i)):
                // implicit in the j < i scan above.
            }

            if (marks.empty()) // rs:72-74
            {
                return any_intersection;
            }
            any_intersection = true; // rs:76
            // rs:77 - tree.clear(): implicit (each pass rescans from j = 0)

            Apply(segments, reusable_buffer); // rs:79

            snap_radius.Increment(); // rs:81
        }

        return any_intersection; // rs:84
    }

    // -----------------------------------------------------------------------
    // fragment split - rs i_overlay-4.5.2/src/split/solver_fragment.rs:13-74
    //
    // PLACEHOLDER: grid index skipped - O(n^2) path (see GridLayout above):
    // with a single bin, serial_split runs bin_split over one group holding
    // every segment whole, i.e. full pairwise detection. The on_border pass
    // below is a verbatim port but never fires (no bin borders, so
    // on_border stays empty).
    // -----------------------------------------------------------------------

    bool FragmentSplit(SnapRadius snap_radius, std::vector<OverlaySegment>& segments)
    {
        GridLayout layout;
        if (!GridLayoutTryNew(segments, layout)) // rs:19-24
        {
            return TreeSplit(snap_radius, segments);
        }

        std::vector<LineMark> reusable_buffer; // rs:26 (unused scratch)
        FragmentBuffer buffer(layout); // rs:27

        bool need_to_fix = true; // rs:29
        bool any_intersection = false; // rs:30

        SnapRadius snap = snap_radius; // rs:32 - moved copy

        std::size_t splitRoundGuard = 0; // same bring-up guard as list_split

        while (need_to_fix && segments.size() > 2) // rs:34
        {
            if (++splitRoundGuard > 64U || segments.size() > (1U << 20))
            {
                throw std::runtime_error("overlay split: guard tripped");
            }
            marks.clear(); // rs:35

            buffer.InitFragmentBuffer(segments); // rs:37
            for (std::size_t i = 0; i < segments.size(); ++i) // rs:38-40
            {
                buffer.AddSegment(i, XSegOf(segments[i]));
            }

            // rs:42 + rs:76-86 process - serial path (multithreading feature
            // disabled in this port)
            need_to_fix = SerialSplit(snap.Radius(), buffer);

            // rs:44-45 debug_assert!(buffer.is_on_border_sorted()) - debug-only, skipped
            std::size_t j = 0; // rs:46
            while (j < buffer.on_border.size()) // rs:47-59
            {
                const std::size_t j0 = j; // rs:48
                const std::int32_t x = buffer.on_border[j].x; // rs:49
                j += 1; // rs:50
                while (j < buffer.on_border.size() && x == buffer.on_border[j].x) // rs:51-53
                {
                    j += 1;
                }

                const std::size_t index = buffer.layout.Index(x); // rs:55
                if (index < buffer.groups.size()) // rs:56 - groups.get(index)
                {
                    OnBorderSplit(x, buffer.groups[index], buffer.on_border, j0, j); // rs:57
                }
            }

            if (marks.empty()) // rs:61-63
            {
                return any_intersection;
            }
            any_intersection = true; // rs:65
            buffer.Clear(); // rs:66

            Apply(segments, reusable_buffer); // rs:68

            snap.Increment(); // rs:70
        }

        return any_intersection; // rs:73
    }

    // rs split/solver_fragment.rs:76-86 process -> serial (parallel_split,
    // rs:101-144, requires the allow_multithreading feature and is skipped)
    bool SerialSplit(std::int64_t radius, FragmentBuffer& buffer)
    {
        // rs:89-99 serial_split
        bool is_any_round = false;
        for (auto& group : buffer.groups) // rs:91
        {
            if (group.empty()) // rs:92-94
            {
                continue;
            }
            const bool any_round = BinSplit(radius, group, marks); // rs:95
            is_any_round = is_any_round || any_round; // rs:96
        }
        return is_any_round; // rs:98
    }

    // rs split/solver_fragment.rs:146-177 bin_split
    static bool BinSplit(std::int64_t radius, std::vector<Fragment>& fragments,
                         std::vector<LineMark>& out_marks)
    {
        if (fragments.size() < 2) // rs:147-149
        {
            return false;
        }

        // rs:151 - sort_unstable_by(rect.min_y); equal keys are value-equal
        // for the pair logic below, so std::sort is equivalent.
        std::sort(fragments.begin(), fragments.end(), [](const Fragment& a, const Fragment& b) {
            return a.rect.min_y < b.rect.min_y;
        });

        bool any_round = false; // rs:153

        for (std::size_t i = 0; i + 1 < fragments.size(); ++i) // rs:155 - take(len-1)
        {
            for (std::size_t j = i + 1; j < fragments.size(); ++j) // rs:156 - skip(i+1)
            {
                if (fragments[i].rect.max_y < fragments[j].rect.min_y) // rs:157-159
                {
                    break;
                }
                if (!IsIntersectBorderInclude(fragments[i].rect, fragments[j].rect)) // rs:160-162
                {
                    continue;
                }

                // MARK: the intersection, ensuring the right order for
                // deterministic results - rs:164-170
                const bool is_round = CmpXSegment(fragments[i].x_segment, fragments[j].x_segment) < 0
                    ? CrossFragments(fragments[i], fragments[j], radius, out_marks) // rs:167
                    : CrossFragments(fragments[j], fragments[i], radius, out_marks); // rs:169

                any_round = any_round || is_round; // rs:172
            }
        }

        return any_round; // rs:176
    }

    // rs split/solver_fragment.rs:179-213 on_border_split.
    // vertical_segments is buffer.on_border[j0..j1].
    // Dead code under the single-bin placeholder (on_border never fills);
    // ported verbatim.
    void OnBorderSplit(std::int32_t border_x, const std::vector<Fragment>& fragments,
                       std::vector<BorderVSegment>& on_border, std::size_t j0, std::size_t j1)
    {
        std::vector<IntPoint> points; // rs:185
        for (const Fragment& fragment : fragments) // rs:186
        {
            if (fragment.x_segment.b.x == border_x) // rs:187
            {
                points.push_back(fragment.x_segment.b); // rs:188
            }
        }

        if (points.empty()) // rs:192
        {
            return;
        }

        // rs:196 - sort_unstable_by y (all points share x == border_x, so
        // equal y means the identical point)
        std::sort(points.begin(), points.end(),
                  [](const IntPoint& p0, const IntPoint& p1) { return p0.y < p1.y; });

        BorderVSegment* vseg = on_border.data() + j0;
        const std::size_t vlen = j1 - j0;
        // rs:197 - sort_by y_range.min (stable in Rust; ties yield the same
        // mark set because each vertical contributes independent marks)
        std::sort(vseg, vseg + vlen, [](const BorderVSegment& s0, const BorderVSegment& s1) {
            return s0.y_range.min < s1.y_range.min;
        });

        std::size_t i = 0; // rs:199
        for (std::size_t k = 0; k < vlen; ++k) // rs:200
        {
            const BorderVSegment& s = vseg[k];
            while (i < points.size() && points[i].y <= s.y_range.min) // rs:201
            {
                i += 1;
            }
            std::size_t jj = i; // rs:204
            while (jj < points.size() && points[jj].y < s.y_range.max) // rs:205
            {
                marks.push_back(LineMark{ s.id, points[jj] }); // rs:206-209
                jj += 1;
            }
        }
    }

    // rs split/solver_fragment.rs:215-301 cross_fragments
    static bool CrossFragments(const Fragment& fi, const Fragment& fj, std::int64_t radius,
                               std::vector<LineMark>& out_marks)
    {
        CrossResult cross;
        if (!CrossSolverCross(fi.x_segment, fj.x_segment, radius, cross)) // rs:216-220
        {
            return false;
        }

        // rs:222 - `radius as i32` (i64 -> i32 truncation, implementation-
        // defined in C++ pre-C++20 but truncating on MSVC/GCC/Clang, matching
        // Rust `as`). Used only for rect membership slack below.
        const std::int32_t r = static_cast<std::int32_t>(radius);

        if (cross.crossType == CrossType::Overlay) // rs:225
        {
            const std::uint8_t mask = Collinear(fi.x_segment, fj.x_segment); // rs:226
            if (mask == 0) // rs:227-229
            {
                return false;
            }

            if (!(ContainsWithRadius(fi.rect, fi.x_segment.a, r) // rs:231-235
                  || ContainsWithRadius(fj.rect, fi.x_segment.a, r)))
            {
                return false;
            }

            if (mask & kTargetA) // rs:237-241
            {
                out_marks.push_back(LineMark{ fj.index, fi.x_segment.a });
            }
            if (mask & kTargetB) // rs:244-248
            {
                out_marks.push_back(LineMark{ fj.index, fi.x_segment.b });
            }
            if (mask & kOtherA) // rs:251-255
            {
                out_marks.push_back(LineMark{ fi.index, fj.x_segment.a });
            }
            if (mask & kOtherB) // rs:258-262
            {
                out_marks.push_back(LineMark{ fi.index, fj.x_segment.b });
            }
        }
        else
        {
            if (!ContainsWithRadius(fi.rect, cross.point, r) // rs:266-269
                || !ContainsWithRadius(fj.rect, cross.point, r))
            {
                return false;
            }

            switch (cross.crossType) // rs:272-296
            {
            case CrossType::Pure: // rs:273-281
                out_marks.push_back(LineMark{ fi.index, cross.point });
                out_marks.push_back(LineMark{ fj.index, cross.point });
                break;
            case CrossType::TargetEnd: // rs:283-287
                out_marks.push_back(LineMark{ fj.index, cross.point });
                break;
            case CrossType::OtherEnd: // rs:289-293
                out_marks.push_back(LineMark{ fi.index, cross.point });
                break;
            case CrossType::Overlay: // rs:295 - unreachable (_ => {})
                break;
            }
        }

        return cross.isRound; // rs:300
    }
};

} // namespace

// ============================================================================
// Entry - rs i_overlay-4.5.2/src/split/solver.rs:17-37 split_segments
//
// Subject-only use case, WindingCount fixed to ShapeCountBoolean and Solver
// fixed to Solver::AUTO + Precision::HIGH (see OverlaySplit.h). The source
// returns `any_merged | any_intersection` (rs:36); the mandated C++ entry is
// void, so both flags are computed and discarded.
// ============================================================================
void splitSegments(std::vector<OverlaySegment>& segments)
{
    if (segments.empty()) // rs:28-30
    {
        return;
    }

    SortByAb(segments); // rs:32 (is_parallel_sort_allowed: serial port)
    const bool any_merged = MergeIfNeeded(segments); // rs:33

    SplitSolver solver; // rs:18-20
    const bool any_intersection = solver.Split(segments); // rs:34

    (void)any_merged; // rs:36 - return value dropped (void entry)
    (void)any_intersection;
}

} // namespace overlay
} // namespace jaguar
