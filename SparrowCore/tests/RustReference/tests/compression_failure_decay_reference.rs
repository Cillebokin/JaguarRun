#[path = "../src/compression_failure_decay_trace.rs"]
mod compression_failure_decay_trace;

const REFERENCE: &str =
    include_str!("../../Fixtures/CompressionFailureDecayThresholdRustReference.tsv");
const EXPONENT_BOUNDARY_REFERENCE: &str =
    include_str!("../../Fixtures/CompressionFailureDecayExponentBoundaryRustReference.tsv");

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

#[test]
fn failure_based_exponents_match_the_rust_f32_threshold_reference() {
    let actual = compression_failure_decay_trace::failure_decay_exponent_boundary_trace_tsv()
        .expect("run FailureBased exponent boundary oracle");
    let expected = EXPONENT_BOUNDARY_REFERENCE.replace("\r\n", "\n");
    assert_eq!(
        actual, expected,
        "Rust FailureBased exponent threshold trace changed"
    );
}
