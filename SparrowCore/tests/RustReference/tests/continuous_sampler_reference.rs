use jagua_rs::geometry::primitives::Rect;
use jagua_rs::io::import::Importer;
use jagua_rs::probs::spp::io::import_instance;
use rand::Rng;
use rand::SeedableRng;
use rand::rngs::Xoshiro256PlusPlus;
use sparrow::config::DEFAULT_SPARROW_CONFIG;
use sparrow::sample::uniform_sampler::UniformBBoxSampler;
use std::collections::HashSet;
use std::path::Path;

const REFERENCE: &str = include_str!("../../Fixtures/UniformSamplerContinuousGridRust.tsv");

#[test]
fn uniform_sampler_continuous_grid_matches_the_rust_reference() {
    let (external, _) =
        sparrow::util::io::read_spp_input(Path::new("../Fixtures/CollisionLossFloorRust.json"))
            .expect("read continuous-rotation fixture");
    let config = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        config.cde_config,
        config.poly_simpl_tolerance,
        config.narrow_concavity_cutoff_ratio,
    );
    let instance = import_instance(&importer, &external).expect("import fixture");
    let bounds = Rect::try_new(-100.0, -100.0, 100.0, 100.0).expect("bounds");
    let sampler = UniformBBoxSampler::new(bounds, instance.item(0), bounds)
        .expect("construct Rust continuous sampler");

    let mut expected_angles = HashSet::new();
    for line in REFERENCE
        .lines()
        .filter(|line| !line.is_empty() && !line.starts_with('#'))
    {
        let mut fields = line.split_whitespace();
        let seed = fields
            .next()
            .expect("seed")
            .parse::<u64>()
            .expect("valid seed");
        let expected_rotation =
            u32::from_str_radix(fields.next().expect("expected rotation bits"), 16)
                .expect("valid rotation bits");
        let expected_following = (0..4)
            .map(|_| {
                u64::from_str_radix(fields.next().expect("post-sample RNG word"), 16)
                    .expect("valid post-sample RNG word")
            })
            .collect::<Vec<_>>();
        assert!(fields.next().is_none(), "unexpected fixture columns");

        let mut rng = Xoshiro256PlusPlus::seed_from_u64(seed);
        let rotation = sampler.sample(&mut rng).rotation();
        assert_eq!(rotation.to_bits(), expected_rotation, "seed={seed}");
        for (index, expected) in expected_following.iter().enumerate() {
            assert_eq!(rng.next_u64(), *expected, "seed={seed} following={index}");
        }
        expected_angles.insert(rotation.to_bits());
    }

    assert_eq!(
        expected_angles.len(),
        16,
        "fixture must cover every grid angle"
    );
}
