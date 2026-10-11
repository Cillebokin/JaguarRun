use jagua_rs::collision_detection::hazards::HazardEntity;
use jagua_rs::collision_detection::hazards::collector::BasicHazardCollector;
use jagua_rs::geometry::DTransformation;
use jagua_rs::geometry::geo_traits::TransformableFrom;
use jagua_rs::io::import::Importer;
use jagua_rs::probs::spp::entities::SPPlacement;
use jagua_rs::probs::spp::entities::SPProblem;
use jagua_rs::probs::spp::io::import_instance;
use sparrow::config::DEFAULT_SPARROW_CONFIG;
use sparrow::eval::sample_eval::{SampleEval, SampleEvaluator};
use sparrow::eval::sep_evaluator::SeparationEvaluator;
use sparrow::quantify::tracker::CollisionTracker;
use std::path::Path;

#[test]
fn separation_evaluator_returns_raw_sub_micro_collision_loss() {
    let (external_instance, _) =
        sparrow::util::io::read_spp_input(Path::new("../Fixtures/CollisionLossFloorRust.json"))
            .expect("read collision loss floor reference instance");
    let config = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        config.cde_config,
        config.poly_simpl_tolerance,
        config.narrow_concavity_cutoff_ratio,
    );
    let mut instance = import_instance(&importer, &external_instance)
        .expect("import collision loss floor reference instance");
    instance.base_strip.width = 1.0;
    let mut problem = SPProblem::new(instance).expect("create collision loss floor problem");
    let first_key = problem.place_item(SPPlacement {
        item_idx: 0,
        d_transf: DTransformation::new(0.0, (0.5, 0.5)),
    });
    let second_key = problem.place_item(SPPlacement {
        item_idx: 1,
        d_transf: DTransformation::new(0.0, (0.5, 0.5)),
    });

    let tracker = CollisionTracker::new(&problem.layout);
    assert!(tracker.get_pair_loss(first_key, second_key) > 0.0);
    let mut evaluator = SeparationEvaluator::new(
        &problem.layout,
        problem.instance.item(1),
        second_key,
        &tracker,
    );
    let evaluation = evaluator.evaluate_sample(DTransformation::new(0.0, (0.5, 0.5)), None);
    let SampleEval::Collision { loss } = evaluation else {
        panic!("overlapping candidate must be classified as a collision");
    };

    println!("raw_collision_loss={loss} bits={:08x}", loss.to_bits());
    assert!(loss > 0.0 && loss < 1.0e-6);
    assert_eq!(loss.to_bits(), 0x3256_579c);

    let rotated = evaluator.evaluate_sample(DTransformation::new(0.137, (0.5, 0.5)), None);
    let SampleEval::Collision { loss: rotated_loss } = rotated else {
        panic!("rotated overlapping candidate must be classified as a collision");
    };
    println!(
        "rotated_collision_loss={rotated_loss} bits={:08x}",
        rotated_loss.to_bits()
    );
    assert!(rotated_loss > 0.0 && rotated_loss < 1.0e-6);
    assert_eq!(rotated_loss.to_bits(), 0x3256_0315);

    let rotation_cases = [
        (0.013, 0x3256_5e21),
        (0.281, 0x3256_95a6),
        (0.5235988, 0x3255_9559),
        (0.7853982, 0x3254_33cd),
        (1.234, 0x3256_8aab),
        (2.345, 0x3254_3197),
        (3.1415927, 0x3256_58d2),
        (4.712389, 0x3256_56fd),
        (5.999, 0x3256_98bc),
    ];
    for (rotation, expected_bits) in rotation_cases {
        let rotated = evaluator.evaluate_sample(DTransformation::new(rotation, (0.5, 0.5)), None);
        let SampleEval::Collision { loss } = rotated else {
            panic!("candidate at rotation {rotation} must remain a collision");
        };
        println!("rotation={rotation} loss_bits={:08x}", loss.to_bits());
        assert_eq!(loss.to_bits(), expected_bits, "rotation={rotation}");
    }
}

#[test]
fn exterior_hazard_loss_matches_rust_container_quantification() {
    let (external_instance, _) =
        sparrow::util::io::read_spp_input(Path::new("../Fixtures/CollisionLossFloorRust.json"))
            .expect("read exterior-loss reference instance");
    let config = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        config.cde_config,
        config.poly_simpl_tolerance,
        config.narrow_concavity_cutoff_ratio,
    );
    let mut instance = import_instance(&importer, &external_instance)
        .expect("import exterior-loss reference instance");
    instance.base_strip.width = 1.0;
    let mut problem = SPProblem::new(instance).expect("create exterior-loss problem");
    let first_key = problem.place_item(SPPlacement {
        item_idx: 0,
        d_transf: DTransformation::new(0.0, (0.2, 0.5)),
    });
    let moving_key = problem.place_item(SPPlacement {
        item_idx: 1,
        d_transf: DTransformation::new(0.0, (0.5, 0.5)),
    });
    let tracker = CollisionTracker::new(&problem.layout);
    let moving_item = problem.instance.item(1);
    let mut evaluator =
        SeparationEvaluator::new(&problem.layout, moving_item, moving_key, &tracker);

    let transform = DTransformation::new(0.0, (1.0002, 0.5));
    let mut candidate = moving_item.shape_cd.as_ref().clone();
    candidate.transform_from(moving_item.shape_cd.as_ref(), &transform.compose());
    let mut collector = BasicHazardCollector::new();
    problem
        .layout
        .cde()
        .collect_poly_collisions(&candidate, &mut collector);
    assert!(
        collector
            .iter()
            .any(|(_, hazard)| matches!(hazard, HazardEntity::Exterior))
    );

    let evaluation = evaluator.evaluate_sample(transform, None);
    let SampleEval::Collision { loss } = evaluation else {
        panic!("out-of-container candidate must be classified as an Exterior collision");
    };
    assert_eq!(tracker.get_pair_loss(first_key, moving_key), 0.0);
    assert_eq!(loss.to_bits(), 0x3911_52f2);
}
