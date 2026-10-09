# SparrowCore Rust parity baseline and P0 closure Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (\`- [ ]\`) syntax for tracking.

**Goal:** Freeze the exact Sparrow, jagua-rs, JaguarCore, and SparrowCore baselines, establish one shared error taxonomy, and close the known P0 Rust/C++ behavior differences in dependency order.

**Architecture:** Keep the RustReference lockfile and resolved jagua-rs checkout as the behavioral oracle. Freeze source files and the existing Debug/Release build artifacts by SHA-256 before changing production code. Use the existing S01–S24 records as the authoritative difference inventory, add a shared error table, then make one test-first parity fix at a time in the order already defined by the roadmap.

**Tech Stack:** Rust 1.98.1, Cargo.lock, x86_64-pc-windows-msvc, C++17, Qt 6.5.3/qmake, MSVC x64, SparrowCore and JaguarCore test runners.

## Global Constraints

- Rust Sparrow and the jagua-rs revision locked by SparrowCore/Tests/RustReference/Cargo.lock define the parity target.
- Preserve the current pre-fix source and build fingerprints in the dated baseline manifest.
- A Rust oracle must establish each expected behavior before changing production C++.
- Compare observable error category, trigger point, state side effects, callback/event order, and continuation behavior.
- Execute P0 work in the replication roadmap order: S01–S02; S17–S18–S12; S09–S16; S03–S08.
- Keep Sparrow-only C++ extensions outside parity inputs unless a matching Rust behavior exists.
- Do not run Git commands, stage files, or create commits.

---

### Task 0: Freeze the four concrete source baselines

**Files:**
- Create: docs/logic-comparison-sparrow/tools/freeze-baseline.ps1
- Create: docs/logic-comparison-sparrow/baseline-2026-10-09.json
- Create: docs/logic-comparison-sparrow/baseline-2026-10-09.md
- Modify: docs/logic-comparison-sparrow/README.md
- Modify: docs/logic-comparison-sparrow/replication-roadmap.md

**Interfaces:**
- The script accepts an output path and refuses to overwrite an existing frozen manifest unless Force is explicitly supplied.
- The manifest records per-file hashes and directory digests for Sparrow, the Cargo-resolved jagua-rs checkout, the local jagua-rs mirror, JaguarCore, SparrowCore, the RustReference inputs, and the parity records.
- The manifest records Rust/Cargo versions, target, locked jagua-rs revision and effective features, qmake/Qt metadata, generated build configuration, and existing test-runner/JaguarCore binary hashes.

- [x] Enumerate package roots from the locked Cargo metadata and source/project roots from the checked-out workspace.
- [x] Hash each source, configuration, test, and fixture file while excluding generated target/build outputs.
- [x] Run the script twice to a temporary comparison path and verify identical source-root digests.
- [x] Record the resolved build configuration and mark any compiler detail that cannot be established from the generated Makefiles.
- [x] Link the dated manifest and baseline rules from the Sparrow comparison README and roadmap.

### Task 1: Establish the shared error taxonomy

**Files:**
- Create: docs/logic-comparison-sparrow/error-semantics.md
- Modify: docs/logic-comparison-sparrow/README.md
- Modify: docs/logic-comparison-sparrow/replication-roadmap.md
- Cross-reference: docs/logic-comparison-sparrow/records/S01–S24

**Interfaces:**
- Each error row has a stable ID, trigger/input class, Rust observable, C++ observable, state-before/state-after contract, callback/phase/worker/process outcome, evidence, and closure status.
- No-fit and timeout are classified as control outcomes, not errors.

- [x] Inventory documented Result, Option/None, panic/assert/unimplemented, exception, error-code, and swallowed-error paths in S01–S24.
- [x] Add stable categories for invalid input, no-fit, infeasible construction, unsupported hazard, internal invariant, numeric/order failure, stale tracker state, IO/CLI failure, and stop/timeout.
- [x] Link every known P0 error mismatch to its owning S-record and reference test; label unmeasured state effects as open.
- [x] Check that the taxonomy does not promote a language-level panic/exception text difference to a behavioral mismatch when trigger and observable state already agree.

### Task 2: Close S01–S02 configuration and RNG differences

**Files:**
- Rust oracle: SparrowCore/Tests/RustReference/tests/rng_reference.rs and focused configuration/optimizer reference tests
- C++ tests: SparrowCore/Tests/TestRng.cpp and the relevant optimizer tests
- Implementation: SparrowCore/Include/SparrowCore/Optimizer/SparrowConfig.h, SparrowCore/Source/Optimizer/SparrowOptimizer.cpp, SparrowCore/Include/SparrowCore/Random/SparrowRng.h, SparrowCore/Source/Random/SparrowRng.cpp
- Records: docs/logic-comparison-sparrow/records/S01-config.md and S02-consts-lib.md

**Interfaces:**
- Preserve the distinction between an absent seed and explicit seed zero, and expose the effective seed used for replay.
- A zero exclusive bound must fail before consuming RNG state, matching the Rust empty-range failure.
- Time values used for parity retain Rust Duration nanosecond precision at SparrowCore entry points.

- [x] Add Rust and C++ reference cases for absent seed, explicit zero seed, zero RNG bound, and a sub-millisecond phase duration.
- [x] Run the focused tests and confirm each test exposes the recorded current difference.
- [x] Make the smallest API and implementation changes that preserve effective seed and duration semantics through construction, Explore, Compression, metrics, and public entry points.
- [x] Run Rust Debug/Release references and SparrowCore Debug/Release focused tests; update S01/S02 and the error table with exact outcomes.

### Task 3: Close S17–S18 PairMatrix and CollisionTracker state differences

**Files:**
- Rust oracle: SparrowCore/Tests/RustReference/tests for pair mapping and tracker state
- C++ tests: SparrowCore/Tests/TestQuantify.cpp
- Implementation: SparrowCore/Include/SparrowCore/Quantify/PairMatrix.h, SparrowCore/Source/Quantify/PairMatrix.cpp, SparrowCore/Include/SparrowCore/Quantify/CollisionTracker.h, SparrowCore/Source/Quantify/CollisionTracker.cpp
- Records: docs/logic-comparison-sparrow/records/S17-overlap-pair.md and S18-tracker.md

**Interfaces:**
- Pair iteration follows the Rust logical row/column order wherever accumulation order is observable.
- Initial tracker pair rows are fully initialized from the Rust pair mapping, including asymmetric detection outcomes.
- Non-finite losses and invalid pair access retain the Rust profile-specific failure behavior or an explicitly recorded Rust-equivalent error mapping.

- [x] Add literal small-N pair-index/loss-sum references and a move/update trace that checks every matrix entry and weight.
- [ ] Add asymmetric collision-discovery, non-finite loss, and out-of-range access references for Debug and Release. Non-finite, stale-key/getter, unsupported-hazard and checked bounds fixtures now pass both profiles; a reproducible asymmetric-discovery fixture remains open.
- [ ] Confirm tests fail against the pre-fix implementation, then correct pair indexing/iteration and tracker initialization/update at the source. PairMatrix ordering and getter RED evidence exists; the unknown-hazard correction was verified after implementation, and no live asymmetric trigger is available to provide RED evidence for row clearing.
- [x] Run focused and full C++ Debug/Release tests with Rust reference tests; update S17/S18 and the taxonomy. At Task 3 completion, RustReference had 84 Debug/Release tests passing; after the two S12 oracle tests, the suite had 86 integration tests; after the S14 and S16 reference additions it had 89; the current suite has 91 integration test functions. SparrowCore Debug/Release full runners pass.

### Task 4: Close S12 collision-loss edge and error semantics

**Files:**
- Rust oracle: SparrowCore/Tests/RustReference/tests/collision_loss_*.rs
- C++ tests: SparrowCore/Tests/TestQuantify.cpp
- Implementation: SparrowCore/Source/Quantify/CollisionTracker.cpp, SparrowCore/Source/Quantify/OverlapProxy.cpp, SparrowCore/Source/Eval/SeparationEvaluator.cpp
- Records: docs/logic-comparison-sparrow/records/S12-collision-loss.md

- [x] Add paired Exterior, multiple-pole, hazard-order, and upper-bound-neighbor cases with literal expected loss bits and stop point. Exterior loss is `0x391152f2`; the three-hazard loss is `0x4ab5c931`, with bounds `0x4ab5c930/0x4ab5c931` rejected and `0x4ab5c932` accepted.
- [ ] Add failure-state assertions for every unsupported hazard class, including tracker and listener state. Candidate dispatch and Tracker init/move state are covered for Hole/QZone; one seed=42 InferiorQualityZone worker-search trace now proves panic/exception propagation, RNG-after, unchanged layout/Tracker and callback cutoff. Multi-worker outer handling and optimizer listener/phase propagation remain open.
- [x] Add a Rust/C++ SeparatorWorker search failure reference from a clear reference placement through one unsupported-QZone container sample; compare RNG state after the sampled candidate, trace events, and layout/Tracker snapshots in Debug and Release.
- [x] No production divergence was established by the new Exterior and upper-bound-neighbor oracles; Rust/C++ Debug/Release produce the same raw loss and classification. No speculative production change was made.
- [x] Update S12 and the shared error table with the exact failure boundary and residual limits.

### Task 5: Close S09–S10 evaluator and ordering differences

**Files:**
- Rust oracle: SparrowCore/Tests/RustReference/tests/sample_eval_*.rs and evaluator references
- C++ tests: SparrowCore/Tests/TestLbfLoss.cpp and SparrowCore/Tests/TestSampling.cpp
- Implementation: SparrowCore/Include/SparrowCore/Eval/LbfEvaluator.h, SparrowCore/Source/Eval/LbfEvaluator.cpp, SparrowCore/Include/SparrowCore/Optimizer/SampleEval.h
- Records: docs/logic-comparison-sparrow/records/S09-lbf-evaluator.md and S10-sample-eval.md

- [x] Resolve the live-layout question against Rust's shared-borrow contract and enumerate consumers. Mutation during the borrow is not a valid safe-Rust input; C++ LBF/Separation evaluator headers now state the matching stability precondition. S10 consumers are BestSamples and CoordinateDescent; UniformSampler's `min_by_key` orders rotation geometry, not SampleEval.
- [x] Add Rust Debug/Release traces for the fixed legal-input ordering sequences implicated by the non-transitive comparator and for NaN failure state. Seven comparator boundary groups cover all six permutations; the four-item chain covers all 24 permutations; BestSamples covers a 32-report insertion sequence and four NaN failure states; coordinate descent has a separate near-tie consumer trace. These close only the listed fixtures, because Rust `Ord` remains non-transitive for arbitrary values.
- [x] No production ordering change was justified: S09's mutation scenario is outside safe Rust's borrow domain, and S10 fixtures currently show the same fixed Rust outcomes.
- [x] Existing fixed Debug/Release references show no profile disagreement; this statement is limited to the recorded sequences and does not certify arbitrary non-transitive orderings.
- [x] Update S09/S10 contracts and consumer inventory; existing focused Rust/C++ references and the full profile runners pass. The shared taxonomy already classifies NaN comparison failure as ERR-NUMERIC-ORDER.

### Task 6: Close S13–S16 search, coordinate descent, BestSamples, and sampler differences

**Files:**
- Rust oracle: SparrowCore/Tests/RustReference/tests/search_*.rs, best_samples_*.rs, uniform_sampler_*.rs
- C++ tests: SparrowCore/Tests/TestSampling.cpp and SparrowCore/Tests/TestLbfLoss.cpp
- Implementation: SparrowCore/Source/Optimizer/LbfSearch.cpp, SparrowCore/Source/Sampling/CoordinateDescent.cpp, SparrowCore/Source/Sampling/BestSamples.cpp, SparrowCore/Source/Sampling/UniformRectSampler.cpp
- Records: docs/logic-comparison-sparrow/records/S13-search.md through S16-uniform-sampler.md

- [x] Add the 5-degree/0.05-degree conversion boundary reference and verify consumer paths. Rust locks 5/1/0.5/0.05-degree f32 bits; LBF search and Separator worker Rust traces passed after both production call sites moved to the shared factor-first helper.
- [x] Extend the seed-42, 16-container-sample LBF trace with a post-search next_u64 marker; the C++ comparator checks the following RNG output after matching the event trace. This proves stream alignment only for this fixed trace.
- [x] Assert that the zero-budget empty LBF candidate pool leaves the seed-42 next_u64 unchanged in Rust and C++; the broader budget/reference-placement and capacity/similarity matrix remains open.
- [ ] Add candidate-budget, reference-placement, empty-pool, capacity, and mixed-similarity sequences with explicit seed and RNG-after state.
- [x] Add direct singleton sample-bbox X/Y/both-axis Rust reference cases and C++ raw-bit comparisons (24 samples total, seed 42). The old C++ rejection was reproduced before the minimal validation change; Rust Debug/Release and SparrowCore Debug/Release full profile tests pass.
- [x] Separate Rust no-fit (`None`) from C++ construction errors by using `Result<std::optional<UniformRectSampler>>`; test spatial no-fit, reversed bounds, and non-finite bounds on both sides, and propagate actual C++ errors through LBF/Separator instead of treating them as no-fit.
- [x] Verify a non-finite discrete rotation returns a C++ sampler error and that `SearchLbf` propagates it instead of reporting ordinary no-fit.
- [x] Compare the seed-42 NaN discrete-rotation panic in Rust search with C++ InvalidInput propagation; both stop before callbacks or RNG consumption in Debug and Release.
- [ ] Compare narrow intervals, failed shape transforms, unrepresentable f64 bounds, and Separator error state against the corresponding Rust paths.
- [ ] Complete the remaining S13/S15 candidate-budget, reference-placement, empty-pool, capacity, and mixed-similarity sequence matrix with explicit seed and RNG-after state.

### Task 7: Close S03–S05 LBF, separator, and worker differences

**Files:**
- Rust oracle: SparrowCore/Tests/RustReference/tests for LBF, separator, and construction
- C++ tests: SparrowCore/Tests/TestBppLbf.cpp, TestSppLbf.cpp, TestSeparator.cpp
- Implementation: SparrowCore/Source/Optimizer/SparrowLbfBuilder.cpp, SparrowCore/Source/Optimizer/LbfSearch.cpp, SparrowCore/Source/Optimizer/Separator.cpp, SparrowCore/Source/Optimizer/SeparatorWorker.cpp
- Records: docs/logic-comparison-sparrow/records/S03-lbf.md through S05-worker.md

- [ ] Compare NoFilter behavior in the presence of InferiorQualityZone, normal and zero budgets, construction failure, and focused-sample settings.
- [ ] Compare empty initial worker rounds, one/two/multiple workers, missing best samples, callback counts, and post-failure layout.
- [ ] Preserve Rust construction and worker state transitions; keep unmatched C++-only controls out of parity fixtures.
- [ ] Run Rust/C++ Debug/Release tests and update S03–S05 plus error classifications.

### Task 8: Close S06 Explore differences

**Files:**
- Rust oracle: SparrowCore/Tests/RustReference/tests/exploration_*.rs
- C++ tests: SparrowCore/Tests/TestExploration.cpp
- Implementation: SparrowCore/Source/Optimizer/Exploration.cpp
- Records: docs/logic-comparison-sparrow/records/S06-explore.md

- [ ] Convert each remaining S06 P0 gap into a minimal shared fixture covering candidate pool, RNG trace, relocation order, failure snapshot, and final state.
- [ ] Add test-first fixes for distinct-pool, contained relocation, rollback, and failure-limit behavior only where a live oracle demonstrates divergence.
- [ ] Run each affected fixture under Rust Debug/Release and C++ Debug/Release and update S06 with bounded claims.

### Task 9: Close S07–S08 Compression and orchestration differences

**Files:**
- Rust oracle: SparrowCore/Tests/RustReference/tests/optimizer_*.rs and compression_*.rs
- C++ tests: SparrowCore/Tests/TestSeparator.cpp, TestExploration.cpp, and optimizer integration tests
- Implementation: SparrowCore/Source/Optimizer/Compression.cpp, SparrowCore/Source/Optimizer/SparrowOptimizer.cpp, SparrowCore/Source/Optimizer/OptimizationTerminator.cpp
- Records: docs/logic-comparison-sparrow/records/S07-compress.md and S08-optimizer-mod.md

- [ ] Extend controlled-clock/callback cases to zero, sub-millisecond, positive deadline, current-attempt completion, and each shrink strategy.
- [ ] Compare cold/warm start, root/phase seeds, stage handoff, best solution, reports, and failure snapshots from Rust-derived inputs.
- [ ] Make test-first fixes for the first verified state, duration, seed, or stop-reason divergence.
- [ ] Run end-to-end Rust and C++ Debug/Release references and update S07/S08 and the taxonomy.

### Task 10: P0 closure audit

**Files:**
- Modify: docs/logic-comparison-sparrow/replication-roadmap.md
- Modify: docs/logic-comparison-sparrow/migration-decision-matrix.md
- Modify: docs/logic-comparison-sparrow/README.md
- Modify: all affected docs/logic-comparison-sparrow/records/S01–S18

- [ ] Re-run locked Rust Debug and Release tests, all SparrowCore Debug and Release tests, and all P0 differential fixtures.
- [ ] For each P0 row, record closed evidence, a stable Rust-domain limitation, or an unresolved blocker; do not mark unknown as closed.
- [ ] Recompute a post-fix source manifest under a new dated filename and link both the frozen pre-fix baseline and current implementation snapshot.
- [ ] Confirm that no P0 status is closed based only on final-layout similarity or C++-generated expectations.
