# Rust/C++ differential oracle

This directory contains the first deterministic differential baseline for JaguarCore.

The RustOracle baseline uses Rust 1.98.1 and jagua-rs revision
`f5e2b5ffd14931f17a20ce283a2122e877953ceb`; invoke it with the pinned toolchain
and lockfile so it does not follow a different default rustup channel.

The files under `../Fixtures/RustReference/` use the jagua-rs external JSON schema. They intentionally contain small, deterministic cases so a failure identifies a geometry/import/state semantic mismatch rather than an optimizer ordering difference.

## Current checks

- `CompareGeometry.cpp`: pinned Rust geometry reference fixture for B1 primitives, transforms, convex hull and `OriginalShape`, plus rectangle area, centered internal contour, bounding box, normalized/floating-boundary rotations, offset and concave shape-modification behavior, PlacedItem/Exterior/Hole/InferiorQualityZone collision behavior and quality filtering.
- `CompareProblem.cpp`: SPP demand/width/density, BPP demand/stock/cost/density, and MSPP demand/max-width/total-width/density invariants, including Layout snapshot restore.
- `CompareMatrix.cpp`: shared edge intersection and containment tables. The matrix explicitly preserves jagua-rs's non-collision rule for collinear segments and collision rule for non-collinear endpoint/T-junction contact.
- `CompareTrajectory.cpp`: the same xorshift64* seed and 256-step operation grammar run in Rust and C++, including placement/removal/restore counts, active count, checksum, and final PRNG state.
- `CompareOptimizer.cpp`: consumes a Rust LBF solution fixture generated with seed `42`; JaguarCore validates external placements, feasibility, item count, density, and strip width. It does not claim to reproduce LBF/Sparrow's search path.
- `RustOracle --model-boundary-parity-only` and `--restore-boundary-parity-only`: probe Debug/Release demand underflow and snapshot restore state directly against jagua-rs. These probes report partial state after caught Rust panics; compare them with JaguarCore's `Result` outcome and state, rather than treating `panic` and `Infeasible` as interchangeable by default.

The expected scalar values are derived from the corresponding jagua-rs import rules (`f32` geometry, centering transform, normalized rotation list, and total-area/fixed-height width). They are intentionally compared with explicit tolerances instead of comparing textual JSON; rotation values use a wider tolerance because JaguarCore stores `double` while jagua-rs stores `f32`.

## Expanding the oracle

1. Run the Rust workspace tests with the pinned toolchain.
2. Add a fixed Rust reference fixture or generated output under `rust_reference/`.
3. Add one C++ comparison with literals derived independently from the Rust run.
4. Compare feasibility, quantity conservation, stock, density, and hazard kinds before comparing coordinates.
5. Record float type, tolerance, item ordering, random seed, and whether a result came from an optimizer.

The current fixed collision matrix also records the boundary convention: CDE treats touching/tangent geometry as collision. A Surrogate negative case therefore uses a separate engine without an Exterior hazard, so it isolates fail-fast filtering from container-boundary semantics.

The B1 geometry fixture uses the locked Rust 1.98.1 toolchain and jagua-rs revision. Existing circle/edge/rect/point/polygon values come from the default oracle; extended B1 values come from `RustOracle --geometry-primitives-parity-only`. Scalar results use JaguarCore's `AlmostEqualRustF32` comparison; booleans, position classifications, and ordered vertices are exact. The extended values were checked against both Rust Debug and Release output on 2026-10-16.

The B1 second round compares Rust transformation matrix behavior through transformed origin/unit-axis probes and decomposed fields, plus exact `is_empty` cases. Edge intersection fixtures cover crossing, endpoint and T-junction contact, collinear overlap/touch, separated parallel edges, and an out-of-segment line intersection. Rectangle relation fixtures sample 1, 2, 3, 4, 8, and 16 ULP below `1.0f` to lock the `almost_relation_to` transition.

The B1 fourth round expands `geometry_primitives.txt` to 52 keys. It compares the independent `Point::x/y`, `Edge::bbox`, and `Rect::sides` accessors; all four `TransformableFrom` implementations; Point-left collision blanket dispatch, rectangle collision/near-collision trait booleans, and Edge/Rect dispatch in both directions; plus direct Rust calls to the four `SPolygon` derived-value helpers. C++ compares those helper values with fields produced by `SimplePolygon::Create`, since there is no matching public C++ helper API. Rust 1.98.1 Debug/Release oracle output matches the fixture; JaguarCore Debug/Release full test runners pass with the assertions enabled.

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

## Debug/Release state probes

Run the Rust oracle in both profiles from the repository root:

```powershell
rustup run 1.98.1 cargo run --locked --manifest-path JaguarCore/tools/RustOracle/Cargo.toml -- --model-boundary-parity-only
rustup run 1.98.1 cargo run --release --locked --manifest-path JaguarCore/tools/RustOracle/Cargo.toml -- --model-boundary-parity-only
rustup run 1.98.1 cargo run --locked --manifest-path JaguarCore/tools/RustOracle/Cargo.toml -- --restore-boundary-parity-only
rustup run 1.98.1 cargo run --release --locked --manifest-path JaguarCore/tools/RustOracle/Cargo.toml -- --restore-boundary-parity-only
```

The model probe covers `total_item_qty()` overflow plus over-demand placement in SPP/BPP/MSPP, recording remaining demand, placed count, and BPP stock. The restore probe records the saved strip fields alongside the layout container width, making intentionally inconsistent snapshots visible. Its current Rust output documents unresolved JaguarCore acceptance/state differences; passing JaguarCore's ordinary Debug/Release suite alone does not close those differences.
