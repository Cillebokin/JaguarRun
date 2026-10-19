use jagua_rs::geometry::DTransformation;
use jagua_rs::io::import::Importer;
use jagua_rs::probs::spp::entities::{SPPlacement, SPProblem};
use jagua_rs::probs::spp::io::import_instance;
use ordered_float::OrderedFloat;
use sparrow::config::DEFAULT_SPARROW_CONFIG;
use sparrow::quantify::tracker::CollisionTracker;
use std::path::Path;

fn worker_tracker_base() -> CollisionTracker {
    let (external, _) =
        sparrow::util::io::read_spp_input(Path::new("../Fixtures/SeparatorRoundRustTrace.json"))
            .expect("read deterministic worker-aggregate instance");
    let defaults = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        defaults.cde_config,
        defaults.poly_simpl_tolerance,
        defaults.narrow_concavity_cutoff_ratio,
    );
    let instance = import_instance(&importer, &external).expect("import worker-aggregate instance");
    let mut problem = SPProblem::new(instance).expect("create worker-aggregate problem");
    problem
        .change_strip_width(9.0)
        .expect("set worker-aggregate strip width");
    for _ in 0..2 {
        problem.place_item(SPPlacement {
            item_idx: 0,
            d_transf: DTransformation::new(0.0, (1.0, 1.0)),
        });
    }
    CollisionTracker::new(&problem.layout)
}

fn selected_worker(losses: &[(usize, f32)]) -> usize {
    losses
        .iter()
        .min_by_key(|worker| OrderedFloat(worker.1))
        .map(|worker| worker.0)
        .unwrap()
}

#[test]
fn mixed_nan_and_infinite_worker_losses_follow_pinned_ordered_float_minimum() {
    let nan = f32::NAN;

    assert_eq!(
        selected_worker(&[(0, nan), (1, 4.0), (2, 2.0), (3, nan)]),
        2
    );
    assert_eq!(selected_worker(&[(0, 4.0), (1, nan)]), 0);
    assert_eq!(selected_worker(&[(0, nan), (1, nan)]), 0);

    assert_eq!(
        selected_worker(&[(0, nan), (1, f32::INFINITY), (2, nan)]),
        1
    );
    assert_eq!(
        selected_worker(&[(0, f32::INFINITY), (1, nan), (2, nan)]),
        0
    );
    assert_eq!(
        selected_worker(&[(0, nan), (1, f32::NEG_INFINITY), (2, f32::INFINITY)]),
        1
    );
    assert_eq!(
        selected_worker(&[(0, f32::INFINITY), (1, f32::INFINITY), (2, nan)]),
        0
    );
}

#[test]
fn mixed_nonfinite_tracker_aggregates_select_the_expected_worker() {
    let base = worker_tracker_base();
    assert_eq!(base.size, 2);

    let components = [
        (f32::INFINITY, f32::NEG_INFINITY),
        (f32::INFINITY, 0.0),
        (f32::NEG_INFINITY, 0.0),
        (2.0, 0.0),
    ];
    let workers = components
        .into_iter()
        .map(|(first_loss, second_loss)| {
            let mut tracker = base.clone();
            for entry in tracker.pair_collisions.data.iter_mut() {
                entry.loss = 0.0;
                entry.weight = 1.0;
            }
            for entry in tracker.container_collisions.iter_mut() {
                entry.loss = 0.0;
                entry.weight = 1.0;
            }
            tracker.pair_collisions.data[0].loss = first_loss;
            tracker.pair_collisions.data[1].loss = second_loss;
            tracker
        })
        .collect::<Vec<_>>();
    let aggregate_keys = workers
        .iter()
        .map(CollisionTracker::get_total_weighted_loss)
        .collect::<Vec<_>>();

    assert!(aggregate_keys[0].is_nan());
    assert_eq!(aggregate_keys[1], f32::INFINITY);
    assert_eq!(aggregate_keys[2], f32::NEG_INFINITY);
    assert_eq!(aggregate_keys[3], 2.0);

    let selected_worker = workers
        .iter()
        .enumerate()
        .min_by_key(|(_, tracker)| OrderedFloat(tracker.get_total_weighted_loss()))
        .map(|(index, _)| index)
        .expect("at least one worker result");
    assert_eq!(selected_worker, 2);
}
