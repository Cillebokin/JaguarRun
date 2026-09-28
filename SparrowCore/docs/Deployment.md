# SparrowCore deployment

## MSVC/qmake

Build JaguarCore first, then SparrowCore, then the optional tools. The project is tested with Qt 5.12.10 and MSVC 2019 x64.

```powershell
qmake SparrowCore/SparrowCore.pro CONFIG+=release SPARROWCORE_BUILD_DIR=D:/build/SparrowCore
nmake /nologo
qmake SparrowCore/Tools/SparrowCli/SparrowCli.pro CONFIG+=release SPARROWCORE_BUILD_DIR=D:/build/SparrowCore JAGUARCORE_BUILD_DIR=D:/build/JaguarCore
nmake /nologo
```

Run the CLI with an external Jaguar SPP instance:

```text
SparrowCli --input instance.json --output solution.json --svg layout.svg \
  --seed 42 --samples 1000 --time-ms 2000 --early-termination
```

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

The return code is `0` for success, `1` for malformed input, `2` for optimization/serialization failure and `3` when the output buffer is too small.

## Browser demo

Serve `SparrowCore/Demo` from a local HTTP server and load the Emscripten-generated bridge as
`window.SparrowWasm.optimizeJson` and `window.SparrowWasm.optimizeSvg`. The JSON result is
shown as text and the SVG result is shown as the layout preview. `Demo/Index.html` intentionally
has no network dependency and can be hosted as static files.
