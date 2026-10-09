#[path = "../src/exploration_pool_sampling_trace.rs"]
mod exploration_pool_sampling_trace;

const REFERENCE: &str = include_str!("../../Fixtures/ExplorationEqualLossPoolSamplerRustTrace.tsv");

#[test]
fn actual_exploration_pool_and_rng_trace_match_locked_rust_reference() {
    let actual = exploration_pool_sampling_trace::exploration_pool_sampling_trace_tsv()
        .expect("run the equal-loss exploration trace");
    assert_eq!(
        actual, REFERENCE,
        "actual Rust Explore pool/sample trace changed"
    );
}
