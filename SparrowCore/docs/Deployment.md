# SparrowCore deployment

## MSVC/qmake

Build JaguarCore first, then SparrowCore, then the optional tools. The project is tested with Qt 5.12.10 and MSVC 2019 x64.

```powershell
qmake SparrowCore/SparrowCore.pro CONFIG+=release SPARROWCORE_BUILD_DIR=<sparrow-build-dir>
nmake /nologo
qmake SparrowCore/Tools/SparrowCli/SparrowCli.pro CONFIG+=release SPARROWCORE_BUILD_DIR=<sparrow-build-dir> JAGUARCORE_BUILD_DIR=<jaguar-build-dir>
nmake /nologo
```

Run the CLI with an external Jaguar SPP instance. By default, it writes `output/final_<instance-name>.json` and `.svg`; both paths can be overridden. The JSON contains flat instance fields and a nested `solution` object, so it can be passed back as `--input` for a warm-start run:

```text
SparrowCli -i instance.json -t 600 -s 42 --workers 3
SparrowCli -i instance.json --output solution.json --svg layout.svg \
  --seed 42 --samples 1000 --time-ms 2000 --early-termination
```

With no time option, the CLI uses 480 seconds for exploration and 120 seconds for compression. `-t/--global-time` splits a total number of seconds 80/20; `-e/--exploration` and `-c/--compression` set both phase limits directly and must be supplied together. `--time-ms` remains a C++ global hard-time-limit extension and cannot be combined with those Rust-style time options. `--help` prints the complete option summary.

To warm-start from the generated JSON, pass it back as `--input`; omit `--output` to write the next result under its instance-derived default name, or provide a new override path.

CLI exit codes: `0` success; `2` usage error; `3` input file unreadable; `4` instance import failed; `5` optimization failed; `6` solution conversion failed; `7` solution serialization failed; `8` output write failed; `9` layout restore, SVG export or SVG write failed; `10` an unexpected exception escaped the library layer (reported on stderr).

`SparrowCoreBench all` runs the fixed CSV/checksum regressions. The statistical benchmark accepts any SPP JSON instance, a total time limit in seconds, and a positive run count: `SparrowCoreBench <input.json> <time-limit-seconds> <runs>`. It writes one SVG per run plus the best-run SVG under `output/`, then prints width and usage distributions.
Like the pinned Rust Bench, it runs a full batch for the final partial batch, so `actual_runs` may exceed the requested `<runs>`; both counts are printed. Batch size is estimated from `std::thread::hardware_concurrency()`, while Rust uses physical-core count, so the overshoot amount can differ across hosts.

On `SIGINT`/Ctrl-C, SparrowCli requests a cooperative stop for the active phase. The optimizer
finishes through its normal phase handoff and final-result path; a later phase clears that
phase-scoped interrupt, matching Sparrow's `CtrlCTerminator` behavior. Native C++ hosts can pass
an `OptimizationTerminator` to `Optimize` and call `RequestStop()` from their cancellation thread
to cancel across phases, or `RequestPhaseStop()` to stop only the current phase. The terminator
must outlive the synchronous `Optimize` call. The WASM C ABI remains synchronous and exposes no
mid-call cancellation handle. The current adapter sets `terminator.maxIterations` to `64`; this is
an internal termination setting, not caller-triggered cancellation. Rust excludes
`ctrlc_terminator` on `wasm32`. Adding cancellation during a WASM call would require a separate
ABI and host concurrency contract.

## WASM

The WASM entry point is `Tools/SparrowWasm/SparrowWasm.cpp`. It exports:

```text
int SparrowOptimizeJson(const char* inputJson,
                        size_t inputSize,
                        char* outputJson,
                        size_t outputCapacity)

int SparrowOptimizeSvg(const char* inputJson,
                       size_t inputSize,
                       char* outputSvg,
                       size_t outputCapacity)

int SparrowOptimizeWithSvg(const char* inputJson,
                           size_t inputSize,
                           char** outputBuffer,
                           size_t* outputJsonSize,
                           size_t* outputSvgSize)

void SparrowFreeOutput(char* outputBuffer)
```

`SparrowOptimizeWithSvg` runs `Optimize` once and returns a malloc-owned
`JSON\0SVG\0` buffer; the two lengths exclude their NUL terminators. Release
the returned buffer with `SparrowFreeOutput`. The existing fixed-buffer JSON
and SVG functions remain available. The combined entry publishes phase,
solution, separation, and compression events through the worker's message
channel; non-Emscripten builds keep those callbacks inert.

With an Emscripten toolchain, CMake builds the SparrowCore and JaguarCore source files by default. The target compiles and links with `-pthread` and prewarms three Pthread workers to match the current WASM configuration. Build it from the repository root:

```text
emcmake cmake -S SparrowCore/Tools/SparrowWasm -B build-wasm \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-wasm --parallel
```

The build writes `sparrow_wasm.js` and `sparrow_wasm.wasm` into the build directory. Existing Emscripten-compatible static archives can be supplied with `SPARROWCORE_LIBRARY` and `JAGUARCORE_LIBRARY`; otherwise the in-tree sources are compiled. JaguarCore's x86 AVX2 implementation is excluded and its portable fallback is used.

The fixed-buffer return code is `0` for success, `1` only when the input is null or empty, `2` for any failure after input is accepted (JSON import/parse failure, optimization failure, layout restore, SVG export or serialization failure) and `3` when the output buffer is null or too small. The combined function uses the same `0/1/2` input and optimization outcomes; `3` means an invalid output pointer or allocation failure. On a non-zero status with a valid combined-output pointer, the JSON segment contains `{"error":"<message>"}` and the SVG segment is empty. Any C++ exception raised inside an export is caught at the C ABI boundary and reported as code `2`.

## Browser demo

Copy the Emscripten-generated `sparrow_wasm.js` and `sparrow_wasm.wasm` beside the files in
`SparrowCore/Demo`, then serve that directory over HTTP. `SparrowWorker.js`
loads the module in a Web Worker and calls `SparrowOptimizeWithSvg`; the JSON
solution and SVG preview therefore come from the same optimization run. The
Demo displays listener-driven phase, progress, result summaries, event logs,
and throttled live SVG updates. Cancel terminates the Web Worker immediately;
it does not request a cooperative phase stop and does not return a partial
solution. The browser Demo currently has no cooperative WASM cancellation
contract.

Because the WASM build uses Pthreads and shared memory, serve the Demo with
`Cross-Origin-Opener-Policy: same-origin` and
`Cross-Origin-Embedder-Policy: require-corp`. Serve JavaScript as
`text/javascript` and the WASM binary as `application/wasm`. Direct `file://`
loading is not supported for this threaded build.
