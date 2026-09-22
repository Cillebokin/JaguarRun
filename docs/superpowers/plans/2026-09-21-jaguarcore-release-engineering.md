# JaguarCore Release Engineering Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Automate clean four-configuration JaguarCore builds, package assembly, external consumer compilation, and runtime/package verification.

**Architecture:** Reuse qmake installation rules and the existing `VerifyPackage.ps1`/`JaguarCoreRuntimeCheck` checker. Add a small package consumer project that links only against the assembled package, and a PowerShell orchestrator that creates isolated out-of-source build directories, installs each variant, runs tests and consumer checks, and fails on the first broken configuration.

**Tech Stack:** PowerShell, Qt 5.12.10 qmake, MSVC2019 x64 `vcvars64.bat`, `nmake`, C++17 consumer executable, existing PE/runtime checker.

## Global Constraints

- Supported matrix is Debug/Release × AVX2/scalar on Windows x64/MSVC2019.
- Public version is `0.1.0-dev`; PE file version is `0.1.0.0`; API version is `1`.
- Package must not include Qt or Rust runtime components.
- Package assembly updates only explicit JaguarCore package directories.
- Build, test, consumer, and verifier failures must produce a non-zero script exit.
- Do not delete the workspace or broad directories and do not create Git commits.

---

### Task 1: Add an external package consumer smoke test

**Files:**
- Create: `JaguarCore/package/JaguarCoreConsumerSmoke.cpp`
- Create: `JaguarCore/package/JaguarCoreConsumerSmoke.pro`

**Interfaces:**
- Consumes: only `JAGUARCORE_PACKAGE_ROOT/include` and the selected package `lib`/`bin` directories.
- Produces: a standalone `JaguarCoreConsumerSmoke.exe` for each Debug/Release and AVX2/scalar package variant.

- [ ] **Step 1: Write the consumer source**

Use only public headers and assert the following contract:

```cpp
#include "JaguarCore/collision/collision_engine.h"
#include "JaguarCore/geometry/simple_polygon.h"
#include "JaguarCore/version.h"

#include <cassert>
#include <cmath>
#include <limits>
#include <memory>
#include <string>

int main()
{
    assert(std::string(JaguarCoreVersion()) == "0.1.0-dev");
    assert(JaguarCoreApiVersion() == 1U);

    const auto polygon = jaguar::SimplePolygon::create(
        {{0.0, 0.0}, {2.0, 0.0}, {0.0, 2.0}});
    assert(polygon.hasValue());

    const auto invalid = polygon.value().transformed(
        jaguar::Transform2d::fromRotationTranslation(
            std::numeric_limits<double>::quiet_NaN(), {0.0, 0.0}));
    assert(!invalid.hasValue());
    assert(invalid.error().code == jaguar::ErrorCode::InvalidInput);

    const auto bounds = jaguar::Rect2d::create(0.0, 0.0, 10.0, 10.0);
    assert(bounds.hasValue());
    jaguar::CollisionEngine engine(bounds.value(), jaguar::CDEConfig{});
    assert(!engine.detectPolygonCollision(polygon.value(), jaguar::NoFilter{}));
    return 0;
}
```

- [ ] **Step 2: Define the package-only qmake project**

The `.pro` file must require these qmake variables and fail early when absent:

```qmake
TEMPLATE = app
TARGET = JaguarCoreConsumerSmoke
CONFIG += console c++17 warn_on
CONFIG -= app_bundle
QT -= core gui

isEmpty(JAGUARCORE_PACKAGE_ROOT): error("JAGUARCORE_PACKAGE_ROOT is required")
isEmpty(JAGUARCORE_PACKAGE_CONFIGURATION): error("JAGUARCORE_PACKAGE_CONFIGURATION is required")
isEmpty(JAGUARCORE_PACKAGE_VARIANT): error("JAGUARCORE_PACKAGE_VARIANT is required")

INCLUDEPATH += $$JAGUARCORE_PACKAGE_ROOT/include
win32:LIBS += -L$$JAGUARCORE_PACKAGE_ROOT/$$JAGUARCORE_PACKAGE_CONFIGURATION/$$JAGUARCORE_PACKAGE_VARIANT/lib

CONFIG(debug, debug|release) {
    LIBS += -lJaguarCored
} else {
    LIBS += -lJaguarCore
}

SOURCES += $$PWD/JaguarCoreConsumerSmoke.cpp
DESTDIR = $$OUT_PWD/bin
OBJECTS_DIR = $$OUT_PWD/obj
```

- [ ] **Step 3: Build and run one package-only smoke test manually**

Configure it against the existing Debug/avx2 package, build with `nmake`, prepend `Debug\avx2\bin` to `PATH`, and run it. Expected: exit code `0` without including any source-tree path.

### Task 2: Extend runtime/package validation to API version

**Files:**
- Modify: `JaguarCore/package/JaguarCoreRuntimeCheck.cpp`
- Modify: `JaguarCore/package/VerifyPackage.ps1`
- Modify: `JaguarCore/package/README.md`

**Interfaces:**
- Consumes: exported `JaguarCoreApiVersion()` and existing checker arguments.
- Produces: a mandatory API-version runtime assertion for all four package variants.

- [ ] **Step 1: Add a failing export lookup**

In `JaguarCoreRuntimeCheck.cpp`, add a `GetProcAddress(module, "JaguarCoreApiVersion")` lookup, cast it to `std::uint32_t (*)() noexcept`, fail if absent, call it, and compare with an expected API version argument.

- [ ] **Step 2: Make the expected API version an explicit checker argument**

Extend the checker usage from:

```text
JaguarCoreRuntimeCheck.exe <dll> <public-version> <file-version> <Debug|Release>
```

to:

```text
JaguarCoreRuntimeCheck.exe <dll> <public-version> <file-version> <api-version> <Debug|Release>
```

Add a mandatory `ExpectedApiVersion` parameter at position 3 in `VerifyPackage.ps1`. Update the four checker invocations to pass `$ExpectedApiVersion` before the Debug/Release configuration, and reject missing or non-numeric API versions before loading the DLL.

- [ ] **Step 3: Run the package verifier against the existing package**

Run the verifier with the new fifth argument contract after rebuilding the checker. Expected before the core DLLs are rebuilt: the version export lookup fails if the package is stale, proving the new check is active.

- [ ] **Step 4: Update package documentation**

Document the API version argument, the expected current value `1`, and the fact that product version and API version are separate checks.

### Task 3: Implement the reproducible build matrix orchestrator

**Files:**
- Create: `JaguarCore/package/BuildReleaseMatrix.ps1`

**Interfaces:**
- Consumes: `JaguarCore.pro`, `JaguarCoreTests.pro`, `JaguarCoreRuntimeCheck.pro`, `JaguarCoreConsumerSmoke.pro`, qmake, VS x64 environment, and package verifier.
- Produces: four installed package variants and a non-zero/zero aggregate result.

- [ ] **Step 1: Define explicit script parameters and matrix data**

Define parameters with current defaults. Resolve `$repoRoot` as the parent of the `JaguarCore` directory (`Split-Path -Parent (Split-Path -Parent $PSScriptRoot)`) before evaluating the build-root default:

```powershell
param(
    [string]$QtBin = 'D:\MySoftwares\Tools\Qt\Qt5.12.10\5.12.10\msvc2017_64\bin',
    [string]$VcVars = 'D:\MySoftwares\Tools\VisualStudio\VS2022\Community\VC\Auxiliary\Build\vcvars64.bat',
    [string]$BuildRoot = (Join-Path (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))) 'build-JaguarCore-release-matrix'),
    [string]$PackageRoot = 'D:\MyWorks\ProQt\package-JaguarCore-0.1.0-msvc2019-x64',
    [string]$ExpectedPublicVersion = '0.1.0-dev',
    [string]$ExpectedFileVersion = '0.1.0.0',
    [int]$ExpectedApiVersion = 1
)
```

Use an in-script matrix with the four tuples `Debug/avx2`, `Debug/scalar`, `Release/avx2`, and `Release/scalar`. Derive the scalar qmake flag only for scalar tuples.

- [ ] **Step 2: Import the VS developer environment safely**

Run the supplied `vcvars64.bat` through `cmd.exe`, parse its `set` output, and copy only environment entries into the current PowerShell process. Resolve qmake by joining `$QtBin` with `qmake.exe`; fail with a clear path error if either tool is missing.

- [ ] **Step 3: Configure and build each core variant in an isolated directory**

For each tuple, create a dedicated child of `$BuildRoot`, invoke qmake on `JaguarCore\JaguarCore.pro` with `CONFIG+=debug` or `CONFIG+=release`, add `CONFIG+=jaguarcore_no_avx2` for scalar, and pass `JAGUARCORE_PACKAGE_ROOT=$PackageRoot`. Run `nmake` and `nmake install`. Do not delete `$BuildRoot`; if a child exists, use a uniquely named run directory or remove only that exact child after checking it is below `$BuildRoot`.

- [ ] **Step 4: Build and run matching native tests**

For each tuple, configure `JaguarCore\tests\JaguarCoreTests.pro` with the matching `JAGUARCORE_BUILD_DIR`, debug/release flag, and scalar flag. Run `nmake`, then execute the test binary with the matching core `bin` directory prepended to `PATH`. Throw immediately on a non-zero exit code.

- [ ] **Step 5: Build matching Debug/Release runtime checkers**

Configure `JaguarCore\package\JaguarCoreRuntimeCheck.pro` once for Debug and once for Release, install each checker into the package configuration directory, and reuse it for both AVX2 and scalar DLLs.

- [ ] **Step 6: Build and run the package consumer for all four variants**

Configure `JaguarCoreConsumerSmoke.pro` once per matrix tuple with `JAGUARCORE_PACKAGE_ROOT`, `JAGUARCORE_PACKAGE_CONFIGURATION`, and `JAGUARCORE_PACKAGE_VARIANT`. Run it with only the selected package `bin` directory prepended to `PATH`; do not add the source-tree DLL directories.

- [ ] **Step 7: Invoke the package verifier and print an aggregate table**

Call:

```powershell
& (Join-Path $PackageRoot 'VerifyPackage.ps1') $PackageRoot $ExpectedPublicVersion $ExpectedFileVersion $ExpectedApiVersion
if ($LASTEXITCODE -ne 0) { throw 'package verification failed' }
```

Print one row per tuple containing build, native-test, consumer, and runtime-check results. End with `release_matrix,result,pass` only when every row passed.

### Task 4: Update deployment documentation and execute a clean release gate

**Files:**
- Modify: `docs/deployment.md`
- Modify: `JaguarCore/package/README.md`

- [ ] **Step 1: Document the one-command matrix entry point**

Add the exact invocation pattern:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\JaguarCore\package\BuildReleaseMatrix.ps1
```

Document overrides for `$QtBin`, `$VcVars`, `$BuildRoot`, `$PackageRoot`, and the three version values.

- [ ] **Step 2: Document the consumer isolation rule**

State that consumer smoke tests use package headers/import libraries and the selected package DLL directory only, and that AVX2/scalar DLLs must not share one runtime directory.

- [ ] **Step 3: Run the full matrix from a clean build root**

Run the script with an explicit build root below `D:\MyWorks\ProQt` and the verified package root. Expected: four native test passes, four consumer passes, four runtime checks, `package_check,result,pass`, and `release_matrix,result,pass`.

- [ ] **Step 4: Inspect the assembled package without Git operations**

Verify the package contains 31 public headers, both Debug and Release checker tools, four DLL/import-library pairs, no Qt/Rust runtime files, and the updated deployment docs. Report the exact changed files and command outputs; do not stage or commit.
