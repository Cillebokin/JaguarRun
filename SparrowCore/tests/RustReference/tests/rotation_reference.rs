use jagua_rs::entities::Item;
use jagua_rs::geometry::DTransformation;
use jagua_rs::geometry::geo_enums::RotationRange;
use jagua_rs::io::import::Importer;
use jagua_rs::probs::spp::io::import_instance;
use sparrow::config::DEFAULT_SPARROW_CONFIG;
use sparrow::sample::uniform_sampler::convert_sample_to_closest_feasible;
use std::path::Path;

#[test]
fn discrete_rotation_uses_normalized_delta_absolute_value() {
    let (external_instance, _) =
        sparrow::util::io::read_spp_input(Path::new("../Fixtures/RustSppSingleItem.json"))
            .expect("read Rust rotation reference item");
    let config = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        config.cde_config,
        config.poly_simpl_tolerance,
        config.narrow_concavity_cutoff_ratio,
    );
    let instance = import_instance(&importer, &external_instance)
        .expect("import Rust rotation reference item");

    let mut item: Item = instance.item(0).as_ref().clone();
    item.allowed_rotation =
        RotationRange::Discrete(vec![1.0_f32.to_radians(), 350.0_f32.to_radians()]);
    let legalized = convert_sample_to_closest_feasible(
        DTransformation::new(359.0_f32.to_radians(), (0.0, 0.0)),
        &item,
    );

    assert_eq!(
        legalized.rotation().to_bits(),
        350.0_f32.to_radians().to_bits(),
        "Rust must choose 350 degrees over 1 degree for a 359 degree sample",
    );
}

#[test]
fn coordinate_descent_degree_constants_match_rust_f32_to_radians() {
    let expected = [
        5.0_f32.to_radians(),
        1.0_f32.to_radians(),
        0.5_f32.to_radians(),
        0.05_f32.to_radians(),
    ];
    let actual = [
        sparrow::consts::PRE_REFINE_CD_R_STEPS.0,
        sparrow::consts::PRE_REFINE_CD_R_STEPS.1,
        sparrow::consts::SND_REFINE_CD_R_STEPS.0,
        sparrow::consts::SND_REFINE_CD_R_STEPS.1,
    ];
    let expected_bits = expected.map(f32::to_bits);
    let actual_bits = actual.map(f32::to_bits);
    assert_eq!(
        actual_bits,
        [0x3db2_b8c2, 0x3c8e_fa35, 0x3c0e_fa35, 0x3a64_c388]
    );
    assert_eq!(actual_bits, expected_bits);
}
