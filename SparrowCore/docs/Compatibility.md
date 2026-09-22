# SparrowCore compatibility

SparrowCore is the C++/Qt-facing optimization layer in JaguarRun. JaguarCore remains the geometry, collision, snapshot, validation and JSON boundary layer.

The current SparrowCore pipeline covers the Sparrow-style SPP path:

1. LBF initial construction ordered by surrogate convex-hull area multiplied by diameter.
2. Uniform and focused placement sampling with coordinate descent.
3. Circle-pole overlap quantification, symmetric pair tracking and container penalties.
4. Candidate separation with exact JaguarCore collision confirmation, rollback and adaptive weights.
5. Exploration by repeated strip shrinking.
6. Compression with time-based or failure-based shrink decay.
7. C++ API, command-line, WASM C ABI and browser demo boundaries.

The following differences from the original `jagua-rs` Sparrow implementation are intentional in this phase:

- The C++ implementation uses JaguarCore `Result<T>` and `SPSolution` snapshots instead of Rust ownership and serde types.
- The first production pass keeps exploration disruption conservative and deterministic; it retries through Separator and preserves the best feasible snapshot.
- The public API is C++17 and uses PascalCase filenames, UpperCamelCase functions, lowercase namespaces and the `Include/Source/Tests` directory layout required by `代码规范.docx`.
- WASM packaging is source-compatible and exposes `SparrowOptimizeJson`; final Emscripten glue and host-side memory management remain deployment concerns.

Every returned solution is revalidated by JaguarCore before `Optimize` succeeds.
