use jagua_rs::geometry::DTransformation;
use jagua_rs::io::import::Importer;
use jagua_rs::probs::spp::entities::SPPlacement;
use jagua_rs::probs::spp::entities::SPProblem;
use jagua_rs::probs::spp::io::import_instance;
use sparrow::config::DEFAULT_SPARROW_CONFIG;
use sparrow::quantify::tracker::CollisionTracker;
use std::panic::{AssertUnwindSafe, catch_unwind};
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

#[test]
fn tracker_non_finite_losses_keep_ieee_values_and_rust_weight_floor() {
    let (external_instance, _) =
        sparrow::util::io::read_spp_input(Path::new("../Fixtures/CollisionTrackerWeightRust.json"))
            .expect("read collision tracker non-finite reference instance");
    let config = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        config.cde_config,
        config.poly_simpl_tolerance,
        config.narrow_concavity_cutoff_ratio,
    );
    let instance = import_instance(&importer, &external_instance)
        .expect("import collision tracker non-finite reference instance");
    let mut problem = SPProblem::new(instance).expect("create collision tracker problem");
    problem
        .change_strip_width(8.0)
        .expect("widen collision tracker strip");
    let keys = (0..3)
        .map(|item_idx| {
            problem.place_item(SPPlacement {
                item_idx,
                d_transf: DTransformation::new(0.0, (1.0, 1.0)),
            })
        })
        .collect::<Vec<_>>();
    let mut tracker = CollisionTracker::new(&problem.layout);
    for entry in tracker.pair_collisions.data.iter_mut() {
        entry.loss = 0.0;
    }
    for entry in tracker.container_collisions.iter_mut() {
        entry.loss = 0.0;
    }
    let first = keys[0];
    let second = keys[1];
    let first_index = tracker.pk_idx_map[first];
    let second_index = tracker.pk_idx_map[second];
    tracker.pair_collisions[(first_index, second_index)].loss = f32::NAN;

    assert!(tracker.get_pair_loss(first, second).is_nan());
    assert!(tracker.get_loss(first).is_nan());
    assert!(tracker.get_weighted_loss(first).is_nan());
    assert!(tracker.get_total_loss().is_nan());
    assert!(tracker.get_total_weighted_loss().is_nan());
    tracker.update_weights();
    assert_eq!(
        tracker.get_pair_weight(first, second).to_bits(),
        1.0_f32.to_bits()
    );

    for entry in tracker.pair_collisions.data.iter_mut() {
        entry.loss = 0.0;
    }
    tracker.pair_collisions[(first_index, second_index)].loss = f32::INFINITY;
    tracker.pair_collisions[(first_index, second_index)].weight = 2.0;
    assert_eq!(tracker.get_total_loss(), f32::INFINITY);
    assert_eq!(tracker.get_total_weighted_loss(), f32::INFINITY);
    tracker.update_weights();
    assert_eq!(
        tracker.get_pair_weight(first, second).to_bits(),
        1.0_f32.to_bits()
    );

    for entry in tracker.pair_collisions.data.iter_mut() {
        entry.loss = 0.0;
    }
    tracker.pair_collisions[(first_index, second_index)].loss = f32::NEG_INFINITY;
    assert_eq!(tracker.get_total_loss(), f32::NEG_INFINITY);
    assert_eq!(tracker.get_total_weighted_loss(), f32::NEG_INFINITY);
    tracker.update_weights();
    assert_eq!(
        tracker.get_pair_weight(first, second).to_bits(),
        1.0_f32.to_bits()
    );
}

#[test]
fn tracker_move_recomputes_only_the_moved_matrix_row_and_preserves_weights() {
    let (external_instance, _) =
        sparrow::util::io::read_spp_input(Path::new("../Fixtures/CollisionTrackerWeightRust.json"))
            .expect("read collision tracker move reference instance");
    let config = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        config.cde_config,
        config.poly_simpl_tolerance,
        config.narrow_concavity_cutoff_ratio,
    );
    let instance = import_instance(&importer, &external_instance)
        .expect("import collision tracker move reference instance");
    let mut problem = SPProblem::new(instance).expect("create collision tracker problem");
    problem
        .change_strip_width(8.0)
        .expect("widen collision tracker strip");
    let keys = (0..3)
        .map(|item_idx| {
            problem.place_item(SPPlacement {
                item_idx,
                d_transf: DTransformation::new(0.0, (1.0, 1.0)),
            })
        })
        .collect::<Vec<_>>();
    let mut tracker = CollisionTracker::new(&problem.layout);
    for (ordinal, entry) in tracker.pair_collisions.data.iter_mut().enumerate() {
        entry.weight = 1.0 + ordinal as f32 * 0.25;
    }
    for (index, entry) in tracker.container_collisions.iter_mut().enumerate() {
        entry.weight = 1.5 + index as f32 * 0.5;
    }
    let before = tracker.save();
    let old_key = keys[2];
    let old_placement = problem.remove_item(old_key);
    let new_key = problem.place_item(SPPlacement {
        item_idx: old_placement.item_idx,
        d_transf: DTransformation::new(0.0, (4.0, 1.0)),
    });
    tracker.register_item_move(&problem.layout, old_key, new_key);

    assert!(tracker.get_pair_loss(keys[0], keys[1]) > 0.0);
    for first in 0..tracker.size {
        for second in first..tracker.size {
            let before_entry = before.pair_collisions[(first, second)];
            let after_entry = tracker.pair_collisions[(first, second)];
            if first == 2 || second == 2 {
                assert_eq!(after_entry.loss.to_bits(), 0.0_f32.to_bits());
            } else {
                assert_eq!(after_entry.loss.to_bits(), before_entry.loss.to_bits());
            }
            assert_eq!(after_entry.weight.to_bits(), before_entry.weight.to_bits());
        }
    }
    for index in 0..tracker.size {
        if index == 2 {
            assert_eq!(
                tracker.container_collisions[index].loss.to_bits(),
                0.0_f32.to_bits()
            );
        } else {
            assert_eq!(
                tracker.container_collisions[index].loss.to_bits(),
                before.container_collisions[index].loss.to_bits()
            );
        }
        assert_eq!(
            tracker.container_collisions[index].weight.to_bits(),
            before.container_collisions[index].weight.to_bits()
        );
    }
    assert_eq!(
        tracker.get_total_loss().to_bits(),
        before.pair_collisions[(0, 1)].loss.to_bits()
    );
}

#[test]
fn tracker_getters_panic_for_a_stale_placed_item_key() {
    let (external_instance, _) =
        sparrow::util::io::read_spp_input(Path::new("../Fixtures/CollisionTrackerWeightRust.json"))
            .expect("read collision tracker stale-key reference instance");
    let config = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        config.cde_config,
        config.poly_simpl_tolerance,
        config.narrow_concavity_cutoff_ratio,
    );
    let instance = import_instance(&importer, &external_instance)
        .expect("import collision tracker stale-key reference instance");
    let mut problem = SPProblem::new(instance).expect("create collision tracker problem");
    problem
        .change_strip_width(8.0)
        .expect("widen collision tracker strip");
    let stale_key = problem.place_item(SPPlacement {
        item_idx: 0,
        d_transf: DTransformation::new(0.0, (1.0, 1.0)),
    });
    let _ = problem.remove_item(stale_key);
    let keys = (0..3)
        .map(|item_idx| {
            problem.place_item(SPPlacement {
                item_idx,
                d_transf: DTransformation::new(0.0, (1.0, 1.0)),
            })
        })
        .collect::<Vec<_>>();
    let tracker = CollisionTracker::new(&problem.layout);

    assert!(
        catch_unwind(AssertUnwindSafe(|| {
            tracker.get_pair_weight(stale_key, keys[0])
        }))
        .is_err()
    );
    assert!(
        catch_unwind(AssertUnwindSafe(|| {
            tracker.get_pair_loss(stale_key, keys[0])
        }))
        .is_err()
    );
    assert!(
        catch_unwind(AssertUnwindSafe(|| {
            tracker.get_container_weight(stale_key)
        }))
        .is_err()
    );
    assert!(
        catch_unwind(AssertUnwindSafe(|| {
            tracker.get_container_loss(stale_key)
        }))
        .is_err()
    );
    assert!(catch_unwind(AssertUnwindSafe(|| tracker.get_loss(stale_key))).is_err());
    assert!(
        catch_unwind(AssertUnwindSafe(|| {
            tracker.get_weighted_loss(stale_key)
        }))
        .is_err()
    );
}
