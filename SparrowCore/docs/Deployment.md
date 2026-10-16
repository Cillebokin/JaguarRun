# SparrowCore deployment

## MSVC/qmake

Build JaguarCore first, then SparrowCore, then the optional tools. The project is tested with Qt 5.12.10 and MSVC 2019 x64.

```powershell
qmake SparrowCore/SparrowCore.pro CONFIG+=release SPARROWCORE_BUILD_DIR=<sparrow-build-dir>
nmake /nologo
qmake SparrowCore/Tools/SparrowCli/SparrowCli.pro CONFIG+=release SPARROWCORE_BUILD_DIR=<sparrow-build-dir> JAGUARCORE_BUILD_DIR=<jaguar-build-dir>
nmake /nologo
```

Run the CLI with an external Jaguar SPP instance:

```text
SparrowCli --input instance.json --output solution.json --svg layout.svg \
  --seed 42 --samples 1000 --time-ms 2000 --early-termination
```

CLI exit codes: `0` success; `2` usage error; `3` input file unreadable; `4` instance import failed; `5` optimization failed; `6` solution conversion failed; `7` solution serialization failed; `8` output write failed; `9` layout restore, SVG export or SVG write failed; `10` an unexpected exception escaped the library layer (reported on stderr).

On `SIGINT`/Ctrl-C, SparrowCli requests a cooperative stop for the active phase. The optimizer
finishes through its normal phase handoff and final-result path; a later phase clears that
phase-scoped interrupt, matching Sparrow's `CtrlCTerminator` behavior. Native C++ hosts can pass
an `OptimizationTerminator` to `Optimize` and call `RequestStop()` from their cancellation thread
to cancel across phases, or `RequestPhaseStop()` to stop only the current phase. The terminator
must outlive the synchronous `Optimize` call. The WASM C ABI remains synchronous and exposes no
mid-call cancellation handle.

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
```

Use an Emscripten toolchain and pass the built SparrowCore and JaguarCore libraries to CMake:

```text
emcmake cmake -S SparrowCore/Tools/SparrowWasm -B build-wasm \
  -DSPARROWCORE_LIBRARY=/absolute/path/libSparrowCore.a \
  -DJAGUARCORE_LIBRARY=/absolute/path/libJaguarCore.a
cmake --build build-wasm
```

The return code is `0` for success, `1` only when the input is null or empty, `2` for any failure after input is accepted (JSON import/parse failure, optimization failure, layout restore, SVG export or serialization failure) and `3` when the output buffer is null or too small. Any C++ exception raised inside the optimizer is caught at the C ABI boundary and reported as code `2` with the payload `{"error":"internal error"}`. On a non-zero return code other than `3`, the output buffer contains `{"error":"<message>"}`.

## Browser demo

Serve `SparrowCore/Demo` from a local HTTP server and load the Emscripten-generated bridge as
`window.SparrowWasm.optimizeJson` and `window.SparrowWasm.optimizeSvg`. Run optimizes once and shows the JSON
result; the bridge must return the JSON text on success, or a `{"error":"..."}` payload on failure. The
`Export SVG preview` button is a separate request that re-runs the optimization to produce the SVG preview. `Demo/Index.html` intentionally
has no network dependency and can be hosted as static files.
