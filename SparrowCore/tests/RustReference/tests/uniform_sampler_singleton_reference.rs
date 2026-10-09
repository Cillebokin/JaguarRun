use jagua_rs::geometry::primitives::Rect;
use jagua_rs::io::import::Importer;
use jagua_rs::probs::spp::io::import_instance;
use rand::SeedableRng;
use rand::rngs::Xoshiro256PlusPlus;
use sparrow::config::DEFAULT_SPARROW_CONFIG;
use sparrow::sample::uniform_sampler::UniformBBoxSampler;
use std::path::Path;

const REFERENCE: &str = include_str!("../../Fixtures/UniformSamplerSingletonAxisRust.tsv");

#[test]
fn singleton_axis_does_not_shift_the_rust_sampling_stream() {
    let (external, _) =
        sparrow::util::io::read_spp_input(Path::new("../Fixtures/RustSppSingleItem.json"))
            .expect("read single-item fixture");
    let config = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        config.cde_config,
        config.poly_simpl_tolerance,
        config.narrow_concavity_cutoff_ratio,
    );
    let instance = import_instance(&importer, &external).expect("import fixture");
    let sample_bounds = Rect::try_new(0.0, 0.0, 2.0, 5.0).expect("sample bounds");
    let container_bounds = Rect::try_new(0.0, 0.0, 2.0, 10.0).expect("container bounds");
    let sampler = UniformBBoxSampler::new(sample_bounds, instance.item(0), container_bounds)
        .expect("construct Rust singleton-axis sampler");

    let mut seed = None;
    let mut rng = None;
    let mut row_count = 0;
    for line in REFERENCE
        .lines()
        .filter(|line| !line.is_empty() && !line.starts_with('#'))
    {
        let mut fields = line.split_whitespace();
        let row_seed = fields
            .next()
            .expect("seed")
            .parse::<u64>()
            .expect("valid seed");
        let sample_index = fields
            .next()
            .expect("sample index")
            .parse::<usize>()
            .expect("valid sample index");
        let expected_x =
            u32::from_str_radix(fields.next().expect("x bits"), 16).expect("valid x bits");
        let expected_y =
            u32::from_str_radix(fields.next().expect("y bits"), 16).expect("valid y bits");
        assert!(fields.next().is_none(), "unexpected fixture columns");

        if seed != Some(row_seed) {
            seed = Some(row_seed);
            rng = Some(Xoshiro256PlusPlus::seed_from_u64(row_seed));
        }
        let transform = sampler.sample(rng.as_mut().expect("initialized RNG"));
        let (x, y) = transform.translation();
        assert_eq!(sample_index, row_count, "fixture sample order");
        assert_eq!(
            x.to_bits(),
            expected_x,
            "seed={row_seed}, sample={sample_index}, x"
        );
        assert_eq!(
            y.to_bits(),
            expected_y,
            "seed={row_seed}, sample={sample_index}, y"
        );
        row_count += 1;
    }
    assert_eq!(row_count, 8, "fixture must cover eight consecutive samples");
}
