use rand::rngs::Xoshiro256PlusPlus;
use rand::seq::SliceRandom;
use rand::{Rng, SeedableRng};

const REFERENCE: &str = include_str!("../../Fixtures/SparrowSliceShuffleRustReference.tsv");

fn slice_shuffle_trace() -> String {
    const SEEDS: [u64; 4] = [0, 1, 42, u64::MAX];
    const MAX_LEN: usize = 32;

    let mut output = String::from(
        "# generator=locked Sparrow RustReference rand 0.10.3 SliceRandom::shuffle\n\
         # seed length permutation next_u64_after_shuffle[0..3]\n",
    );
    for seed in SEEDS {
        for length in 0..=MAX_LEN {
            let mut rng = Xoshiro256PlusPlus::seed_from_u64(seed);
            let mut values: Vec<usize> = (0..length).collect();
            values.shuffle(&mut rng);
            let permutation = if values.is_empty() {
                "-".to_owned()
            } else {
                values
                    .iter()
                    .map(usize::to_string)
                    .collect::<Vec<_>>()
                    .join(",")
            };
            output.push_str(&format!(
                "shuffle {seed} {length} {permutation} {:016x} {:016x} {:016x} {:016x}\n",
                rng.next_u64(),
                rng.next_u64(),
                rng.next_u64(),
                rng.next_u64()
            ));
        }
    }
    output
}

#[test]
fn checked_in_slice_shuffle_fixture_matches_locked_rand() {
    assert_eq!(
        slice_shuffle_trace(),
        REFERENCE.replace("\r\n", "\n"),
        "SliceRandom shuffle or post-shuffle RNG output changed"
    );
}
