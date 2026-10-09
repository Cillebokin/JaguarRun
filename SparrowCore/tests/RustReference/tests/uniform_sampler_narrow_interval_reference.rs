use jagua_rs::geometry::primitives::Rect;
use jagua_rs::io::import::Importer;
use jagua_rs::probs::spp::io::import_instance;
use rand::rngs::Xoshiro256PlusPlus;
use rand::{Rng, SeedableRng};
use sparrow::config::DEFAULT_SPARROW_CONFIG;
use sparrow::sample::uniform_sampler::UniformBBoxSampler;
use std::path::Path;

const REFERENCE: &str = include_str!("../../Fixtures/UniformSamplerNarrowIntervalRust.tsv");

#[test]
fn one_ulp_interval_samples_and_rng_state_match_the_rust_reference() {
    let external =
        sparrow::util::io::read_spp_input(Path::new("../Fixtures/RustSppSingleItem.json"))
            .expect("read fixture");
    let config = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        config.cde_config,
        config.poly_simpl_tolerance,
        config.narrow_concavity_cutoff_ratio,
    );
    let instance = import_instance(&importer, &external.0).expect("import fixture");
    let low = f32::from_bits(0x3f80_0000);
    let high = f32::from_bits(0x3f80_0001);
    let sample_bounds = Rect::try_new(low, 1.0, high, 2.0).expect("sample bounds");
    let container_bounds = Rect::try_new(0.0, 0.0, 10.0, 10.0).expect("container bounds");
    let sampler = UniformBBoxSampler::new(sample_bounds, instance.item(0), container_bounds)
        .expect("construct one-ULP sampler");
    let mut rng = Xoshiro256PlusPlus::seed_from_u64(42);
    let mut expected_rows = REFERENCE
        .lines()
        .filter(|line| !line.is_empty() && !line.starts_with('#'));

    for ordinal in 0..16 {
        let row = expected_rows.next().expect("sample reference row");
        let fields = row.split_whitespace().collect::<Vec<_>>();
        assert_eq!(fields.len(), 5);
        assert_eq!(fields[0], "sample");
        assert_eq!(fields[1].parse::<u64>().unwrap(), 42);
        assert_eq!(fields[2].parse::<usize>().unwrap(), ordinal);
        let expected_x = u32::from_str_radix(fields[3], 16).unwrap();
        let expected_y = u32::from_str_radix(fields[4], 16).unwrap();
        let (x, y) = sampler.sample(&mut rng).translation();
        assert_eq!(x.to_bits(), expected_x, "sample {ordinal} X");
        assert_eq!(y.to_bits(), expected_y, "sample {ordinal} Y");
        assert!((low..=high).contains(&x));
    }

    let row = expected_rows.next().expect("post-sample RNG reference row");
    let fields = row.split_whitespace().collect::<Vec<_>>();
    assert_eq!(fields.len(), 2);
    assert_eq!(fields[0], "rng_after");
    let expected_next = u64::from_str_radix(fields[1], 16).unwrap();
    assert_eq!(rng.next_u64(), expected_next);
    assert!(expected_rows.next().is_none(), "unexpected reference rows");
}
