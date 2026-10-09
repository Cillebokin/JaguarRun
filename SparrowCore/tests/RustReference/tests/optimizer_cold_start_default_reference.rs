use jagua_rs::Instant;
use jagua_rs::entities::Layout;
use jagua_rs::io::import::Importer;
use jagua_rs::probs::spp::entities::SPSolution;
use jagua_rs::probs::spp::io::import_instance;
use rand::SeedableRng;
use rand::rngs::Xoshiro256PlusPlus;
use sparrow::config::DEFAULT_SPARROW_CONFIG;
use sparrow::config::ShrinkDecayStrategy;
use sparrow::optimizer::optimize;
use sparrow::util::listener::{OptimizationPhase, ReportType, SeparationResult, SolutionListener};
use sparrow::util::terminator::Terminator;
use std::path::Path;
use std::sync::Arc;
use std::sync::atomic::{AtomicBool, Ordering};
use std::time::Duration;

const REFERENCE: &str = include_str!("../../Fixtures/OptimizeColdStartDefaultRustReference.tsv");

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
    trace: Vec<String>,
    phase: Option<&'static str>,
    stop: Option<Arc<AtomicBool>>,
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

    fn report_compression_progress(&mut self, step: f32) {
        assert!(step > 0.00001 && step <= 0.0005);
        self.trace
            .push("compression_step\tdefault_range".to_owned());
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
        instance,
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

    if std::env::var_os("SPARROW_PRINT_COLD_START_DEFAULT_REFERENCE").is_some() {
        for event in &listener.trace {
            println!("{event}");
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
}
