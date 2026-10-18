#[path = "../src/compression_minimum_width_trace.rs"]
mod compression_minimum_width_trace;

const REFERENCE: &str = include_str!("../../Fixtures/CompressionMinimumWidthRustReference.tsv");

#[test]
fn compression_minimum_width_and_threshold_behavior_match_locked_rust_trace() {
    let actual = compression_minimum_width_trace::compression_minimum_width_trace_tsv()
        .expect("run compression minimum-width boundary oracle");
    let expected = REFERENCE.replace("\r\n", "\n");
    assert_eq!(
        actual, expected,
        "Rust compression minimum-width boundary trace changed"
    );
}

#[test]
fn failure_based_decay_zero_and_one_reach_the_compression_phase() {
    let actual = compression_minimum_width_trace::failure_decay_endpoint_attempt_counts()
        .expect("run failure-decay endpoint oracle");
    assert_eq!(actual, vec![(0.0, 1), (1.0, 1)]);
}

#[test]
fn shrink_range_zero_and_one_endpoints_follow_compression_width_guards() {
    let actual = compression_minimum_width_trace::shrink_range_endpoint_attempt_counts()
        .expect("run compression shrink-range endpoint oracle");
    assert_eq!(actual, vec![(0.0, 0.0, 0), (0.01, 0.0, 1), (1.0, 1.0, 0)]);
}

#[test]
fn direct_time_based_zero_duration_skips_compression_attempts() {
    let actual = compression_minimum_width_trace::zero_duration_time_based_state()
        .expect("run direct zero-duration TimeBased compression oracle");
    assert_eq!(actual, (0x4080_0000, 0x4080_0000, 0));
}
