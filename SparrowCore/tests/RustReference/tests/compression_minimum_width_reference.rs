#[path = "../src/compression_minimum_width_trace.rs"]
mod compression_minimum_width_trace;

const REFERENCE: &str = include_str!("../../Fixtures/CompressionMinimumWidthRustReference.tsv");

#[test]
fn compression_minimum_width_and_threshold_behavior_match_locked_rust_trace() {
    let actual = compression_minimum_width_trace::compression_minimum_width_trace_tsv()
        .expect("run compression minimum-width boundary oracle");
    assert_eq!(
        actual, REFERENCE,
        "Rust compression minimum-width boundary trace changed"
    );
}
