# JaguarCore deployment

## API v2 migration

The public C++ API is currently version `2`. The naming and error-handling
changes are source-compatible only after the caller is migrated; old lowercase
API names and lowercase include paths are not compatibility aliases. Use
`docs/jaguarcore-api-v2-migration.md` for the complete mapping and the required
`Result<T>` handling pattern.

## Reproducible release matrix

From the repository root, run the complete clean release gate with one
command from an MSVC2019 x64-capable PowerShell environment:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\JaguarCore\Package\BuildReleaseMatrix.ps1
```

The script builds and tests Debug/Release × AVX2/scalar in isolated
out-of-source directories, installs the four package variants, compiles a
consumer against only the selected package headers/import library, runs the
matching runtime checker, and finishes with `VerifyPackage.ps1`.

The default tool and output locations are:

- Qt qmake: `D:\MySoftwares\Tools\Qt\Qt5.12.10\5.12.10\msvc2017_64\bin`
- MSVC environment: `D:\MySoftwares\Tools\VisualStudio\VS2019\Professional\VC\Auxiliary\Build\vcvars64.bat`
- Build root: `D:\MyWorks\ProQt\build-JaguarCore-matrix`
- Package root: `D:\MyWorks\ProQt\package-JaguarCore-0.1.0-msvc2019-x64`

Override any location or version contract explicitly when needed:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\JaguarCore\Package\BuildReleaseMatrix.ps1 `
  -QtBin 'D:\path\to\Qt\bin' `
  -VcVars 'D:\path\to\vcvars64.bat' `
  -BuildRoot 'D:\path\to\build-root' `
  -PackageRoot 'D:\path\to\package-root' `
  -ExpectedPublicVersion '0.1.0-dev' `
  -ExpectedFileVersion '0.1.0.0' `
  -ExpectedApiVersion 2
```

For a clean package assembly, use a new or empty package root and add
`-RequireEmptyPackageRoot`. This is a non-destructive guard: it refuses to
overwrite or mix an existing package instead of deleting it.

Run the source/API contract check independently with:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\JaguarCore\Package\VerifySourceContract.ps1
```

Use a neutral build-root name without `debug` or `release`; Qt 5.12 qmake
rewrites shadow-build output paths when those tokens appear in the physical
path. The script creates a unique `run-*` child and does not delete the build
root. Its successful end markers are `package_check,result,pass` and
`release_matrix,result,pass`.

## Supported binary contract

The current binary package target is Windows x64, MSVC2019, C++17, and the
dynamic MSVC runtime. Debug and Release are separate binary contracts:

| Package | DLL / LIB | CRT | Intended use |
|---|---|---|---|
| Debug | `JaguarCored.dll` / `JaguarCored.lib` | `/MDd` | development machines only |
| Release | `JaguarCore.dll` / `JaguarCore.lib` | `/MD` | deployment |

The qmake projects reject non-MSVC, non-x64, and non-MSVC2019 toolchains before
generating a package with the `msvc2019-x64` label.

The public C++ API contains STL value types. The caller must therefore use the
same architecture, MSVC toolset family, runtime configuration, C++ standard,
and compatible `_ITERATOR_DEBUG_LEVEL`. Debug and Release libraries must never
be mixed.

JaguarCore does not require Qt or a Rust runtime. A Release deployment requires
the Microsoft Visual C++ 2015-2022 Redistributable (x64). Debug CRT DLLs are
provided by Visual Studio and must not be redistributed as an application
runtime.

## Package variants

The default build is the AVX2-capable variant. Its AVX2 code is isolated and
runtime-dispatched; `Auto` falls back to scalar execution when AVX2 is not
available. For the widest deployment compatibility, build the scalar variant
with `CONFIG+=jaguarcore_no_avx2`.

AVX2 and scalar variants intentionally use the same DLL and LIB names. Install
only the selected variant into an application's runtime directory.

## API and version contract

JaguarCore exposes three independent compatibility values. They must not be
used interchangeably:

| Value | Meaning |
|---|---|
| `JaguarCoreApiVersion()` | Stable C-linkage API probe, returning `std::uint32_t` value `2` |
| `JaguarCoreVersion()` / `jaguar::Version()` | Public product version string, currently `0.1.0-dev` |
| PE fixed file version | Numeric file version, currently `0.1.0.0` |

The API probe identifies the frozen C++ API contract; it is distinct from the
development suffix in the public product version and from the numeric PE file
version used for deployment checks. The package name's `0.1.0` base is also a
packaging identifier, not a substitute for the API probe.

The fallible public operations at this API boundary have these signatures:

```cpp
Result<Surrogate> Surrogate::Transformed(const Transform2d& transform) const;

Result<bool> CollisionEngine::DetectSurrogateCollision(
    const Surrogate&, const Transform2d&, const HazardFilter&) const;

Result<bool> CollisionEngine::CollectSurrogateCollisionsUntil(
    const Surrogate&, const Transform2d&, HazardCollector&,
    const HazardFilter&, const CollisionStopCallback&) const;

Result<bool> CollisionEngine::CollectSurrogateCollisionsUntil(
    const Surrogate&, const Transform2d&, HazardCollector&,
    const CollisionStopCallback&) const;

Result<std::vector<std::uint8_t>>
CollisionEngine::DetectPolygonCollisionsParallel(
    const std::vector<SimplePolygon>&, const HazardFilter&,
    const BatchCollisionOptions&) const;
```

Callers must check `HasValue()` before reading `Value()` and propagate or
handle `GetError()` otherwise. Low-level `Transform2d` math remains value-level:
its construction and operations such as `Apply()`, `Inverse()`, and
`FollowedBy()` do not become `Result` APIs. Fallible geometry and collision
operations validate their inputs at their public boundary. Non-finite
transforms and invalid batch options use `ErrorCode::InvalidInput`; in
particular, `BatchCollisionOptions::grainSize == 0` is invalid while
`workerCount == 0` retains automatic worker selection.

## Reproducible package installation

Run these commands from four separate out-of-source build directories in an
MSVC2019 x64 developer environment. Replace `<source-root>` and use the same
absolute package root for every configuration:

```text
qmake <source-root>\JaguarCore\JaguarCore.pro CONFIG+=debug JAGUARCORE_PACKAGE_ROOT=<package-root>
nmake
nmake install

qmake <source-root>\JaguarCore\JaguarCore.pro CONFIG+=release JAGUARCORE_PACKAGE_ROOT=<package-root>
nmake
nmake install

qmake <source-root>\JaguarCore\JaguarCore.pro CONFIG+=debug CONFIG+=jaguarcore_no_avx2 JAGUARCORE_PACKAGE_ROOT=<package-root>
nmake
nmake install

qmake <source-root>\JaguarCore\JaguarCore.pro CONFIG+=release CONFIG+=jaguarcore_no_avx2 JAGUARCORE_PACKAGE_ROOT=<package-root>
nmake
nmake install
```

Build and install the matching runtime checkers:

```text
qmake <source-root>\JaguarCore\Package\JaguarCoreRuntimeCheck.pro CONFIG+=debug JAGUARCORE_PACKAGE_ROOT=<package-root>
nmake
nmake install

qmake <source-root>\JaguarCore\Package\JaguarCoreRuntimeCheck.pro CONFIG+=release JAGUARCORE_PACKAGE_ROOT=<package-root>
nmake
nmake install
```

## Runtime check

The package checker does not link to JaguarCore. It opens the requested DLL by
absolute path and verifies:

1. the PE machine is AMD64;
2. the fixed file version equals the required value (`0.1.0.0` currently);
3. `LoadLibraryExW` succeeds;
4. the stable C-linkage `JaguarCoreVersion` export exists;
5. the exported public version equals the expected value (`0.1.0-dev` currently);
6. the stable C-linkage `JaguarCoreApiVersion` export exists and equals `2`;
7. the target DLL's PE import table proves `/MDd` Debug or `/MD` Release CRT use
   without relying on CRT modules loaded by the checker itself.

Example:

```text
Debug/Tools/JaguarCoreRuntimeCheck.exe Debug/AVX2/Bin/JaguarCored.dll 0.1.0-dev 0.1.0.0 2 Debug
Release/Tools/JaguarCoreRuntimeCheck.exe Release/Scalar/Bin/JaguarCore.dll 0.1.0-dev 0.1.0.0 2 Release
```

The checker must match the DLL's Debug/Release configuration. An exit code of
zero together with `runtime_check,result,pass` is the acceptance condition.
All arguments are mandatory so version validation cannot be skipped.

After installing all four configurations, run the package-level acceptance
script from an MSVC2019 x64 developer prompt:

```text
powershell.exe -NoProfile -ExecutionPolicy Bypass -File <package-root>\VerifyPackage.ps1 <package-root> 0.1.0-dev 0.1.0.0 2
```

It validates the package layout and runs the correct checker against all four
installed DLLs. Success ends with `package_check,result,pass`.

The package verifier also rejects non-conforming public header names and any
Qt or Rust runtime DLL/import-library artifacts. The API v2 migration guide is
installed as `Docs/jaguarcore-api-v2-migration.md` beside this document.

## Ownership and ABI rules

- `JaguarCoreVersion()` and `jaguar::Version()` return pointers to DLL-owned,
  immutable static strings. The caller must not free or modify them, and the
  pointers become invalid immediately after `JaguarCore.dll` is unloaded.
- `SimplePolygon::GetSurrogate()` returns a borrowed pointer owned by the polygon.
  It becomes invalid when the polygon is destroyed or replaced and must not be
  deleted by the caller.
- Public raw pointers do not transfer ownership. Internal STL storage is not
  returned as an owning raw pointer.
- `CollisionEnginePtr` uses the exported `CollisionEngineDeleter`; a
  DLL-created engine is therefore destroyed and deallocated inside JaguarCore,
  not by the caller's delete operator.
- Values returned by value are used only with the matching MSVC2019 x64 CRT
  contract described above.
- Do not pass JaguarCore objects across modules built with a different CRT,
  compiler ABI, architecture, or Debug/Release mode.
- The stable C export is a deployment probe, not a C replacement for the C++
  geometry API. The C++ ABI is not promised across compiler/toolset upgrades.
- Const collision queries may run concurrently on one `CollisionEngine`. The
  `HazardFilter` used by a parallel query may be invoked concurrently from
  multiple worker threads, so captured filter state must be safe for concurrent
  invocation.
- `RegisterHazard`, `DeregisterHazard`, and `Restore` mutate engine state. The
  caller must externally serialize each mutation against all queries and other
  mutations on the same engine.

## Pre-deployment acceptance

For the complete release matrix:

1. build JaguarCore and its import library from a clean out-of-source directory;
2. build and run the matching complete `JaguarCoreTests` executable;
3. install all four package variants and run `VerifyPackage.ps1`;
4. use `dumpbin /DEPENDENTS` to confirm the expected CRT family and no Qt/Rust dependency;
5. use `dumpbin /EXPORTS` to confirm `JaguarCoreVersion` and
   `JaguarCoreApiVersion` are exported without C++ name mangling;
6. launch the real consumer with only the selected package `Bin` directory on its DLL search path.

The package consumer smoke test follows the same isolation rule: it includes
only `package-root\Include`, links only the selected `Lib` directory, and runs
with only the selected `Bin` directory prepended to `PATH`. AVX2 and scalar
variants must use separate runtime directories because their DLL and import
library names are intentionally identical.
