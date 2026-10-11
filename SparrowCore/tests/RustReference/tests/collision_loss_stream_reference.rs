use jagua_rs::collision_detection::hazards::HazardEntity;
use jagua_rs::collision_detection::hazards::collector::BasicHazardCollector;
use jagua_rs::geometry::DTransformation;
use jagua_rs::geometry::geo_traits::TransformableFrom;
use jagua_rs::io::import::Importer;
use jagua_rs::probs::spp::entities::{SPPlacement, SPProblem};
use jagua_rs::probs::spp::io::import_instance;
use sparrow::config::DEFAULT_SPARROW_CONFIG;
use sparrow::eval::sample_eval::{SampleEval, SampleEvaluator};
use sparrow::eval::sep_evaluator::SeparationEvaluator;
use sparrow::quantify::tracker::CollisionTracker;
use std::path::Path;

fn build_multi_hazard_case() -> (
    SPProblem,
    CollisionTracker,
    Vec<jagua_rs::entities::PItemKey>,
) {
    let (external_instance, _) = sparrow::util::io::read_spp_input(Path::new(
        "../Fixtures/CollisionLossMultiHazardRust.json",
    ))
    .expect("read multi-hazard reference instance");
    let config = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        config.cde_config,
        config.poly_simpl_tolerance,
        config.narrow_concavity_cutoff_ratio,
    );
    let mut instance = import_instance(&importer, &external_instance)
        .expect("import multi-hazard reference instance");
    instance.base_strip.width = 1.0;
    let mut problem = SPProblem::new(instance).expect("create multi-hazard problem");
    let keys = (0..4)
        .map(|item_idx| {
            problem.place_item(SPPlacement {
                item_idx,
                d_transf: DTransformation::new(0.0, (0.5, 0.5)),
            })
        })
        .collect::<Vec<_>>();
    let mut tracker = CollisionTracker::new(&problem.layout);
    let moving_index = tracker.pk_idx_map[keys[3]];
    let weights = [5.0e14, 1.0e8, 5.0e6];
    for (other_idx, weight) in (0..3).zip(weights) {
        let other_index = tracker.pk_idx_map[keys[other_idx]];
        tracker.pair_collisions[(moving_index, other_index)].weight = weight;
    }
    (problem, tracker, keys)
}

fn raw_collision_loss(evaluation: SampleEval) -> f32 {
    let SampleEval::Collision { loss } = evaluation else {
        panic!("unbounded overlapping candidate must be a collision");
    };
    loss
}

#[test]
fn weighted_multi_hazard_loss_matches_the_rust_accumulation_order() {
    let (problem, tracker, keys) = build_multi_hazard_case();
    let moving_key = keys[3];
    let moving_item = &problem.layout.placed_items[moving_key].item;
    let transform = DTransformation::new(0.0, (0.5, 0.5));
    let mut candidate = moving_item.shape_cd.as_ref().clone();
    candidate.transform_from(moving_item.shape_cd.as_ref(), &transform.compose());
    let mut collector = BasicHazardCollector::new();
    let cde = problem.layout.cde();
    let own_hazard_key = cde
        .haz_key_from_pi_key(moving_key)
        .expect("moving placement must have a collision hazard");
    collector.insert(own_hazard_key, cde.hazards_map[own_hazard_key].entity);
    let mut discovery_order = Vec::new();
    let surrogate_stopped =
        cde.collect_surrogate_collisions_until(&candidate, &mut collector, |hazard| {
            discovery_order.push(hazard);
            false
        });
    let precise_stopped = cde.collect_poly_collisions_until(&candidate, &mut collector, |hazard| {
        discovery_order.push(hazard);
        false
    });
    let item_order = discovery_order
        .iter()
        .map(|hazard| match hazard {
            HazardEntity::PlacedItem { pk, .. } => keys
                .iter()
                .position(|key| key == pk)
                .expect("collision hazard must map to a placed item"),
            other => panic!("unexpected hazard in fixture: {other:?}"),
        })
        .collect::<Vec<_>>();
    println!(
        "hazard_discovery_order={item_order:?} surrogate_stopped={surrogate_stopped} precise_stopped={precise_stopped}"
    );
    let mut evaluator =
        SeparationEvaluator::new(&problem.layout, moving_item, moving_key, &tracker);
    let loss = raw_collision_loss(evaluator.evaluate_sample(transform, None));
    println!("multi_hazard_loss={loss} bits={:08x}", loss.to_bits());
    let contributions = discovery_order
        .iter()
        .map(|hazard| match hazard {
            HazardEntity::PlacedItem { pk, .. } => {
                let other_shape = &problem.layout.placed_items[*pk].shape;
                sparrow::quantify::quantify_collision_poly_poly(other_shape, &candidate)
                    * tracker.get_pair_weight(moving_key, *pk)
            }
            other => panic!("unexpected hazard in fixture: {other:?}"),
        })
        .collect::<Vec<_>>();
    let ordered_sum = contributions
        .iter()
        .copied()
        .fold(0.0f32, |sum, item| sum + item);
    let reversed_sum = contributions
        .iter()
        .rev()
        .copied()
        .fold(0.0f32, |sum, item| sum + item);
    println!(
        "ordered_bits={:08x} reversed_bits={:08x}",
        ordered_sum.to_bits(),
        reversed_sum.to_bits()
    );
    assert!(loss.is_finite() && loss > 0.0);
    assert_eq!(loss.to_bits(), 0x4ab5_c931);
    assert_eq!(ordered_sum.to_bits(), loss.to_bits());
    assert_ne!(reversed_sum.to_bits(), loss.to_bits());
}

#[test]
fn collision_upper_bound_rejects_after_the_first_over_budget_hazard() {
    let (problem, tracker, keys) = build_multi_hazard_case();
    let moving_key = keys[3];
    let moving_item = &problem.layout.placed_items[moving_key].item;
    let mut evaluator =
        SeparationEvaluator::new(&problem.layout, moving_item, moving_key, &tracker);
    let evaluation = evaluator.evaluate_sample(
        DTransformation::new(0.0, (0.5, 0.5)),
        Some(SampleEval::Collision { loss: 0.01 }),
    );
    assert!(matches!(evaluation, SampleEval::Invalid));

    let clear_bound = evaluator.evaluate_sample(
        DTransformation::new(0.0, (0.5, 0.5)),
        Some(SampleEval::Clear { loss: 0.0 }),
    );
    assert!(matches!(clear_bound, SampleEval::Invalid));

    // Pin one hazard's collision discovery callback contract: it is called after insertion
    // and returning true leaves traversal with the discovered prefix only.
    let mut collector = BasicHazardCollector::new();
    let cde = problem.layout.cde();
    let own_hazard_key = cde
        .haz_key_from_pi_key(moving_key)
        .expect("moving placement must have a collision hazard");
    collector.insert(own_hazard_key, cde.hazards_map[own_hazard_key].entity);
    let mut calls = 0usize;
    let mut transformed_candidate = moving_item.shape_cd.as_ref().clone();
    let transform = DTransformation::new(0.0, (0.5, 0.5)).compose();
    transformed_candidate.transform_from(moving_item.shape_cd.as_ref(), &transform);
    let stopped =
        cde.collect_poly_collisions_until(&transformed_candidate, &mut collector, |hazard| {
            calls += 1;
            let HazardEntity::PlacedItem { pk, .. } = hazard else {
                panic!("unexpected hazard in fixture: {hazard:?}");
            };
            let other_shape = &problem.layout.placed_items[pk].shape;
            let weighted_loss = sparrow::quantify::quantify_collision_poly_poly(
                other_shape,
                &transformed_candidate,
            ) * tracker.get_pair_weight(moving_key, pk);
            assert!(weighted_loss > 0.01);
            weighted_loss > 0.01
        });
    println!("bounded_collection_stopped={stopped} callbacks={calls}");
    assert!(stopped);
    assert_eq!(calls, 1);
    assert_eq!(collector.len(), 2);
}

#[test]
fn collision_upper_bound_matches_adjacent_float_neighbors() {
    let (problem, tracker, keys) = build_multi_hazard_case();
    let moving_key = keys[3];
    let moving_item = &problem.layout.placed_items[moving_key].item;
    let transform = DTransformation::new(0.0, (0.5, 0.5));
    let mut evaluator =
        SeparationEvaluator::new(&problem.layout, moving_item, moving_key, &tracker);

    let unbounded = raw_collision_loss(evaluator.evaluate_sample(transform, None));
    assert_eq!(unbounded.to_bits(), 0x4ab5_c931);

    let below = evaluator.evaluate_sample(
        transform,
        Some(SampleEval::Collision {
            loss: f32::from_bits(0x4ab5_c930),
        }),
    );
    assert!(matches!(below, SampleEval::Invalid));

    let equal =
        evaluator.evaluate_sample(transform, Some(SampleEval::Collision { loss: unbounded }));
    assert!(matches!(equal, SampleEval::Invalid));

    let above = evaluator.evaluate_sample(
        transform,
        Some(SampleEval::Collision {
            loss: f32::from_bits(0x4ab5_c932),
        }),
    );
    assert!(matches!(
        above,
        SampleEval::Collision { loss } if loss.to_bits() == 0x4ab5_c931
    ));
}

#[test]
fn clear_upper_bound_preserves_a_zero_loss_collision_classification() {
    let (problem, mut tracker, keys) = build_multi_hazard_case();
    let moving_key = keys[3];
    let moving_index = tracker.pk_idx_map[moving_key];
    for other_key in &keys[..3] {
        let other_index = tracker.pk_idx_map[*other_key];
        tracker.pair_collisions[(moving_index, other_index)].weight = f32::from_bits(1);
    }

    let moving_item = &problem.layout.placed_items[moving_key].item;
    let mut evaluator =
        SeparationEvaluator::new(&problem.layout, moving_item, moving_key, &tracker);
    let evaluation = evaluator.evaluate_sample(
        DTransformation::new(0.0, (0.5, 0.5)),
        Some(SampleEval::Clear { loss: 0.0 }),
    );

    let SampleEval::Collision { loss } = evaluation else {
        panic!("a zero-loss overlap under a Clear bound must remain a collision");
    };
    assert_eq!(loss.to_bits(), 0);
}
