# SparrowCore release checklist

- Build JaguarCore and SparrowCore in Release mode with the supported MSVC/Qt toolchain.
- Run `SparrowCoreTests.exe` with the matching JaguarCore DLL on `PATH`.
- Run `SparrowCoreBench all` twice with the same seed; retain both CSV lines and confirm the checksum is stable.
- Bench columns after `elapsed_ms` are `final_valid_rate`, per-sample `surrogate_rejects`,
  per-sample `exact_collisions`, `exact_collision_layout`, and `solution_checksum`.
  `surrogate_rejects` counts fail-fast surrogate hits; exact polygon collision remains the
  final decision, so a surrogate hit can still be an exact false positive.
- Build and smoke-test `SparrowCli` with JSON output, `--time-ms`, and `--svg` on a representative external SPP JSON instance.
- Build the WASM target with Emscripten and verify both `SparrowOptimizeJson` and `SparrowOptimizeSvg`, including a buffer-too-small case.
- Serve `Demo/Index.html` and verify file selection, missing bridge messaging, successful JSON display, and SVG preview.
- Confirm public headers use `Include/SparrowCore/...` paths and no stale lowercase source/include paths remain.
- Verify the final solution through `jaguar::ValidateSPSolution` before publication.
- Verify the final layout through `Layout::IsFeasible`; structural snapshot validation alone is not a geometry acceptance check.
