# SparrowCore differential verification

The behavioral source is vendored at `CodeResource/sparrow`. The Cargo oracle in
`Tests/RustReference` calls that Rust implementation directly and locks the resolved Rust and
`jagua-rs` revisions in its `Cargo.lock`; it does not copy Sparrow algorithms into the oracle.
The vendored call sites also adapt the `jagua-rs` 0.9 `Result`-returning strip APIs with explicit
expectations, matching their former infallible behavior for validated instances.

## Fixture matrix

| Area | Fixture | Covered behavior |
| --- | --- | --- |
| RNG | `Tests/Fixtures/LbfSeed42Reference.tsv` | Rust rand 0.10.3 seed-42 xoshiro raw outputs and `f32 random_range`; Rust oracle and C++ RNG tests consume the same values |
| Bounded integer RNG | `Tests/Fixtures/SparrowUniformIndexRustReference.tsv` | Rust rand 0.10.3 `i32 random_range(0..bound)` for six seeds and bounds 1, 2, 3, 4, 6, and 16; C++ `UniformIndex` must match each draw |
| Sampling and coordinate descent | `Tests/Fixtures/SamplingCoordinateDescentRustReference.tsv` | Eight real Rust `UniformBBoxSampler` samples plus every candidate, evaluation kind/loss, evaluation count, and final state from `refine_coord_desc` with a deterministic synthetic evaluator; compared against the C++ sampler and coordinate descent |
| Direct LBF search | `Tests/Fixtures/LbfSearchRustTrace.tsv` | Actual Rust `search_placement` with `LBFEvaluator`: candidate evaluations, report acceptance, ordered retained-sample snapshots, and final refined result; compared against `SearchLbf` |
| Direct separator move search | `Tests/Fixtures/SeparatorRustTrace.json`, `Tests/Fixtures/SeparatorSearchRustTrace.tsv` | Actual Rust `search_placement` with `SeparationEvaluator` and `ref_pk=Some(...)`: starting placement, focused/container sampling, pre/final refinement, evaluation/report events, retained snapshots, and returned result; compared against `FindBestSeparatorMove` |
| Direct LBF construction | `Tests/Fixtures/RustSppSingleItem.json`, `SparrowLbfRustReference.tsv` | Rust `LBFBuilder` and C++ constructor on the same one-item SPP input and sample configuration |
| Legacy Jagua LBF metrics | `Tests/Fixtures/LbfMetricsReference.tsv` | Schema, provenance, and stored values from `jagua-rs/lbf`; not a Sparrow parity fixture |
| LBF search | `Tests/Fixtures/LbfSearchReference.tsv` | C++ focused/container sample order, best-sample retention, coordinate-descent regression |
| SPP/BPP construction | `Tests/Fixtures/SppLbfCppReference.tsv`, `BppLbfCppReference.tsv` | fixed-seed constructor output and metric envelope |
| Single-worker separator round | `Tests/Fixtures/SeparatorRoundRustTrace.json`, `SeparatorRoundRustTrace.tsv` | Actual Rust `Separator::separate` run with one worker; progress, reports, counters, final placements, and loss/weight state after rollback |
| Two-worker separator round | `Tests/Fixtures/SeparatorRoundRustTrace.json`, `SeparatorMultiWorkerRoundRustTrace.tsv` | Actual Rust `Separator::separate` run with two Rayon workers; progress, reports, counters, selected placements, and loss/weight state after rollback |
| Three-worker / alternate-seed separator round | `Tests/Fixtures/SeparatorRoundRustTrace.json`, `SeparatorThreeWorkerSeed43RoundRustTrace.tsv` | Actual Rust `Separator::separate` run with three Rayon workers and seed 43; same end-to-end state compared with C++ |
| Legacy two-worker separator regression | `Tests/Fixtures/SeparatorReference.tsv` | Fixed-seed C++ regression for two workers; retained as an additional regression baseline |
| Direct Exploration phase | `Tests/Fixtures/ExplorationRustTrace.tsv` | Actual Rust `exploration_phase` with zero separator strikes; compares repeated shrinking, minimum-width stopping, failed-pool termination, best width/density, feasibility, report sequence, progress widths, and placed-item bounds |
| Exploration weighted failure pool | `Tests/Fixtures/ExplorationPool.json`, `Tests/Fixtures/ExplorationPoolRustTrace.tsv` | Actual Rust `exploration_phase` with two distinct item types; compares each failure loss and item bounds across a two-candidate pool, including the sampled higher-loss candidate and subsequent pair-swap state |
| Exploration contained-item relocation | `Tests/Fixtures/ExplorationContained.json`, `Tests/Fixtures/ExplorationContainedRustTrace.tsv` | Actual Rust `exploration_phase` with three rectangle item types; compares three failed states, losses, and placements, including POI-driven child relocation into a swapped large item's vacated space |
| Compression and end-to-end phase regression | `Tests/Fixtures/PhaseReference.tsv` | C++ phase transitions, strip width, density, and deterministic termination path; not Rust differential evidence |

The `*CppReference.tsv` files are locked C++ outputs for behavior that depends on the
JaguarCore geometry bridge. They are C++ regressions, not Rust differential evidence.

`PhaseReference.tsv`, `SeparatorReference.tsv`, and `LbfSearchReference.tsv` are C++ regression
baselines. `ExplorationRustTrace.tsv` checks deterministic Exploration boundaries, failed-pool
termination, and the single-item disruption early return. `ExplorationPoolRustTrace.tsv` separately
checks the fixed two-item weighted-pool and pair-swap path. `ExplorationContainedRustTrace.tsv`
checks one three-item case where a swapped large item contains the child's POI and the child is
relocated into the host's vacated space. `SeparatorRoundRustTrace.tsv`, `SeparatorMultiWorkerRoundRustTrace.tsv`, and
`SeparatorThreeWorkerSeed43RoundRustTrace.tsv` are Rust differential evidence for complete one-,
two-, and three-worker separator rounds, including best-worker selection, no-improvement termination,
and rollback with accumulated item/pair weights. Two independent Rust generations of each multi-worker
fixture produced identical traces; this is a fixture-specific determinism check, not a claim about
arbitrary workloads or worker counts.
`LbfMetricsReference.tsv` is retained from the earlier `jagua-rs/lbf` work; the C++ test now
consumes and validates its schema, provenance, and known rows, but does not compare those values
to SparrowCore because the source algorithm differs.

## Acceptance rules

The direct Rust/C++ checks cover raw and `f32` RNG values, bounded integer draws for the seeds
and bounds listed above, the eight-sample sampler trace, the full coordinate-descent candidate
sequence against a deterministic synthetic evaluator, one-item LBF initial construction, and a
real container-wide LBF placement search (`seed=42`, width 10, 16 container samples, 2 retained
samples, no reference placement), plus a real separator single-move search (`seed=42`, width 8,
two overlapping 2-by-2 items, 8 focused samples, 16 container samples, 2 retained samples,
Rust-default CDE/surrogate settings matched in C++). For direct search, evaluation/report event order, phase,
evaluation kind, acceptance, retained candidate order, and evaluation count must match; transforms
use absolute tolerance `2e-5` and finite losses `2e-3`. The separator trace covers the focused
reference-placement branch and final refined return semantics for one item move. The Exploration
trace checks two feasible paths: two shrink attempts from width 6.0 with step 0.1, and a width-4.0
case whose 0.6 shrink would cross the minimum-width bound. A third case shrinks from width 6.0 to
3.5, then stops after two failed attempts; its one placed item exercises the disruption early return.
All cases compare the best solution's width, density, feasibility, report types, separator progress
widths, and placed-item bounds. A zero strike limit means no separator movement rounds, isolating
phase transitions and failure-pool limits from separator sampling and worker RNG behavior. The
separate weighted-pool fixture uses `(seed=43, initial width=8, shrink step=0.1875, pool stddev=1)`
with two item types and checks all four Rust failure reports: collision loss within `1e-3`, each
item's bounding box within `1e-4`, and the trace's low-/high-loss candidate sequence and later
pair-swap state. The contained-relocation fixture uses `(seed=43, initial width=10, shrink step=0.1,
iteration limit=3, pool stddev=0.25)`, zero separator strikes, and rectangle sizes 5x2, 3x2, and
1x1. It compares all three failure losses within `1e-3` and all item bounds within `1e-4`; the
second failure verifies the POI-contained child is relocated 4.3 units into the host's vacated
space. These checks are fixture-specific and do not establish parity for other contained layouts
or arbitrary disruption settings.
The three separate
round fixtures run actual Rust `Separator::separate` with width 9, one container sample, no focused
samples, one coordinate descent, and a one-no-improvement limit: `(seed=42, n_workers=1)`,
`(seed=42, n_workers=2)`, and `(seed=43, n_workers=3)`. Each compares all progress events and reports,
summary counters, final solution, per-item losses, and pair losses/weights after rollback.
The C++ worker shuffle follows rand 0.10's increasing-uniform `SliceRandom` path, and `SampleEval`
ordering follows Rust `float-cmp`'s default `f32::EPSILON` plus four-ULP equality margin. The round
tests use the same input and configuration except for `n_workers=1` or `n_workers=2`; each uses the
Rust-reported iteration count as the C++ iteration cap so both stop after the same outer attempt.
The sampler/CDE trace allows absolute transform error up to `1e-6` and loss error up to `1e-5`;
evaluation kind and count must match. The LBF test
requires exact seed, placed count, remaining demand, and feasibility; it allows absolute width
error below `4e-3` and density error below `1e-3`, covering two final coordinate-descent terminal
steps for the two-unit item. These tolerances are specific to their fixtures. The Exploration trace
uses `1e-5` absolute tolerance for widths, density, and placed-item bounds; reports, feasibility,
and event counts must match exactly. Compression and end-to-end phase comparisons remain C++-only
regressions and do not establish Rust parity for those paths.

The final native acceptance path also validates both `jaguar::ValidateSPSolution` and
`jaguar::Layout::IsFeasible`. The CLI and WASM boundaries additionally exercise JSON import,
optimization, solution export, SVG export, buffer-capacity errors, and empty-input errors.

## Re-running the gate

Build and run the native Debug and Release test runners from a VS2022 x64 developer command
prompt. Use the same MSVC toolset for SparrowCore and JaguarCore; in this checkout, mixing
VS2019 and VS2022 objects caused linker failures. Replace `<repo>` below with the repository
root. Run the Release library build before its test build, then repeat for Debug:

```bat
cd /d <repo>\SparrowCore\build\build_Kit_MSVC_2019_x64-Release
nmake /f Makefile.Release
cd /d <repo>\SparrowCore\build\tests-rust-equivalence-release
nmake /f Makefile.Release
set "PATH=<repo>\JaguarCore\build\build_Kit_MSVC_2019_x64-Release\bin;%PATH%"
release\SparrowCoreTests.exe

cd /d <repo>\SparrowCore\build\build_Kit_MSVC_2019_x64-Debug
nmake /f Makefile.Debug
cd /d <repo>\SparrowCore\build\tests-rust-equivalence-debug
nmake /f Makefile.Debug
set "PATH=<repo>\JaguarCore\build\build_Kit_MSVC_2019_x64-Debug\bin;%PATH%"
debug\SparrowCoreTests.exe
```

Run the CLI smoke fixture twice with the same seed and compare the JSON solution after removing
the nondeterministic timing line from stdout. Run `SparrowCoreBench.exe` twice and compare its
checksum, validity flags, and collision counters; elapsed time is informational only.

From the repository root in a PowerShell session with Rust installed, run the direct oracle:

```powershell
cargo test --locked --manifest-path SparrowCore/Tests/RustReference/Cargo.toml
cargo run --locked --release --manifest-path SparrowCore/Tests/RustReference/Cargo.toml -- SparrowCore/Tests/Fixtures/RustSppSingleItem.json SparrowCore/Tests/Fixtures/SparrowLbfRustReference.tsv SparrowCore/Tests/Fixtures/LbfSeed42Reference.tsv SparrowCore/Tests/Fixtures/SamplingCoordinateDescentRustReference.tsv SparrowCore/Tests/Fixtures/SparrowUniformIndexRustReference.tsv SparrowCore/Tests/Fixtures/LbfSearchRustTrace.tsv SparrowCore/Tests/Fixtures/SeparatorRustTrace.json SparrowCore/Tests/Fixtures/SeparatorSearchRustTrace.tsv SparrowCore/Tests/Fixtures/SeparatorRoundRustTrace.json SparrowCore/Tests/Fixtures/SeparatorRoundRustTrace.tsv SparrowCore/Tests/Fixtures/SeparatorMultiWorkerRoundRustTrace.tsv SparrowCore/Tests/Fixtures/SeparatorThreeWorkerSeed43RoundRustTrace.tsv SparrowCore/Tests/Fixtures/ExplorationRustTrace.tsv SparrowCore/Tests/Fixtures/ExplorationPool.json SparrowCore/Tests/Fixtures/ExplorationPoolRustTrace.tsv SparrowCore/Tests/Fixtures/ExplorationContained.json SparrowCore/Tests/Fixtures/ExplorationContainedRustTrace.tsv
```

The run writes the Rust-derived LBF, RNG, sampler/CDE, bounded-integer, direct LBF search,
single-move and one-/two-/three-worker-round separator, and all three Exploration phase fixtures. Keep the
input, seed, sample configuration, Cargo lockfile, matched CDE settings, and tolerances together
when changing outputs. The native Debug and Release test runners remain separate gates. Beyond the
verified container-wide LBF path, checked separator rounds, and the tested Exploration boundary,
failure-limit, fixed weighted-pool, pair-swap, and one fixed contained-item relocation scenario,
other contained-layout geometries, arbitrary disruption settings, compression, worker counts above
three, and full end-to-end parity remain unverified; bit-identical optimizer output is not claimed.
