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
  Pass criterion: `--help` returns 0; exit code 0 produces JSON with root instance fields and a nested `solution` object; feed that output back as `--input` for a warm-start round-trip. Confirm the default instance-derived JSON/SVG paths and explicit nested `--output`/`--svg` overrides. Also confirm invalid options return 2, a missing input file returns 3, malformed instance data returns 4, and JSON write failure returns 8 (see `Deployment.md` for the full table).
  2026-10-19 evidence: VS2019 Release build with Qt6.5.3 qmake passed for JaguarCore, SparrowCore and SparrowCli. The single-item RustReference SPP fixture passed cold-start output, warm-start re-import, instance/solution field stability, nested SVG directory creation, and exit-code checks 2/3/4; JSON write failure returned 8. The S23 follow-up confirmed `--help` writes stdout and exits 0, unknown options write stderr and exit 2, invalid time combinations and zero workers exit 2, and both default paths and explicit output overrides produce JSON/SVG. Zero and 1-second phase-budget smokes exited 0. These checks do not establish full algorithm parity.
- Run `SparrowCoreBench <input.json> <time-limit-seconds> <runs>` on a representative fixture with at least two runs.
  Pass criterion: it accepts an arbitrary SPP JSON input, prints requested and actual run counts, widths/usages plus worst, quartile, median, best, average and standard deviation, and writes one `final_bench_*.svg` per actual run plus `final_best_*.svg`. The final partial batch follows pinned Rust and may exceed the requested count. Compare statistical semantics with the Rust bench; do not treat random-seed runs as deterministic trajectory parity.
  2026-10-19 evidence: the two-run zero-second RustReference fixture smoke exited 0, printed statistics and wrote two per-run SVGs plus the best SVG; a follow-up with a 1-second total phase budget and two runs also exited 0 in about 1.03 seconds. These smokes exercise input/config/output wiring but do not establish general benchmark quality or Rust trajectory parity.
- Ctrl-C probe on Windows: start `SparrowCli` on a long `--time-ms` run, send Ctrl-C, and confirm the process
  stops cooperatively (exit code 0 with a valid output file, or the documented stop reason) within a bounded
  time. Pass criterion: the process exits without a crash dialog and the output file is valid JSON.
- C++ vs Rust comparison: run the same instance through the Rust Sparrow reference (see `SparrowCore/tests/RustReference`)
  and the C++ CLI with the same seed and effective configuration. Discrete outputs (stop reason, phase
  sequence, accepted counts) must match exactly; floating-point fields must stay within the per-field
  tolerance recorded in `docs/Differential.md`. Until the oracle toolchain is unified (E4), mark this
  item as "not run" rather than "passed".
- Build the WASM target with Emscripten and verify the fixed-buffer JSON/SVG exports and the combined
  `SparrowOptimizeWithSvg`/`SparrowFreeOutput` path, including malformed and empty input.
  Pass criterion: output lengths and `JSON\0SVG\0` offsets are correct, the combined call invokes
  Optimize once, and every non-success status follows the documented return code.
  2026-10-19 evidence: Emscripten 6.0.12 Release build passed from in-tree SparrowCore/JaguarCore sources. Node 24.19.0 API smoke passed the combined valid call (371 JSON bytes, 2722 SVG bytes, one final report), empty/malformed status 1/2, fixed-buffer JSON/SVG success, and undersized-buffer status 3. A separate fixed-buffer boundary probe passed empty and malformed JSON/SVG statuses 1/2 and their error payloads.
- Serve `Demo/Index.html` with the generated `sparrow_wasm.js/.wasm` beside the Demo files. Verify file
  selection, one-run JSON+SVG output, listener-driven phase/progress/log/live-SVG updates, and Cancel.
  Pass criterion: a single Optimize call produces the final JSON and SVG; Cancel terminates the Worker
  and clearly reports that no partial result is available.
  2026-10-19 evidence: Chrome 155.0.8059.82 passed with COOP/COEP isolation and `text/javascript` / `application/wasm` responses. `RustSppSingleItem.json` produced final JSON and SVG from one Optimize, phase/progress/log and live-SVG updates; a second `LbfSppSmall.json` run was cancelled during Exploration and produced no final JSON/SVG or late result; malformed JSON surfaced an error with controls restored. No page errors, console errors, or failed requests. Cancel remains a Worker hard stop, not a cooperative Rust phase stop.
- Confirm public headers use `Include/SparrowCore/...` paths and no stale lowercase source/include paths remain.
- Verify the final solution through `jaguar::ValidateSPSolution` before publication.
- Verify the final layout through `Layout::IsFeasible`; structural snapshot validation alone is not a geometry acceptance check.
