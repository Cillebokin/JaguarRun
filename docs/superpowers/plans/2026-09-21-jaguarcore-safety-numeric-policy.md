# JaguarCore Safety and Numeric Policy Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make JaguarCore transform failures observable through `Result` and make the public geometry defaults use the existing `NumericPolicy` without changing NestLab or other out-of-scope components.

**Architecture:** Keep `Transform2d` as a small value type, add a finite-value predicate, and make polygon transformation a fallible operation. Propagate that result through `PlacedItem` and `OriginalShape`. Treat `NumericPolicy` as the source of truth for public equality/area defaults while leaving algorithm-specific stability epsilons local and explicitly named.

**Tech Stack:** C++17, Qt 5.12 qmake project files, MSVC x64, JaguarCore `Result<T>`, assert-based native tests, AVX2 and scalar source variants.

## Global Constraints

- Only modify JaguarCore and its tests; do not add NestLab, DXF, `Exp_Entity`, lbf, WASM, or C ABI work.
- Invalid transforms and transform-generated invalid polygons must return `ErrorCode::InvalidInput` or the underlying polygon error instead of being extracted with `Result::value()`.
- `NumericPolicy::areaTolerance` controls polygon area validity; its equality default controls public approximate-comparison defaults.
- Keep algorithm-specific internal stability epsilons local and named; do not retune them in this change.
- Follow test-driven development: every production change has a test that fails before the implementation.
- Do not create a Git commit; verify the working tree through build and test commands only.

---

### Task 1: Make polygon transformation fallible and propagate errors

**Files:**
- Modify: `JaguarCore/include/JaguarCore/geometry/transformation.h`
- Modify: `JaguarCore/include/JaguarCore/geometry/simple_polygon.h`
- Modify: `JaguarCore/src/geometry/primitives/simple_polygon.cpp`
- Modify: `JaguarCore/src/entities/placed_item.cpp`
- Modify: `JaguarCore/src/geometry/original_shape/original_shape.cpp`
- Modify: `JaguarCore/tests/test_geometry.cpp`
- Modify: `JaguarCore/tests/test_layout.cpp`
- Modify: `JaguarCore/tests/test_shape_modification.cpp`
- Modify: `JaguarCore/tests/test_surrogate.cpp`
- Modify: `JaguarCore/tests/bench/bench_geometry.cpp`
- Modify: `JaguarCore/tests/bench/bench_layout.cpp`
- Modify: `JaguarCore/tests/bench/bench_collision.cpp`

**Interfaces:**
- Produces `Transform2d::isFinite() const noexcept`.
- Changes `SimplePolygon::transformed(const Transform2d&)` to return `Result<SimplePolygon>`.
- Keeps `PlacedItem::create()` and `OriginalShape::convertToInternal()` as `Result` APIs and makes them propagate the new transformation result.

- [x] **Step 1: Add the failing direct-transform test**

In `runGeometryTests()`, create a valid polygon and call the desired fallible API with a `NaN` rotation and an infinite translation. Assert that the result has no value and has `ErrorCode::InvalidInput`.

```cpp
const auto polygon = jaguar::SimplePolygon::create({
    {0.0, 0.0}, {2.0, 0.0}, {0.0, 2.0}});
assert(polygon.hasValue());
const auto invalidTransform = polygon.value().transformed(
    jaguar::Transform2d::fromRotationTranslation(
        std::numeric_limits<double>::quiet_NaN(),
        {0.0, std::numeric_limits<double>::infinity()}));
assert(!invalidTransform.hasValue());
assert(invalidTransform.error().code == jaguar::ErrorCode::InvalidInput);
```

Add `#include <limits>` to the test file. The current code must fail to compile because `SimplePolygon::transformed()` is not yet a `Result` API; that is the expected RED state for this signature change.

- [x] **Step 2: Run the geometry test target and verify the RED failure**

Run the existing Debug AVX2 test executable from the source root after ensuring the Debug DLL directory is first on `PATH`:

```powershell
$env:PATH = "D:\MyWorks\ProQt\JaguarRun\build-JaguarCore-MSVC2019x64-Debug\bin;" + $env:PATH
& "D:\MyWorks\ProQt\JaguarRun\build-JaguarCoreTests-MSVC2019x64-Debug\debug\JaguarCoreTests.exe"
```

Expected result: compilation or link/update fails at the new test expression because the production signature still returns `SimplePolygon`, not a `Result<SimplePolygon>`.

- [x] **Step 3: Implement the minimal fallible transformation path**

Add this predicate to `Transform2d`:

```cpp
bool isFinite() const noexcept
{
    return std::isfinite(rotationRad) && translation.isFinite();
}
```

Include `<cmath>` in the header. Change the polygon declaration and definition to return `Result<SimplePolygon>`. At the start of the definition, reject a non-finite transform with:

```cpp
if (!transform.isFinite())
    return Result<SimplePolygon>::fromError(
        {ErrorCode::InvalidInput, "transform contains a non-finite value"});
```

After applying the transform, return the `SimplePolygon::create()` error instead of calling `.value()` without checking. Only transform and attach the existing surrogate after the polygon result is known to be valid.

Update `PlacedItem::create()` to store the transformed result and return its error before constructing the placed item. Update `OriginalShape::convertToInternal()` to propagate the transformed result before offset/simplification.

- [x] **Step 4: Update existing callers to consume `Result<SimplePolygon>`**

For every existing direct call, assert or check `hasValue()` at the current error boundary, then use `.value()` only after the check. Update the test and benchmark callers listed above; do not add a second unsafe transformation overload.

- [x] **Step 5: Add layout and original-shape regression tests**

In `runLayoutTests()`, call `layout.placeItem()` with a non-finite transform and assert:

```cpp
assert(!invalidPlacement.hasValue());
assert(invalidPlacement.error().code == jaguar::ErrorCode::InvalidInput);
assert(layout.placedItemCount() == 0);
```

Add a valid `OriginalShape` test with a non-finite `preTransform` and assert that `convertToInternal()` returns `InvalidInput`. These tests must exercise the production propagation path, not only the helper predicate.

- [x] **Step 6: Run the focused RED-to-GREEN verification**

Rebuild the Debug AVX2 test target and run it from `D:\MyWorks\ProQt\JaguarRun`. Expected result: the new direct transform, layout placement, and original-shape tests pass, and no existing test fails.

---

### Task 2: Make NumericPolicy the source of public geometry defaults

**Files:**
- Modify: `JaguarCore/include/JaguarCore/numeric_policy.h`
- Modify: `JaguarCore/include/JaguarCore/geometry/edge.h`
- Modify: `JaguarCore/include/JaguarCore/geometry/simple_polygon.h`
- Modify: `JaguarCore/src/collision/soa_geometry.h`
- Modify: `JaguarCore/src/geometry/primitives/simple_polygon.cpp`
- Modify: `JaguarCore/src/io/importer.cpp`
- Modify: `JaguarCore/src/entities/layout.cpp`
- Modify: `JaguarCore/src/support/assertions.cpp`
- Modify: `JaguarCore/src/problems/spp/problem.cpp`
- Modify: `JaguarCore/src/problems/bpp/bpp_problem.cpp`
- Modify: `JaguarCore/src/problems/mspp/msp_problem.cpp`
- Modify: `JaguarCore/tests/test_support.cpp`
- Modify: `JaguarCore/tests/test_geometry.cpp`

**Interfaces:**
- Produces named `NumericPolicy::defaultEqualityTolerance()` and `NumericPolicy::defaultAreaTolerance()` constants/functions usable in default arguments.
- Changes `SimplePolygon::create(std::vector<Point2d>)` to accept an optional `NumericPolicy` value while preserving all existing one-argument call sites.
- Makes public `Edge2d`, `SimplePolygon`, and SoA default tolerances reference the same policy default.

- [x] **Step 1: Add the failing policy-driven area test**

In `runGeometryTests()`, construct a small triangle and verify that a policy with a deliberately larger area tolerance rejects it:

```cpp
const jaguar::NumericPolicy strictPolicy{
    1.0e-9, 1.0e-7, 1.0, 1.0e-3};
const auto strictPolygon = jaguar::SimplePolygon::create(
    {{0.0, 0.0}, {0.5, 0.0}, {0.0, 0.5}}, strictPolicy);
assert(!strictPolygon.hasValue());
assert(strictPolygon.error().code == jaguar::ErrorCode::ZeroArea);
```

The current `SimplePolygon::create()` has no policy parameter and must fail to compile. This is the expected RED state.

- [x] **Step 2: Run the focused test and verify the RED failure**

Run the Debug AVX2 JaguarCore test executable using the command from Task 1, Step 2. Expected result: compilation fails because the policy overload does not yet exist.

- [x] **Step 3: Add reusable policy defaults and wire polygon validity**

Define the default tolerance accessors in `numeric_policy.h`, initialize the existing fields from them, and change `SimplePolygon::create()` to accept `NumericPolicy policy = {}`. Use `policy.areaTolerance` for the raw-area rejection instead of the hard-coded `1.0e-12`.

Keep the existing error codes and messages stable except where the new policy explicitly changes a threshold. Do not introduce global mutable numeric state.

- [x] **Step 4: Replace public default arguments and generic comparisons**

Use `NumericPolicy::defaultEqualityTolerance()` for the default tolerance arguments in `Edge2d`, `SimplePolygon`, and `PolygonSoA`. Replace the generic `1.0e-9` comparisons in importer duplicate cleanup, layout snapshot comparison, assertion helpers, and SPP/BPP/MSPP state comparison with a local `const NumericPolicy policy{}` or its named default accessor.

For importer consecutive-point cleanup, use:

```cpp
const NumericPolicy policy{};
const double toleranceSquared =
    policy.equalityTolerance * policy.equalityTolerance;
```

Do not replace algorithm-specific denominator, skeleton, offset, or simplification epsilons in this task.

- [x] **Step 5: Add policy default assertions**

Extend `testNumericPolicy()` to assert the named defaults equal the field defaults. Keep the existing finite and approximate-equality assertions. Add one geometry assertion that the default policy still accepts the existing normal polygons.

- [x] **Step 6: Run the focused policy tests and then the full Debug target**

Rebuild and run the Debug AVX2 test target. Expected result: the custom area tolerance test and all existing tests pass.

---

### Task 3: Full configuration and package verification

**Files:**
- Verify only; no additional production source files.
- Inspect: `D:\MyWorks\ProQt\package-JaguarCore-0.1.0-msvc2019-x64\VerifyPackage.ps1`

**Interfaces:**
- Consumes the completed transform and NumericPolicy changes.
- Produces fresh evidence for Debug/Release × AVX2/Scalar and the package checker.

- [x] **Step 1: Run all four JaguarCore test executables**

Run from `D:\MyWorks\ProQt\JaguarRun`, selecting the matching DLL directory and CRT environment for each existing configuration:

```powershell
& "D:\MyWorks\ProQt\JaguarRun\build-JaguarCoreTests-MSVC2019x64-Debug\debug\JaguarCoreTests.exe"
& "D:\MyWorks\ProQt\JaguarRun\build-JaguarCoreTests-MSVC2019x64-Release\release\JaguarCoreTests.exe"
& "D:\MyWorks\ProQt\JaguarRun\build-JaguarCoreTests-MSVC2019x64-Debug-Scalar\debug\JaguarCoreTests.exe"
& "D:\MyWorks\ProQt\JaguarRun\build-JaguarCoreTests-MSVC2019x64-Release-Scalar\release\JaguarCoreTests.exe"
```

Expected result: each process exits with code `0`, with no assertion failure or uncaught exception.

- [x] **Step 2: Run package verification**

Run:

```powershell
& "D:\MyWorks\ProQt\package-JaguarCore-0.1.0-msvc2019-x64\VerifyPackage.ps1"
```

Expected result: the output contains `package_check,result,pass` and all four DLL variants pass architecture, version, CRT, and exported-version checks.

- [x] **Step 3: Synchronize the package with the rebuilt JaguarCore artifacts**

Copy the current public headers from `D:\MyWorks\ProQt\JaguarRun\JaguarCore\include\JaguarCore` into `D:\MyWorks\ProQt\package-JaguarCore-0.1.0-msvc2019-x64\include\JaguarCore`, then copy the rebuilt Debug/Release and AVX2/scalar DLL/lib pairs into their matching package subdirectories. Compare the changed headers byte-for-byte and rerun:

```powershell
& "D:\MyWorks\ProQt\package-JaguarCore-0.1.0-msvc2019-x64\VerifyPackage.ps1" "D:\MyWorks\ProQt\package-JaguarCore-0.1.0-msvc2019-x64" "0.1.0-dev" "0.1.0.0"
```

Expected result: the changed package headers match the source headers and the package checker returns `package_check,result,pass` using the newly rebuilt binaries.

- [x] **Step 4: Inspect the final change set without Git mutation**

Use `rg` and file inspection to confirm that:

- no NestLab or DXF files changed;
- no unsafe `.value()` remains on the new transform-result path;
- no test command left a newly generated source-root artifact beyond the existing SVG test behavior;
- no Git commit, branch, staging, or reset command was executed.
