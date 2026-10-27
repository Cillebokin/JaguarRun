# Sparrow LBF WASM Demo

This page calls the standalone C++ LBF WASM entry. It accepts an SPP or BPP instance and an optional Rust-shaped LBF config. Leaving the instance blank selects the bundled `swim.json` or `baldacci1.json` reference fixture.

## Build

Use an Emscripten environment and build the `LbfWasm` target:

```powershell
emcmake cmake -S ../SparrowWasm -B build
cmake --build build --target LbfWasm --config Release
```

The generated `lbf.js`, `lbf.wasm`, and `lbf.data` files are written to `build/` and are loaded by `index.js`.

## Run

From this directory, start the server:

```powershell
python serve.py
```

Open `http://127.0.0.1:8082/`. The server sets the cross-origin isolation headers needed by WebAssembly threads.
