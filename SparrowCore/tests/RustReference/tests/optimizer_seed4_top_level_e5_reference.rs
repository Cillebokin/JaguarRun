use jagua_rs::Instant;
use jagua_rs::entities::Layout;
use jagua_rs::geometry::DTransformation;
use jagua_rs::io::import::Importer;
use jagua_rs::probs::spp::entities::{SPPlacement, SPProblem, SPSolution};
use jagua_rs::probs::spp::io::import_instance;
use rand::SeedableRng;
use rand::rngs::Xoshiro256PlusPlus;
use sparrow::config::{DEFAULT_SPARROW_CONFIG, ShrinkDecayStrategy};
use sparrow::optimizer::optimize;
use sparrow::quantify::tracker::CollisionTracker;
use sparrow::util::listener::{
    OptimizationPhase, ReportType, SeparationProgress, SeparationResult, SolutionListener,
};
use sparrow::util::terminator::Terminator;
use std::path::Path;
use std::sync::Arc;
use std::sync::atomic::{AtomicBool, Ordering};
use std::time::Duration;

#[cfg(not(debug_assertions))]
const REFERENCE: &str =
    include_str!("../../Fixtures/OptimizeSeed4TopLevelOptimizeCompressE5Reference.tsv");
#[cfg(not(debug_assertions))]
const TWO_WORKER_COMPRESSION_REFERENCE: &str =
    include_str!("../../Fixtures/OptimizeSeed4TopLevelTwoWorkerCompressionE5Reference.tsv");
#[cfg(not(debug_assertions))]
const SEED10_TWO_WORKER_COMPRESSION_REFERENCE: &str =
    include_str!("../../Fixtures/OptimizeSeed10TopLevelTwoWorkerCompressionE5Reference.tsv");
const EXPLORE_NO_IMPROVEMENT_LIMIT: usize = 451;
const COMPRESSION_NO_IMPROVEMENT_LIMIT: usize = 219;

#[derive(Clone)]
struct PhaseTerminator {
    stop: Arc<AtomicBool>,
}
impl Terminator for PhaseTerminator {
    fn kill(&self) -> bool {
        self.stop.load(Ordering::SeqCst)
    }

    fn new_timeout(&mut self, _timeout: Duration) {
        self.stop.store(false, Ordering::SeqCst);
    }

    fn timeout_at(&self) -> Option<Instant> {
        None
    }
}

#[derive(Default)]
struct RecordingListener {
    trace: Vec<String>,
    phase: Option<&'static str>,
    stop: Option<Arc<AtomicBool>>,
    exploration_stopped: bool,
    compression_stopped: bool,
    exploration_progress_count: usize,
    separation_results: Vec<SeparationResult>,
}

fn report_name(report: &ReportType) -> &'static str {
    match report {
        ReportType::ExplFeas => "expl_feas",
        ReportType::ExplInfeas => "expl_infeas",
        ReportType::ExplImproving => "expl_improving",
        ReportType::CmprFeas => "cmpr_feas",
        ReportType::Final => "final",
    }
}

fn append_solution_rows(trace: &mut Vec<String>, label: &str, solution: &SPSolution) {
    trace.push(format!(
        "report\t{label}\t{}\t{:.9}\t{:.9}",
        solution.layout_snapshot.placed_items.len(),
        solution.strip_width(),
        solution.density(),
    ));
    let mut placements = solution
        .layout_snapshot
        .placed_items
        .values()
        .collect::<Vec<_>>();
    placements.sort_by(|left, right| {
        left.d_transf
            .translation()
            .0
            .total_cmp(&right.d_transf.translation().0)
    });
    for placement in placements {
        let (x, y) = placement.d_transf.translation();
        trace.push(format!(
            "placement\t{label}\t{}\t{:.9}\t{:.9}\t{:.9}",
            placement.item.idx,
            placement.d_transf.rotation(),
            x,
            y,
        ));
    }
}

impl SolutionListener for RecordingListener {
    fn report(&mut self, report: ReportType, solution: &SPSolution) {
        append_solution_rows(&mut self.trace, report_name(&report), solution);
    }

    fn report_phase(&mut self, phase: OptimizationPhase) {
        let label = match phase {
            OptimizationPhase::Exploration => {
                self.phase = Some("exploration");
                "exploration"
            }
            OptimizationPhase::Compression => {
                self.phase = Some("compression");
                "compression"
            }
        };
        self.trace.push(format!("phase\t{label}"));
    }

    fn report_separation_progress(&mut self, progress: SeparationProgress) {
        let phase = self.phase.expect("progress follows a phase callback");
        self.trace.push(format!(
            "progress\t{phase}\t{}\t{:.9}\t{:.9}\t{:.9}",
            progress.iteration, progress.strip_width, progress.density, progress.min_loss,
        ));
        if phase == "exploration" {
            self.exploration_progress_count += usize::from(progress.iteration > 0);
            if progress.iteration > 0 {
                self.trace.push(format!(
                    "post_selection_tracker\tstate\t{:.17}",
                    f64::from(progress.min_loss),
                ));
            }
        }
    }

    fn report_separation_result(&mut self, result: SeparationResult) {
        let phase = self.phase.expect("result follows a phase callback");
        self.trace.push(format!(
            "separation\t{phase}\t{}\t{}\t{}\t{}",
            u8::from(result.success),
            result.total_evals,
            result.total_moves,
            result.iterations,
        ));
        self.separation_results.push(result);
        match phase {
            "exploration" if !self.exploration_stopped => {
                self.exploration_stopped = true;
                self.stop
                    .as_ref()
                    .expect("phase terminator handle")
                    .store(true, Ordering::SeqCst);
            }
            "compression" if !self.compression_stopped => {
                self.compression_stopped = true;
                self.stop
                    .as_ref()
                    .expect("phase terminator handle")
                    .store(true, Ordering::SeqCst);
            }
            _ => {}
        }
    }

    fn report_compression_progress(&mut self, step: f32) {
        self.trace
            .push(format!("compression_step\tvalue\t{step:.9}"));
    }
}

fn append_terminal_rows(
    trace: &mut Vec<String>,
    seed: u64,
    instance: &jagua_rs::probs::spp::entities::SPInstance,
    solution: &SPSolution,
) {
    let mut problem = SPProblem::new(instance.clone()).expect("create terminal SPP problem");
    problem.restore(solution);
    let layout = Layout::from_snapshot(&solution.layout_snapshot);
    let remaining_demand = problem.item_demand_qtys.iter().sum::<usize>();
    let layout_feasible = layout.is_feasible();
    let solution_feasible = remaining_demand == 0 && layout_feasible;
    let final_loss = CollisionTracker::new(&layout).get_total_loss();
    trace.push(format!(
        "terminal\tstate\t{seed}\t{}\t{remaining_demand}\t{:.9}\t{:.9}\t{}\t{}\t{:.9}",
        problem.n_placed_items(),
        solution.strip_width(),
        solution.density(),
        u8::from(solution_feasible),
        u8::from(layout_feasible),
        final_loss,
    ));
    let mut placed_ids = solution
        .layout_snapshot
        .placed_items
        .values()
        .map(|placement| placement.item.idx)
        .collect::<Vec<_>>();
    placed_ids.sort_unstable();
    let mut placed_row = String::from("placed_ids\tlist");
    for item_index in placed_ids {
        placed_row.push_str(&format!("\t{item_index}"));
    }
    trace.push(placed_row);
    if remaining_demand == 0 {
        trace.push("remaining_item\tnone".to_owned());
    } else {
        trace.extend(
            problem
                .item_demand_qtys
                .iter()
                .enumerate()
                .filter(|(_, quantity)| **quantity > 0)
                .map(|(item_index, quantity)| format!("remaining_item\t{item_index}\t{quantity}")),
        );
    }
}

fn run_top_level_trace(
    seed: u64,
    compression_worker_count: usize,
) -> (Vec<String>, RecordingListener, bool) {
    let (external, _) =
        sparrow::util::io::read_spp_input(Path::new("../Fixtures/RustSppCompressWorkerMove.json"))
            .expect("read seed4 top-level fixture");
    let defaults = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        defaults.cde_config,
        defaults.poly_simpl_tolerance,
        defaults.narrow_concavity_cutoff_ratio,
    );
    let instance = import_instance(&importer, &external).expect("import seed4 top-level fixture");

    let mut warm_problem = SPProblem::new(instance.clone()).expect("create seed4 warm start");
    warm_problem
        .change_strip_width(3.0)
        .expect("set seed4 strip width");
    warm_problem.place_item(SPPlacement {
        item_idx: 0,
        d_transf: DTransformation::new(0.0, (1.05, 0.55)),
    });
    warm_problem.place_item(SPPlacement {
        item_idx: 0,
        d_transf: DTransformation::new(0.0, (1.55, 0.55)),
    });
    let warm_start = warm_problem.save();

    let mut config = DEFAULT_SPARROW_CONFIG;
    config.expl_cfg.time_limit = Duration::from_secs(3600);
    config.expl_cfg.separator_config.n_workers = 1;
    config.expl_cfg.separator_config.strike_limit = 1;
    config.expl_cfg.separator_config.iter_no_imprv_limit = EXPLORE_NO_IMPROVEMENT_LIMIT;
    config.expl_cfg.separator_config.sample_config = sparrow::sample::search::SampleConfig {
        n_container_samples: 1,
        n_focussed_samples: 0,
        n_coord_descents: 1,
    };
    config.cmpr_cfg.shrink_range = (0.03, 0.01);
    config.cmpr_cfg.shrink_decay = ShrinkDecayStrategy::FailureBased(0.5);
    config.cmpr_cfg.time_limit = Duration::from_secs(3600);
    config.cmpr_cfg.separator_config.n_workers = compression_worker_count;
    config.cmpr_cfg.separator_config.strike_limit = 1;
    config.cmpr_cfg.separator_config.iter_no_imprv_limit = COMPRESSION_NO_IMPROVEMENT_LIMIT;
    config.cmpr_cfg.separator_config.sample_config = sparrow::sample::search::SampleConfig {
        n_container_samples: 1,
        n_focussed_samples: 0,
        n_coord_descents: 1,
    };

    let stop = Arc::new(AtomicBool::new(false));
    let mut terminator = PhaseTerminator {
        stop: Arc::clone(&stop),
    };
    let mut listener = RecordingListener {
        stop: Some(stop),
        ..RecordingListener::default()
    };
    listener.trace.push(format!(
        "config\tparameters\t{seed}\t{:.9}\t1\t1\t0\t1\t{EXPLORE_NO_IMPROVEMENT_LIMIT}\t{compression_worker_count}\t{COMPRESSION_NO_IMPROVEMENT_LIMIT}",
        warm_start.strip_width(),
    ));
    append_solution_rows(&mut listener.trace, "initial", &warm_start);

    let solution = optimize(
        instance.clone(),
        Xoshiro256PlusPlus::seed_from_u64(seed),
        &mut listener,
        &mut terminator,
        &config.expl_cfg,
        &config.cmpr_cfg,
        Some(&warm_start),
    )
    .expect("run seed4 top-level optimize->compress");
    append_terminal_rows(&mut listener.trace, seed, &instance, &solution);
    let stopped_at_fixed_boundaries = listener.exploration_stopped
        && listener.compression_stopped
        && listener.separation_results.len() == 2
        && listener.separation_results[0].iterations > 0
        && listener.exploration_progress_count == listener.separation_results[0].iterations;
    (
        listener.trace.clone(),
        listener,
        stopped_at_fixed_boundaries,
    )
}

#[cfg(not(debug_assertions))]
#[test]
fn seed4_top_level_optimize_compress_matches_reference() {
    let (actual, listener, stopped_at_fixed_boundaries) = run_top_level_trace(4, 1);
    if std::env::var_os("SPARROW_PRINT_E5_SEED4_TOP_LEVEL_REFERENCE").is_some() {
        for row in &actual {
            println!("E5TRACE\t{row}");
        }
        return;
    }

    let expected = REFERENCE
        .lines()
        .filter(|line| !line.is_empty() && !line.starts_with('#'))
        .collect::<Vec<_>>()
        .join("\n");
    assert_eq!(
        actual.join("\n"),
        expected,
        "seed4 top-level E5 trace changed"
    );
    assert!(stopped_at_fixed_boundaries);
    assert_eq!(listener.phase, Some("compression"));
    assert_eq!(
        (
            listener.separation_results[1].iterations,
            listener.separation_results[1].total_evals,
            listener.separation_results[1].total_moves,
        ),
        (447, 25_928, 545),
        "seed4 Compression must reach its locked PlacedItem +Inf search boundary"
    );
}

#[cfg(not(debug_assertions))]
#[test]
fn seed4_top_level_two_worker_compression_matches_reference() {
    let (actual, listener, stopped_at_fixed_boundaries) = run_top_level_trace(4, 2);
    if std::env::var_os("SPARROW_PRINT_E5_SEED4_TWO_WORKER_COMPRESSION_REFERENCE").is_some() {
        for row in &actual {
            println!("E5TRACE\t{row}");
        }
        return;
    }

    let expected = TWO_WORKER_COMPRESSION_REFERENCE
        .lines()
        .filter(|line| !line.is_empty() && !line.starts_with('#'))
        .collect::<Vec<_>>()
        .join("\n");
    assert_eq!(
        actual.join("\n"),
        expected,
        "seed4 top-level two-worker Compression trace changed"
    );
    assert!(stopped_at_fixed_boundaries);
    assert_eq!(listener.phase, Some("compression"));
    assert_eq!(
        (
            listener.separation_results[1].iterations,
            listener.separation_results[1].total_evals,
            listener.separation_results[1].total_moves,
        ),
        (445, 50_550, 1_077),
        "seed4 two-worker Compression must match the locked phase result"
    );
}

#[cfg(not(debug_assertions))]
#[test]
fn seed10_top_level_two_worker_compression_matches_reference() {
    let (actual, listener, stopped_at_fixed_boundaries) = run_top_level_trace(10, 2);
    let expected = SEED10_TWO_WORKER_COMPRESSION_REFERENCE
        .lines()
        .filter(|line| !line.is_empty() && !line.starts_with('#'))
        .collect::<Vec<_>>()
        .join("\n");
    assert_eq!(
        actual.join("\n"),
        expected,
        "seed10 top-level two-worker Compression trace changed"
    );
    assert!(stopped_at_fixed_boundaries);
    assert_eq!(listener.phase, Some("compression"));
    assert_eq!(
        (
            listener.separation_results[1].iterations,
            listener.separation_results[1].total_evals,
            listener.separation_results[1].total_moves,
        ),
        (449, 51_094, 1_078),
        "seed10 top-level two-worker Compression must reach the locked non-finite selection fixture"
    );
}

#[cfg(debug_assertions)]
#[test]
fn seed4_top_level_debug_retains_known_gls_nonfinite_assertion() {
    use std::panic::AssertUnwindSafe;

    let panic = match std::panic::catch_unwind(AssertUnwindSafe(|| run_top_level_trace(4, 1))) {
        Ok(_) => panic!("this pinned Debug scenario must hit the GLS assertion"),
        Err(payload) => payload,
    };
    let message = panic
        .downcast_ref::<String>()
        .map(String::as_str)
        .or_else(|| panic.downcast_ref::<&str>().copied())
        .unwrap_or_default();
    assert!(
        message.contains("weighted loss should never increase"),
        "unexpected Debug panic: {message}"
    );
}

#[cfg(debug_assertions)]
fn assert_top_level_debug_retains_known_gls_nonfinite_assertion(
    seed: u64,
    compression_workers: usize,
) {
    use std::panic::AssertUnwindSafe;

    let panic = match std::panic::catch_unwind(AssertUnwindSafe(|| {
        run_top_level_trace(seed, compression_workers)
    })) {
        Ok(_) => panic!("seed {seed} must hit the pinned Debug GLS assertion"),
        Err(payload) => payload,
    };
    let message = panic
        .downcast_ref::<String>()
        .map(String::as_str)
        .or_else(|| panic.downcast_ref::<&str>().copied())
        .unwrap_or_default();
    assert!(
        message.contains("weighted loss should never increase"),
        "unexpected Debug panic for seed {seed} with {compression_workers} compression workers: {message}"
    );
}

#[cfg(debug_assertions)]
#[test]
fn seed4_and_seed10_two_worker_compression_retain_known_debug_assertion() {
    assert_top_level_debug_retains_known_gls_nonfinite_assertion(4, 2);
    assert_top_level_debug_retains_known_gls_nonfinite_assertion(10, 2);
}
