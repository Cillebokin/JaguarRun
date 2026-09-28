# SparrowCore Rust-equivalent algorithm design

## Goal

Make SparrowCore functionally equivalent to the current Rust `sparrow` implementation for the SPP optimization path, while keeping JaguarCore as the geometry, collision-engine, layout, and JSON/SVG foundation.

## Reference and scope

- Behavioral reference: `CodeResource/sparrow` in this workspace.
- Scope: SPP construction, sampling, collision quantification, separation, exploration, compression, termination, warm start, and observable optimization progress.
- Existing C++ extensions such as BPP LBF remain available but are not part of the Sparrow parity gate.
- WASM packaging and UI polish are deferred until the native algorithm passes parity checks.

## Design

Keep the current public C++17 module layout and adapt the internals to the Rust algorithm rather than rewriting the module. Reproduce the reference formulas and state transitions first, then map Rust concepts to existing JaguarCore snapshots, handles, `Result<T>`, and value-semantic C++ types.

The data flow is:

1. Construct or restore an SPP solution.
2. Run the reference LBF placement search with uniform/focused samples, `BestSamples`, and two-stage coordinate descent.
3. Build a hazard-backed collision tracker whose entries are zero unless JaguarCore reports the corresponding collision.
4. Run the reference separator with independent workers, random item orders, weighted-loss selection, rollback, strikes, and GLS weight updates.
5. Run exploration with the minimum-width bound, sorted infeasible pool, weighted pool selection, large-item swap, and contained-item relocation.
6. Run compression with random split-position strip shrinking and reference decay rules.
7. Validate structure and geometry before returning the final solution.

## Compatibility invariants

- Seeded RNG calls remain deterministic and are consumed in the same logical order as the reference.
- Discrete, continuous, and fixed rotations remain legal after every sampled or transformed placement.
- A candidate is accepted only when both JaguarCore structural validation and geometric feasibility agree.
- Snapshot restore must preserve layout handles, tracker losses, and tracker weights consistently.
- Existing listener, CLI, and WASM callers remain source-compatible where possible.

## Verification

Each phase starts with a failing regression test. Tests compare the C++ result with fixed reference fixtures for RNG values, loss formulas, sample ordering, separator decisions, phase transitions, final feasibility, and deterministic checksums. Release and Debug builds must both pass; a passing C++ test without a Rust-reference comparison is not considered parity evidence.

