# Rust/C++ differential oracle

This directory contains the first deterministic differential baseline for JaguarCore.

The files under `../Fixtures/RustReference/` use the jagua-rs external JSON schema. They intentionally contain small, deterministic cases so a failure identifies a geometry/import/state semantic mismatch rather than an optimizer ordering difference.

## Current checks

- `CompareGeometry.cpp`: rectangle area, centered internal contour, bounding box, normalized/floating-boundary rotations, offset and concave shape-modification behavior, plus PlacedItem/Exterior/Hole/InferiorQualityZone collision behavior and quality filtering.
- `CompareProblem.cpp`: SPP demand/width/density, BPP demand/stock/cost/density, and MSPP demand/max-width/total-width/density invariants, including Layout snapshot restore.
- `CompareMatrix.cpp`: shared edge intersection and containment tables. The matrix explicitly preserves jagua-rs's non-collision rule for collinear segments and collision rule for non-collinear endpoint/T-junction contact.
- `CompareTrajectory.cpp`: the same xorshift64* seed and 256-step operation grammar run in Rust and C++, including placement/removal/restore counts, active count, checksum, and final PRNG state.
- `CompareOptimizer.cpp`: consumes a Rust LBF solution fixture generated with seed `42`; JaguarCore validates external placements, feasibility, item count, density, and strip width. It does not claim to reproduce LBF/Sparrow's search path.

The expected scalar values are derived from the corresponding jagua-rs import rules (`f32` geometry, centering transform, normalized rotation list, and total-area/fixed-height width). They are intentionally compared with explicit tolerances instead of comparing textual JSON; rotation values use a wider tolerance because JaguarCore stores `double` while jagua-rs stores `f32`.

## Expanding the oracle

1. Run the Rust workspace tests with the pinned toolchain.
2. Add a fixed Rust reference fixture or generated output under `rust_reference/`.
3. Add one C++ comparison with literals derived independently from the Rust run.
4. Compare feasibility, quantity conservation, stock, density, and hazard kinds before comparing coordinates.
5. Record float type, tolerance, item ordering, random seed, and whether a result came from an optimizer.

The current fixed collision matrix also records the boundary convention: CDE treats touching/tangent geometry as collision. A Surrogate negative case therefore uses a separate engine without an Exterior hazard, so it isolates fail-fast filtering from container-boundary semantics.

`CompareBoundary.cpp` additionally locks the primitive convention that non-collinear
near-contact uses the configured tolerance while collinear overlap remains outside the
Rust `Edge::collides_with` predicate. The randomized problem-state test runs 1000 steps
per SPP/BPP/MSPP model and compares each state with a separately restored problem.

## Rust LBF fixture

The optimizer fixture was generated with:

```powershell
cargo +1.90.0 run --offline --manifest-path jagua-rs/lbf/Cargo.toml -- `
  --input-file JaguarCore/Tests/Fixtures/RustReference/optimizer_spp_instance.json `
  --solution-folder JaguarCore/Tests/Fixtures/RustReference/LbfSpp `
  --config-file JaguarCore/Tests/Fixtures/RustReference/optimizer_lbf_config.json `
  --prob-var spp --log-level info
```

The recorded Rust result is approximately `strip_width = 4.398323`, `density = 0.53050524`, with three placed items. These values are acceptance bounds for the external result, not a promise that another optimizer or floating-point implementation will produce identical coordinates.

The oracle is not a claim that Sparrow or any optimizer must produce identical placements. It checks the shared geometry and problem-model contract.
