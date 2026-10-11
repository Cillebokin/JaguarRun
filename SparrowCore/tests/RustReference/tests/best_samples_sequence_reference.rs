use jagua_rs::geometry::DTransformation;
use std::panic::{AssertUnwindSafe, catch_unwind};

#[path = "../../../../CodeResource/sparrow/src/eval/sample_eval.rs"]
#[rustfmt::skip]
#[allow(dead_code)]
mod sample_eval_impl;
mod eval {
    pub mod sample_eval {
        pub use crate::sample_eval_impl::*;
    }
}
#[path = "../../../../CodeResource/sparrow/src/sample/best_samples.rs"]
#[rustfmt::skip]
#[allow(dead_code)]
mod best_samples_impl;

use best_samples_impl::BestSamples;
use sample_eval_impl::SampleEval;

const REFERENCE: &str = include_str!("../../Fixtures/BestSamplesSequenceRustReference.tsv");

#[derive(Clone, Copy)]
struct Operation {
    x: f32,
    evaluation: SampleEval,
}

struct Case {
    name: &'static str,
    capacity: usize,
    threshold: f32,
    operations: Vec<Operation>,
}

fn clear(x: f32, loss: f32) -> Operation {
    Operation {
        x,
        evaluation: SampleEval::Clear { loss },
    }
}

fn cases() -> Vec<Case> {
    let mixed_order = [
        0_u32, 13, 26, 7, 20, 1, 14, 27, 8, 21, 2, 15, 28, 9, 22, 3, 16, 29, 10, 23, 4, 17, 30, 11,
        24, 5, 18, 31, 12, 25, 6, 19,
    ];
    let long_non_transitive = mixed_order
        .into_iter()
        .map(|id| clear(id as f32 * 10.0, f32::from_bits(0x3f80_0000 + id * 4)))
        .collect();

    vec![
        Case {
            name: "nontransitive_32_mixed_insertions",
            capacity: 32,
            threshold: 0.5,
            operations: long_non_transitive,
        },
        Case {
            name: "nan_upper_bound_comparison",
            capacity: 1,
            threshold: 0.5,
            operations: vec![clear(0.0, f32::NAN), clear(10.0, 1.0)],
        },
        Case {
            name: "nan_similar_comparison",
            capacity: 2,
            threshold: 0.5,
            operations: vec![clear(0.0, f32::NAN), clear(0.0, 1.0)],
        },
        Case {
            name: "nan_sort_comparison",
            capacity: 2,
            threshold: 0.5,
            operations: vec![clear(0.0, f32::NAN), clear(10.0, 1.0)],
        },
        Case {
            name: "nan_different_kind_comparison",
            capacity: 2,
            threshold: 0.5,
            operations: vec![
                clear(0.0, f32::NAN),
                Operation {
                    x: 10.0,
                    evaluation: SampleEval::Collision { loss: 1.0 },
                },
            ],
        },
    ]
}

fn float_bits(value: f32) -> String {
    format!("{:08x}", value.to_bits())
}

fn evaluation_tag(evaluation: SampleEval) -> String {
    match evaluation {
        SampleEval::Clear { loss } => format!("clear:{}", float_bits(loss)),
        SampleEval::Collision { loss } => format!("collision:{}", float_bits(loss)),
        SampleEval::Invalid => "invalid".to_owned(),
    }
}

fn main_trace() -> Vec<String> {
    let mut rows = Vec::new();
    for case in cases() {
        let mut samples = BestSamples::new(case.capacity, case.threshold);
        for (index, operation) in case.operations.iter().enumerate() {
            let transform = DTransformation::new(0.0, (operation.x, 0.0));
            let result = catch_unwind(AssertUnwindSafe(|| {
                samples.report(transform, operation.evaluation)
            }));
            let outcome = match result {
                Ok(true) => "accepted",
                Ok(false) => "rejected",
                Err(_) => "panic",
            };
            let retained = samples
                .samples
                .iter()
                .map(|(_, evaluation)| evaluation_tag(*evaluation))
                .collect::<Vec<_>>()
                .join(",");
            rows.push(format!(
                "{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}",
                case.name,
                index + 1,
                case.capacity,
                float_bits(case.threshold),
                float_bits(0.0),
                float_bits(operation.x),
                float_bits(0.0),
                evaluation_tag(operation.evaluation),
                outcome,
                samples.samples.len(),
                if retained.is_empty() { "-" } else { &retained },
                evaluation_tag(samples.upper_bound()),
            ));
        }
    }
    rows
}

#[test]
fn best_samples_long_nontransitive_and_nan_failure_traces_match_locked_rust() {
    let actual_rows = main_trace();
    if std::env::var_os("SPARROW_DUMP_BEST_SAMPLES_SEQUENCE_REFERENCE").is_some() {
        for row in &actual_rows {
            println!("REFERENCE\t{row}");
        }
        return;
    }

    let expected_rows = REFERENCE
        .lines()
        .filter(|line| !line.is_empty() && !line.starts_with('#') && !line.starts_with("case\t"))
        .map(str::to_owned)
        .collect::<Vec<_>>();
    assert_eq!(
        actual_rows, expected_rows,
        "Rust BestSamples long-sequence and NaN failure trace changed"
    );
}
