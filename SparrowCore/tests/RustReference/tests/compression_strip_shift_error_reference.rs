use jagua_rs::Instant;
use jagua_rs::geometry::DTransformation;
use jagua_rs::io::import::Importer;
use jagua_rs::probs::spp::entities::{SPPlacement, SPProblem, SPSolution};
use jagua_rs::probs::spp::io::import_instance;
use rand::rngs::Xoshiro256PlusPlus;
use rand::{Rng, RngExt, SeedableRng};
use sparrow::config::{DEFAULT_SPARROW_CONFIG, ShrinkDecayStrategy};
use sparrow::optimizer::optimize;
use sparrow::util::io::read_spp_input;
use sparrow::util::listener::{
    OptimizationPhase, ReportType, SeparationProgress, SeparationResult, SolutionListener,
};
use sparrow::util::terminator::Terminator;
use std::panic::{AssertUnwindSafe, catch_unwind};
use std::path::Path;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::time::Duration;

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

#[test]
fn top_level_optimize_characterizes_strip_shift_out_of_container_behavior() {
    let (external, _) = read_spp_input(Path::new(
        "../Fixtures/CompressionStripShiftTransformError.json",
    ))
    .expect("read shared strip-shift fixture");
    let mut config = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        config.cde_config,
        config.poly_simpl_tolerance,
        config.narrow_concavity_cutoff_ratio,
    );
    let instance = import_instance(&importer, &external)
        .expect("finite strip-shift fixture remains importer-accepted");

    let mut problem = SPProblem::new(instance.clone()).expect("create warm-start problem");
    let strip_width = 1_152_921_504_606_846_976.0_f32;
    problem
        .change_strip_width(strip_width)
        .expect("set warm-start strip width");
    problem.place_item(SPPlacement {
        item_idx: 0,
        d_transf: DTransformation::new(0.0, (144_115_188_075_855_872.0_f32, 0.0)),
    });
    let warm_start = problem.save();
    let initial_centroid = problem
        .layout
        .placed_items
        .values()
        .next()
        .expect("warm-start item exists")
        .shape
        .centroid()
        .0;
    eprintln!("S07 strip-shift geometry: initial_centroid_x={initial_centroid:.9}");
    assert!(initial_centroid.is_nan());

    config.expl_cfg.time_limit = Duration::ZERO;
    config.expl_cfg.separator_config.n_workers = 1;
    config.cmpr_cfg.shrink_range = (0.875, 0.875);
    config.cmpr_cfg.shrink_decay = ShrinkDecayStrategy::FailureBased(0.0);
    config.cmpr_cfg.time_limit = Duration::from_secs(10);
    config.cmpr_cfg.separator_config.n_workers = 1;

    let mut top_level_rng = Xoshiro256PlusPlus::seed_from_u64(4);
    let exploration_seed = top_level_rng.next_u64();
    let compression_seed = top_level_rng.next_u64();
    let mut compression_rng = Xoshiro256PlusPlus::seed_from_u64(compression_seed);
    let initial_worker_seed: u64 = compression_rng.random();
    let reset_worker_seed: u64 = compression_rng.random();
    let mut split_probe = compression_rng.clone();
    let split_raw_u32 = split_probe.next_u32();
    let split_position = compression_rng.random_range(0.0_f32..strip_width);
    eprintln!(
        "S07 strip-shift RNG: exploration_seed={exploration_seed} compression_seed={compression_seed} initial_worker_seed={initial_worker_seed} reset_worker_seed={reset_worker_seed} split_raw_u32={split_raw_u32} split={split_position:.9} fraction={:.9}",
        split_position / strip_width
    );
    assert_eq!(exploration_seed, 12_535_900_630_535_485_233);
    assert_eq!(compression_seed, 11_181_963_833_181_290_292);
    assert_eq!(initial_worker_seed, 15_267_030_695_124_774_896);
    assert_eq!(reset_worker_seed, 4_245_659_235_216_480_335);
    assert_eq!(split_raw_u32, 224_259_143);

    let mut listener = CompressionFailureListener::default();
    let mut terminator = SkipExplorationRunCompression(AtomicUsize::new(0));
    let outcome = catch_unwind(AssertUnwindSafe(|| {
        optimize(
            instance,
            Xoshiro256PlusPlus::seed_from_u64(4),
            &mut listener,
            &mut terminator,
            &config.expl_cfg,
            &config.cmpr_cfg,
            Some(&warm_start),
        )
    }));
    assert_eq!(
        listener.phases,
        vec![
            OptimizationPhase::Exploration,
            OptimizationPhase::Compression
        ]
    );
    if cfg!(debug_assertions) {
        let panic =
            outcome.expect_err("Rust Debug must preserve the quadtree partial-hazard assertion");
        let panic_message = panic
            .downcast_ref::<String>()
            .map(String::as_str)
            .or_else(|| panic.downcast_ref::<&str>().copied())
            .unwrap_or("<non-string panic payload>");
        assert!(
            panic_message.contains("constricted_hazards"),
            "expected quadtree partial-hazard assertion, got: {panic_message}"
        );
        assert_eq!(
            listener.reports,
            vec![ReportType::ExplFeas, ReportType::CmprFeas]
        );
        assert_eq!(listener.compression_steps, vec![0.875_f32; 2]);
        assert_eq!(listener.separation_progress_count, 2);
        assert_eq!(listener.separation_results.len(), 1);
    } else {
        let final_solution = outcome
            .expect("Rust Release must not run Debug-only quadtree assertions")
            .expect("Rust Release optimize should return its final solution");
        assert_eq!(listener.reports.first(), Some(&ReportType::ExplFeas));
        assert_eq!(listener.reports.last(), Some(&ReportType::Final));
        assert_eq!(
            listener.reports[1..listener.reports.len() - 1],
            vec![ReportType::CmprFeas; 8]
        );
        assert_eq!(listener.compression_steps, vec![0.875_f32; 8]);
        assert_eq!(listener.separation_progress_count, 16);
        assert_eq!(listener.separation_results.len(), 8);
        assert_eq!(final_solution.strip_width(), 68_719_476_736.0_f32);
    }
}
