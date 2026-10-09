use jagua_rs::geometry::DTransformation;
use jagua_rs::io::import::Importer;
use jagua_rs::probs::spp::entities::{SPPlacement, SPProblem};
use jagua_rs::probs::spp::io::import_instance;
use rand::SeedableRng;
use rand::rngs::Xoshiro256PlusPlus;
use sparrow::config::{DEFAULT_SPARROW_CONFIG, ShrinkDecayStrategy};
use sparrow::optimizer::optimize;
use sparrow::sample::search::SampleConfig;
use sparrow::util::listener::{OptimizationPhase, ReportType, SolutionListener};
use sparrow::util::terminator::BasicTerminator;
use std::path::Path;

const REFERENCE: &str = include_str!("../../Fixtures/OptimizeCompressRustReference.tsv");

#[derive(Default)]
struct RecordingListener {
    phases: Vec<&'static str>,
    reports: Vec<&'static str>,
    compression_steps: Vec<f32>,
}

impl SolutionListener for RecordingListener {
    fn report(
        &mut self,
        report: ReportType,
        _solution: &jagua_rs::probs::spp::entities::SPSolution,
    ) {
        self.reports.push(match report {
            ReportType::ExplFeas => "expl_feas",
            ReportType::ExplInfeas => "expl_infeas",
            ReportType::ExplImproving => "expl_improving",
            ReportType::CmprFeas => "cmpr_feas",
            ReportType::Final => "final",
        });
    }

    fn report_phase(&mut self, phase: OptimizationPhase) {
        self.phases.push(match phase {
            OptimizationPhase::Exploration => "exploration",
            OptimizationPhase::Compression => "compression",
        });
    }

    fn report_compression_progress(&mut self, step: f32) {
        self.compression_steps.push(step);
    }
}

#[test]
fn optimize_warm_start_exploration_and_compression_match_the_checked_in_reference() {
    let seed = 3;
    let (external_instance, _) = sparrow::util::io::read_spp_input(Path::new(
        "../Fixtures/RustSppOptimizeCompressSingleItem.json",
    ))
    .expect("read shared SPP fixture");
    let mut config = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        config.cde_config,
        config.poly_simpl_tolerance,
        config.narrow_concavity_cutoff_ratio,
    );
    let instance = import_instance(&importer, &external_instance).expect("import SPP fixture");
    let item_demand = instance
        .items
        .iter()
        .map(|(_, quantity)| *quantity)
        .sum::<usize>();

    let mut warm_problem = SPProblem::new(instance.clone()).expect("create warm-start problem");
    warm_problem
        .change_strip_width(4.0)
        .expect("set warm-start strip width");
    warm_problem.place_item(SPPlacement {
        item_idx: 0,
        d_transf: DTransformation::new(0.0, (1.003, 1.01)),
    });
    let warm_start = warm_problem.save();

    config.expl_cfg.shrink_step = 0.2;
    config.expl_cfg.time_limit = std::time::Duration::from_secs(30);
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
        Some(&warm_start),
    )
    .expect("run Rust optimize warm-start path");

    let mut placements = final_solution
        .layout_snapshot
        .placed_items
        .values()
        .collect::<Vec<_>>();
    placements.sort_by_key(|placement| placement.item.idx);
    let placement = placements.first().expect("final solution must place item");
    let (x, y) = placement.d_transf.translation();
    let remaining = item_demand.saturating_sub(final_solution.layout_snapshot.placed_items.len());
    println!(
        "Rust optimize reference: seed={} placed={} remaining={} width={:.9} density={:.9} rotation={:.9} x={:.9} y={:.9} phases={:?} reports={:?} compression_steps={:?}",
        seed,
        placements.len(),
        remaining,
        final_solution.strip_width(),
        final_solution.density(),
        placement.d_transf.rotation(),
        x,
        y,
        listener.phases,
        listener.reports,
        listener.compression_steps,
    );

    let row = REFERENCE
        .lines()
        .find(|line| !line.is_empty() && !line.starts_with('#'))
        .expect("Rust optimize reference row");
    let fields = row.split_whitespace().collect::<Vec<_>>();
    assert_eq!(fields.len(), 12);
    assert_eq!(fields[0], "optimize_compress_single_warm");
    assert_eq!(fields[1].parse::<u64>().unwrap(), seed);
    assert_eq!(placements.len(), fields[2].parse::<usize>().unwrap());
    assert_eq!(remaining, fields[3].parse::<usize>().unwrap());
    assert_eq!(listener.phases, ["exploration", "compression"]);
    let expected_reports = fields[10].split(',').collect::<Vec<_>>();
    assert_eq!(listener.reports, expected_reports);
    assert_eq!(
        listener.compression_steps.len(),
        fields[11].parse::<usize>().unwrap()
    );
    assert!(
        listener
            .compression_steps
            .iter()
            .all(|step| (*step - 0.01).abs() < 1e-7)
    );
    assert!((final_solution.strip_width() - fields[4].parse::<f32>().unwrap()).abs() < 5e-7);
    assert!((final_solution.density() - fields[5].parse::<f32>().unwrap()).abs() < 5e-7);
    assert!((placement.d_transf.rotation() - fields[6].parse::<f32>().unwrap()).abs() < 5e-7);
    assert!((x - fields[7].parse::<f32>().unwrap()).abs() < 5e-6);
    assert!((y - fields[8].parse::<f32>().unwrap()).abs() < 5e-6);
    assert_eq!(fields[9].split(',').collect::<Vec<_>>(), listener.phases);
}
