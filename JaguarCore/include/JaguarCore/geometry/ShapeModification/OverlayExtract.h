// OverlayExtract.h
//
// Line-by-line C++17 port of the i_overlay 4.5.2 build + extract chain.
//
// Rust sources (read-only reference):
//   - i_overlay-4.5.2/src/build/     boolean.rs, builder.rs, graph.rs, sweep.rs, util.rs, mod.rs
//   - i_overlay-4.5.2/src/core/      extract.rs, graph.rs, link.rs, overlay_rule.rs, fill_rule.rs,
//                                    solver.rs, overlay.rs (IntOverlayOptions only)
//   - i_overlay-4.5.2/src/segm/      boolean.rs, segment.rs, winding.rs, sort.rs
//   - dependencies actually referenced by that chain:
//       i_float-1.16.0   int/point.rs (IntPoint), fix_vec.rs (FixVec), triangle.rs (Triangle)
//       i_shape-1.18.0   int/path.rs (unsafe_area), int/simple.rs (Simplify), util/reserve.rs
//       i_tree-0.18.0    key/list.rs (KeyExpList), key/tree.rs (KeyExpTree), key/{entity,node,pool}.rs
//       i_key_sort-0.10.3 sort/two_keys.rs (TwoKeysSort)
//
// All arithmetic is integer only (no floating point / libm).
// i64 products follow Rust release-mode wrapping semantics via unsigned intermediates.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace jaguar
{
namespace overlay
{

// rs i_float-1.16.0/src/int/point.rs:6-10 (IntPoint)
// Lexicographic order (x, then y) matches rs IntPoint::cmp (point.rs:86-98); see pointCmp in .cpp.
#ifndef JAGUAR_OVERLAY_SPLIT_INT_POINT
#define JAGUAR_OVERLAY_SPLIT_INT_POINT
struct IntPoint
{
    int32_t x;
    int32_t y;
};

#endif // JAGUAR_OVERLAY_SPLIT_INT_POINT

// rs i_overlay-4.5.2/src/segm/segment.rs:22-26 Segment<ShapeCountBoolean>
// rs i_overlay-4.5.2/src/segm/boolean.rs:4-8   ShapeCountBoolean { subj: i32, clip: i32 }
//
// `fill` packs the two i32 winding counters of ShapeCountBoolean into one int32_t:
//   bits  0..15 : subj count (signed 16 bit, two's complement)
//   bits 16..31 : clip count (signed 16 bit, two's complement)
// Winding sums are accumulated internally in full i32 (exactly as rs), so the packing only
// constrains the per-input-segment counts to +/-32767 (real inputs are +/-1; see
// mesh/subject.rs:8-20).
//
// Precondition (identical to the Rust chain, which receives segments from the split solver):
//   - a < b lexicographically (XSegment invariant, rs geom/x_segment.rs + segm/segment.rs:30-42),
//   - segments sorted by (a.x, a.y, then b) — rs segm/sort.rs:8-18 sort_by_ab,
//   - segments are already split at intersections (the split solver is upstream, not part of
//     this port).
#ifndef JAGUAR_OVERLAY_SPLIT_SEGMENT
#define JAGUAR_OVERLAY_SPLIT_SEGMENT
struct OverlaySegment
{
    IntPoint a;
    IntPoint b;
    int32_t fill; // packed ShapeCountBoolean
};

#endif // JAGUAR_OVERLAY_SPLIT_SEGMENT

// rs i_overlay-4.5.2/src/segm/boolean.rs:10-14 (SUBJ_DIRECT / SUBJ_INVERT / CLIP_DIRECT / CLIP_INVERT)
constexpr int32_t packShapeCount(int32_t subjCount, int32_t clipCount) noexcept
{
    return static_cast<int32_t>((static_cast<uint32_t>(static_cast<uint16_t>(subjCount)) << 0)
        | (static_cast<uint32_t>(static_cast<uint16_t>(clipCount)) << 16));
}

inline int32_t shapeCountSubj(int32_t fill) noexcept
{
    return static_cast<int16_t>(static_cast<uint32_t>(fill) & 0xFFFFu);
}

inline int32_t shapeCountClip(int32_t fill) noexcept
{
    return static_cast<int16_t>((static_cast<uint32_t>(fill) >> 16) & 0xFFFFu);
}

// rs i_overlay-4.5.2/src/segm/segment.rs:30-42 Segment::create_and_validate
// Normalizes the endpoints so that a < b, inverting the winding counters when swapped.
OverlaySegment makeOverlaySegment(IntPoint p0, IntPoint p1, int32_t packedFill) noexcept;

// rs i_overlay-4.5.2/src/mesh/subject.rs:8-20 Segment::subject
// { subj: +/-1, clip: 0 } — this is what the offset (mesh outline) use case feeds in.
inline OverlaySegment makeSubjectSegment(IntPoint p0, IntPoint p1) noexcept
{
    if (p0.x == p1.x ? p0.y < p1.y : p0.x < p1.x)
    {
        return OverlaySegment{ p0, p1, packShapeCount(1, 0) };
    }
    else
    {
        return OverlaySegment{ p1, p0, packShapeCount(-1, 0) };
    }
}

// Flat extraction result — rs i_overlay-4.5.2/src/core/extract.rs:212-265 extract_contours
// (the flat path used by the offset solver, rs mesh/outline/offset.rs:306-318).
// `is_hole` is the value computed at rs core/extract.rs:240 (overlay_rule.is_fill_top(link.fill));
// contours are NOT nested / hole-joined (that is the separate extract() path, out of scope here).
// Contour direction: with the default counter-clockwise output, hulls come out CCW and holes CW
// (rs core/extract.rs:48, 118-123).
struct ExtractedContour
{
    std::vector<IntPoint> points;
    bool is_hole;
};

// rs i_overlay-4.5.2/src/core/overlay.rs:379-400 Overlay::overlay_into(Subject, fill_rule)
//   -> build/boolean.rs:38-57 build_boolean_overlay:
//        build_boolean_fills   (rs build/boolean.rs:60-72, sweep strategy Positive/Negative)
//        build_links_by_filter (rs build/boolean.rs:48, SubjectFilter)
//        boolean_graph         (rs build/boolean.rs:74-82, build_nodes_and_connect_links)
//   -> core/extract.rs:76-85 extract_contours_into(Subject)
//
// fillRulePositive: true  -> FillRule::Positive
//                   false -> FillRule::Negative
// preserveOutputCollinear: rs IntOverlayOptions::preserve_output_collinear (core/overlay.rs:37)
// outputClockwise:        rs IntOverlayOptions::output_direction == Clockwise (core/overlay.rs:58-61);
//                         the offset solver uses CCW for outer builds and CW for inner builds
//                         (rs mesh/outline/offset.rs:300,310)
// minOutputArea:          rs IntOverlayOptions::min_output_area (core/overlay.rs:39)
//
// The solver is fixed to Solver::AUTO (rs core/solver.rs:132-138) as in the offset path
// (rs mesh/outline/offset.rs:280 uses Overlay::new -> Default solver).
std::vector<ExtractedContour> extractSubjectContours(
    std::vector<OverlaySegment> segments,
    bool fillRulePositive,
    bool preserveOutputCollinear,
    bool outputClockwise = false,
    std::uint64_t minOutputArea = 0);

} // namespace overlay
} // namespace jaguar
