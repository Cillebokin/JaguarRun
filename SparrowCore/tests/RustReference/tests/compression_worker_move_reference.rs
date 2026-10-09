use jagua_rs::entities::Layout;
use jagua_rs::geometry::DTransformation;
use jagua_rs::io::import::Importer;
use jagua_rs::probs::spp::entities::{SPPlacement, SPProblem};
use jagua_rs::probs::spp::io::import_instance;
use rand::SeedableRng;
use rand::rngs::Xoshiro256PlusPlus;
use sparrow::config::{DEFAULT_SPARROW_CONFIG, ShrinkDecayStrategy};
use sparrow::optimizer::compress::compression_phase;
use sparrow::optimizer::separator::Separator;
use sparrow::util::listener::{ReportType, SeparationProgress, SeparationResult, SolutionListener};
use sparrow::util::terminator::BasicTerminator;
use std::path::Path;

const REFERENCE: &str = include_str!("../../Fixtures/CompressionWorkerMoveRustReference.tsv");
const MULTI_WORKER_REFERENCE: &str =
    include_str!("../../Fixtures/CompressionMultiWorkerMoveRustReference.tsv");

#[derive(Debug)]
enum TraceEvent {
    Progress(SeparationProgress),
    Result(SeparationResult),
}

struct ExpectedProgress {
    iteration: usize,
    width: f32,
    density: f32,
    minimum_loss: f32,
}

struct ExpectedResult {
    success: bool,
    evaluations: usize,
    moves: usize,
    iterations: usize,
}

struct ExpectedReportState {
    total_loss: f32,
    positions: Vec<(f32, f32)>,
}

#[derive(Default)]
struct RecordingListener {
    compression_steps: Vec<f32>,
    reports: Vec<&'static str>,
    report_positions: Vec<Vec<(f32, f32)>>,
    report_losses: Vec<f32>,
    separation_events: Vec<TraceEvent>,
}

impl SolutionListener for RecordingListener {
    fn report(
        &mut self,
        report: ReportType,
        solution: &jagua_rs::probs::spp::entities::SPSolution,
    ) {
        self.reports.push(match report {
            ReportType::ExplFeas => "expl_feas",
            ReportType::ExplInfeas => "expl_infeas",
            ReportType::ExplImproving => "expl_improving",
            ReportType::CmprFeas => "cmpr_feas",
            ReportType::Final => "final",
        });
        let mut positions = solution
            .layout_snapshot
            .placed_items
            .values()
            .map(|placement| placement.d_transf.translation())
            .collect::<Vec<_>>();
        positions.sort_by(|left, right| left.0.total_cmp(&right.0));
        self.report_positions.push(positions);
        let layout = Layout::from_snapshot(&solution.layout_snapshot);
        self.report_losses
            .push(sparrow::quantify::tracker::CollisionTracker::new(&layout).get_total_loss());
    }

    fn report_separation_progress(&mut self, progress: SeparationProgress) {
        self.separation_events.push(TraceEvent::Progress(progress));
    }

    fn report_separation_result(&mut self, result: SeparationResult) {
        self.separation_events.push(TraceEvent::Result(result));
    }

    fn report_compression_progress(&mut self, step: f32) {
        self.compression_steps.push(step);
    }
}

fn run_compression_reference(reference: &str, expected_case: &str, worker_count: usize) {
    let mut case = None;
    let mut expected_steps = Vec::new();
    let mut expected_reports = Vec::new();
    let mut expected_progress = Vec::new();
    let mut expected_results = Vec::new();
    let mut expected_placements = Vec::new();
    let mut expected_report_states = Vec::new();
    let mut expected_event_order = Vec::new();
    for line in reference
        .lines()
        .filter(|line| !line.is_empty() && !line.starts_with('#'))
    {
        let mut fields = line.split_whitespace();
        match fields.next().expect("reference row kind") {
            "case" => {
                assert!(case.is_none(), "reference must contain exactly one case");
                let name = fields.next().unwrap();
                let seed = fields.next().unwrap().parse::<u64>().unwrap();
                let width = fields.next().unwrap().parse::<f32>().unwrap();
                let density = fields.next().unwrap().parse::<f32>().unwrap();
                let placed_count = fields.next().unwrap().parse::<usize>().unwrap();
                let step_count = fields.next().unwrap().parse::<usize>().unwrap();
                let feasible = fields.next().unwrap().parse::<u8>().unwrap() != 0;
                let reports = fields
                    .next()
                    .unwrap()
                    .split(',')
                    .map(str::to_owned)
                    .collect::<Vec<_>>();
                case = Some((
                    name.to_owned(),
                    seed,
                    width,
                    density,
                    placed_count,
                    step_count,
                    feasible,
                ));
                expected_reports = reports;
            }
            "step" => expected_steps.push(fields.next().unwrap().parse::<f32>().unwrap()),
            "report_state" => {
                let _index = fields.next().unwrap().parse::<usize>().unwrap();
                let total_loss = fields.next().unwrap().parse::<f32>().unwrap();
                let mut positions = Vec::new();
                while let Some(x) = fields.next() {
                    let y = fields.next().expect("report state y coordinate");
                    positions.push((x.parse().unwrap(), y.parse().unwrap()));
                }
                expected_report_states.push(ExpectedReportState {
                    total_loss,
                    positions,
                });
            }
            "report" => expected_reports.push(fields.next().unwrap().to_owned()),
            "progress" => {
                expected_progress.push(ExpectedProgress {
                    iteration: fields.next().unwrap().parse().unwrap(),
                    width: fields.next().unwrap().parse().unwrap(),
                    density: fields.next().unwrap().parse().unwrap(),
                    minimum_loss: fields.next().unwrap().parse().unwrap(),
                });
                expected_event_order.push("progress");
            }
            "result" => {
                expected_results.push(ExpectedResult {
                    success: fields.next().unwrap().parse::<u8>().unwrap() != 0,
                    evaluations: fields.next().unwrap().parse().unwrap(),
                    moves: fields.next().unwrap().parse().unwrap(),
                    iterations: fields.next().unwrap().parse().unwrap(),
                });
                expected_event_order.push("result");
            }
            "placement" => expected_placements.push((
                fields.next().unwrap().parse::<usize>().unwrap(),
                fields.next().unwrap().parse::<usize>().unwrap(),
                fields.next().unwrap().parse::<f32>().unwrap(),
                fields.next().unwrap().parse::<f32>().unwrap(),
                fields.next().unwrap().parse::<f32>().unwrap(),
            )),
            kind => panic!("unknown reference row kind: {kind}"),
        }
    }
    let (
        case_name,
        seed,
        expected_width,
        expected_density,
        placed_count,
        step_count,
        expected_feasible,
    ) = case.expect("Rust compression reference case");
    assert_eq!(case_name, expected_case);

    let (external_instance, _) =
        sparrow::util::io::read_spp_input(Path::new("../Fixtures/RustSppCompressWorkerMove.json"))
            .expect("read shared SPP fixture");
    let mut config = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        config.cde_config,
        config.poly_simpl_tolerance,
        config.narrow_concavity_cutoff_ratio,
    );
    let instance = import_instance(&importer, &external_instance).expect("import SPP fixture");

    let mut problem = SPProblem::new(instance).expect("create compression problem");
    problem
        .change_strip_width(4.2)
        .expect("set initial strip width");
    problem.place_item(SPPlacement {
        item_idx: 0,
        d_transf: DTransformation::new(0.0, (1.05, 0.55)),
    });
    problem.place_item(SPPlacement {
        item_idx: 0,
        d_transf: DTransformation::new(0.0, (3.15, 0.55)),
    });
    let initial_solution = problem.save();
    let initial_layout = Layout::from_snapshot(&initial_solution.layout_snapshot);
    assert!(initial_layout.is_feasible(), "warm start must be feasible");

    config.cmpr_cfg.shrink_range = (0.2, 0.1);
    config.cmpr_cfg.shrink_decay = ShrinkDecayStrategy::FailureBased(0.5);
    config.cmpr_cfg.time_limit = std::time::Duration::from_secs(30);
    config.cmpr_cfg.separator_config.iter_no_imprv_limit = 1;
    config.cmpr_cfg.separator_config.strike_limit = 1;
    config.cmpr_cfg.separator_config.n_workers = worker_count;
    config
        .cmpr_cfg
        .separator_config
        .sample_config
        .n_container_samples = 8;
    config
        .cmpr_cfg
        .separator_config
        .sample_config
        .n_focussed_samples = 0;
    config
        .cmpr_cfg
        .separator_config
        .sample_config
        .n_coord_descents = 1;

    let mut separator = Separator::new(
        problem,
        Xoshiro256PlusPlus::seed_from_u64(seed),
        config.cmpr_cfg.separator_config,
    );
    let mut listener = RecordingListener::default();
    let terminator = BasicTerminator::new();
    let final_solution = compression_phase(
        &mut separator,
        &initial_solution,
        &mut listener,
        &terminator,
        &config.cmpr_cfg,
    );

    println!(
        "Rust compression worker-move reference: seed={seed} width={:.9} density={:.9} steps={:?} reports={:?}",
        final_solution.strip_width(),
        final_solution.density(),
        listener.compression_steps,
        listener.reports,
    );
    let mut placements = final_solution
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
    assert_eq!(listener.compression_steps.len(), step_count);
    assert!(
        listener
            .compression_steps
            .iter()
            .zip(expected_steps.iter())
            .all(|(actual, expected)| (*actual - expected).abs() < 1e-7)
    );
    assert_eq!(
        listener.reports,
        expected_reports
            .iter()
            .map(String::as_str)
            .collect::<Vec<_>>()
    );
    assert_eq!(
        listener.report_positions.len(),
        expected_report_states.len()
    );
    for (actual_positions, (actual_loss, expected)) in listener
        .report_positions
        .iter()
        .zip(listener.report_losses.iter().zip(&expected_report_states))
    {
        assert_eq!(actual_positions.len(), expected.positions.len());
        assert!((*actual_loss - expected.total_loss).abs() < 1e-4);
        for ((actual_x, actual_y), (expected_x, expected_y)) in
            actual_positions.iter().zip(&expected.positions)
        {
            assert!((actual_x - expected_x).abs() < 5e-6);
            assert!((actual_y - expected_y).abs() < 5e-6);
        }
    }
    let actual_event_order = listener
        .separation_events
        .iter()
        .map(|event| match event {
            TraceEvent::Progress(_) => "progress",
            TraceEvent::Result(_) => "result",
        })
        .collect::<Vec<_>>();
    assert_eq!(actual_event_order, expected_event_order);
    let results = listener
        .separation_events
        .iter()
        .filter_map(|event| match event {
            TraceEvent::Result(result) => Some(result),
            TraceEvent::Progress(_) => None,
        })
        .collect::<Vec<_>>();
    assert_eq!(results.len(), expected_results.len());
    for (actual, expected) in results.iter().zip(&expected_results) {
        assert_eq!(actual.success, expected.success);
        assert_eq!(actual.total_evals, expected.evaluations);
        assert_eq!(actual.total_moves, expected.moves);
        assert_eq!(actual.iterations, expected.iterations);
        assert!(!actual.success && actual.total_moves > 0);
    }
    let progress = listener
        .separation_events
        .iter()
        .filter_map(|event| match event {
            TraceEvent::Progress(progress) => Some(progress),
            TraceEvent::Result(_) => None,
        })
        .collect::<Vec<_>>();
    assert_eq!(progress.len(), expected_progress.len());
    for (actual, expected) in progress.iter().zip(&expected_progress) {
        assert_eq!(actual.iteration, expected.iteration);
        assert!((actual.strip_width - expected.width).abs() < 1e-6);
        assert!((actual.density - expected.density).abs() < 5e-6);
        assert!((actual.min_loss - expected.minimum_loss).abs() < 1e-4);
    }
    assert_eq!(placements.len(), placed_count);
    assert_eq!(placements.len(), expected_placements.len());
    for (actual, expected) in placements.iter().zip(&expected_placements) {
        let (x, y) = actual.d_transf.translation();
        assert_eq!(actual.item.idx, expected.1);
        assert!((actual.d_transf.rotation() - expected.2).abs() < 5e-7);
        assert!((x - expected.3).abs() < 5e-6);
        assert!((y - expected.4).abs() < 5e-6);
    }
    assert!((initial_solution.strip_width() - expected_width).abs() < 1e-6);
    assert!((final_solution.strip_width() - expected_width).abs() < 1e-6);
    assert!((final_solution.density() - expected_density).abs() < 1e-6);
    assert_eq!(
        Layout::from_snapshot(&final_solution.layout_snapshot).is_feasible(),
        expected_feasible
    );
}

#[test]
fn compression_failure_retry_runs_worker_moves_and_matches_the_rust_trace() {
    run_compression_reference(REFERENCE, "compression_worker_move", 1);
}

#[test]
fn compression_multi_worker_retry_matches_the_rust_trace() {
    run_compression_reference(MULTI_WORKER_REFERENCE, "compression_multi_worker_move", 2);
}
