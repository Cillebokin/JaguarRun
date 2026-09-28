# SparrowCore compatibility

SparrowCore is the C++/Qt-facing optimizer in JaguarRun. JaguarCore remains the geometry,
collision, snapshot, validation, JSON, and SVG foundation. SparrowCore implements the main
Sparrow optimization phases: LBF construction/search, sampling and coordinate descent,
collision quantification, separator workers, exploration, compression, and the C++/CLI/WASM
boundaries.

This phase adds a live Rust oracle rather than treating C++-generated snapshots as parity
evidence. The currently direct-checked surface is intentionally narrow:

- Rust `Xoshiro256++` seed-42 raw values and `f32 random_range` values are checked by the Cargo
  oracle and consumed by the native RNG tests.
- Rust rand 0.10.3 bounded `i32 random_range` results are checked across six seeds and six bounds;
  C++ `UniformIndex` consumes the same values for those cases.
- Eight Rust `UniformBBoxSampler` outputs and the complete candidate/evaluation trace from
  `refine_coord_desc` with a deterministic synthetic evaluator are compared directly with C++;
  transforms, losses, evaluation kinds, and evaluation counts are all checked with the
  fixture-specific tolerances.
- The actual Rust container-wide `search_placement` path with `LBFEvaluator` is compared event by
  event against `SearchLbf`: evaluation results, report acceptance, sorted retained-sample
  snapshots, and the final coordinate-descent result. The focused/reference-placement path remains
  outside this fixture.
- The actual Rust `search_placement` path with `SeparationEvaluator` and a reference placement is
  compared event by event against `FindBestSeparatorMove`: current-placement evaluation, focused
  and container samples, both refinement phases, evaluation kinds/losses, report acceptance,
  retained order, count, and final result. The C++ fixture importer explicitly matches Sparrow's
  default CDE/surrogate settings.
- Rust `LBFBuilder` and SparrowCore build the same one-item JSON fixture with seed 42 and the
  same sample configuration. Placed count, remaining demand, and feasibility must match exactly;
  strip width and density use tolerances documented in `Docs/Differential.md`.
- Actual Rust `Separator::separate` rounds with `(seed=42, workers=1)`, `(seed=42, workers=2)`, and
  `(seed=43, workers=3)` are compared end-to-end with C++: worker result selection, progress/reports,
  counters, final placements, and per-item/pair loss-weight state after rollback. The multi-worker
  fixtures directly exercise Rayon and establish parity for those fixed inputs/configurations, not
  worker counts above three. A direct Rust Exploration trace checks repeated feasible shrinking,
  minimum-width termination, failed-pool termination, and the one-item disruption early return with
  zero separator strikes. A separate seed-43 Rust trace checks a two-candidate weighted failure pool,
  the sampled higher-loss candidate, and the resulting two-item pair-swap state. Another seed-43
  three-item trace checks all three failed states and verifies the POI-contained child is relocated
  4.3 units into its swapped large-item host's vacated space. Other contained-layout geometries,
  arbitrary disruption settings, and compression remain outside Rust differential evidence.

Known boundaries and compatibility choices:

- `Result<T>`, `SPSolution`, JSON, and layout handles are JaguarCore representations of the Rust
  ownership and serde model.
- The vendored Sparrow call sites include a small API bridge for `jagua-rs` 0.9's fallible
  `SPProblem` operations. It preserves the prior successful-path behavior; the oracle exercises
  validated instances.
- JaguarCore now exposes the point-of-inaccessibility geometry primitive, and contained-item
  recovery uses that result; the earlier centroid-approximation note is obsolete.
- Rust and C++ now match the checked xoshiro raw stream, `f32` range values, and bounded integer
  draws; the direct sampler/CDE fixture and a real container-wide LBF search trace match
  candidate-by-candidate. A real focused separator single-move trace and complete one-, two-, and
  three-worker separator rounds also match, including rand 0.10 `SliceRandom` ordering and
  `float-cmp`'s approximate `f32` loss ordering. Deterministic Exploration shrink/minimum-width and
  failed-pool termination paths also match directly. Fixed Exploration traces check multi-candidate
  weighted selection and a two-item pair-swap state, plus three-item POI-driven contained-child
  relocation into vacated space after a large-item swap. The checked multi-worker Rust
  traces are reproducible across independent generations. This does not establish full optimizer
  parity: geometry kernels, floating-point execution outside those traces, worker counts above
three, other contained-layout geometries and disruption settings, compression, and end-to-end runs
remain outside this evidence; bit-identical optimizer output is not claimed.
- Legacy iteration, failure, and infeasible-pool limits remain available to existing C++ callers.
- WASM exports the stable C ABI (`SparrowOptimizeJson` and `SparrowOptimizeSvg`); Emscripten
  module packaging and host-side memory ownership remain deployment concerns.

Every successful C++ result is revalidated by JaguarCore. See `Docs/Differential.md` for the
oracle command, fixture provenance, tolerances, and the remaining verification gaps.
