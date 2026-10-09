use jagua_rs::geometry::DTransformation;
use rand::SeedableRng;
use rand::rngs::Xoshiro256PlusPlus;
use sparrow::eval::sample_eval::{SampleEval, SampleEvaluator};
use sparrow::sample::{CDConfig, refine_coord_desc};
use std::cmp::Ordering;

const REFERENCE: &str =
    include_str!("../../Fixtures/SampleEvalComparatorBoundaryRustReference.tsv");
const COORDINATE_REFERENCE: &str =
    include_str!("../../Fixtures/SampleEvalCoordinateDescentRustReference.tsv");

#[derive(Clone, Copy)]
struct ComparatorCase {
    name: &'static str,
    evaluations: [SampleEval; 3],
    expected_relations: [Ordering; 3],
}

fn cases() -> [ComparatorCase; 7] {
    [
        ComparatorCase {
            name: "positive_ulp_chain",
            evaluations: [
                SampleEval::Clear {
                    loss: f32::from_bits(0x3f80_0000),
                },
                SampleEval::Clear {
                    loss: f32::from_bits(0x3f80_0004),
                },
                SampleEval::Clear {
                    loss: f32::from_bits(0x3f80_0008),
                },
            ],
            expected_relations: [Ordering::Equal, Ordering::Equal, Ordering::Less],
        },
        ComparatorCase {
            name: "epsilon_chain",
            evaluations: [
                SampleEval::Clear { loss: 0.0 },
                SampleEval::Clear {
                    loss: f32::EPSILON * 0.75,
                },
                SampleEval::Clear {
                    loss: f32::EPSILON * 1.5,
                },
            ],
            expected_relations: [Ordering::Equal, Ordering::Equal, Ordering::Less],
        },
        ComparatorCase {
            name: "negative_epsilon_chain",
            evaluations: [
                SampleEval::Clear {
                    loss: -f32::EPSILON * 1.5,
                },
                SampleEval::Clear {
                    loss: -f32::EPSILON * 0.75,
                },
                SampleEval::Clear { loss: 0.0 },
            ],
            expected_relations: [Ordering::Equal, Ordering::Equal, Ordering::Less],
        },
        ComparatorCase {
            name: "negative_ulp_chain",
            evaluations: [
                SampleEval::Clear {
                    loss: f32::from_bits(0xbf80_0000),
                },
                SampleEval::Clear {
                    loss: f32::from_bits(0xbf7f_fffc),
                },
                SampleEval::Clear {
                    loss: f32::from_bits(0xbf7f_fff8),
                },
            ],
            expected_relations: [Ordering::Equal, Ordering::Equal, Ordering::Less],
        },
        ComparatorCase {
            name: "positive_midrange_ulp_chain",
            evaluations: [
                SampleEval::Clear {
                    loss: f32::from_bits(0x4000_0000),
                },
                SampleEval::Clear {
                    loss: f32::from_bits(0x4000_0004),
                },
                SampleEval::Clear {
                    loss: f32::from_bits(0x4000_0008),
                },
            ],
            expected_relations: [Ordering::Equal, Ordering::Equal, Ordering::Less],
        },
        ComparatorCase {
            name: "max_finite_to_infinity_chain",
            evaluations: [
                SampleEval::Clear {
                    loss: f32::from_bits(0x7f7f_fffb),
                },
                SampleEval::Clear { loss: f32::MAX },
                SampleEval::Clear {
                    loss: f32::INFINITY,
                },
            ],
            expected_relations: [Ordering::Equal, Ordering::Equal, Ordering::Less],
        },
        ComparatorCase {
            name: "signed_zero_ties",
            evaluations: [
                SampleEval::Clear { loss: 0.0 },
                SampleEval::Clear { loss: -0.0 },
                SampleEval::Clear { loss: 0.0 },
            ],
            expected_relations: [Ordering::Equal, Ordering::Equal, Ordering::Equal],
        },
    ]
}

fn input_orders() -> [[usize; 3]; 6] {
    [
        [0, 1, 2],
        [0, 2, 1],
        [1, 0, 2],
        [1, 2, 0],
        [2, 0, 1],
        [2, 1, 0],
    ]
}

struct ApproximateTieEvaluator {
    evaluations: Vec<DTransformation>,
}

impl SampleEvaluator for ApproximateTieEvaluator {
    fn evaluate_sample(
        &mut self,
        transform: DTransformation,
        _upper_bound: Option<SampleEval>,
    ) -> SampleEval {
        self.evaluations.push(transform);
        SampleEval::Clear { loss: 1.0 }
    }

    fn n_evals(&self) -> usize {
        self.evaluations.len()
    }
}

fn encode_order(order: &[usize; 3]) -> String {
    order
        .iter()
        .map(|id| char::from(b'A' + *id as u8))
        .collect()
}

fn rust_sort(case: &ComparatorCase, input: [usize; 3]) -> [usize; 3] {
    let mut samples = input.map(|id| (id, case.evaluations[id]));
    samples.sort_by_key(|(_, evaluation)| *evaluation);
    samples.map(|(id, _)| id)
}

#[test]
fn comparator_boundaries_match_the_locked_rust_ordering_reference() {
    let mut actual_rows = Vec::new();
    for case in cases() {
        let [a, b, c] = case.evaluations;
        assert_eq!(a.cmp(&b), case.expected_relations[0], "{}: A/B", case.name);
        assert_eq!(b.cmp(&c), case.expected_relations[1], "{}: B/C", case.name);
        assert_eq!(a.cmp(&c), case.expected_relations[2], "{}: A/C", case.name);

        for input in input_orders() {
            actual_rows.push(format!(
                "{}\t{}\t{}",
                case.name,
                encode_order(&input),
                encode_order(&rust_sort(&case, input)),
            ));
        }
    }

    if std::env::var_os("SPARROW_DUMP_SAMPLE_EVAL_BOUNDARY_REFERENCE").is_some() {
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
        "Rust SampleEval boundary trace changed"
    );
}

#[test]
fn signed_zero_equality_matches_derived_rust_partial_eq() {
    let positive_zero = SampleEval::Clear { loss: 0.0 };
    let negative_zero = SampleEval::Clear { loss: -0.0 };
    assert_eq!(positive_zero, negative_zero);
}

#[test]
fn coordinate_descent_uses_rust_approximate_tie_ordering() {
    let initial = DTransformation::new(0.5, (2.0, -1.0));
    let initial_eval = SampleEval::Clear {
        loss: f32::from_bits(0x3f80_0004),
    };
    let config = CDConfig {
        t_step_init: 1.0,
        t_step_limit: 0.6,
        r_step_init: 0.0,
        r_step_limit: 0.0,
        wiggle: false,
    };
    let mut evaluator = ApproximateTieEvaluator {
        evaluations: Vec::new(),
    };
    let mut rng = Xoshiro256PlusPlus::seed_from_u64(42);
    let (result, evaluation) =
        refine_coord_desc((initial, initial_eval), &mut evaluator, config, &mut rng);

    let mut actual_rows = evaluator
        .evaluations
        .iter()
        .map(|candidate| {
            let (x, y) = candidate.translation();
            format!(
                "candidate\t{:08x}\t{:08x}\t{:08x}",
                candidate.rotation().to_bits(),
                x.to_bits(),
                y.to_bits(),
            )
        })
        .collect::<Vec<_>>();
    let (x, y) = result.translation();
    let evaluation_loss = match evaluation {
        SampleEval::Clear { loss } => loss,
        _ => panic!("approximate-tie evaluator returned a non-clear result"),
    };
    actual_rows.push(format!(
        "result\t{:08x}\t{:08x}\t{:08x}\t{:08x}\t{}",
        result.rotation().to_bits(),
        x.to_bits(),
        y.to_bits(),
        evaluation_loss.to_bits(),
        evaluator.n_evals(),
    ));

    if std::env::var_os("SPARROW_DUMP_SAMPLE_EVAL_BOUNDARY_REFERENCE").is_some() {
        for row in &actual_rows {
            println!("{row}");
        }
    }
    let expected_rows = COORDINATE_REFERENCE
        .lines()
        .filter(|line| !line.is_empty() && !line.starts_with('#'))
        .map(str::to_owned)
        .collect::<Vec<_>>();
    assert_eq!(
        actual_rows, expected_rows,
        "coordinate descent comparator boundary trace changed",
    );
}

#[test]
#[should_panic]
fn nan_sample_eval_ordering_panics_like_rust_fpa_partial_cmp_unwrap() {
    let nan = SampleEval::Clear { loss: f32::NAN };
    let finite = SampleEval::Clear { loss: 1.0 };
    let _ = nan.cmp(&finite);
}
