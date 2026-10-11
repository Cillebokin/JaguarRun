use rand::rngs::Xoshiro256PlusPlus;
use rand::{Rng, RngExt, SeedableRng};

const SEED: u64 = 42;
const SAMPLE_COUNT: usize = 4;
const RNG_FIXTURE: &str = include_str!("../../Fixtures/LbfSeed42Reference.tsv");
const UNIFORM_INDEX_FIXTURE: &str =
    include_str!("../../Fixtures/SparrowUniformIndexRustReference.tsv");

#[test]
fn checked_in_rng_fixture_matches_sparrow_rust_rng() {
    let mut raw_rng = Xoshiro256PlusPlus::seed_from_u64(SEED);
    let raw_values: Vec<u64> = (0..SAMPLE_COUNT).map(|_| raw_rng.next_u64()).collect();
    let mut uniform_rng = Xoshiro256PlusPlus::seed_from_u64(SEED);
    let uniform_values: Vec<f32> = (0..SAMPLE_COUNT)
        .map(|_| uniform_rng.random_range(0.0_f32..1.0_f32))
        .collect();

    let mut raw_seen = [false; SAMPLE_COUNT];
    let mut uniform_seen = [false; SAMPLE_COUNT];
    for line in RNG_FIXTURE
        .lines()
        .filter(|line| !line.is_empty() && !line.starts_with('#'))
    {
        let fields: Vec<&str> = line.split_whitespace().collect();
        assert_eq!(fields.len(), 5, "malformed RNG fixture row: {line}");
        assert_eq!(fields[0].parse::<u64>().unwrap(), SEED);
        let ordinal = fields[2].parse::<usize>().unwrap();
        assert!(ordinal < SAMPLE_COUNT, "RNG fixture ordinal out of range");

        match fields[1] {
            "raw_u64" => {
                assert_eq!(fields[3].parse::<u64>().unwrap(), raw_values[ordinal]);
                assert_eq!(
                    u64::from_str_radix(fields[4], 16).unwrap(),
                    raw_values[ordinal]
                );
                raw_seen[ordinal] = true;
            }
            "uniform_f32" => {
                assert_eq!(
                    fields[3].parse::<f32>().unwrap().to_bits(),
                    uniform_values[ordinal].to_bits()
                );
                assert_eq!(fields[4], "-");
                uniform_seen[ordinal] = true;
            }
            primitive => panic!("unknown RNG fixture primitive: {primitive}"),
        }
    }

    assert!(raw_seen.into_iter().all(|seen| seen));
    assert!(uniform_seen.into_iter().all(|seen| seen));
}

#[test]
fn checked_in_uniform_index_fixture_matches_rand_range() {
    const SEEDS: [u64; 6] = [0, 1, 2, 3, 10, 42];
    const BOUNDS: [i32; 6] = [1, 2, 3, 4, 6, 16];

    for seed in SEEDS {
        for bound in BOUNDS {
            let mut rng = Xoshiro256PlusPlus::seed_from_u64(seed);
            let rows: Vec<Vec<&str>> = UNIFORM_INDEX_FIXTURE
                .lines()
                .filter(|line| !line.is_empty() && !line.starts_with('#'))
                .map(|line| line.split_whitespace().collect())
                .filter(|fields: &Vec<&str>| {
                    fields[0].parse::<u64>().unwrap() == seed
                        && fields[1].parse::<i32>().unwrap() == bound
                })
                .collect();
            assert_eq!(rows.len(), SAMPLE_COUNT, "seed={seed} bound={bound}");
            for (ordinal, fields) in rows.iter().enumerate() {
                assert_eq!(fields.len(), 4, "malformed uniform-index fixture row");
                assert_eq!(fields[2].parse::<usize>().unwrap(), ordinal);
                let expected = fields[3].parse::<i32>().unwrap();
                assert_eq!(rng.random_range(0..bound), expected);
            }
        }
    }
}

#[test]
fn empty_uniform_range_panics_without_consuming_rng() {
    let mut rng = Xoshiro256PlusPlus::seed_from_u64(SEED);
    let mut expected = Xoshiro256PlusPlus::seed_from_u64(SEED);
    let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        let _: u32 = rng.random_range(0_u32..0_u32);
    }));

    assert!(result.is_err(), "an empty range must panic");
    assert_eq!(
        rng.next_u64(),
        expected.next_u64(),
        "range validation must happen before consuming RNG state"
    );
}
