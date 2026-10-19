use jagua_rs::geometry::DTransformation;
use jagua_rs::io::import::Importer;
use jagua_rs::probs::spp::entities::{SPPlacement, SPProblem, SPSolution};
use jagua_rs::probs::spp::io::import_instance;
use ordered_float::OrderedFloat;
use rand::rngs::Xoshiro256PlusPlus;
use rand::SeedableRng;
use sparrow::config::DEFAULT_SPARROW_CONFIG;
use sparrow::optimizer::separator::Separator;
use sparrow::sample::search::SampleConfig;
use std::path::Path;

const REFERENCE: &str =
    include_str!("../../Fixtures/OptimizeSameSeedWorkerTrajectoryE5Reference.tsv");
const SEED4_REFERENCE: &str =
    include_str!("../../Fixtures/OptimizeSameSeedWorkerTrajectorySeed4E5Reference.tsv");
const STEPS: usize = 450;

fn make_separator(seed: u64) -> Separator {
    let (external, _) =
        sparrow::util::io::read_spp_input(Path::new("../Fixtures/RustSppCompressWorkerMove.json"))
            .expect("read same-seed worker trajectory fixture");
    let defaults = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        defaults.cde_config,
        defaults.poly_simpl_tolerance,
        defaults.narrow_concavity_cutoff_ratio,
    );
    let instance =
        import_instance(&importer, &external).expect("import same-seed worker trajectory fixture");
    let mut problem = SPProblem::new(instance).expect("create worker trajectory problem");
    problem.change_strip_width(3.0).expect("set strip width");
    problem.place_item(SPPlacement {
        item_idx: 0,
        d_transf: DTransformation::new(0.0, (1.05, 0.55)),
    });
    problem.place_item(SPPlacement {
        item_idx: 0,
        d_transf: DTransformation::new(0.0, (1.55, 0.55)),
    });

    let mut config = defaults.expl_cfg.separator_config;
    config.n_workers = 3;
    config.sample_config = SampleConfig {
        n_container_samples: 1,
        n_focussed_samples: 0,
        n_coord_descents: 1,
    };
    Separator::new(problem, Xoshiro256PlusPlus::seed_from_u64(seed), config)
}

fn append_state(
    trace: &mut Vec<String>,
    kind: &str,
    round: usize,
    min_loss: f32,
    solution: &SPSolution,
    tracker: &sparrow::quantify::tracker::CollisionTracker,
) {
    let mut placements = solution
        .layout_snapshot
        .placed_items
        .values()
        .map(|placed| {
            let (x, y) = placed.d_transf.translation();
            format!(
                "{}:{:08x}:{:08x}:{:08x}",
                placed.item.idx,
                placed.d_transf.rotation().to_bits(),
                x.to_bits(),
                y.to_bits(),
            )
        })
        .collect::<Vec<_>>();
    placements.sort();
    trace.push(format!(
        "{kind}\t{round}\t{:08x}\t{:08x}\t{:08x}\t{}",
        min_loss.to_bits(),
        tracker.get_total_loss().to_bits(),
        tracker.get_total_weighted_loss().to_bits(),
        placements.join("\t"),
    ));
}

fn run_fixed_production_rounds(seed: u64) -> Vec<String> {
    let mut separator = make_separator(seed);
    let mut best_solution = separator.prob.save();
    let mut best_tracker = separator.ct.save();
    let mut min_loss = separator.ct.get_total_loss();
    let mut total_moves = 0usize;
    let mut total_evals = 0usize;
    let mut trace = Vec::with_capacity(STEPS * 2 + 3);

    trace.push(format!(
        "config\tseed\t{seed}\tsteps\t{STEPS}\tworkers\t3\tsamples\t1/0/1"
    ));
    append_state(
        &mut trace,
        "state",
        0,
        min_loss,
        &best_solution,
        &separator.ct,
    );

    for round in 0..STEPS {
        let master_solution = separator.prob.save();
        let master_tracker = separator.ct.save();
        let mut worker_stats = Vec::with_capacity(separator.workers.len());
        for worker in &mut separator.workers {
            worker.load(&master_solution, &master_tracker);
            worker_stats.push(worker.move_items());
        }

        let keys = separator
            .workers
            .iter()
            .map(|worker| worker.ct.get_total_weighted_loss())
            .collect::<Vec<_>>();
        let raw_losses = separator
            .workers
            .iter()
            .map(|worker| worker.ct.get_total_loss())
            .collect::<Vec<_>>();
        let (best_index, selected_solution, selected_tracker) = separator
            .workers
            .iter()
            .enumerate()
            .min_by_key(|(_, worker)| OrderedFloat(worker.ct.get_total_weighted_loss()))
            .map(|(index, worker)| (index, worker.prob.save(), worker.ct.clone()))
            .expect("worker pool is non-empty");

        separator.prob.restore(&selected_solution);
        separator.ct = selected_tracker;
        let loss = separator.ct.get_total_loss();
        if loss == 0.0 {
            best_solution = separator.prob.save();
            best_tracker = separator.ct.save();
            min_loss = loss;
        } else if loss < min_loss {
            best_solution = separator.prob.save();
            best_tracker = separator.ct.save();
            min_loss = loss;
        }

        total_moves += worker_stats
            .iter()
            .map(|stats| stats.total_moves)
            .sum::<usize>();
        total_evals += worker_stats
            .iter()
            .map(|stats| stats.total_evals)
            .sum::<usize>();

        trace.push(format!(
            "workers\t{}\t{:08x}\t{:08x}\t{:08x}\t{:08x}\t{:08x}\t{:08x}\t{}\t{}\t{}\t{}\t{}",
            round + 1,
            keys[0].to_bits(),
            keys[1].to_bits(),
            keys[2].to_bits(),
            raw_losses[0].to_bits(),
            raw_losses[1].to_bits(),
            raw_losses[2].to_bits(),
            best_index,
            worker_stats[0].total_moves,
            worker_stats[1].total_moves,
            worker_stats[2].total_moves,
            worker_stats
                .iter()
                .map(|stats| stats.total_evals)
                .sum::<usize>(),
        ));

        if loss != 0.0 {
            separator.ct.update_weights();
        }
        append_state(
            &mut trace,
            "state",
            round + 1,
            min_loss,
            &separator.prob.save(),
            &separator.ct,
        );
        if loss == 0.0 {
            break;
        }
    }

    separator.prob.restore(&best_solution);
    separator
        .ct
        .restore_but_keep_weights(&best_tracker, &separator.prob.layout);
    trace.push(format!(
        "terminal\tsteps\t{}\tmoves\t{}\tevals\t{}\tloss\t{:08x}\tweighted\t{:08x}",
        trace
            .iter()
            .filter(|line| line.starts_with("state\t"))
            .count()
            - 1,
        total_moves,
        total_evals,
        separator.ct.get_total_loss().to_bits(),
        separator.ct.get_total_weighted_loss().to_bits(),
    ));
    trace
}

fn assert_fixed_production_worker_rounds_match_reference(
    seed: u64,
    reference: &str,
    print_env: &str,
) {
    let actual = run_fixed_production_rounds(seed);
    if std::env::var_os(print_env).is_some() {
        for line in &actual {
            println!("{line}");
        }
        return;
    }
    let expected = reference
        .lines()
        .filter(|line| !line.is_empty() && !line.starts_with('#'))
        .collect::<Vec<_>>()
        .join("\n");
    assert_eq!(
        actual.join("\n"),
        expected,
        "same-seed fixed-step worker trajectory changed"
    );
}

#[test]
fn same_seed_fixed_production_worker_rounds_match_reference() {
    assert_fixed_production_worker_rounds_match_reference(
        5,
        REFERENCE,
        "SPARROW_PRINT_E5_SAME_SEED_WORKER_TRAJECTORY",
    );
}

#[test]
fn seed4_fixed_production_worker_rounds_match_reference() {
    assert_fixed_production_worker_rounds_match_reference(
        4,
        SEED4_REFERENCE,
        "SPARROW_PRINT_E5_SAME_SEED_WORKER_TRAJECTORY_SEED4",
    );
}
