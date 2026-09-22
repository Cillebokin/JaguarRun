# SparrowCore release checklist

- Build JaguarCore and SparrowCore in Release mode with the supported MSVC/Qt toolchain.
- Run `SparrowCoreTests.exe` with the matching JaguarCore DLL on `PATH`.
- Run `SparrowCoreBench all` and retain the CSV line with the release artifact.
- Build and smoke-test `SparrowCli` on a representative external SPP JSON instance.
- Build the WASM target with Emscripten and verify `SparrowOptimizeJson` with a buffer-too-small case.
- Serve `Demo/Index.html` and verify file selection, missing bridge messaging and successful JSON display.
- Confirm public headers use `Include/SparrowCore/...` paths and no stale lowercase source/include paths remain.
- Verify the final solution through `jaguar::ValidateSPSolution` before publication.
