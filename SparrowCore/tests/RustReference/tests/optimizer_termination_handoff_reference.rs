use jagua_rs::Instant;
use jagua_rs::entities::Layout;
use jagua_rs::geometry::DTransformation;
use jagua_rs::io::import::Importer;
use jagua_rs::probs::spp::entities::{SPPlacement, SPProblem, SPSolution};
use jagua_rs::probs::spp::io::import_instance;
use rand::SeedableRng;
use rand::rngs::Xoshiro256PlusPlus;
use sparrow::config::{CompressionConfig, DEFAULT_SPARROW_CONFIG, ShrinkDecayStrategy};
use sparrow::optimizer::compress::compression_phase;
use sparrow::optimizer::explore::exploration_phase;
use sparrow::optimizer::separator::Separator;
use sparrow::util::listener::{ReportType, SeparationProgress, SeparationResult, SolutionListener};
use sparrow::util::terminator::Terminator;
use std::path::Path;
use std::sync::Arc;
use std::sync::atomic::{AtomicBool, Ordering};
use std::time::Duration;

const REFERENCE: &str = include_str!("../../Fixtures/OptimizerTerminationHandoffRustReference.tsv");

#[derive(Clone)]
struct PhaseTerminator {
    stop: Arc<AtomicBool>,
    timeout: Option<Instant>,
}

impl PhaseTerminator {
    fn new() -> Self {
        Self {
            stop: Arc::new(AtomicBool::new(false)),
            timeout: None,
        }
    }
}

impl Terminator for PhaseTerminator {
    fn kill(&self) -> bool {
        self.stop.load(Ordering::SeqCst)
            || self.timeout.is_some_and(|timeout| Instant::now() > timeout)
    }

    fn new_timeout(&mut self, timeout: Duration) {
        self.stop.store(false, Ordering::SeqCst);
        self.timeout = Some(Instant::now() + timeout);
    }

    fn timeout_at(&self) -> Option<Instant> {
        self.timeout
    }
}

#[derive(Default)]
struct RecordingListener {
    reports: Vec<&'static str>,
    compression_steps: Vec<f32>,
    separation_progress: Vec<SeparationProgress>,
    separation_results: Vec<SeparationResult>,
    interrupt_on_exploration_infeasible: bool,
    stop_after_compression_attempt: bool,
    stop: Option<Arc<AtomicBool>>,
}

impl SolutionListener for RecordingListener {
    fn report(&mut self, report: ReportType, _solution: &SPSolution) {
        let label = match report {
            ReportType::ExplFeas => "expl_feas",
            ReportType::ExplInfeas => "expl_infeas",
            ReportType::ExplImproving => "expl_improving",
            ReportType::CmprFeas => "cmpr_feas",
            ReportType::Final => "final",
        };
        self.reports.push(label);
        if self.interrupt_on_exploration_infeasible && report == ReportType::ExplInfeas {
            self.stop
                .as_ref()
                .expect("phase terminator handle")
                .store(true, Ordering::SeqCst);
            self.interrupt_on_exploration_infeasible = false;
        }
    }

    fn report_separation_progress(&mut self, progress: SeparationProgress) {
        self.separation_progress.push(progress);
    }

    fn report_separation_result(&mut self, result: SeparationResult) {
        self.separation_results.push(result);
        if self.stop_after_compression_attempt {
            self.stop
                .as_ref()
                .expect("phase terminator handle")
                .store(true, Ordering::SeqCst);
            self.stop_after_compression_attempt = false;
        }
    }

    fn report_compression_progress(&mut self, step: f32) {
        self.compression_steps.push(step);
    }
}

fn sorted_placements(solution: &SPSolution) -> Vec<(usize, f32, f32, f32)> {
    let mut placements = solution
        .layout_snapshot
        .placed_items
        .values()
        .map(|placement| {
            let (x, y) = placement.d_transf.translation();
            (placement.item.idx, placement.d_transf.rotation(), x, y)
        })
        .collect::<Vec<_>>();
    placements.sort_by_key(|placement| placement.0);
    placements
}

fn append_solution_rows(trace: &mut Vec<String>, label: &str, solution: &SPSolution) {
    trace.push(format!(
        "solution {label} {:.9} {:.9} {} {}",
        solution.strip_width(),
        solution.density(),
        solution.layout_snapshot.placed_items.len(),
        u8::from(Layout::from_snapshot(&solution.layout_snapshot).is_feasible()),
    ));
    for (item, rotation, x, y) in sorted_placements(solution) {
        trace.push(format!(
            "placement {label} {item} {rotation:.9} {x:.9} {y:.9}"
        ));
    }
}

#[test]
fn terminated_exploration_hands_separator_state_and_best_solution_to_compression() {
    let (external_instance, _) =
        sparrow::util::io::read_spp_input(Path::new("../Fixtures/ExplorationPool.json"))
            .expect("read shared handoff SPP fixture");
    let config = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        config.cde_config,
        config.poly_simpl_tolerance,
        config.narrow_concavity_cutoff_ratio,
    );
    let instance = import_instance(&importer, &external_instance).expect("import SPP fixture");

    let mut problem = SPProblem::new(instance.clone()).expect("create handoff problem");
    problem.change_strip_width(8.0).expect("set initial width");
    problem.place_item(SPPlacement {
        item_idx: 0,
        d_transf: DTransformation::new(0.0, (2.6, 2.0)),
    });
    problem.place_item(SPPlacement {
        item_idx: 1,
        d_transf: DTransformation::new(0.0, (6.5, 2.0)),
    });
    let initial_solution = problem.save();
    assert!(Layout::from_snapshot(&initial_solution.layout_snapshot).is_feasible());

    let mut exploration = DEFAULT_SPARROW_CONFIG.expl_cfg;
    exploration.shrink_step = 0.1875;
    exploration.time_limit = Duration::from_secs(30);
    exploration.max_conseq_failed_attempts = None;
    exploration.solution_pool_distribution_stddev = 1.0;
    exploration.separator_config.strike_limit = 0;
    exploration.separator_config.n_workers = 1;

    let mut compression: CompressionConfig = DEFAULT_SPARROW_CONFIG.cmpr_cfg;
    compression.shrink_range = (0.05, 0.01);
    compression.shrink_decay = ShrinkDecayStrategy::FailureBased(0.5);
    compression.time_limit = Duration::from_secs(30);
    compression.separator_config.iter_no_imprv_limit = 0;
    compression.separator_config.strike_limit = 1;
    compression.separator_config.n_workers = 1;
    compression
        .separator_config
        .sample_config
        .n_container_samples = 8;
    compression
        .separator_config
        .sample_config
        .n_focussed_samples = 0;
    compression.separator_config.sample_config.n_coord_descents = 1;

    let mut separator = Separator::new(
        problem,
        Xoshiro256PlusPlus::seed_from_u64(43),
        exploration.separator_config,
    );
    let mut terminator = PhaseTerminator::new();
    terminator.new_timeout(exploration.time_limit);
    let mut listener = RecordingListener {
        interrupt_on_exploration_infeasible: true,
        stop: Some(Arc::clone(&terminator.stop)),
        ..RecordingListener::default()
    };
    let feasible_solutions =
        exploration_phase(&mut separator, &mut listener, &terminator, &exploration);
    let best_solution = feasible_solutions
        .last()
        .expect("exploration returns its best feasible solution")
        .clone();
    let separator_state = separator.prob.save();
    assert!(terminator.kill(), "listener interruption stops exploration");
    assert!((best_solution.strip_width() - 8.0).abs() < 1e-6);
    assert!((separator_state.strip_width() - 6.5).abs() < 1e-6);
    assert_ne!(
        sorted_placements(&separator_state),
        sorted_placements(&best_solution),
        "disruption after the infeasible report leaves a distinct separator state",
    );

    terminator.new_timeout(compression.time_limit);
    listener.stop_after_compression_attempt = true;
    let mut compression_separator = Separator::new(
        separator.prob,
        Xoshiro256PlusPlus::seed_from_u64(42),
        compression.separator_config,
    );
    let final_solution = compression_phase(
        &mut compression_separator,
        &best_solution,
        &mut listener,
        &terminator,
        &compression,
    );

    let mut actual = Vec::new();
    append_solution_rows(&mut actual, "best", &best_solution);
    append_solution_rows(&mut actual, "separator", &separator_state);
    append_solution_rows(&mut actual, "final", &final_solution);
    for step in &listener.compression_steps {
        actual.push(format!("step {step:.9}"));
    }
    for report in &listener.reports {
        actual.push(format!("report {report}"));
    }
    for progress in &listener.separation_progress {
        actual.push(format!(
            "progress {} {:.9} {:.9} {:.9}",
            progress.iteration, progress.strip_width, progress.density, progress.min_loss,
        ));
    }
    for result in &listener.separation_results {
        actual.push(format!(
            "result {} {} {} {}",
            u8::from(result.success),
            result.total_evals,
            result.total_moves,
            result.iterations,
        ));
    }
    if std::env::var_os("SPARROW_DUMP_HANDOFF_REFERENCE").is_some() {
        for row in &actual {
            println!("{row}");
        }
    }
    let expected = REFERENCE
        .lines()
        .filter(|line| !line.is_empty() && !line.starts_with('#'))
        .map(str::to_owned)
        .collect::<Vec<_>>();
    assert_eq!(actual, expected, "Rust termination handoff trace changed");
}
