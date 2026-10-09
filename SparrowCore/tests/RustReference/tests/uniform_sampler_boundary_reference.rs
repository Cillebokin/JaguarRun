use jagua_rs::geometry::primitives::Rect;
use jagua_rs::io::import::Importer;
use jagua_rs::probs::spp::io::import_instance;
use rand::SeedableRng;
use rand::rngs::Xoshiro256PlusPlus;
use sparrow::config::DEFAULT_SPARROW_CONFIG;
use sparrow::sample::uniform_sampler::UniformBBoxSampler;
use std::path::Path;

const REFERENCE: &str = include_str!("../../Fixtures/UniformSamplerUpperBoundaryRust.tsv");

#[test]
fn uniform_sampler_upper_endpoint_rounding_matches_the_rust_reference() {
    let row = REFERENCE
        .lines()
        .find(|line| !line.is_empty() && !line.starts_with('#'))
        .expect("Rust uniform sampler reference row");
    let fields = row.split_whitespace().collect::<Vec<_>>();
    assert_eq!(fields.len(), 4);
    let seed = fields[0].parse::<u64>().unwrap();
    let low = f32::from_bits(u32::from_str_radix(fields[1], 16).unwrap());
    let high = f32::from_bits(u32::from_str_radix(fields[2], 16).unwrap());
    let expected_x = u32::from_str_radix(fields[3], 16).unwrap();

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
    let sample_bounds = Rect::try_new(low, 1.0, high, 2.0).expect("sample bounds");
    let container_bounds = Rect::try_new(0.0, 0.0, 10.0, 10.0).expect("container bounds");
    let sampler = UniformBBoxSampler::new(sample_bounds, instance.item(0), container_bounds)
        .expect("create sampler");

    let mut rng = Xoshiro256PlusPlus::seed_from_u64(seed);
    let (x, _) = sampler.sample(&mut rng).translation();
    assert_eq!(high.to_bits(), expected_x);
    assert_eq!(x.to_bits(), expected_x);
}
