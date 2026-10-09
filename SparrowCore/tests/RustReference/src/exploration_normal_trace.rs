use rand::rngs::Xoshiro256PlusPlus;
use rand::{Rng, SeedableRng};
use rand_distr::{Distribution, Normal};
use std::fmt::Write;

const SAMPLE_COUNT: usize = 128;
const NEXT_WORDS: usize = 4;
const MAX_SAMPLE_WORDS: usize = 64;
const SEEDS: [u64; 5] = [0, 1, 42, 596, 20_261_008];
const STANDARD_DEVIATIONS: [(&str, f32); 4] = [
    ("small", 0.000001),
    ("default", 0.25),
    ("unit", 1.0),
    ("tiny", 1.0e-20),
];

fn consumed_words(before: &Xoshiro256PlusPlus, after: &Xoshiro256PlusPlus) -> usize {
    let mut probe = before.clone();
    for words in 0..=MAX_SAMPLE_WORDS {
        if &probe == after {
            return words;
        }
        probe.next_u64();
    }
    panic!("normal sample consumed more than {MAX_SAMPLE_WORDS} u64 words");
}

pub fn exploration_normal_sample_reference_tsv() -> String {
    let mut output = String::from(
        "# generator=rand_distr 0.6.0 Normal<f32> with Xoshiro256PlusPlus; pinned rustc 1.98.1\n\
         # columns: case seed stddev_f32_bits sample_count\n\
         # sample columns: ordinal sample_f32_bits consumed_u64_words first_layer_index next_u64_after_0..3\n",
    );

    for (stddev_name, stddev) in STANDARD_DEVIATIONS {
        let mut has_fast_path = false;
        let mut has_rejection_path = false;
        let mut has_tail_path = false;
        let mut has_tail_retry_path = false;

        for seed in SEEDS {
            let case = format!("{stddev_name}_seed_{seed}");
            let distribution =
                Normal::<f32>::new(0.0, stddev).expect("finite normal standard deviation");
            let mut rng = Xoshiro256PlusPlus::seed_from_u64(seed);

            writeln!(
                output,
                "case {case} {seed} {:08x} {SAMPLE_COUNT}",
                stddev.to_bits(),
            )
            .unwrap();

            for ordinal in 0..SAMPLE_COUNT {
                let before = rng.clone();
                let first_layer_index = (before.clone().next_u64() & 0xff) as u8;
                let sample = distribution.sample(&mut rng);
                let words = consumed_words(&before, &rng);
                let mut following = rng.clone();
                let following_words =
                    std::array::from_fn::<_, NEXT_WORDS, _>(|_| following.next_u64());

                has_fast_path |= words == 1;
                has_rejection_path |= first_layer_index != 0 && words >= 2;
                has_tail_path |= first_layer_index == 0 && words >= 3;
                has_tail_retry_path |= first_layer_index == 0 && words >= 5;

                writeln!(
                    output,
                    "sample {ordinal} {:08x} {words} {first_layer_index:02x} {:016x} {:016x} {:016x} {:016x}",
                    sample.to_bits(),
                    following_words[0],
                    following_words[1],
                    following_words[2],
                    following_words[3],
                )
                .unwrap();
            }
        }

        assert!(
            has_fast_path,
            "{stddev_name} must include a Ziggurat fast-path sample"
        );
        assert!(
            has_rejection_path,
            "{stddev_name} must include a non-tail Ziggurat rejection-path sample"
        );
        assert!(
            has_tail_path,
            "{stddev_name} must include a Ziggurat tail sample"
        );
        assert!(
            has_tail_retry_path,
            "{stddev_name} must include a Ziggurat tail-retry sample"
        );
    }
    output
}
