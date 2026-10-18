#[path = "../src/compression_failure_decay_trace.rs"]
mod compression_failure_decay_trace;

const REFERENCE: &str =
    include_str!("../../Fixtures/CompressionFailureDecayThresholdRustReference.tsv");

#[test]
fn failure_based_decay_stops_at_the_rust_f32_minimum_threshold() {
    let actual = compression_failure_decay_trace::failure_decay_threshold_trace_tsv()
        .expect("run FailureBased shrink-min threshold oracle");
    let expected = REFERENCE.replace("\r\n", "\n");
    assert_eq!(
        actual, expected,
        "Rust FailureBased shrink-min threshold trace changed"
    );
}
