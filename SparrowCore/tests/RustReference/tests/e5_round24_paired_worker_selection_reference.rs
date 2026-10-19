use jagua_rs::geometry::DTransformation;
use jagua_rs::io::import::Importer;
use jagua_rs::probs::spp::entities::{SPInstance, SPPlacement, SPProblem, SPSolution};
use jagua_rs::probs::spp::io::import_instance;
use sparrow::config::DEFAULT_SPARROW_CONFIG;
use sparrow::optimizer::separator::select_worker_index_for_trace;
use sparrow::quantify::tracker::CollisionTracker;
use std::path::Path;

struct CapturedWorkerResult {
    solution: SPSolution,
    tracker: CollisionTracker,
    raw_loss: f32,
    weighted_loss: f32,
    moves: usize,
    evaluations: usize,
}

fn make_captured_result(
    instance: &SPInstance,
    positions: [(f32, f32); 2],
    moves: usize,
    evaluations: usize,
) -> CapturedWorkerResult {
    let mut problem = SPProblem::new(instance.clone()).expect("create worker problem");
    problem.change_strip_width(3.0).expect("set strip width");
    for (x, y) in positions {
        problem.place_item(SPPlacement {
            item_idx: 0,
            d_transf: DTransformation::new(0.0, (x, y)),
        });
    }

    let solution = problem.save();
    let mut tracker = CollisionTracker::new(&problem.layout);
    // Recreate the captured NaN selector key while retaining a real tracker
    // snapshot for the worker's recorded geometry.
    tracker.pair_collisions[(0, 0)].weight = f32::INFINITY;
    let raw_loss = tracker.get_total_loss();
    let weighted_loss = tracker.get_total_weighted_loss();
    CapturedWorkerResult {
        solution,
        tracker,
        raw_loss,
        weighted_loss,
        moves,
        evaluations,
    }
}

fn close(actual: f32, expected: f32) -> bool {
    (actual - expected).abs() <= 5.0e-6
}

#[test]
fn round24_captured_worker_results_select_and_restore_worker_zero() {
    let (external, _) =
        sparrow::util::io::read_spp_input(Path::new("../Fixtures/RustSppCompressWorkerMove.json"))
            .expect("read Round24 E5 fixture");
    let defaults = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        defaults.cde_config,
        defaults.poly_simpl_tolerance,
        defaults.narrow_concavity_cutoff_ratio,
    );
    let instance = import_instance(&importer, &external).expect("import Round24 E5 fixture");

    let workers = vec![
        make_captured_result(
            &instance,
            [(1.0169114, 0.52927935), (1.8374491, 1.5295486)],
            2,
            130,
        ),
        make_captured_result(
            &instance,
            [(1.0013936, 0.50548494), (1.8697289, 1.5056324)],
            1,
            72,
        ),
    ];
    let keys = workers
        .iter()
        .map(|worker| worker.weighted_loss)
        .collect::<Vec<_>>();

    assert!(keys.iter().all(|key| key.is_nan()));
    assert!(close(workers[0].raw_loss, f32::from_bits(0x403a_4f1c)));
    assert!(close(workers[1].raw_loss, f32::from_bits(0x4036_0dd7)));
    assert!(workers[1].raw_loss < workers[0].raw_loss);
    assert_eq!(workers.iter().map(|worker| worker.moves).sum::<usize>(), 3);
    assert_eq!(
        workers
            .iter()
            .map(|worker| worker.evaluations)
            .sum::<usize>(),
        202
    );

    let selected_index =
        select_worker_index_for_trace(&keys).expect("worker results are non-empty");
    assert_eq!(selected_index, 0);

    let selected = workers
        .into_iter()
        .nth(selected_index)
        .expect("selected worker");
    let mut master = SPProblem::new(instance).expect("create master problem");
    master
        .change_strip_width(3.0)
        .expect("set master strip width");
    master.restore(&selected.solution);
    let restored = master.save();
    let restored_tracker = selected.tracker.clone();
    let mut restored_positions = restored
        .layout_snapshot
        .placed_items
        .values()
        .map(|placed| placed.d_transf.translation())
        .collect::<Vec<_>>();
    restored_positions.sort_by(|left, right| left.0.total_cmp(&right.0));

    assert_eq!(restored_positions.len(), 2);
    assert!(close(restored_positions[0].0, 1.0169114));
    assert!(close(restored_positions[0].1, 0.52927935));
    assert!(close(restored_positions[1].0, 1.8374491));
    assert!(close(restored_positions[1].1, 1.5295486));
    assert!(close(restored_tracker.get_total_loss(), selected.raw_loss));
    assert!(restored_tracker.get_total_weighted_loss().is_nan());
}
