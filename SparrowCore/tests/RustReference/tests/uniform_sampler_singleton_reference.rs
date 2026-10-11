use jagua_rs::geometry::primitives::Rect;
use jagua_rs::io::import::Importer;
use jagua_rs::probs::spp::io::import_instance;
use rand::SeedableRng;
use rand::rngs::Xoshiro256PlusPlus;
use sparrow::config::DEFAULT_SPARROW_CONFIG;
use sparrow::sample::uniform_sampler::UniformBBoxSampler;
use std::path::Path;

const REFERENCE: &str = include_str!("../../Fixtures/UniformSamplerSingletonAxisRust.tsv");
const DEGENERATE_REFERENCE: &str =
    include_str!("../../Fixtures/UniformSamplerDegenerateBboxRust.tsv");

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

#[test]
fn degenerate_sample_bbox_axes_are_accepted() {
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
    let container_bounds = Rect::try_new(0.0, 0.0, 10.0, 10.0).expect("container bounds");
    let cases = [
        (
            "x",
            Rect {
                x_min: 2.0,
                y_min: 0.0,
                x_max: 2.0,
                y_max: 5.0,
            },
        ),
        (
            "y",
            Rect {
                x_min: 0.0,
                y_min: 2.0,
                x_max: 5.0,
                y_max: 2.0,
            },
        ),
        (
            "xy",
            Rect {
                x_min: 2.0,
                y_min: 2.0,
                x_max: 2.0,
                y_max: 2.0,
            },
        ),
    ];
    let mut reference_rows = DEGENERATE_REFERENCE
        .lines()
        .filter(|line| !line.is_empty() && !line.starts_with('#'));

    for (case_name, sample_bounds) in cases {
        let sampler = UniformBBoxSampler::new(sample_bounds, instance.item(0), container_bounds)
            .unwrap_or_else(|| panic!("Rust rejected {case_name} singleton sample bbox"));
        let mut rng = Xoshiro256PlusPlus::seed_from_u64(42);
        for sample_index in 0..8 {
            let row = reference_rows.next().expect("reference sample row");
            let mut fields = row.split_whitespace();
            assert_eq!(fields.next(), Some(case_name), "fixture case order");
            assert_eq!(fields.next(), Some("42"), "fixture seed");
            assert_eq!(
                fields.next().and_then(|value| value.parse::<usize>().ok()),
                Some(sample_index),
                "fixture sample order"
            );
            let expected_x =
                u32::from_str_radix(fields.next().expect("x bits"), 16).expect("valid x bits");
            let expected_y =
                u32::from_str_radix(fields.next().expect("y bits"), 16).expect("valid y bits");
            assert!(fields.next().is_none(), "unexpected fixture columns");

            let transform = sampler.sample(&mut rng);
            let (x, y) = transform.translation();
            assert_eq!(
                x.to_bits(),
                expected_x,
                "case={case_name}, sample={sample_index}, x"
            );
            assert_eq!(
                y.to_bits(),
                expected_y,
                "case={case_name}, sample={sample_index}, y"
            );
        }
    }
    assert!(
        reference_rows.next().is_none(),
        "unexpected extra fixture rows"
    );
}

#[test]
fn no_fit_and_unusable_sample_bounds_return_none() {
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
    let container_bounds = Rect::try_new(0.0, 0.0, 10.0, 10.0).expect("container bounds");
    let cases = [
        (
            "spatial no-fit",
            Rect {
                x_min: 20.0,
                y_min: 0.0,
                x_max: 25.0,
                y_max: 5.0,
            },
        ),
        (
            "reversed sample range",
            Rect {
                x_min: 5.0,
                y_min: 0.0,
                x_max: 0.0,
                y_max: 5.0,
            },
        ),
        (
            "non-finite sample range",
            Rect {
                x_min: f32::NAN,
                y_min: 0.0,
                x_max: 5.0,
                y_max: 5.0,
            },
        ),
    ];

    for (case_name, sample_bounds) in cases {
        assert!(
            UniformBBoxSampler::new(sample_bounds, instance.item(0), container_bounds).is_none(),
            "Rust should return None for {case_name}"
        );
    }
}
