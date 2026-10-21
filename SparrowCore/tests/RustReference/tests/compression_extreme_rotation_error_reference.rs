use jagua_rs::Instant;
use jagua_rs::geometry::DTransformation;
use jagua_rs::io::import::Importer;
use jagua_rs::probs::spp::entities::{SPPlacement, SPProblem, SPSolution};
use jagua_rs::probs::spp::io::import_instance;
use rand::SeedableRng;
use rand::rngs::Xoshiro256PlusPlus;
use sparrow::config::{DEFAULT_SPARROW_CONFIG, ShrinkDecayStrategy};
use sparrow::optimizer::compress::compression_phase;
use sparrow::optimizer::optimize;
use sparrow::optimizer::separator::Separator;
use sparrow::sample::search::SampleConfig;
use sparrow::util::io::read_spp_input;
use sparrow::util::listener::{
    OptimizationPhase, ReportType, SeparationProgress, SeparationResult, SolutionListener,
};
use sparrow::util::terminator::Terminator;
use std::cell::Cell;
use std::panic::{AssertUnwindSafe, catch_unwind};
use std::path::Path;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::time::Duration;

struct StopAfterFirstCompressionAttempt(Cell<usize>);

impl Terminator for StopAfterFirstCompressionAttempt {
    fn kill(&self) -> bool {
        let call = self.0.get();
        self.0.set(call + 1);
        call >= 2
    }

    fn new_timeout(&mut self, _timeout: Duration) {}

    fn timeout_at(&self) -> Option<Instant> {
        None
    }
}

struct SkipExplorationRunCompression(AtomicUsize);

impl Terminator for SkipExplorationRunCompression {
    fn kill(&self) -> bool {
        self.0.load(Ordering::Relaxed) <= 1
    }

    fn new_timeout(&mut self, _timeout: Duration) {
        self.0.fetch_add(1, Ordering::Relaxed);
    }

    fn timeout_at(&self) -> Option<Instant> {
        None
    }
}

#[derive(Default)]
struct CompressionFailureListener {
    phases: Vec<OptimizationPhase>,
    reports: Vec<ReportType>,
    separation_progress_count: usize,
    compression_steps: Vec<f32>,
    separation_results: Vec<SeparationResult>,
}

impl SolutionListener for CompressionFailureListener {
    fn report(&mut self, report: ReportType, _solution: &SPSolution) {
        self.reports.push(report);
    }

    fn report_phase(&mut self, phase: OptimizationPhase) {
        self.phases.push(phase);
    }

    fn report_separation_progress(&mut self, _progress: SeparationProgress) {
        self.separation_progress_count += 1;
    }

    fn report_compression_progress(&mut self, step: f32) {
        self.compression_steps.push(step);
    }

    fn report_separation_result(&mut self, result: SeparationResult) {
        self.separation_results.push(result);
    }
}

fn assert_compression_panics_on_invalid_rotated_bbox(
    fixture: &str,
    expect_compression_attempt: bool,
) {
    let (external, _) =
        read_spp_input(Path::new(fixture)).expect("read shared rotation SPP fixture");
    let mut config = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        config.cde_config,
        config.poly_simpl_tolerance,
        config.narrow_concavity_cutoff_ratio,
    );
    let instance = import_instance(&importer, &external)
        .expect("the finite rotation fixture remains importer-accepted");
    let bbox = instance.item(0).shape_cd.bbox;
    assert_eq!(bbox.x_min, 403210.875_f32);
    assert_eq!(bbox.y_min, 98.50647_f32);
    assert_eq!(bbox.x_max, 403918.0_f32);
    assert_eq!(bbox.y_max, 805.6203_f32);

    let mut problem = SPProblem::new(instance).expect("create compression problem");
    problem
        .change_strip_width(2_000_000.0)
        .expect("set compression strip width");
    let placement = DTransformation::new(0.0, (500_000.0 - bbox.x_min, 1_000.0 - bbox.y_min));
    problem.place_item(SPPlacement {
        item_idx: 0,
        d_transf: placement,
    });
    problem.place_item(SPPlacement {
        item_idx: 0,
        d_transf: placement,
    });
    let initial_solution = problem.save();

    config.cmpr_cfg.shrink_range = (0.01, 0.01);
    config.cmpr_cfg.shrink_decay = ShrinkDecayStrategy::FailureBased(1.0);
    config.cmpr_cfg.time_limit = Duration::from_secs(10);
    config.cmpr_cfg.separator_config.n_workers = 1;
    config.cmpr_cfg.separator_config.strike_limit = 1;
    config.cmpr_cfg.separator_config.iter_no_imprv_limit = 1;
    config.cmpr_cfg.separator_config.sample_config = SampleConfig {
        n_container_samples: 1,
        n_focussed_samples: 1,
        n_coord_descents: 1,
    };

    let mut separator = Separator::new(
        problem,
        Xoshiro256PlusPlus::seed_from_u64(123),
        config.cmpr_cfg.separator_config,
    );
    let mut listener = CompressionFailureListener::default();
    let terminator = StopAfterFirstCompressionAttempt(Cell::new(0));
    let outcome = catch_unwind(AssertUnwindSafe(|| {
        compression_phase(
            &mut separator,
            &initial_solution,
            &mut listener,
            &terminator,
            &config.cmpr_cfg,
        )
    }));
    let panic = outcome
        .expect_err("Rust Compression must expose an invalid rotated bounding box as a panic");
    let panic_message = panic
        .downcast_ref::<String>()
        .map(String::as_str)
        .or_else(|| panic.downcast_ref::<&str>().copied())
        .unwrap_or("<non-string panic payload>");
    assert!(
        panic_message.contains("invalid rectangle"),
        "expected invalid-rectangle panic, got: {panic_message}"
    );
    if expect_compression_attempt {
        assert_eq!(listener.compression_steps, vec![0.01_f32]);
        assert_eq!(listener.separation_progress_count, 1);
    } else {
        assert!(listener.compression_steps.is_empty());
        assert_eq!(listener.separation_progress_count, 0);
    }
    assert!(listener.separation_results.is_empty());
}

#[test]
fn valid_extreme_rotation_reaches_compression_worker_and_panics_on_invalid_bbox() {
    assert_compression_panics_on_invalid_rotated_bbox(
        "../Fixtures/CompressionExtremeRotationWorkerError.json",
        true,
    );
}

#[test]
fn valid_discrete_rotation_panics_during_compression_minimum_width() {
    assert_compression_panics_on_invalid_rotated_bbox(
        "../Fixtures/CompressionDiscreteRotationWorkerError.json",
        false,
    );
}

#[test]
fn top_level_optimize_propagates_the_compression_worker_panic() {
    let (external, _) = read_spp_input(Path::new(
        "../Fixtures/CompressionExtremeRotationWorkerError.json",
    ))
    .expect("read shared extreme-rotation SPP fixture");
    let mut config = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        config.cde_config,
        config.poly_simpl_tolerance,
        config.narrow_concavity_cutoff_ratio,
    );
    let instance = import_instance(&importer, &external)
        .expect("the finite extreme-rotation fixture remains importer-accepted");

    let mut problem = SPProblem::new(instance.clone()).expect("create warm-start problem");
    problem
        .change_strip_width(2_000_000.0)
        .expect("set warm-start strip width");
    let bbox = instance.item(0).shape_cd.bbox;
    let placement = DTransformation::new(0.0, (500_000.0 - bbox.x_min, 1_000.0 - bbox.y_min));
    problem.place_item(SPPlacement {
        item_idx: 0,
        d_transf: placement,
    });
    problem.place_item(SPPlacement {
        item_idx: 0,
        d_transf: placement,
    });
    let warm_start = problem.save();

    config.expl_cfg.time_limit = Duration::ZERO;
    config.expl_cfg.separator_config.n_workers = 1;
    config.cmpr_cfg.shrink_range = (0.01, 0.01);
    config.cmpr_cfg.shrink_decay = ShrinkDecayStrategy::FailureBased(0.0);
    config.cmpr_cfg.time_limit = Duration::from_secs(10);
    config.cmpr_cfg.separator_config.n_workers = 1;
    config.cmpr_cfg.separator_config.iter_no_imprv_limit = 1;
    config.cmpr_cfg.separator_config.strike_limit = 1;
    config.cmpr_cfg.separator_config.sample_config = SampleConfig {
        n_container_samples: 1,
        n_focussed_samples: 1,
        n_coord_descents: 1,
    };

    let mut listener = CompressionFailureListener::default();
    let mut terminator = SkipExplorationRunCompression(AtomicUsize::new(0));
    let outcome = catch_unwind(AssertUnwindSafe(|| {
        optimize(
            instance,
            Xoshiro256PlusPlus::seed_from_u64(123),
            &mut listener,
            &mut terminator,
            &config.expl_cfg,
            &config.cmpr_cfg,
            Some(&warm_start),
        )
    }));
    let panic =
        outcome.expect_err("top-level Rust optimize must propagate the Compression worker panic");
    let panic_message = panic
        .downcast_ref::<String>()
        .map(String::as_str)
        .or_else(|| panic.downcast_ref::<&str>().copied())
        .unwrap_or("<non-string panic payload>");
    assert!(
        panic_message.contains("invalid rectangle"),
        "expected invalid-rectangle panic, got: {panic_message}"
    );
    assert_eq!(
        listener.phases,
        vec![
            OptimizationPhase::Exploration,
            OptimizationPhase::Compression
        ]
    );
    assert_eq!(listener.reports, vec![ReportType::ExplFeas]);
    assert_eq!(listener.compression_steps, vec![0.01_f32]);
    assert_eq!(listener.separation_progress_count, 1);
    assert!(listener.separation_results.is_empty());
}
