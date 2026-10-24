#pragma once

// ============================================================================
// OverlaySplit.h
//
// C++17 port of the i_overlay 4.5.2 "split" chain: splitting segments at
// their intersection points. Integer-only implementation: no floating point,
// no libm (all geometry math is i32/i64/u64 bit arithmetic; the fixed-point
// line/line intersection uses an explicit 128-bit multiply helper ported
// from i_float's UInt128).
//
// Ported source (read-only reference, annotated "rs <file>:<line>" in the
// implementation file OverlaySplit.cpp):
//   - CodeResource/i_overlay-4.5.2/src/split/solver.rs            (246 lines)
//   - CodeResource/i_overlay-4.5.2/src/split/solver_list.rs       (62)
//   - CodeResource/i_overlay-4.5.2/src/split/solver_tree.rs       (129)
//   - CodeResource/i_overlay-4.5.2/src/split/solver_fragment.rs   (302)
//   - CodeResource/i_overlay-4.5.2/src/split/fragment.rs          (33)
//   - CodeResource/i_overlay-4.5.2/src/split/line_mark.rs         (25)
//   - CodeResource/i_overlay-4.5.2/src/split/snap_radius.rs       (25)
//   - CodeResource/i_overlay-4.5.2/src/split/cross_solver.rs      (578)
//   - CodeResource/i_overlay-4.5.2/src/split/grid_layout.rs       (SKIPPED:
//     performance grid index - replaced by a single-bin placeholder, i.e.
//     full pairwise detection; see OverlaySplit.cpp for the exact behavioral
//     notes)
//   - CodeResource/i_overlay-4.5.2/src/segm/sort.rs, segment.rs, merge.rs,
//     winding.rs, boolean.rs (sorting / comparators / merge / winding count)
//   - CodeResource/i_float-1.16.0/src/int/point.rs, int/rect.rs,
//     triangle.rs, fix_vec.rs, u128.rs (int intersection primitives)
//
// Entry point:
//   void splitSegments(std::vector<OverlaySegment>& segments)
//
//   Corresponds to SplitSolver::split_segments (rs split/solver.rs:23-37)
//   for the Subject-only scenario:
//   - the WindingCount generic is fixed to ShapeCountBoolean
//     (rs segm/winding.rs:3-13, rs segm/boolean.rs:4-57);
//   - the Solver is fixed to the default Solver::AUTO with Precision::HIGH
//     (rs core/solver.rs:132-138, 46-50), serial execution (no
//     multithreading feature).
//
// fill semantics (Subject-only): ShapeCountBoolean { subj, clip } collapses
// into the single i32 `fill` because the clip component is identically 0 for
// Subject shapes (rs segm/boolean.rs:29-34):
//   fill == +1  subject direct edge,
//   fill == -1  inverted edge (rs segm/boolean.rs:11-12),
//   accumulate on merge (rs segm/merge.rs:30-54), drop when fill == 0
//   (rs segm/boolean.rs:19-21).
//
// Preconditions inherited from the source (rs split/cross_solver.rs:208-210):
// segment endpoint coordinates are assumed to satisfy |x|, |y| < 2^30.
// ============================================================================

#include <cstdint>
#include <vector>

// ---------------------------------------------------------------------------
// IntPoint - guard against a duplicate definition on the build/extract side.
// If another header already defines jaguar::overlay::IntPoint with this exact
// layout (int32_t x; int32_t y;), define JAGUAR_OVERLAY_SPLIT_INT_POINT
// before including this header and this definition is skipped. This header
// and OverlaySplit.cpp compile standalone with the definition below.
// ---------------------------------------------------------------------------
#ifndef JAGUAR_OVERLAY_SPLIT_INT_POINT
#define JAGUAR_OVERLAY_SPLIT_INT_POINT
namespace jaguar
{
namespace overlay
{
// rs i_float-1.16.0/src/int/point.rs:7-10
struct IntPoint
{
    std::int32_t x;
    std::int32_t y;
};
} // namespace overlay
} // namespace jaguar
#endif // JAGUAR_OVERLAY_SPLIT_INT_POINT

// ---------------------------------------------------------------------------
// OverlaySegment - same guard strategy as IntPoint. Layout-compatible with a
// build/extract-side definition of the same name; the split chain only reads
// {a, b} and the winding count carried by `fill` (see file header).
// ---------------------------------------------------------------------------
#ifndef JAGUAR_OVERLAY_SPLIT_SEGMENT
#define JAGUAR_OVERLAY_SPLIT_SEGMENT
namespace jaguar
{
namespace overlay
{
// rs i_overlay-4.5.2/src/segm/segment.rs:22-26 (x_segment + count), with the
// ShapeCountBoolean count collapsed to a single i32 (Subject-only use case).
struct OverlaySegment
{
    IntPoint a;          // x_segment.a (normalized: a < b, rs segment.rs:30-43)
    IntPoint b;          // x_segment.b
    std::int32_t fill;   // winding count, see file header
};
} // namespace overlay
} // namespace jaguar
#endif // JAGUAR_OVERLAY_SPLIT_SEGMENT

namespace jaguar
{
namespace overlay
{
// Splits all segments at their mutual intersection points.
// rs split/solver.rs:23-37 SplitSolver::split_segments (Subject-only, see
// file header). On return the vector is sorted (a.x, a.y, then b), equal
// segments are merged with accumulated fill, and every segment pair that
// crosses, touches or overlaps is split at the corresponding points.
//
// Input precondition (inherited from the source contract): segments must
// already be normalized exactly as Segment::create_and_validate produces
// (rs segm/segment.rs:30-43), i.e. a < b lexicographically (x, then y) and
// fill == -1 for inverted edges. The split chain itself does not normalize
// raw input; it assumes the caller built segments accordingly (in Rust this
// is done by the build phase). The Rust return value any_merged |
// any_intersection (rs split/solver.rs:36) is dropped by this void entry.
void splitSegments(std::vector<OverlaySegment>& segments);
} // namespace overlay
} // namespace jaguar
