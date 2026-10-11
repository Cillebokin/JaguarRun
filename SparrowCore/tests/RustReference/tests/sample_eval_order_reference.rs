use sparrow::eval::sample_eval::SampleEval;
use std::cmp::Ordering;

const REFERENCE: &str = include_str!("../../Fixtures/SampleEvalNonTransitiveSortRustReference.tsv");

fn observed_order(input: &[usize], evaluations: &[SampleEval]) -> Vec<usize> {
    let mut samples = input
        .iter()
        .map(|&id| (id, evaluations[id]))
        .collect::<Vec<_>>();
    samples.sort_by_key(|(_, evaluation)| *evaluation);
    samples.into_iter().map(|(id, _)| id).collect()
}

fn four_sample_orders() -> [[usize; 4]; 24] {
    [
        [0, 1, 2, 3],
        [0, 1, 3, 2],
        [0, 2, 1, 3],
        [0, 2, 3, 1],
        [0, 3, 1, 2],
        [0, 3, 2, 1],
        [1, 0, 2, 3],
        [1, 0, 3, 2],
        [1, 2, 0, 3],
        [1, 2, 3, 0],
        [1, 3, 0, 2],
        [1, 3, 2, 0],
        [2, 0, 1, 3],
        [2, 0, 3, 1],
        [2, 1, 0, 3],
        [2, 1, 3, 0],
        [2, 3, 0, 1],
        [2, 3, 1, 0],
        [3, 0, 1, 2],
        [3, 0, 2, 1],
        [3, 1, 0, 2],
        [3, 1, 2, 0],
        [3, 2, 0, 1],
        [3, 2, 1, 0],
    ]
}

fn encode_order(order: &[usize]) -> String {
    order
        .iter()
        .map(|id| char::from(b'A' + *id as u8))
        .collect()
}

#[test]
fn four_sample_non_transitive_sort_matches_the_locked_rust_reference() {
    let evaluations =
        [0x3f80_0000, 0x3f80_0004, 0x3f80_0008, 0x3f80_000c].map(|bits| SampleEval::Clear {
            loss: f32::from_bits(bits),
        });
    assert_eq!(evaluations[0].cmp(&evaluations[1]), Ordering::Equal);
    assert_eq!(evaluations[1].cmp(&evaluations[2]), Ordering::Equal);
    assert_eq!(evaluations[2].cmp(&evaluations[3]), Ordering::Equal);
    assert_eq!(evaluations[0].cmp(&evaluations[2]), Ordering::Less);
    assert_eq!(evaluations[1].cmp(&evaluations[3]), Ordering::Less);

    let actual_rows = four_sample_orders()
        .iter()
        .map(|input| {
            format!(
                "{}\t{}",
                encode_order(input),
                encode_order(&observed_order(input, &evaluations)),
            )
        })
        .collect::<Vec<_>>();
    if std::env::var_os("SPARROW_DUMP_SAMPLE_EVAL_SEQUENCE_REFERENCE").is_some() {
        for row in &actual_rows {
            println!("{row}");
        }
    }

    let expected_rows = REFERENCE
        .lines()
        .filter(|line| {
            !line.is_empty() && !line.starts_with('#') && !line.starts_with("input_order\t")
        })
        .map(str::to_owned)
        .collect::<Vec<_>>();
    assert_eq!(
        actual_rows, expected_rows,
        "four-sample non-transitive Rust sort trace changed",
    );
}
