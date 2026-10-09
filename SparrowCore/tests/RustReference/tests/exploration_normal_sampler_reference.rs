#[path = "../src/exploration_normal_trace.rs"]
mod exploration_normal_trace;

const REFERENCE: &str = include_str!("../../Fixtures/ExplorationNormalPoolSamplesRust.tsv");

#[test]
fn normal_sample_outputs_and_following_rng_words_match_locked_rust_trace() {
    let actual = exploration_normal_trace::exploration_normal_sample_reference_tsv();
    assert_eq!(
        actual, REFERENCE,
        "locked Rust normal-sampler trace changed"
    );
}
