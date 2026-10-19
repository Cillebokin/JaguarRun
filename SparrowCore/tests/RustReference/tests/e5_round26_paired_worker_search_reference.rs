use jagua_rs::geometry::DTransformation;
use jagua_rs::io::import::Importer;
use jagua_rs::probs::spp::entities::{SPInstance, SPPlacement, SPProblem, SPSolution};
use jagua_rs::probs::spp::io::import_instance;
use rand::rngs::Xoshiro256PlusPlus;
use rand::{Rng, RngExt, SeedableRng};
use sparrow::config::DEFAULT_SPARROW_CONFIG;
use sparrow::optimizer::separator::{Separator, select_worker_index_for_trace};
use sparrow::quantify::tracker::CollisionTracker;
use sparrow::sample::search::SampleConfig;
use std::path::Path;

const REPLAYED_ROUNDS_BEFORE_PAIR: usize = 446;

struct WorkerOutcome {
    solution: SPSolution,
    tracker: CollisionTracker,
    moves: usize,
    evaluations: usize,
}

fn load_round24_instance() -> SPInstance {
    let (external, _) =
        sparrow::util::io::read_spp_input(Path::new("../Fixtures/RustSppCompressWorkerMove.json"))
            .expect("read Round26 E5 fixture");
    let defaults = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        defaults.cde_config,
        defaults.poly_simpl_tolerance,
        defaults.narrow_concavity_cutoff_ratio,
    );
    import_instance(&importer, &external).expect("import Round26 E5 fixture")
}

fn make_compression_start(instance: &SPInstance) -> (SPProblem, SPSolution) {
    let mut problem = SPProblem::new(instance.clone()).expect("create compression problem");
    problem
        .change_strip_width(3.0)
        .expect("set compression width");
    problem.place_item(SPPlacement {
        item_idx: 0,
        d_transf: DTransformation::new(0.0, (1.05, 0.55)),
    });
    problem.place_item(SPPlacement {
        item_idx: 0,
        d_transf: DTransformation::new(0.0, (1.55, 0.55)),
    });
    let solution = problem.save();
    (problem, solution)
}

fn compression_separator_config() -> sparrow::optimizer::separator::SeparatorConfig {
    let defaults = DEFAULT_SPARROW_CONFIG;
    let mut config = defaults.cmpr_cfg.separator_config;
    config.iter_no_imprv_limit = 219;
    config.strike_limit = 1;
    config.n_workers = 1;
    config.sample_config = SampleConfig {
        n_container_samples: 1,
        n_focussed_samples: 0,
        n_coord_descents: 1,
    };
    config
}

fn run_worker(
    instance: &SPInstance,
    master_solution: &SPSolution,
    master_tracker: &CollisionTracker,
    config: sparrow::optimizer::separator::SeparatorConfig,
    rng: Xoshiro256PlusPlus,
) -> WorkerOutcome {
    let mut problem = SPProblem::new(instance.clone()).expect("create isolated worker problem");
    problem
        .change_strip_width(master_solution.strip_width())
        .expect("set isolated worker width");
    problem.restore(master_solution);

    let mut separator = Separator::new(problem, Xoshiro256PlusPlus::seed_from_u64(0), config);
    separator.workers[0].rng = rng;
    separator.workers[0].load(master_solution, master_tracker);
    let stats = separator.workers[0].move_items();
    WorkerOutcome {
        solution: separator.workers[0].prob.save(),
        tracker: separator.workers[0].ct.clone(),
        moves: stats.total_moves,
        evaluations: stats.total_evals,
    }
}

fn sorted_positions(solution: &SPSolution) -> Vec<(f32, f32)> {
    let mut positions = solution
        .layout_snapshot
        .placed_items
        .values()
        .map(|placed| placed.d_transf.translation())
        .collect::<Vec<_>>();
    positions.sort_by(|left, right| left.0.total_cmp(&right.0));
    positions
}

fn close(actual: f32, expected: f32) -> bool {
    (actual - expected).abs() <= 5.0e-6
}

#[test]
#[cfg_attr(
    debug_assertions,
    ignore = "the locked compression replay reaches the known weighted-loss NaN debug assertion"
)]
fn round26_two_worker_search_replays_compression_round_446() {
    let instance = load_round24_instance();
    let (problem, initial_solution) = make_compression_start(&instance);
    let mut top_level_rng = Xoshiro256PlusPlus::seed_from_u64(4);
    let _exploration_seed = top_level_rng.next_u64();
    let compression_seed = top_level_rng.next_u64();

    let mut config = compression_separator_config();
    let mut separator = Separator::new(
        problem,
        Xoshiro256PlusPlus::seed_from_u64(compression_seed),
        config,
    );

    // Match the first Compression attempt's reset, rollback, random split,
    // and worker-RNG reset before replaying its single-worker rounds.
    separator.change_strip_width(initial_solution.strip_width(), None);
    separator.rollback(&initial_solution, None);
    let split_position = separator
        .rng
        .random_range(0.0..separator.prob.strip_width());
    let shrunken_width = initial_solution.strip_width() * (1.0 - 0.03);
    separator.change_strip_width(shrunken_width, Some(split_position));
    assert_eq!(separator.ct.get_total_loss().to_bits(), 0x403b_7db8);

    let mut replayed_moves = 0usize;
    let mut replayed_evaluations = 0usize;
    let mut min_loss = separator.ct.get_total_loss();
    let mut best_solution = separator.prob.save();
    let mut best_tracker = separator.ct.save();
    let mut strikes = 0usize;
    let mut no_improvement = 0usize;
    let mut initial_strike_loss = min_loss;
    // Compression round 446 is zero-based, so replay the 446 preceding rounds.
    for _ in 0..REPLAYED_ROUNDS_BEFORE_PAIR {
        let (moves, evaluations) = separator.move_items_multi_for_trace();
        replayed_moves += moves;
        replayed_evaluations += evaluations;
        let loss = separator.ct.get_total_loss();
        assert_ne!(loss, 0.0, "baseline must remain infeasible");
        if loss < min_loss {
            if loss < min_loss * 0.98 {
                no_improvement = 0;
            }
            best_solution = separator.prob.save();
            best_tracker = separator.ct.save();
            min_loss = loss;
        } else {
            no_improvement += 1;
        }
        separator.ct.update_weights();

        if no_improvement == config.iter_no_imprv_limit {
            if initial_strike_loss * 0.98 <= min_loss {
                strikes += 1;
            } else {
                strikes = 0;
            }
            separator.rollback(&best_solution, Some(&best_tracker));
            assert!(
                strikes < config.strike_limit,
                "single-worker baseline ended before the pair round"
            );
            initial_strike_loss = separator.ct.get_total_loss();
            no_improvement = 0;
        }
    }

    let master_solution = separator.prob.save();
    let master_tracker = separator.ct.clone();
    let worker0_rng = separator.workers[0].rng.clone();

    // A one-worker separator seeded from the current root stream creates the
    // same new child stream used for worker 1 in the captured paired replay.
    config.n_workers = 2;
    let mut pair = Separator::new(separator.prob.clone(), separator.rng.clone(), config);
    pair.ct = master_tracker.clone();
    let worker1 = pair.workers.remove(0);
    let worker0 = separator.workers.remove(0);
    pair.workers[0] = worker0;
    pair.workers.push(worker1);

    let worker1_rng = pair.workers[1].rng.clone();
    let worker0_outcome = run_worker(
        &instance,
        &master_solution,
        &master_tracker,
        compression_separator_config(),
        worker0_rng,
    );
    let worker1_outcome = run_worker(
        &instance,
        &master_solution,
        &master_tracker,
        compression_separator_config(),
        worker1_rng,
    );

    let (pair_moves, pair_evaluations) = pair.move_items_multi_for_trace();
    let worker_keys = pair
        .workers
        .iter()
        .map(|worker| worker.ct.get_total_weighted_loss())
        .collect::<Vec<_>>();

    assert_eq!(replayed_moves, 543);
    assert_eq!(replayed_evaluations, 25_798);
    assert!(worker_keys.iter().all(|key| key.is_nan()));
    assert!(worker1_outcome.tracker.get_total_loss() < worker0_outcome.tracker.get_total_loss());
    assert_eq!(
        worker0_outcome.tracker.get_total_loss().to_bits(),
        0x403a_4f1c
    );
    assert_eq!(
        worker1_outcome.tracker.get_total_loss().to_bits(),
        0x4036_0dd7
    );
    assert_eq!(
        (worker0_outcome.moves, worker0_outcome.evaluations),
        (2, 130)
    );
    assert_eq!(
        (worker1_outcome.moves, worker1_outcome.evaluations),
        (1, 72)
    );
    assert_eq!(pair_moves, 3);
    assert_eq!(pair_evaluations, 202);
    assert_eq!(select_worker_index_for_trace(&worker_keys), Some(0));

    let worker0_positions = sorted_positions(&worker0_outcome.solution);
    let worker1_positions = sorted_positions(&worker1_outcome.solution);
    let selected_positions = sorted_positions(&pair.prob.save());
    assert_eq!(worker0_positions.len(), 2);
    assert!(close(worker0_positions[0].0, 1.0169114));
    assert!(close(worker0_positions[0].1, 0.52927935));
    assert!(close(worker0_positions[1].0, 1.8374491));
    assert!(close(worker0_positions[1].1, 1.5295486));
    assert!(close(worker1_positions[0].0, 1.0013936));
    assert!(close(worker1_positions[0].1, 0.50548494));
    assert!(close(worker1_positions[1].0, 1.8697289));
    assert!(close(worker1_positions[1].1, 1.5056324));
    assert_eq!(selected_positions, worker0_positions);
    assert_eq!(
        sorted_positions(&pair.workers[0].prob.save()),
        worker0_positions
    );
    assert_eq!(
        sorted_positions(&pair.workers[1].prob.save()),
        worker1_positions
    );
    assert_eq!(
        pair.workers[0].ct.get_total_loss().to_bits(),
        worker0_outcome.tracker.get_total_loss().to_bits()
    );
    assert_eq!(
        pair.workers[1].ct.get_total_loss().to_bits(),
        worker1_outcome.tracker.get_total_loss().to_bits()
    );
    assert_eq!(
        pair.ct.get_total_loss().to_bits(),
        worker0_outcome.tracker.get_total_loss().to_bits()
    );
    assert!(pair.ct.get_total_weighted_loss().is_nan());
}
