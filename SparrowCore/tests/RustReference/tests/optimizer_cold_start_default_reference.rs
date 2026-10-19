use jagua_rs::Instant;
use jagua_rs::entities::Layout;
use jagua_rs::io::import::Importer;
use jagua_rs::probs::spp::entities::{SPInstance, SPProblem, SPSolution};
use jagua_rs::probs::spp::io::import_instance;
use rand::SeedableRng;
use rand::rngs::Xoshiro256PlusPlus;
use sparrow::config::DEFAULT_SPARROW_CONFIG;
use sparrow::config::ShrinkDecayStrategy;
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

const REFERENCE: &str = include_str!("../../Fixtures/OptimizeColdStartDefaultRustReference.tsv");
const E5_TERMINAL_REFERENCE: &str =
    include_str!("../../Fixtures/OptimizeColdStartDefaultE5TerminalRustReference.tsv");

#[derive(Clone)]
struct PhaseTerminator {
    stop: Arc<AtomicBool>,
}

impl PhaseTerminator {
    fn new() -> Self {
        Self {
            stop: Arc::new(AtomicBool::new(false)),
        }
    }
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

#[test]
fn phase_terminator_ignores_wall_clock_deadlines() {
    let mut terminator = PhaseTerminator::new();
    terminator.new_timeout(Duration::ZERO);

    assert!(
        terminator.timeout_at().is_none(),
        "the reference trace must not inherit a wall-clock phase deadline"
    );
    assert!(
        !terminator.kill(),
        "an elapsed timeout must not stop the reference phase"
    );
}

#[derive(Default)]
struct RecordingListener {
    trace: Vec<String>,
    phase: Option<&'static str>,
    stop: Option<Arc<AtomicBool>>,
    stopped_exploration: bool,
    stopped_compression: bool,
    separation_progress_count: usize,
    first_separation_progress: Option<SeparationProgress>,
    last_separation_progress: Option<SeparationProgress>,
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
    placements.sort_by_key(|placement| placement.item.idx);
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

fn terminal_state_rows(seed: u64, instance: &SPInstance, solution: &SPSolution) -> Vec<String> {
    let mut problem = SPProblem::new(instance.clone()).expect("create terminal SPP problem");
    problem.restore(solution);
    let layout = Layout::from_snapshot(&solution.layout_snapshot);
    let remaining_demand = problem.item_demand_qtys.iter().sum::<usize>();
    let layout_feasible = layout.is_feasible();
    let solution_feasible = remaining_demand == 0 && layout_feasible;
    let final_loss = CollisionTracker::new(&layout).get_total_loss();

    let mut placed_indices = solution
        .layout_snapshot
        .placed_items
        .values()
        .map(|placement| placement.item.idx)
        .collect::<Vec<_>>();
    placed_indices.sort_unstable();
    let mut placed_row = String::from("placed_ids\tlist");
    for item_index in placed_indices {
        placed_row.push_str(&format!("\t{item_index}"));
    }

    let mut rows = vec![
        format!(
            "terminal_state\tstate\t{seed}\t{}\t{remaining_demand}\t{}\t{}\t{:.9}\t{:.9}\t{:.9}",
            problem.n_placed_items(),
            u8::from(solution_feasible),
            u8::from(layout_feasible),
            problem.strip_width(),
            problem.density(),
            final_loss,
        ),
        placed_row,
    ];
    let remaining_items = problem
        .item_demand_qtys
        .iter()
        .enumerate()
        .filter(|(_, quantity)| **quantity > 0)
        .collect::<Vec<_>>();
    if remaining_items.is_empty() {
        rows.push("remaining_item\tnone".to_owned());
    } else {
        rows.extend(
            remaining_items
                .into_iter()
                .map(|(item_index, quantity)| format!("remaining_item\t{item_index}\t{quantity}")),
        );
    }
    rows
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

    fn report_separation_result(&mut self, result: SeparationResult) {
        let phase = self
            .phase
            .expect("separator result follows a phase callback");
        let first = self
            .first_separation_progress
            .expect("separator reports initial progress before its result");
        let last = self
            .last_separation_progress
            .expect("separator reports progress before its result");
        self.trace.push(format!(
            "separation_progress_summary\t{phase}\t{}\t{}\t{:.9}\t{:.9}\t{:.9}\t{}\t{:.9}\t{:.9}\t{:.9}",
            self.separation_progress_count,
            first.iteration,
            first.min_loss,
            first.strip_width,
            first.density,
            last.iteration,
            last.min_loss,
            last.strip_width,
            last.density,
        ));
        self.separation_progress_count = 0;
        self.first_separation_progress = None;
        self.last_separation_progress = None;
        self.trace.push(format!(
            "separation\t{phase}\t{}\t{}\t{}\t{}",
            u8::from(result.success),
            result.total_evals,
            result.total_moves,
            result.iterations,
        ));
        let should_stop = match phase {
            "exploration" => !self.stopped_exploration,
            "compression" => !self.stopped_compression,
            _ => unreachable!(),
        };
        if should_stop {
            self.stop
                .as_ref()
                .expect("phase terminator handle")
                .store(true, Ordering::SeqCst);
            match phase {
                "exploration" => self.stopped_exploration = true,
                "compression" => self.stopped_compression = true,
                _ => unreachable!(),
            }
        }
    }

    fn report_separation_progress(&mut self, progress: SeparationProgress) {
        self.separation_progress_count += 1;
        self.first_separation_progress.get_or_insert(progress);
        self.last_separation_progress = Some(progress);
    }

    fn report_compression_progress(&mut self, step: f32) {
        assert!(step > 0.00001 && step <= 0.0005);
        self.trace
            .push(format!("compression_step\tvalue\t{step:.9}"));
    }
}

#[test]
fn optimize_cold_start_with_default_stage_configs_matches_reference() {
    let seed = 42;
    let config = DEFAULT_SPARROW_CONFIG;
    let (external_instance, _) =
        sparrow::util::io::read_spp_input(Path::new("../Fixtures/RustSppOptimizeColdStart.json"))
            .expect("read shared cold-start SPP fixture");
    let importer = Importer::new(
        config.cde_config,
        config.poly_simpl_tolerance,
        config.narrow_concavity_cutoff_ratio,
    );
    let instance = import_instance(&importer, &external_instance).expect("import SPP fixture");

    let mut terminator = PhaseTerminator::new();
    let mut listener = RecordingListener {
        stop: Some(Arc::clone(&terminator.stop)),
        ..RecordingListener::default()
    };
    let exploration = config.expl_cfg;
    let exploration_samples = exploration.separator_config.sample_config;
    listener.trace.push(format!(
        "config\texploration\t{:.9}\t{}\t{}\t{}\t{}\t{}\t{}\t{}",
        exploration.shrink_step,
        exploration.time_limit.as_secs(),
        exploration.separator_config.iter_no_imprv_limit,
        exploration.separator_config.strike_limit,
        exploration.separator_config.n_workers,
        exploration_samples.n_container_samples,
        exploration_samples.n_focussed_samples,
        exploration_samples.n_coord_descents,
    ));
    let compression = config.cmpr_cfg;
    let compression_samples = compression.separator_config.sample_config;
    assert!(matches!(
        compression.shrink_decay,
        ShrinkDecayStrategy::TimeBased
    ));
    listener.trace.push(format!(
        "config\tcompression_time_based\t{:.9}\t{:.9}\t{}\t{}\t{}\t{}\t{}\t{}\t{}",
        compression.shrink_range.0,
        compression.shrink_range.1,
        compression.time_limit.as_secs(),
        compression.separator_config.iter_no_imprv_limit,
        compression.separator_config.strike_limit,
        compression.separator_config.n_workers,
        compression_samples.n_container_samples,
        compression_samples.n_focussed_samples,
        compression_samples.n_coord_descents,
    ));
    let final_solution = optimize(
        instance.clone(),
        Xoshiro256PlusPlus::seed_from_u64(seed),
        &mut listener,
        &mut terminator,
        &config.expl_cfg,
        &config.cmpr_cfg,
        None,
    )
    .expect("run Rust cold-start path with default exploration and compression configs");

    assert_eq!(final_solution.layout_snapshot.placed_items.len(), 3);
    assert!(Layout::from_snapshot(&final_solution.layout_snapshot).is_feasible());
    assert!(listener.stopped_exploration);
    assert!(listener.stopped_compression);

    let terminal_rows = terminal_state_rows(seed, &instance, &final_solution);

    if std::env::var_os("SPARROW_PRINT_COLD_START_DEFAULT_REFERENCE").is_some() {
        for event in &listener.trace {
            println!("{event}");
        }
    }
    if std::env::var_os("SPARROW_PRINT_COLD_START_DEFAULT_E5_TERMINAL_REFERENCE").is_some() {
        for row in &terminal_rows {
            println!("{row}");
        }
    }
    let expected = REFERENCE
        .lines()
        .filter(|line| !line.is_empty() && !line.starts_with('#'))
        .collect::<Vec<_>>();
    let actual = listener
        .trace
        .iter()
        .map(String::as_str)
        .collect::<Vec<_>>();
    assert_eq!(actual, expected, "Rust default cold-start trace changed");

    let expected_terminal = E5_TERMINAL_REFERENCE
        .lines()
        .filter(|line| !line.is_empty() && !line.starts_with('#'))
        .collect::<Vec<_>>();
    let actual_terminal = terminal_rows.iter().map(String::as_str).collect::<Vec<_>>();
    assert_eq!(
        actual_terminal, expected_terminal,
        "Rust default cold-start E5 terminal state changed"
    );
}
