# SparrowCore Rust-equivalent algorithm Implementation Plan

> **For agentic workers:** Implement this plan task-by-task with a red-green test cycle. Do not create Git commits in this workspace unless the user explicitly authorizes a commit in the current task.

**Goal:** Restore functional logic equivalence between SparrowCore and the current Rust `CodeResource/sparrow` SPP optimizer.

**Architecture:** Preserve the existing public C++17 headers and JaguarCore integration. Replace simplified internal heuristics with the reference Sparrow formulas and state transitions, using JaguarCore snapshots and handles to model Rust layout ownership and rollback.

**Tech Stack:** C++17, Qt/qmake, MSVC 2019 x64, JaguarCore, existing SparrowCore tests and fixtures.

## Global Constraints

- Use `CodeResource/sparrow` as the behavioral reference.
- Keep JaguarCore responsible for geometry, collision detection, layout, JSON, and SVG.
- Write a failing test before each production behavior change.
- Preserve existing user changes and do not create Git commits.
- Keep public API and code naming compatible with the repository conventions.

---

### Task 1: Restore Sparrow collision quantification and hazard-backed tracking

**Files:**
- Modify: `SparrowCore/Include/SparrowCore/Quantify/OverlapProxy.h`
- Modify: `SparrowCore/Source/Quantify/OverlapProxy.cpp`
- Modify: `SparrowCore/Source/Quantify/CollisionTracker.cpp`
- Test: `SparrowCore/Tests/TestQuantify.cpp`

**Interfaces:**
- Preserve the existing `CalculateOverlapProxy`, `CalculateContainerPenalty`, and `CollisionTracker` APIs.
- Add only the smallest internal helpers needed for Sparrow's shape penalty and collision-only entry updates.

- [x] Add tests proving scale-dependent epsilon, shape penalty, container formula, and zero loss for a non-colliding pair.
- [x] Run the focused quantify test and verify it fails against the current implementation.
- [x] Implement the reference pair/container formulas.
- [x] Change tracker rebuild and moved-item updates to populate loss only for JaguarCore-reported hazards while preserving weights.
- [x] Run quantify, separator, and full SparrowCore tests.

### Task 2: Restore reference LBF placement search

**Files:**
- Modify: `SparrowCore/Include/SparrowCore/Sampling/UniformRectSampler.h`
- Modify: `SparrowCore/Source/Sampling/UniformRectSampler.cpp`
- Modify: `SparrowCore/Source/Optimizer/LbfSearch.cpp`
- Modify: `SparrowCore/Source/Optimizer/SparrowLbfBuilder.cpp`
- Modify: `SparrowCore/Source/Optimizer/SeparatorWorker.cpp`
- Test: `SparrowCore/Tests/TestSampling.cpp`
- Test: `SparrowCore/Tests/TestSppLbf.cpp`

**Interfaces:**
- Keep `SearchLbf` and `FindBestSeparatorMove` public signatures stable.
- Add the focused bbox sampler behavior internally or as a compatible `UniformRectSampler` factory.

- [x] Add reference fixtures for multiple best samples, focused sampling, and the second coordinate-descent refinement.
- [x] Verify the new tests fail.
- [x] Implement reference sample order, `BestSamples` retention, focused sampling, and two refinement stages.
- [x] Run sampling and SPP LBF tests.

### Task 3: Restore multi-worker Separator behavior

**Files:**
- Modify: `SparrowCore/Include/SparrowCore/Optimizer/SeparatorWorker.h`
- Modify: `SparrowCore/Include/SparrowCore/Optimizer/Separator.h`
- Modify: `SparrowCore/Include/SparrowCore/Optimizer/SparrowConfig.h`
- Modify: `SparrowCore/Source/Optimizer/SeparatorWorker.cpp`
- Modify: `SparrowCore/Source/Optimizer/Separator.cpp`
- Modify: `SparrowCore/Source/Optimizer/SparrowConfig.cpp`
- Test: `SparrowCore/Tests/TestSeparator.cpp`

**Interfaces:**
- Add the reference worker-count setting without removing existing separator settings.
- Keep `Separator::Run` snapshot-safe and return the best solution/tracker pair.

- [x] Add deterministic worker-selection and GLS weight-update tests.
- [x] Verify failure.
- [x] Implement independent worker state, random candidate orders, best-worker selection, rollback, and strike rules.
- [x] Run separator and full regression tests.

### Task 4: Restore Exploration state transitions

**Files:**
- Modify: `SparrowCore/Include/SparrowCore/Optimizer/SparrowConfig.h`
- Modify: `SparrowCore/Source/Optimizer/Exploration.cpp`
- Test: `SparrowCore/Tests/TestExploration.cpp`

**Interfaces:**
- Preserve `Explore` and `ExplorationResult`.
- Add reference configuration values for pool distribution and large-item cutoff where needed.

- [x] Add tests for minimum-width stopping, sorted pool selection, large-item exchange, and contained relocation.
- [x] Verify failure.
- [x] Implement the reference exploration loop and disruption transform using JaguarCore handles/snapshots.
- [x] Run exploration and full regression tests.

### Task 5: Restore Compression and phase termination semantics

**Files:**
- Modify: `SparrowCore/Include/SparrowCore/Optimizer/Compression.h`
- Modify: `SparrowCore/Include/SparrowCore/Optimizer/OptimizationTerminator.h`
- Modify: `SparrowCore/Include/SparrowCore/Optimizer/SparrowConfig.h`
- Modify: `SparrowCore/Source/Optimizer/Compression.cpp`
- Modify: `SparrowCore/Source/Optimizer/OptimizationTerminator.cpp`
- Modify: `SparrowCore/Source/Optimizer/SparrowOptimizer.cpp`
- Test: `SparrowCore/Tests/TestExploration.cpp`

**Interfaces:**
- Keep external `Optimize` overloads source-compatible.
- Model separate exploration and compression timeouts while retaining caller stop requests.

- [x] Add tests for random split shifting, time-based and failure-based decay, and phase timeout reset.
- [x] Verify failure.
- [x] Implement reference compression transitions and phase-specific terminator behavior.
- [x] Run full native tests.

### Task 6: Add Rust-reference differential acceptance

**Files:**
- Modify: `SparrowCore/Docs/Differential.md`
- Create or modify: `SparrowCore/Tests/Fixtures/*Reference.tsv`
- Modify: `SparrowCore/Tests/TestRng.cpp`
- Modify: `SparrowCore/Tests/TestSampling.cpp`
- Modify: `SparrowCore/Tests/TestSeparator.cpp`
- Modify: `SparrowCore/Tests/TestExploration.cpp`
- Modify: `SparrowCore/Bench/BenchMain.cpp`

**Interfaces:**
- Keep differential fixtures deterministic and independent of live process execution.

- [x] Generate or verify checked-in reference outputs from `CodeResource/sparrow` for fixed seeds and fixtures.
- [x] Add comparisons for RNG, sample ordering, loss, phase metrics, and final solution checksum.
- [x] Run Release/Debug tests, CLI smoke, and benchmark twice.
- [x] Update compatibility documentation with verified parity and remaining non-core differences.

> Direct Cargo regeneration was run on Windows with the installed Cargo/rustc toolchain. WASM
> packaging remains separately environment-dependent because this host does not provide
> `emcc` or `emcmake`.

### Task 7: Differentially verify the real container-wide LBF placement search

**Files:**
- Modify: `CodeResource/sparrow/src/sample/search.rs`
- Modify: `SparrowCore/Source/Eval/LbfEvaluator.cpp`
- Modify: `SparrowCore/Source/Optimizer/LbfSearch.cpp`
- Create: `SparrowCore/Source/Optimizer/LbfSearchTrace.h` (internal trace hook)
- Modify: `SparrowCore/Tests/RustReference/src/main.rs`
- Create: `SparrowCore/Tests/Fixtures/LbfSearchRustTrace.tsv`
- Modify: `SparrowCore/Tests/TestSampling.cpp`
- Modify: `SparrowCore/Docs/Differential.md`
- Modify: `SparrowCore/docs/Compatibility.md`

**Interfaces:**
- Keep public `SearchLbf` unchanged; expose the trace entry only through an internal source header.
- Keep Rust tracing behind the oracle feature and behavior-neutral for normal search calls.

- [x] Capture real Rust evaluations, report decisions, retained ordering, and final refined output.
- [x] Add a C++ differential test and demonstrate the initial mismatch before behavior changes.
- [x] Match Rust's POI/bbox-corner loss and evaluate candidates without `upperBound` pruning.
- [x] Return the final coordinate-descent result without reporting it back into `BestSamples`.
- [x] Update the fixture matrix and compatibility boundary with the verified scope.
- [x] Run Rust oracle, Debug/Release native tests, and verify fixture reproducibility.
