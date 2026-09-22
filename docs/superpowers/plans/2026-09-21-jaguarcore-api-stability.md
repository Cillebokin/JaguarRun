# JaguarCore API Stability Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Freeze the JaguarCore C++ API after one minimal cleanup pass that makes fallible transforms, batch options, API versioning, and thread guarantees explicit.

**Architecture:** Keep the existing C++17 `Result<T>` error model and MSVC2019 x64 package contract. Convert only public operations that can reject a transform or batch request from raw return values to `Result`, while leaving low-level value math and existing collision semantics unchanged. Add the API version probe independently from the product version string.

**Tech Stack:** C++17, Qt 5.12.10 qmake, MSVC2019 x64, JaguarCore DLL, STL, assert-based native tests.

## Global Constraints

- Scope is JaguarCore only; do not modify NestLab compatibility code.
- Preserve the public version string `0.1.0-dev` and file version `0.1.0.0`.
- The API version probe returns `std::uint32_t` value `1`.
- Keep `ErrorCode::InvalidInput` for invalid transforms and invalid batch options.
- Do not add Qt or Rust runtime dependencies.
- Do not create Git commits; report changed files and verification results only.

---

### Task 1: Add the independent API version probe

**Files:**
- Modify: `JaguarCore/include/JaguarCore/version.h`
- Modify: `JaguarCore/src/support/version.cpp`
- Modify: `JaguarCore/tests/test_support.cpp`
- Test later: `JaguarCore/package/JaguarCoreRuntimeCheck.cpp`

**Interfaces:**
- Consumes: existing `version()` and `JaguarCoreVersion()`.
- Produces: `extern "C" JAGUARCORE_API std::uint32_t JaguarCoreApiVersion() noexcept;` returning `1`.

- [ ] **Step 1: Add the failing native assertion**

In `testVersion()` in `JaguarCore/tests/test_support.cpp`, add this assertion after the existing public-version checks:

```cpp
assert(JaguarCoreApiVersion() == 1U);
```

- [ ] **Step 2: Run the focused test to verify the missing symbol is detected**

Run from a matching existing Debug build directory:

```powershell
& 'D:\MySoftwares\Tools\Qt\Qt5.12.10\5.12.10\msvc2017_64\bin\qmake.exe' 'D:\MyWorks\ProQt\JaguarRun\JaguarCore\tests\JaguarCoreTests.pro' CONFIG+=debug JAGUARCORE_BUILD_DIR='D:\MyWorks\ProQt\build-JaguarCore-MSVC2019x64-Debug'
nmake
```

Expected: compilation fails because `JaguarCoreApiVersion` is not yet declared by the current public header.

- [ ] **Step 3: Implement the declaration and definition**

Add `<cstdint>` and the C-linkage declaration to `version.h`. Define the function in `version.cpp` next to `JaguarCoreVersion()`:

```cpp
extern "C" JAGUARCORE_API std::uint32_t JaguarCoreApiVersion() noexcept
{
    return 1U;
}
```

- [ ] **Step 4: Rebuild and run the focused test**

Run the same qmake/nmake command, then run the resulting `JaguarCoreTests.exe` with the matching core `bin` directory first in `PATH`. Expected: exit code `0`.

### Task 2: Make surrogate transforms and surrogate queries fallible

**Files:**
- Modify: `JaguarCore/include/JaguarCore/fail_fast/surrogate.h`
- Modify: `JaguarCore/src/fail_fast/surrogate.cpp`
- Modify: `JaguarCore/include/JaguarCore/collision/collision_engine.h`
- Modify: `JaguarCore/src/collision/collision_engine.cpp`
- Modify: `JaguarCore/src/geometry/primitives/simple_polygon.cpp`
- Test: `JaguarCore/tests/test_surrogate.cpp`
- Test: `JaguarCore/tests/test_collision.cpp`

**Interfaces:**
- Consumes: existing `Transform2d::isFinite()` and `Result<T>`.
- Produces: `Result<Surrogate> Surrogate::transformed(...)`, `Result<bool>` for both surrogate collision methods, and explicit propagation through `SimplePolygon::transformed()`.

- [ ] **Step 1: Write failing transform and query tests**

In `runSurrogateTests()`, create a finite surrogate and add:

```cpp
const auto invalidSurrogateTransform = surrogate.value().transformed(
    jaguar::Transform2d::fromRotationTranslation(
        std::numeric_limits<double>::quiet_NaN(), {0.0, 0.0}));
assert(!invalidSurrogateTransform.hasValue());
assert(invalidSurrogateTransform.error().code == jaguar::ErrorCode::InvalidInput);
```

In `runCollisionTests()`, call `detectSurrogateCollision()` with the same invalid transform and assert that the returned `Result<bool>` has no value and `InvalidInput`.

- [ ] **Step 2: Run the focused tests and record the expected compile failures**

Run the existing `JaguarCoreTests` build. Expected: the new assertions fail to compile because `Surrogate::transformed()` and `detectSurrogateCollision()` still return raw values.

- [ ] **Step 3: Change the public signatures**

Change the declarations exactly as follows:

```cpp
Result<Surrogate> transformed(const Transform2d& transform) const;

Result<bool> detectSurrogateCollision(
    const Surrogate&, const Transform2d&, const HazardFilter&) const;

Result<bool> collectSurrogateCollisionsUntil(
    const Surrogate&, const Transform2d&, HazardCollector&,
    const HazardFilter&, const CollisionStopCallback&) const;

Result<bool> collectSurrogateCollisionsUntil(
    const Surrogate&, const Transform2d&, HazardCollector&,
    const CollisionStopCallback&) const;
```

- [ ] **Step 4: Implement transform validation and error propagation**

At the beginning of `Surrogate::transformed()`, reject `!transform.isFinite()` with `InvalidInput`. Return the transformed surrogate through `Result<Surrogate>::fromValue()`.

In `SimplePolygon::transformed()`, replace the direct raw surrogate construction with:

```cpp
const auto transformedSurrogate = m_Surrogate->transformed(transform);
if (!transformedSurrogate.hasValue())
    return Result<SimplePolygon>::fromError(transformedSurrogate.error());
transformedPolygon.m_Surrogate = std::make_shared<const Surrogate>(
    transformedSurrogate.value());
```

At the beginning of both collision-engine surrogate query implementations, return `Result<bool>::fromError(...)` for a non-finite transform. Wrap existing `true` and `false` returns with `Result<bool>::fromValue(...)`; make the no-filter overload forward the `Result<bool>` unchanged.

- [ ] **Step 5: Migrate all in-tree callers**

Use `rg -n "detectSurrogateCollision|collectSurrogateCollisionsUntil|\.transformed\(" JaguarCore -g '*.cpp' -g '*.h'` and update every call site. Valid calls must assert `hasValue()` before reading `.value()`. Do not replace error checks with `.value()` directly.

- [ ] **Step 6: Rebuild and run geometry, surrogate, collision, and layout coverage**

Run the Debug AVX2 test executable with the matching DLL on `PATH`. Expected: exit code `0`, including existing surrogate oracle and collision callback assertions.

### Task 3: Make parallel batch options and thread semantics explicit

**Files:**
- Modify: `JaguarCore/include/JaguarCore/collision/collision_engine.h`
- Modify: `JaguarCore/src/collision/collision_engine.cpp`
- Modify: `JaguarCore/tests/test_collision.cpp`

**Interfaces:**
- Consumes: `BatchCollisionOptions`, `ThreadPool::parallelFor()`, existing same-engine query mutex.
- Produces: `Result<std::vector<std::uint8_t>> detectPolygonCollisionsParallel(...)` and documented concurrency rules.

- [ ] **Step 1: Change tests to require a result value**

Update the existing parallel assertions in `runCollisionTests()` to use:

```cpp
const auto parallelResults = engine.detectPolygonCollisionsParallel(
    batch, jaguar::NoFilter{}, parallelOptions);
assert(parallelResults.hasValue());
assert(parallelResults.value() == batchResults);
```

Add an invalid-options case:

```cpp
const auto invalidBatch = engine.detectPolygonCollisionsParallel(
    batch, jaguar::NoFilter{}, jaguar::BatchCollisionOptions{2, 0});
assert(!invalidBatch.hasValue());
assert(invalidBatch.error().code == jaguar::ErrorCode::InvalidInput);
```

- [ ] **Step 2: Implement validation and result wrapping**

Reject `options.grainSize == 0` before acquiring the parallel-query mutex. Keep `workerCount == 0` as automatic selection. Return a successful empty vector for an empty shape list, and wrap the existing result vector with `Result<std::vector<std::uint8_t>>::fromValue()`.

- [ ] **Step 3: Document the thread contract in the public header**

Place a comment above `CollisionEngine` that states:

```cpp
// Const queries may run concurrently on one engine. registerHazard,
// deregisterHazard, and restore mutate engine state and must be externally
// serialized against all queries and other mutations. A parallel query may
// invoke its filter concurrently from multiple workers.
```

- [ ] **Step 4: Verify concurrent read-only behavior**

Keep the existing two concurrent batch submissions and update them for `Result`. Add one concurrent ordinary-query thread pair; both threads must produce the same collision vector as the sequential baseline. Run the test twice in Debug and once in Release.

### Task 4: Update API documentation and complete the API migration gate

**Files:**
- Modify: `docs/deployment.md`
- Modify: `JaguarCore/package/README.md`
- Modify: all caller files found by the Task 2 `rg` search

- [ ] **Step 1: Document the API freeze boundary**

Add the API version probe, its value `1`, the `Result` signatures, and the rule that low-level transform math is value-level while fallible geometry/collision operations validate inputs.

- [ ] **Step 2: Document ownership and callback rules**

Keep the existing ownership rules and add the concurrent-filter requirement and external serialization requirement for mutating engine calls.

- [ ] **Step 3: Compile every caller before moving to boundary validation**

Run:

```powershell
rg -n "detectSurrogateCollision|collectSurrogateCollisionsUntil|detectPolygonCollisionsParallel|\.transformed\(" JaguarCore -g '*.cpp' -g '*.h'
```

Review every result to confirm it handles the new `Result` return type. Then build and run one Debug AVX2 configuration. This is the handoff gate for the boundary-validation plan.
