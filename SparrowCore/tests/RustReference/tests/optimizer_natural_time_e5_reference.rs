use jagua_rs::entities::Layout;
use jagua_rs::geometry::DTransformation;
use jagua_rs::io::import::Importer;
use jagua_rs::probs::spp::entities::{SPPlacement, SPProblem, SPSolution};
use jagua_rs::probs::spp::io::import_instance;
use rand::SeedableRng;
use rand::rngs::Xoshiro256PlusPlus;
use sparrow::config::{DEFAULT_SPARROW_CONFIG, SparrowConfig};
use sparrow::optimizer::optimize;
use sparrow::util::listener::{
    OptimizationPhase, ReportType, SeparationProgress, SeparationResult, SolutionListener,
};
use sparrow::util::terminator::BasicTerminator;
use std::path::Path;
use std::time::{Duration, Instant};

#[derive(Default)]
struct NaturalTimeListener {
    phases: Vec<OptimizationPhase>,
    current_phase: Option<OptimizationPhase>,
    exploration_started: Option<Instant>,
    compression_started: Option<Instant>,
    exploration_elapsed: Option<Duration>,
    separation_results_by_phase: [usize; 2],
    solution_reports: usize,
    separation_progress_count: usize,
    separation_result_count: usize,
    compression_progress_count: usize,
    final_reports: usize,
    final_report_feasible: bool,
    final_width: Option<f32>,
    final_density: Option<f32>,
    compression_steps_valid: bool,
}

impl NaturalTimeListener {
    fn new() -> Self {
        Self {
            compression_steps_valid: true,
            ..Self::default()
        }
    }

    fn current_phase_index(&self) -> Option<usize> {
        match self.current_phase {
            Some(OptimizationPhase::Exploration) => Some(0),
            Some(OptimizationPhase::Compression) => Some(1),
            None => None,
        }
    }
}

impl SolutionListener for NaturalTimeListener {
    fn report(&mut self, report: ReportType, solution: &SPSolution) {
        self.solution_reports += 1;
        if report == ReportType::Final {
            self.final_reports += 1;
            self.final_report_feasible =
                Layout::from_snapshot(&solution.layout_snapshot).is_feasible();
            self.final_width = Some(solution.strip_width());
            self.final_density = Some(solution.density());
        }
    }

    fn report_phase(&mut self, phase: OptimizationPhase) {
        let now = Instant::now();
        self.phases.push(phase);
        if phase == OptimizationPhase::Compression {
            self.exploration_elapsed = self
                .exploration_started
                .map(|start| now.duration_since(start));
            self.compression_started = Some(now);
        } else {
            self.exploration_started = Some(now);
        }
        self.current_phase = Some(phase);
    }

    fn report_separation_progress(&mut self, _progress: SeparationProgress) {
        self.separation_progress_count += 1;
    }

    fn report_separation_result(&mut self, _result: SeparationResult) {
        self.separation_result_count += 1;
        if let Some(index) = self.current_phase_index() {
            self.separation_results_by_phase[index] += 1;
        }
    }

    fn report_compression_progress(&mut self, shrink_step: f32) {
        self.compression_progress_count += 1;
        self.compression_steps_valid = self.compression_steps_valid
            && shrink_step.is_finite()
            && (0.00001..=0.0005).contains(&shrink_step);
    }
}

fn within_deadline_window(elapsed: Duration, budget: Duration) -> bool {
    elapsed >= budget.saturating_sub(Duration::from_secs(2))
        && elapsed <= budget + Duration::from_secs(120)
}

#[test]
#[ignore = "real default E5 deadlines take about ten minutes"]
fn optimize_expires_default_natural_time_budgets_and_returns_valid_state() {
    let seed = 42;
    let (external_instance, _) =
        sparrow::util::io::read_spp_input(Path::new("../Fixtures/RustSppCompressWorkerMove.json"))
            .expect("read E5 natural-time fixture");
    let config: SparrowConfig = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        config.cde_config,
        config.poly_simpl_tolerance,
        config.narrow_concavity_cutoff_ratio,
    );
    let instance =
        import_instance(&importer, &external_instance).expect("import E5 natural-time fixture");
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
        d_transf: DTransformation::new(0.0, (3.15, 0.55)),
    });
    let warm_start = warm_problem.save();
    assert!(Layout::from_snapshot(&warm_start.layout_snapshot).is_feasible());

    assert_eq!(config.expl_cfg.time_limit, Duration::from_secs(9 * 60));
    assert_eq!(config.cmpr_cfg.time_limit, Duration::from_secs(60));
    assert_eq!(config.expl_cfg.separator_config.n_workers, 3);
    assert_eq!(config.cmpr_cfg.separator_config.n_workers, 3);
    assert!(config.expl_cfg.max_conseq_failed_attempts.is_none());

    let mut terminator = BasicTerminator::new();
    let mut listener = NaturalTimeListener::new();
    let run_started = Instant::now();
    let final_solution = optimize(
        instance,
        Xoshiro256PlusPlus::seed_from_u64(seed),
        &mut listener,
        &mut terminator,
        &config.expl_cfg,
        &config.cmpr_cfg,
        Some(&warm_start),
    )
    .expect("natural-time optimize must return a solution");
    let run_elapsed = run_started.elapsed();
    let compression_elapsed = listener
        .compression_started
        .map(|started| started.elapsed());

    assert_eq!(
        listener.phases,
        vec![
            OptimizationPhase::Exploration,
            OptimizationPhase::Compression
        ]
    );
    assert!(within_deadline_window(
        listener
            .exploration_elapsed
            .expect("exploration phase duration"),
        config.expl_cfg.time_limit,
    ));
    assert!(within_deadline_window(
        compression_elapsed.expect("compression phase duration"),
        config.cmpr_cfg.time_limit,
    ));
    assert!(
        listener
            .separation_results_by_phase
            .iter()
            .all(|count| *count > 0)
    );
    assert!(listener.compression_progress_count > 0);
    assert!(listener.final_reports == 1 && listener.final_report_feasible);
    assert!(listener.compression_steps_valid);
    assert!(Layout::from_snapshot(&final_solution.layout_snapshot).is_feasible());
    assert_eq!(final_solution.layout_snapshot.placed_items.len(), 2);
    assert!(final_solution.strip_width().is_finite());
    assert!(final_solution.density().is_finite());
    assert_eq!(listener.final_width, Some(final_solution.strip_width()));
    assert_eq!(listener.final_density, Some(final_solution.density()));

    eprintln!(
        "E5 natural-time: explore={}ms/540000ms, compression={}ms/60000ms, run={}ms, separation_results={:?}, reports={}, progress={}, compression_steps={}, feasible=true",
        listener.exploration_elapsed.unwrap().as_millis(),
        compression_elapsed.unwrap().as_millis(),
        run_elapsed.as_millis(),
        listener.separation_results_by_phase,
        listener.solution_reports,
        listener.separation_progress_count,
        listener.compression_progress_count,
    );
}
