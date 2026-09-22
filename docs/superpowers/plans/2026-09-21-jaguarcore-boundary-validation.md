# JaguarCore Boundary Validation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make JaguarCore public factories, mutators, snapshots, and geometry calculations reject invalid or non-finite input without corrupting observable state.

**Architecture:** Keep validation at public Result-returning boundaries and reuse the existing `ErrorCode` values. Add finite-result checks around geometry derivations, validate `NumericPolicy`, and use a dedicated deterministic boundary test translation unit for malformed input, state atomicity, and fixed-seed random geometry.

**Tech Stack:** C++17, Qt 5.12.10 qmake, MSVC2019 x64, JaguarCore `Result<T>`, assert-based native tests, fixed-seed `std::mt19937_64`.

## Global Constraints

- Run this plan after `2026-09-21-jaguarcore-api-stability.md` has passed its Debug AVX2 gate.
- Reuse `InvalidInput`, `SnapshotMismatch`, and `Infeasible`; do not add new error codes.
- Accept zero as a valid tolerance, but reject non-finite and negative `NumericPolicy` tolerances.
- Preserve existing surrogate ranges and rotation-angle limits; do not invent unrelated business limits.
- Keep failure paths atomic and do not modify NestLab or add dependencies.
- Do not create Git commits.

---

### Task 1: Add the dedicated boundary test translation unit

**Files:**
- Create: `JaguarCore/tests/test_boundary.cpp`
- Modify: `JaguarCore/tests/JaguarCoreTests.pro`
- Modify: `JaguarCore/tests/test_support.cpp`

**Interfaces:**
- Consumes: public JaguarCore factories and `Result<T>`.
- Produces: `void runBoundaryValidationTests();` registered by the existing test main.

- [ ] **Step 1: Register the test entry point**

Add `void runBoundaryValidationTests();` beside the other test declarations in `test_support.cpp`, call it at the end of `main()`, and add `$$PWD/test_boundary.cpp` to `JaguarCoreTests.pro`.

- [ ] **Step 2: Write the initial failing boundary assertions**

Create `test_boundary.cpp` with the following initial shape:

```cpp
#include "JaguarCore/collision/collision_engine.h"
#include "JaguarCore/entities/layout.h"
#include "JaguarCore/geometry/simple_polygon.h"
#include "JaguarCore/numeric_policy.h"

#include <cassert>
#include <cmath>
#include <limits>
#include <random>

void runBoundaryValidationTests()
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();

    const jaguar::NumericPolicy invalidPolicy{nan, 1.0e-7, 1.0e-9, 1.0e-3};
    const auto invalidPolicyPolygon = jaguar::SimplePolygon::create(
        {{0.0, 0.0}, {1.0, 0.0}, {0.0, 1.0}}, invalidPolicy);
    assert(!invalidPolicyPolygon.hasValue());
    assert(invalidPolicyPolygon.error().code == jaguar::ErrorCode::InvalidInput);

    const auto invalidRectangle = jaguar::Rect2d::create(0.0, 0.0, inf, 1.0);
    assert(!invalidRectangle.hasValue());

    std::mt19937_64 generator(0x4A4752434F5245ULL);
    (void)generator;
}
```

- [ ] **Step 3: Build to verify the test exposes missing validation**

Build the Debug AVX2 tests. Expected: the invalid policy case fails because the current policy has no aggregate validation.

### Task 2: Validate NumericPolicy and finite derived geometry

**Files:**
- Modify: `JaguarCore/include/JaguarCore/numeric_policy.h`
- Modify: `JaguarCore/src/geometry/primitives/simple_polygon.cpp`
- Test: `JaguarCore/tests/test_support.cpp`
- Test: `JaguarCore/tests/test_geometry.cpp`
- Test: `JaguarCore/tests/test_boundary.cpp`

**Interfaces:**
- Consumes: existing default tolerance accessors and `SimplePolygon::create()`.
- Produces: `bool NumericPolicy::isValid() const noexcept` and finite internal polygon state for every successful factory result.

- [ ] **Step 1: Add direct policy validity tests**

Extend `testNumericPolicy()` with:

```cpp
assert(policy.isValid());
assert(!jaguar::NumericPolicy{std::numeric_limits<double>::quiet_NaN(),
                               policy.geometryTolerance,
                               policy.areaTolerance,
                               policy.arcChordTolerance}.isValid());
assert(!jaguar::NumericPolicy{policy.equalityTolerance,
                               -1.0,
                               policy.areaTolerance,
                               policy.arcChordTolerance}.isValid());
```

- [ ] **Step 2: Add the extreme finite-coordinate regression**

In `test_boundary.cpp`, call `SimplePolygon::create()` with three distinct finite points around `1.0e308` whose cross products overflow. Assert that the result has no value and its error is `InvalidInput`, not a successful polygon containing infinity.

- [ ] **Step 3: Implement `NumericPolicy::isValid()`**

In the public header, add:

```cpp
bool isValid() const noexcept
{
    return std::isfinite(equalityTolerance) && equalityTolerance >= 0.0 &&
           std::isfinite(geometryTolerance) && geometryTolerance >= 0.0 &&
           std::isfinite(areaTolerance) && areaTolerance >= 0.0 &&
           std::isfinite(arcChordTolerance) && arcChordTolerance >= 0.0;
}
```

- [ ] **Step 4: Validate policy before polygon computation**

At the beginning of `SimplePolygon::create()`, return `InvalidInput` with the message `numeric policy contains an invalid tolerance` when `!policy.isValid()`.

- [ ] **Step 5: Reject non-finite derived values**

After `signedArea(points)`, reject a non-finite raw area. After orientation normalization, verify the computed area, bounding-box fields, centroid, and diameter are finite before constructing `SimplePolygon`. Use `InvalidInput` and preserve `ZeroArea` for finite values within `areaTolerance`.

- [ ] **Step 6: Run the focused geometry tests**

Build and run Debug AVX2 `JaguarCoreTests.exe`. Expected: all existing geometry and numeric-policy assertions plus the new invalid-policy and overflow assertions pass.

### Task 3: Complete public configuration and batch boundary checks

**Files:**
- Modify: `JaguarCore/src/collision/collision_engine.cpp`
- Modify: `JaguarCore/src/fail_fast/surrogate.cpp`
- Modify: `JaguarCore/src/entities/container.cpp`
- Modify: `JaguarCore/src/entities/item.cpp`
- Modify: `JaguarCore/src/problems/spp/instance.cpp`
- Modify: `JaguarCore/src/problems/spp/strip.cpp`
- Modify: `JaguarCore/src/problems/bpp/bpp_instance.cpp`
- Modify: `JaguarCore/src/problems/mspp/msp_instance.cpp`
- Test: `JaguarCore/tests/test_boundary.cpp`

**Interfaces:**
- Consumes: existing factory and mutation Result APIs, plus the API-plan batch result signature.
- Produces: consistent InvalidInput rejection before expensive geometry or thread-pool work.

- [ ] **Step 1: Add malformed configuration cases**

Add assertions for NaN/Inf and non-positive dimensions in the SPP/BPP/MSPP factory inputs, invalid rotation parameters, invalid surrogate ratios/thresholds, and `BatchCollisionOptions{0, 0}`. Assert the corresponding Result is an error and the code is `InvalidInput`.

- [ ] **Step 2: Audit existing checks without changing accepted semantics**

Use:

```powershell
rg -n "Result<|std::isfinite|<= 0|< 0|std::size_t|reserve\(|for \(.*<" JaguarCore/src -g '*.cpp'
```

For each public factory or mutation, verify that finite checks happen before arithmetic, vector sizing, or thread-pool creation. Keep existing rotation count and surrogate pole limits. Do not impose a new arbitrary maximum on `failFastPiers`; ensure only that zero is the no-op case and that the loop cannot receive a wrapped or non-finite quantity.

- [ ] **Step 3: Add stale-handle and malformed-snapshot tests**

In `test_boundary.cpp`, register and deregister one hazard, reuse its old handle, and assert the second deregistration returns `InvalidInput`. Save a valid engine snapshot, mutate a copy into a null-shape dynamic hazard and a non-dynamic hazard, call `restore()`, and assert `InvalidInput`/`SnapshotMismatch` without changing the baseline query result.

- [ ] **Step 4: Verify state atomicity for Layout**

Create a valid `Layout`, capture `placedItemCount()`, `density()`, and `save()`, attempt placement with a non-finite transform and removal with an invalid handle, then assert all captured values and collision results are unchanged.

- [ ] **Step 5: Run the boundary test subset**

Build and run `JaguarCoreTests.exe`; because the test binary is a single translation unit group, use its exit code and the focused assertions to gate this task. Expected: exit code `0`.

### Task 4: Add fixed-seed finite random geometry coverage

**Files:**
- Modify: `JaguarCore/tests/test_boundary.cpp`

- [ ] **Step 1: Generate bounded deterministic polygons**

Use the fixed `std::mt19937_64` seed from Task 1 to generate 3–12 points in `[-1000, 1000]`, sort them by angle around their centroid, and call `SimplePolygon::create()`. For successful polygons, assert every vertex, bounding-box field, centroid coordinate, area, and diameter is finite.

- [ ] **Step 2: Exercise valid transforms and surrogate creation**

For each successful polygon, apply a finite rotation/translation, assert the transformed Result is valid, and call `Surrogate::create()` with the default config. If surrogate generation returns an error, accept only `InvalidPolygon`; any successful surrogate must contain finite pole centers/radii and pier endpoints.

- [ ] **Step 3: Keep the random run bounded and reproducible**

Run exactly 256 generated cases and do not use wall-clock time, hardware concurrency, or unseeded randomness. Add a comment recording the seed and case count.

- [ ] **Step 4: Run Debug/Release AVX2 and scalar tests**

Run the four existing test executables with their matching core `bin` directory first in `PATH`. Expected: all four exit with code `0`.
