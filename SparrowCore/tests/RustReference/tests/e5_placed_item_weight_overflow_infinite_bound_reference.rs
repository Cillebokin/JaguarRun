use jagua_rs::geometry::DTransformation;
use jagua_rs::io::import::Importer;
use jagua_rs::probs::spp::entities::{SPPlacement, SPProblem};
use jagua_rs::probs::spp::io::import_instance;
use sparrow::config::DEFAULT_SPARROW_CONFIG;
use sparrow::eval::sample_eval::{SampleEval, SampleEvaluator};
use sparrow::eval::sep_evaluator::SeparationEvaluator;
use sparrow::quantify::tracker::CollisionTracker;
use std::path::Path;

#[test]
fn placed_item_weight_overflow_with_infinite_bound_is_collision_in_rust() {
    let (external, _) = sparrow::util::io::read_spp_input(Path::new(
        "../Fixtures/CollisionLossPlacedItemWeightOverflowInfiniteBoundE5.json",
    ))
    .expect("read E5 placed-item overflow fixture");
    let defaults = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        defaults.cde_config,
        defaults.poly_simpl_tolerance,
        defaults.narrow_concavity_cutoff_ratio,
    );
    let mut instance = import_instance(&importer, &external).expect("import E5 fixture");
    instance.base_strip.width = 8.0;
    let mut problem = SPProblem::new(instance).expect("create E5 problem");
    let placement = DTransformation::new(0.0, (2.0, 2.0));
    let first_key = problem.place_item(SPPlacement {
        item_idx: 0,
        d_transf: placement,
    });
    let moving_key = problem.place_item(SPPlacement {
        item_idx: 1,
        d_transf: placement,
    });
    let mut tracker = CollisionTracker::new(&problem.layout);
    let raw_loss = tracker.get_pair_loss(first_key, moving_key);
    assert!(raw_loss.is_finite() && raw_loss > 1.0);
    assert_eq!(raw_loss.to_bits(), 0x41ad_5b74);
    let first_index = tracker.pk_idx_map[first_key];
    let moving_index = tracker.pk_idx_map[moving_key];
    tracker.pair_collisions[(first_index, moving_index)].weight = f32::MAX;
    assert_eq!(tracker.get_pair_weight(first_key, moving_key), f32::MAX);
    assert_eq!((raw_loss * f32::MAX).to_bits(), f32::INFINITY.to_bits());

    let moving_item = problem.instance.item(1);
    let mut evaluator =
        SeparationEvaluator::new(&problem.layout, moving_item, moving_key, &tracker);
    let upper_bound = SampleEval::Collision {
        loss: f32::INFINITY,
    };
    let evaluation = evaluator.evaluate_sample(placement, Some(upper_bound));

    assert_eq!(
        evaluation,
        SampleEval::Collision {
            loss: f32::INFINITY,
        }
    );
    let finite_bound_evaluation =
        evaluator.evaluate_sample(placement, Some(SampleEval::Collision { loss: f32::MAX }));
    assert_eq!(finite_bound_evaluation, SampleEval::Invalid);
    println!(
        "E5 placed-item overflow: raw_loss_bits={:08x} pair_weight_bits={:08x} upper_bound_bits=7f800000 result=Collision(+Inf), finite_upper_bound_result=Invalid",
        raw_loss.to_bits(),
        f32::MAX.to_bits(),
    );
}
