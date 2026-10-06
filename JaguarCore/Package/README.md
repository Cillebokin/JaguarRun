# JaguarCore MSVC2019 x64 package

This package contains the public C++ headers, JaguarCore DLL/import library
pairs, the standalone runtime checker, the deployment guide, and the API v2
migration guide. It does not contain Qt or Rust runtime components.

## Layout

```text
JaguarCore-0.1.0-msvc2019-x64/
  README.md
  VerifyPackage.ps1
  Docs/deployment.md
  Docs/jaguarcore-api-v2-migration.md
  Include/JaguarCore/**/*.h
  Debug/
    AVX2/Bin/JaguarCored.dll
    AVX2/Lib/JaguarCored.lib
    Scalar/Bin/JaguarCored.dll
    Scalar/Lib/JaguarCored.lib
    Tools/JaguarCoreRuntimeCheck.exe
  Release/
    AVX2/Bin/JaguarCore.dll
    AVX2/Lib/JaguarCore.lib
    Scalar/Bin/JaguarCore.dll
    Scalar/Lib/JaguarCore.lib
    Tools/JaguarCoreRuntimeCheck.exe
```

Use exactly one matching configuration and CPU variant in an application.
Do not put both AVX2 and scalar DLLs with the same file name in one runtime
directory.

## Reproducible release matrix

From the repository root, build and validate the complete matrix with:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\JaguarCore\Package\BuildReleaseMatrix.ps1
```

The script creates isolated Debug/Release × AVX2/scalar build directories,
assembles the package, compiles the consumer smoke test from package headers
and import libraries only, runs the matching DLL checker, and invokes
`VerifyPackage.ps1`. Override `-QtBin`, `-VcVars`, `-BuildRoot`,
`-PackageRoot`, `-ExpectedPublicVersion`, `-ExpectedFileVersion`, or
`-ExpectedApiVersion` when using a different toolchain or release contract.
Pass `-RequireEmptyPackageRoot` when the package root must be a newly created
or empty directory; the script then refuses to mix old artifacts with the
current release.
Keep the physical build-root path free of `debug` and `release` tokens because
Qt 5.12 qmake rewrites shadow-build output paths containing those tokens.

Before compiling, the matrix invokes `VerifySourceContract.ps1`. It checks the
33-header public surface, uppercase code paths and filenames, public API
naming, and the project-level no-exception/no-Qt-object conventions.

## Runtime validation

Run the checker built for the same Debug/Release configuration as the DLL:

```text
JaguarCoreRuntimeCheck.exe <absolute-dll-path> 0.1.0-dev 0.1.0.0 2 Debug
JaguarCoreRuntimeCheck.exe <absolute-dll-path> 0.1.0-dev 0.1.0.0 2 Release
```

It verifies the PE architecture, exact numeric file version, target DLL import
table, stable `JaguarCoreVersion` export, exact public version string, loader
behavior, and the MSVC CRT family. A successful run ends with:

```text
runtime_check,result,pass
```

From an MSVC2019 x64 developer prompt, validate the complete installed matrix
with:

```text
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\VerifyPackage.ps1 . 0.1.0-dev 0.1.0.0 2
```

The matrix check ends with `package_check,result,pass`.

## API and version contract

The independent API probe is the stable C-linkage export
`JaguarCoreApiVersion()`. It returns `std::uint32_t` value `2`. This API
version is distinct from the public product version string returned by
`JaguarCoreVersion()` and `jaguar::Version()` (`0.1.0-dev`) and from the PE
fixed file version (`0.1.0.0`). The package name's `0.1.0` base is a packaging
identifier and is not the API version.

The runtime checker validates these version dimensions independently: the
public product version string, the PE fixed file version, and the API version.
Changing one does not silently change the others.

The fallible public signatures are:

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

Check `HasValue()` before reading a `Result` with `Value()` and handle or
forward `GetError()` otherwise. Low-level `Transform2d` construction and math
remain value-level operations. Fallible geometry and collision operations
validate inputs at their public boundary: non-finite transforms and invalid
batch options return `ErrorCode::InvalidInput`; `grainSize == 0` is invalid,
while `workerCount == 0` selects workers automatically.

## Ownership and concurrency

- `JaguarCoreVersion()` and `jaguar::Version()` return immutable DLL-owned
  strings. Do not free or modify them; their pointers become invalid after the
  DLL is unloaded.
- `SimplePolygon::GetSurrogate()` returns a borrowed pointer owned by the polygon;
  do not delete it, and do not retain it after the polygon is destroyed or the
  surrogate is replaced.
- Public raw pointers do not transfer ownership. `CollisionEnginePtr` uses the
  exported `CollisionEngineDeleter`, so a DLL-created engine is destroyed and
  deallocated inside JaguarCore.
- Const queries may run concurrently on one engine. A filter used by a
  parallel query may be invoked concurrently from multiple workers, so its
  captured state must support concurrent invocation.
- `RegisterHazard`, `DeregisterHazard`, and `Restore` mutate engine state and
  require external serialization against all queries and other mutations on
  that engine.

See `Docs/deployment.md` for build, installation, dependency, ABI, ownership,
and compatibility requirements. See
`Docs/jaguarcore-api-v2-migration.md` for source-level migration details.
