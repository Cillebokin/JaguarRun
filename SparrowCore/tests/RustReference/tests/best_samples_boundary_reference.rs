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

const REFERENCE: &str = include_str!("../../Fixtures/BestSamplesBoundaryRustReference.tsv");

#[derive(Clone, Copy)]
struct Operation {
    rotation: f32,
    x: f32,
    y: f32,
    loss: f32,
}

struct BoundaryCase {
    name: &'static str,
    capacity: usize,
    threshold: f32,
    operations: Vec<Operation>,
}

fn operation(x: f32, y: f32, rotation: f32, loss: f32) -> Operation {
    Operation {
        rotation,
        x,
        y,
        loss,
    }
}

fn cases() -> Vec<BoundaryCase> {
    let translation_threshold = 0.05_f32;
    let translation_below = f32::from_bits(translation_threshold.to_bits() - 1);
    let rotation_threshold = 1.0_f32.to_radians();
    let rotation_below = f32::from_bits(rotation_threshold.to_bits() - 1);

    vec![
        BoundaryCase {
            name: "translation_f32_rounding",
            capacity: 3,
            threshold: translation_threshold,
            operations: vec![
                operation(1.0, 0.0, 0.0, 1.0),
                // The C++ input deliberately retains this value as f64. Rust's
                // DTransformation stores the nearest f32 value before subtracting.
                operation(1.05000001_f32, 0.0, 0.0, 2.0),
            ],
        },
        BoundaryCase {
            name: "translation_threshold_positive_infinity",
            capacity: 3,
            threshold: f32::INFINITY,
            operations: vec![
                operation(0.0, 0.0, 0.0, 1.0),
                operation(10.0, 0.0, 0.0, 2.0),
            ],
        },
        BoundaryCase {
            name: "translation_threshold_nan",
            capacity: 3,
            threshold: f32::NAN,
            operations: vec![operation(0.0, 0.0, 0.0, 1.0), operation(0.0, 0.0, 0.0, 2.0)],
        },
        BoundaryCase {
            name: "translation_threshold_negative_infinity",
            capacity: 3,
            threshold: f32::NEG_INFINITY,
            operations: vec![operation(0.0, 0.0, 0.0, 1.0), operation(0.0, 0.0, 0.0, 2.0)],
        },
        BoundaryCase {
            name: "translation_x_exact",
            capacity: 3,
            threshold: translation_threshold,
            operations: vec![
                operation(0.0, 0.0, 0.0, 1.0),
                operation(translation_threshold, 0.0, 0.0, 2.0),
            ],
        },
        BoundaryCase {
            name: "translation_x_below",
            capacity: 3,
            threshold: translation_threshold,
            operations: vec![
                operation(0.0, 0.0, 0.0, 1.0),
                operation(translation_below, 0.0, 0.0, 2.0),
            ],
        },
        BoundaryCase {
            name: "translation_y_exact",
            capacity: 3,
            threshold: translation_threshold,
            operations: vec![
                operation(0.0, 0.0, 0.0, 1.0),
                operation(0.0, translation_threshold, 0.0, 2.0),
            ],
        },
        BoundaryCase {
            name: "translation_y_below",
            capacity: 3,
            threshold: translation_threshold,
            operations: vec![
                operation(0.0, 0.0, 0.0, 1.0),
                operation(0.0, translation_below, 0.0, 2.0),
            ],
        },
        BoundaryCase {
            name: "rotation_one_degree_exact",
            capacity: 3,
            threshold: translation_threshold,
            operations: vec![
                operation(0.0, 0.0, 0.0, 1.0),
                operation(0.0, 0.0, rotation_threshold, 2.0),
            ],
        },
        BoundaryCase {
            name: "rotation_one_degree_below",
            capacity: 3,
            threshold: translation_threshold,
            operations: vec![
                operation(0.0, 0.0, 0.0, 1.0),
                operation(0.0, 0.0, rotation_below, 2.0),
            ],
        },
        BoundaryCase {
            name: "capacity_three",
            capacity: 3,
            threshold: 0.5,
            operations: vec![
                operation(0.0, 0.0, 0.0, 5.0),
                operation(10.0, 0.0, 0.0, 3.0),
                operation(20.0, 0.0, 0.0, 4.0),
                operation(30.0, 0.0, 0.0, 5.0),
                operation(40.0, 0.0, 0.0, 6.0),
                operation(50.0, 0.0, 0.0, 4.5),
                operation(60.0, 0.0, 0.0, 2.0),
            ],
        },
        BoundaryCase {
            name: "capacity_one",
            capacity: 1,
            threshold: 0.5,
            operations: vec![
                operation(0.0, 0.0, 0.0, 5.0),
                operation(10.0, 0.0, 0.0, 4.0),
                operation(20.0, 0.0, 0.0, 6.0),
            ],
        },
    ]
}

fn loss_bits(loss: f32) -> String {
    format!("{:08x}", loss.to_bits())
}

fn upper_bound_tag(evaluation: SampleEval) -> String {
    match evaluation {
        SampleEval::Invalid => "invalid".to_owned(),
        SampleEval::Clear { loss } => loss_bits(loss),
        SampleEval::Collision { loss } => format!("collision:{}", loss_bits(loss)),
    }
}

#[test]
fn best_samples_similarity_and_capacity_boundaries_match_locked_rust_trace() {
    let mut actual_rows = Vec::new();
    for case in cases() {
        let mut samples = BestSamples::new(case.capacity, case.threshold);
        for (index, operation) in case.operations.iter().enumerate() {
            let transform = DTransformation::new(operation.rotation, (operation.x, operation.y));
            let accepted = samples.report(
                transform,
                SampleEval::Clear {
                    loss: operation.loss,
                },
            );
            let retained = samples
                .samples
                .iter()
                .map(|(_, evaluation)| match evaluation {
                    SampleEval::Clear { loss } => loss_bits(*loss),
                    SampleEval::Collision { loss } => {
                        format!("collision:{}", loss_bits(*loss))
                    }
                    SampleEval::Invalid => "invalid".to_owned(),
                })
                .collect::<Vec<_>>()
                .join(",");
            actual_rows.push(format!(
                "{}\t{}\t{}\t{}\t{}\t{}",
                case.name,
                index + 1,
                u8::from(accepted),
                samples.samples.len(),
                if retained.is_empty() { "-" } else { &retained },
                upper_bound_tag(samples.upper_bound()),
            ));
        }
    }

    if std::env::var_os("SPARROW_DUMP_BEST_SAMPLES_BOUNDARY_REFERENCE").is_some() {
        for row in &actual_rows {
            println!("{row}");
        }
    }

    let expected_rows = REFERENCE
        .lines()
        .filter(|line| !line.is_empty() && !line.starts_with('#') && !line.starts_with("case\t"))
        .map(str::to_owned)
        .collect::<Vec<_>>();
    assert_eq!(
        actual_rows, expected_rows,
        "Rust BestSamples similarity/capacity trace changed"
    );
}

#[test]
fn zero_capacity_behavior_is_profile_specific_in_rust() {
    let mut samples = BestSamples::new(0, 0.5);
    let report = catch_unwind(AssertUnwindSafe(|| {
        samples.report(
            DTransformation::new(0.0, (0.0, 0.0)),
            SampleEval::Clear { loss: 1.0 },
        )
    }));
    let upper_bound = catch_unwind(AssertUnwindSafe(|| samples.upper_bound()));

    if cfg!(debug_assertions) {
        assert!(report.is_err(), "Debug must detect size - 1 underflow");
        assert!(
            upper_bound.is_err(),
            "Debug upper_bound must detect underflow"
        );
        assert!(samples.samples.is_empty());
    } else {
        assert!(matches!(report, Ok(true)));
        assert!(matches!(upper_bound, Ok(SampleEval::Invalid)));
        assert_eq!(samples.samples.len(), 1);
        assert_eq!(samples.upper_bound(), SampleEval::Invalid);
        assert!(samples.report(
            DTransformation::new(0.0, (10.0, 0.0)),
            SampleEval::Clear { loss: 2.0 },
        ));
        assert_eq!(samples.samples.len(), 2);
    }
}
