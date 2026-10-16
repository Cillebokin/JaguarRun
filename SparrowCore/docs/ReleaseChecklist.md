# SparrowCore release checklist

- Build JaguarCore and SparrowCore in Release mode with the supported MSVC/Qt toolchain.
- Run `SparrowCoreTests.exe` with the matching JaguarCore DLL on `PATH`.
- Run `SparrowCoreBench all` twice with the same seed; retain both CSV lines and confirm the checksum is stable.
  Pass criterion: both runs produce identical `solution_checksum` values on the same machine and toolchain.
  The checksum depends on the platform (MSVC version, CPU features such as AVX2, and the Release/Debug
  profile), so it must be compared only against a checksum recorded on the same platform, never across
  platforms.
- Bench columns after `elapsed_ms` are `final_valid_rate`, per-sample `surrogate_rejects`,
  per-sample `exact_collisions`, `exact_collision_layout`, and `solution_checksum`.
  `surrogate_rejects` counts fail-fast surrogate hits; exact polygon collision remains the
  final decision, so a surrogate hit can still be an exact false positive.
- Build and smoke-test `SparrowCli` with JSON output, `--time-ms`, and `--svg` on a representative external SPP JSON instance.
  Pass criterion: exit code 0 and output JSON that parses. Also confirm that a malformed instance returns 4,
  a missing input returns 3, and a missing `--output` returns 2 (see `Deployment.md` for the full table).
- Ctrl-C probe on Windows: start `SparrowCli` on a long `--time-ms` run, send Ctrl-C, and confirm the process
  stops cooperatively (exit code 0 with a valid output file, or the documented stop reason) within a bounded
  time. Pass criterion: the process exits without a crash dialog and the output file is valid JSON.
- C++ vs Rust comparison: run the same instance through the Rust Sparrow reference (see `SparrowCore/tests/RustReference`)
  and the C++ CLI with the same seed and effective configuration. Discrete outputs (stop reason, phase
  sequence, accepted counts) must match exactly; floating-point fields must stay within the per-field
  tolerance recorded in `docs/Differential.md`. Until the oracle toolchain is unified (E4), mark this
  item as "not run" rather than "passed".
- Build the WASM target with Emscripten and verify both `SparrowOptimizeJson` and `SparrowOptimizeSvg`, including a buffer-too-small case.
  Pass criterion: `TestWasm.cpp` passes, including the malformed-input cases that must return 2 with an
  `{"error":...}` payload, and the empty-input case that must return 1.
- Serve `Demo/Index.html` and verify file selection, missing bridge messaging, successful JSON display, and SVG preview.
  Pass criterion: `Run SparrowCore` calls `optimizeJson` exactly once; `Export SVG preview` is a separate action;
  an `{"error":...}` payload or non-JSON text is shown as a failure, not as a result.
- Confirm public headers use `Include/SparrowCore/...` paths and no stale lowercase source/include paths remain.
- Verify the final solution through `jaguar::ValidateSPSolution` before publication.
- Verify the final layout through `Layout::IsFeasible`; structural snapshot validation alone is not a geometry acceptance check.
