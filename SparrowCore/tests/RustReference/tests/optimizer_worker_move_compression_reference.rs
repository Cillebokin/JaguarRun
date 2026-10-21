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
use sparrow::sample::search::SampleConfig;
use sparrow::util::listener::{
    OptimizationPhase, ReportType, SeparationProgress, SeparationResult, SolutionListener,
};
use sparrow::util::terminator::Terminator;
use std::panic::{AssertUnwindSafe, catch_unwind};
use std::path::Path;
use std::sync::Arc;
use std::sync::atomic::{AtomicBool, Ordering};
use std::time::Duration;

const REFERENCE: &str =
    include_str!("../../Fixtures/OptimizeWorkerMoveCompressionRustReference.tsv");
const TIME_BASED_REFERENCE: &str =
    include_str!("../../Fixtures/OptimizeTimeBasedClockWorkerMoveRustReference.tsv");
const EXPLORATION_WORKER_REFERENCE: &str =
    include_str!("../../Fixtures/OptimizeExplorationWorkerCompressionRustReference.tsv");

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
    separation_results: Vec<SeparationResult>,
    stop_after_compression_results: usize,
    compression_results_seen: usize,
    stop_on_initial_exploration_report: bool,
    post_selection_losses: Vec<f32>,
    stopped_exploration: bool,
    stopped_compression: bool,
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
        if self.phase == Some("exploration") && !self.stopped_exploration {
            if self.stop_on_initial_exploration_report {
                self.stopped_exploration = true;
                self.stop
                    .as_ref()
                    .expect("phase terminator handle")
                    .store(true, Ordering::SeqCst);
            }
        }
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
        if phase == "exploration" && progress.iteration > 0 {
            self.post_selection_losses.push(progress.min_loss);
            self.trace.push(format!(
                "post_selection_tracker\tstate\t{:.9}",
                progress.min_loss,
            ));
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
        if phase == "compression" {
            self.compression_results_seen += 1;
        }
        match phase {
            "exploration" if !self.stopped_exploration => {
                self.stopped_exploration = true;
                self.stop
                    .as_ref()
                    .expect("phase terminator handle")
                    .store(true, Ordering::SeqCst);
            }
            "compression"
                if !self.stopped_compression
                    && self.compression_results_seen >= self.stop_after_compression_results =>
            {
                self.stopped_compression = true;
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

fn run_optimize_compression_worker_move_reference(time_based: bool) {
    run_optimize_worker_move_reference(time_based, false, false);
}

fn run_optimize_worker_move_reference(
    time_based: bool,
    exploration_worker_search: bool,
    zero_capacity_compression_profile: bool,
) {
    let seed = 42;
    let (external_instance, _) =
        sparrow::util::io::read_spp_input(Path::new("../Fixtures/RustSppCompressWorkerMove.json"))
            .expect("read shared compression worker-move fixture");
    let mut config = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        config.cde_config,
        config.poly_simpl_tolerance,
        config.narrow_concavity_cutoff_ratio,
    );
    let instance = import_instance(&importer, &external_instance)
        .expect("import compression worker-move fixture");
    let mut warm_problem = SPProblem::new(instance.clone()).expect("create warm-start problem");
    warm_problem
        .change_strip_width(4.2)
        .expect("set warm-start strip width");
    warm_problem.place_item(SPPlacement {
        item_idx: 0,
        d_transf: DTransformation::new(0.0, (1.05, 0.55)),
    });
    warm_problem.place_item(SPPlacement {
        item_idx: 0,
        d_transf: DTransformation::new(
            0.0,
            (
                if exploration_worker_search {
                    1.55
                } else {
                    3.15
                },
                0.55,
            ),
        ),
    });
    let warm_start = warm_problem.save();
    assert_eq!(
        Layout::from_snapshot(&warm_start.layout_snapshot).is_feasible(),
        !exploration_worker_search,
    );

    config.expl_cfg.time_limit = Duration::from_secs(30);
    config.expl_cfg.separator_config.n_workers = if exploration_worker_search { 2 } else { 1 };
    if exploration_worker_search {
        config.expl_cfg.separator_config.iter_no_imprv_limit = 1;
        config.expl_cfg.separator_config.strike_limit = 1;
        config.expl_cfg.separator_config.sample_config = SampleConfig {
            n_container_samples: 1,
            n_focussed_samples: 0,
            n_coord_descents: 1,
        };
    }
    let clock_sequence = [Duration::ZERO, Duration::ZERO, Duration::from_secs(1)];
    if time_based {
        config.cmpr_cfg.shrink_range = (0.03, 0.01);
        config.cmpr_cfg.shrink_decay = ShrinkDecayStrategy::TimeBased;
        config.cmpr_cfg.time_limit = Duration::from_secs(2);
    } else {
        config.cmpr_cfg.shrink_range = (0.03, 0.03);
        config.cmpr_cfg.shrink_decay = ShrinkDecayStrategy::FailureBased(0.5);
        config.cmpr_cfg.time_limit = Duration::from_secs(30);
    }
    config.cmpr_cfg.separator_config.iter_no_imprv_limit = 1;
    config.cmpr_cfg.separator_config.strike_limit = 1;
    config.cmpr_cfg.separator_config.n_workers = 1;
    config.cmpr_cfg.separator_config.sample_config = SampleConfig {
        n_container_samples: 8,
        n_focussed_samples: 0,
        n_coord_descents: if zero_capacity_compression_profile {
            0
        } else {
            1
        },
    };

    let stop = Arc::new(AtomicBool::new(false));
    let mut terminator = PhaseTerminator {
        stop: Arc::clone(&stop),
    };
    let mut listener = RecordingListener {
        stop: Some(stop),
        stop_after_compression_results: if time_based { 2 } else { 1 },
        stop_on_initial_exploration_report: !exploration_worker_search,
        ..RecordingListener::default()
    };
    listener.trace.push(format!(
        "config\tparameters\t{seed}\t{:.9}\t{}\t{:.9}\t{:.9}\t{}\t{}\t{}\t{}\t{}",
        warm_start.strip_width(),
        config.expl_cfg.time_limit.as_secs(),
        config.cmpr_cfg.shrink_range.0,
        config.cmpr_cfg.shrink_range.1,
        config.cmpr_cfg.separator_config.n_workers,
        config.cmpr_cfg.separator_config.iter_no_imprv_limit,
        config
            .cmpr_cfg
            .separator_config
            .sample_config
            .n_container_samples,
        config
            .cmpr_cfg
            .separator_config
            .sample_config
            .n_focussed_samples,
        config
            .cmpr_cfg
            .separator_config
            .sample_config
            .n_coord_descents,
    ));
    if exploration_worker_search {
        listener.trace.push(format!(
            "config\texploration_workers\t{}\t{}\t{}\t{}\t{}",
            config.expl_cfg.separator_config.n_workers,
            config.expl_cfg.separator_config.iter_no_imprv_limit,
            config
                .expl_cfg
                .separator_config
                .sample_config
                .n_container_samples,
            config
                .expl_cfg
                .separator_config
                .sample_config
                .n_focussed_samples,
            config
                .expl_cfg
                .separator_config
                .sample_config
                .n_coord_descents,
        ));
    }
    if time_based {
        listener.trace.push(format!(
            "config\tcompression_time_limit\t{}",
            config.cmpr_cfg.time_limit.as_secs(),
        ));
        listener
            .trace
            .push("config\tshrink_decay_time_based".to_owned());
        for sample in clock_sequence {
            listener
                .trace
                .push(format!("clock_sample\tmonotonic_ns\t{}", sample.as_nanos(),));
        }
    }
    append_solution_rows(&mut listener.trace, "initial", &warm_start);

    let clock = time_based
        .then(|| sparrow::optimizer::compress::ScopedCompressionClockSequence::new(clock_sequence));

    let optimize_result = catch_unwind(AssertUnwindSafe(|| {
        optimize(
            instance.clone(),
            Xoshiro256PlusPlus::seed_from_u64(seed),
            &mut listener,
            &mut terminator,
            &config.expl_cfg,
            &config.cmpr_cfg,
            Some(&warm_start),
        )
    }));
    if zero_capacity_compression_profile {
        assert!(listener.stopped_exploration);
        assert_eq!(listener.phase, Some("compression"));
        assert!(
            listener
                .trace
                .iter()
                .any(|row| row.starts_with("compression_step\t"))
        );
        if cfg!(debug_assertions) {
            assert!(listener.separation_results.is_empty());
            let panic = optimize_result.expect_err(
                "Rust Debug top-level Compression must fail fast for zero-capacity BestSamples",
            );
            let panic_message = panic
                .downcast_ref::<String>()
                .map(String::as_str)
                .or_else(|| panic.downcast_ref::<&str>().copied())
                .unwrap_or("<non-string panic payload>");
            assert!(
                panic_message.contains("subtract with overflow"),
                "expected the zero-capacity BestSamples underflow, got: {panic_message}"
            );
        } else {
            let final_solution = optimize_result
                .expect("Rust Release top-level Compression must not panic")
                .expect("Rust Release top-level Compression must return a solution");
            assert!(listener.stopped_compression);
            assert_eq!(listener.compression_results_seen, 1);
            assert_eq!(listener.separation_results.len(), 1);
            assert!(final_solution.strip_width() <= warm_start.strip_width());
        }
        return;
    }
    let final_solution = optimize_result
        .expect("top-level Rust optimize worker-move case must not panic")
        .expect("run top-level Rust optimize worker-move case");
    if let Some(clock) = &clock {
        listener.trace.push(format!(
            "clock_usage\tcompression\t{}\t{}",
            clock.samples_consumed(),
            u8::from(clock.exhausted()),
        ));
    }
    append_terminal_rows(&mut listener.trace, seed, &instance, &final_solution);

    if std::env::var_os("SPARROW_PRINT_OPTIMIZE_WORKER_MOVE_REFERENCE").is_some() {
        for row in &listener.trace {
            println!("{row}");
        }
    }

    assert_eq!(listener.phase, Some("compression"));
    assert!(listener.stopped_exploration);
    assert!(listener.stopped_compression);
    assert_eq!(
        listener.compression_results_seen,
        if time_based { 2 } else { 1 },
    );
    assert_eq!(
        listener.separation_results.len(),
        if time_based {
            2
        } else if exploration_worker_search {
            2
        } else {
            1
        },
    );
    assert!(
        listener
            .separation_results
            .iter()
            .any(|separation| separation.success && separation.total_moves > 0)
    );
    assert!(final_solution.strip_width() < warm_start.strip_width());
    assert!(Layout::from_snapshot(&final_solution.layout_snapshot).is_feasible());
    if exploration_worker_search {
        assert_eq!(listener.post_selection_losses, [0.0]);
        let exploration_result = &listener.separation_results[0];
        let compression_result = listener.separation_results.last().unwrap();
        assert!(exploration_result.success && exploration_result.total_moves > 0);
        assert!(compression_result.success && compression_result.total_moves > 0);
    }
    if let Some(clock) = &clock {
        assert_eq!(clock.samples_consumed(), 3);
        assert!(!clock.exhausted());
    }

    let reference = if exploration_worker_search {
        EXPLORATION_WORKER_REFERENCE
    } else if time_based {
        TIME_BASED_REFERENCE
    } else {
        REFERENCE
    };
    let expected = reference
        .lines()
        .filter(|line| !line.is_empty() && !line.starts_with('#'))
        .collect::<Vec<_>>();
    let actual = listener
        .trace
        .iter()
        .map(String::as_str)
        .collect::<Vec<_>>();
    assert_eq!(actual, expected, "Rust top-level worker-move trace changed");
}

#[test]
fn optimize_compression_succeeds_after_a_worker_move_and_matches_reference() {
    run_optimize_compression_worker_move_reference(false);
}

#[test]
fn optimize_time_based_compression_uses_shared_clock_and_matches_reference() {
    run_optimize_compression_worker_move_reference(true);
}

#[test]
fn optimize_real_exploration_workers_then_compress_matches_reference() {
    run_optimize_worker_move_reference(false, true, false);
}

#[test]
fn optimize_zero_capacity_compression_profile_reaches_worker_handoff() {
    run_optimize_worker_move_reference(false, false, true);
}
