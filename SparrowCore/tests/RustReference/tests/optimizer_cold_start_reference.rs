use jagua_rs::io::import::Importer;
use jagua_rs::probs::spp::io::import_instance;
use rand::SeedableRng;
use rand::rngs::Xoshiro256PlusPlus;
use sparrow::config::{DEFAULT_SPARROW_CONFIG, ShrinkDecayStrategy};
use sparrow::optimizer::optimize;
use sparrow::sample::search::SampleConfig;
use sparrow::util::listener::{OptimizationPhase, ReportType, SolutionListener};
use sparrow::util::terminator::BasicTerminator;
use std::path::Path;

const REFERENCE: &str = include_str!("../../Fixtures/OptimizeColdStartRustReference.tsv");

#[derive(Default)]
struct RecordingListener {
    trace: Vec<String>,
}

impl SolutionListener for RecordingListener {
    fn report(
        &mut self,
        report: ReportType,
        solution: &jagua_rs::probs::spp::entities::SPSolution,
    ) {
        let report = match report {
            ReportType::ExplFeas => "expl_feas",
            ReportType::ExplInfeas => "expl_infeas",
            ReportType::ExplImproving => "expl_improving",
            ReportType::CmprFeas => "cmpr_feas",
            ReportType::Final => "final",
        };
        let mut placements = solution
            .layout_snapshot
            .placed_items
            .values()
            .collect::<Vec<_>>();
        placements.sort_by_key(|placement| placement.item.idx);
        let placement_count = placements.len();
        self.trace.push(format!(
            "report\t{report}\t{placement_count}\t{:.9}\t{:.9}",
            solution.strip_width(),
            solution.density(),
        ));
        for placement in placements {
            let (x, y) = placement.d_transf.translation();
            self.trace.push(format!(
                "placement\t{report}\t{}\t{:.9}\t{:.9}\t{:.9}",
                placement.item.idx,
                placement.d_transf.rotation(),
                x,
                y,
            ));
        }
    }

    fn report_phase(&mut self, phase: OptimizationPhase) {
        self.trace.push(format!(
            "phase\t{}",
            match phase {
                OptimizationPhase::Exploration => "exploration",
                OptimizationPhase::Compression => "compression",
            }
        ));
    }

    fn report_compression_progress(&mut self, step: f32) {
        self.trace.push(format!("step\t{step:.9}"));
    }
}

#[test]
fn optimize_cold_start_matches_the_checked_in_rust_reference() {
    let seed = 42;
    let (external_instance, _) =
        sparrow::util::io::read_spp_input(Path::new("../Fixtures/RustSppOptimizeColdStart.json"))
            .expect("read shared cold-start SPP fixture");
    let mut config = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        config.cde_config,
        config.poly_simpl_tolerance,
        config.narrow_concavity_cutoff_ratio,
    );
    let instance = import_instance(&importer, &external_instance).expect("import SPP fixture");

    // Keep the default LBF constructor budget (1000/0/3); bound only the later
    // phases so this cold-start reference is deterministic and quick to replay.
    config.expl_cfg.shrink_step = 0.2;
    config.expl_cfg.time_limit = std::time::Duration::from_secs(30);
    config.expl_cfg.max_conseq_failed_attempts = Some(1);
    config.expl_cfg.separator_config.iter_no_imprv_limit = 0;
    config.expl_cfg.separator_config.strike_limit = 1;
    config.expl_cfg.separator_config.n_workers = 1;
    config.expl_cfg.separator_config.sample_config = SampleConfig {
        n_container_samples: 8,
        n_focussed_samples: 0,
        n_coord_descents: 1,
    };
    config.cmpr_cfg.shrink_range = (0.01, 0.01);
    config.cmpr_cfg.shrink_decay = ShrinkDecayStrategy::FailureBased(0.5);
    config.cmpr_cfg.time_limit = std::time::Duration::from_secs(30);
    config.cmpr_cfg.separator_config.iter_no_imprv_limit = 0;
    config.cmpr_cfg.separator_config.strike_limit = 1;
    config.cmpr_cfg.separator_config.n_workers = 1;
    config.cmpr_cfg.separator_config.sample_config = SampleConfig {
        n_container_samples: 8,
        n_focussed_samples: 0,
        n_coord_descents: 1,
    };

    let mut listener = RecordingListener::default();
    let mut terminator = BasicTerminator::new();
    let final_solution = optimize(
        instance,
        Xoshiro256PlusPlus::seed_from_u64(seed),
        &mut listener,
        &mut terminator,
        &config.expl_cfg,
        &config.cmpr_cfg,
        None,
    )
    .expect("run Rust optimize cold-start path");
    assert_eq!(final_solution.layout_snapshot.placed_items.len(), 3);
    assert!(final_solution.density().is_finite());

    if std::env::var_os("SPARROW_PRINT_COLD_START_REFERENCE").is_some() {
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
    assert_eq!(actual, expected, "Rust cold-start trace changed");
}
