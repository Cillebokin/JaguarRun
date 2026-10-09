use jagua_rs::geometry::DTransformation;
use jagua_rs::io::import::Importer;
use jagua_rs::probs::spp::entities::SPPlacement;
use jagua_rs::probs::spp::entities::SPProblem;
use jagua_rs::probs::spp::io::import_instance;
use sparrow::config::DEFAULT_SPARROW_CONFIG;
use sparrow::quantify::tracker::CollisionTracker;
use std::path::Path;

#[test]
fn collision_weights_match_rust_without_a_one_million_cap() {
    let (external_instance, _) =
        sparrow::util::io::read_spp_input(Path::new("../Fixtures/CollisionTrackerWeightRust.json"))
            .expect("read collision tracker weight reference instance");
    let config = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        config.cde_config,
        config.poly_simpl_tolerance,
        config.narrow_concavity_cutoff_ratio,
    );
    let instance = import_instance(&importer, &external_instance)
        .expect("import collision tracker weight reference instance");
    let mut problem = SPProblem::new(instance).expect("create collision tracker problem");
    problem
        .change_strip_width(8.0)
        .expect("widen collision tracker strip");
    let first_key = problem.place_item(SPPlacement {
        item_idx: 0,
        d_transf: DTransformation::new(0.0, (1.0, 1.0)),
    });
    let second_key = problem.place_item(SPPlacement {
        item_idx: 1,
        d_transf: DTransformation::new(0.0, (1.0, 1.0)),
    });
    let third_key = problem.place_item(SPPlacement {
        item_idx: 2,
        d_transf: DTransformation::new(0.0, (1.5, 1.0)),
    });

    let mut tracker = CollisionTracker::new(&problem.layout);
    assert!(tracker.get_pair_loss(first_key, second_key) > 0.0);
    let partial_loss = tracker.get_pair_loss(first_key, third_key);
    assert!(partial_loss > 0.0);
    for _ in 0..20 {
        tracker.update_weights();
    }

    assert_eq!(tracker.get_pair_weight(first_key, second_key), 1_048_576.0);
    let partial_weight = tracker.get_pair_weight(first_key, third_key);
    assert_eq!(partial_weight.to_bits(), 0x4801_92d6);
}
