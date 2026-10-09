use jagua_rs::io::import::Importer;
use jagua_rs::probs::spp::io::import_instance;
use rand::SeedableRng;
use rand::rngs::Xoshiro256PlusPlus;
use sparrow::config::DEFAULT_SPARROW_CONFIG;
use sparrow::optimizer::lbf::LBFBuilder;
use sparrow::sample::search::SampleConfig;
use std::path::Path;

#[test]
fn construction_order_uses_f32_key_and_keeps_input_order_on_ties() {
    let (external_instance, _) =
        sparrow::util::io::read_spp_input(Path::new("../Fixtures/LbfSortPrecisionRust.json"))
            .expect("read LBF sort precision reference instance");
    let config = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        config.cde_config,
        config.poly_simpl_tolerance,
        config.narrow_concavity_cutoff_ratio,
    );
    let instance = import_instance(&importer, &external_instance)
        .expect("import LBF sort precision reference instance");

    let first = instance.item(0).shape_cd.as_ref();
    let second = instance.item(1).shape_cd.as_ref();
    let first_key = first.surrogate().convex_hull_area * first.diameter;
    let second_key = second.surrogate().convex_hull_area * second.diameter;
    assert_eq!(first_key.to_bits(), second_key.to_bits());

    let builder = LBFBuilder::new(
        instance,
        Xoshiro256PlusPlus::seed_from_u64(42),
        SampleConfig {
            n_container_samples: 64,
            n_focussed_samples: 0,
            n_coord_descents: 1,
        },
    )
    .construct()
    .expect("construct precision-tie instance");
    let first_placed_item = builder
        .prob
        .layout
        .placed_items
        .values()
        .next()
        .expect("the reference construction must place an item")
        .item
        .idx;
    assert_eq!(first_placed_item, 0);
}
