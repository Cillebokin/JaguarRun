use jagua_rs::entities::Layout;
use jagua_rs::geometry::DTransformation;
use jagua_rs::io::import::Importer;
use jagua_rs::probs::spp::entities::{SPInstance, SPPlacement, SPProblem, SPSolution};
use jagua_rs::probs::spp::io::import_instance;
use rand::SeedableRng;
use rand::rngs::Xoshiro256PlusPlus;
use sparrow::config::DEFAULT_SPARROW_CONFIG;
use sparrow::optimizer::optimize;
use sparrow::util::ctrlc_terminator::CtrlCTerminator;
use sparrow::util::listener::{
    OptimizationPhase, ReportType, SeparationProgress, SeparationResult, SolutionListener,
};
use std::path::Path;
use std::sync::Arc;
use std::sync::atomic::{AtomicBool, Ordering};
use std::time::Duration;

const REFERENCE: &str = include_str!("../../Fixtures/OptimizePartialWarmStartRustReference.tsv");

#[derive(Default)]
struct RecordingListener {
    case_name: String,
    events: Vec<String>,
    stop: Option<Arc<AtomicBool>>,
    stopped_after_initial_expl_feas: bool,
    separation_progress_count: usize,
    separation_result_count: usize,
    compression_step_count: usize,
}

impl SolutionListener for RecordingListener {
    fn report(&mut self, report: ReportType, solution: &SPSolution) {
        let stops_exploration = report == ReportType::ExplFeas;
        let label = match &report {
            ReportType::ExplFeas => "expl_feas",
            ReportType::ExplInfeas => "expl_infeas",
            ReportType::ExplImproving => "expl_improving",
            ReportType::CmprFeas => "cmpr_feas",
            ReportType::Final => "final",
        };
        self.events.push(format!(
            "report\t{}\t{}\t{}",
            self.case_name,
            label,
            solution.layout_snapshot.placed_items.len()
        ));
        if stops_exploration && !self.stopped_after_initial_expl_feas {
            self.stop
                .as_ref()
                .expect("stop flag is wired to CtrlCTerminator")
                .store(true, Ordering::SeqCst);
            self.stopped_after_initial_expl_feas = true;
        }
    }

    fn report_phase(&mut self, phase: OptimizationPhase) {
        let label = match phase {
            OptimizationPhase::Exploration => "exploration",
            OptimizationPhase::Compression => "compression",
        };
        self.events
            .push(format!("phase\t{}\t{}", self.case_name, label));
    }

    fn report_separation_progress(&mut self, _progress: SeparationProgress) {
        self.separation_progress_count += 1;
    }

    fn report_separation_result(&mut self, _result: SeparationResult) {
        self.separation_result_count += 1;
    }

    fn report_compression_progress(&mut self, _shrink_step: f32) {
        self.compression_step_count += 1;
    }
}

fn id_list(ids: &[usize]) -> String {
    if ids.is_empty() {
        return "-".to_owned();
    }
    ids.iter()
        .map(usize::to_string)
        .collect::<Vec<_>>()
        .join(",")
}

fn append_state(
    trace: &mut Vec<String>,
    case_name: &str,
    stage: &str,
    instance: &SPInstance,
    solution: &SPSolution,
) {
    let mut remaining_by_index = instance
        .items
        .iter()
        .map(|(_, demand)| *demand)
        .collect::<Vec<_>>();
    let mut placements = solution
        .layout_snapshot
        .placed_items
        .values()
        .collect::<Vec<_>>();
    placements.sort_by_key(|placement| placement.item.idx);
    let placed_ids = placements
        .iter()
        .map(|placement| placement.item.idx)
        .collect::<Vec<_>>();
    for placement in &placements {
        remaining_by_index[placement.item.idx] -= 1;
    }
    let remaining_ids = remaining_by_index
        .iter()
        .enumerate()
        .flat_map(|(item_index, demand)| std::iter::repeat_n(item_index, *demand))
        .collect::<Vec<_>>();
    let layout_feasible = Layout::from_snapshot(&solution.layout_snapshot).is_feasible();
    let solution_feasible = remaining_ids.is_empty() && layout_feasible;
    trace.push(format!(
        "state\t{case_name}\t{stage}\t{}\t{}\t{:.9}\t{:.9}\t{}\t{}\t{}\t{}",
        placed_ids.len(),
        remaining_ids.len(),
        solution.strip_width(),
        solution.density(),
        u8::from(solution_feasible),
        u8::from(layout_feasible),
        id_list(&placed_ids),
        id_list(&remaining_ids),
    ));
    for placement in placements {
        let (x, y) = placement.d_transf.translation();
        trace.push(format!(
            "placement\t{case_name}\t{stage}\t{}\t{:.9}\t{:.9}\t{:.9}",
            placement.item.idx,
            placement.d_transf.rotation(),
            x,
            y,
        ));
    }
}

fn run_case(
    case_name: &str,
    instance: &SPInstance,
    overlapping: bool,
    terminator: &mut CtrlCTerminator,
) -> Vec<String> {
    let mut problem = SPProblem::new(instance.clone()).expect("create warm-start problem");
    problem
        .change_strip_width(8.0)
        .expect("set warm-start strip width");
    problem.place_item(SPPlacement {
        item_idx: 1,
        d_transf: DTransformation::new(0.0, (1.5, 2.5)),
    });
    problem.place_item(SPPlacement {
        item_idx: 2,
        d_transf: DTransformation::new(0.0, (if overlapping { 1.5 } else { 6.5 }, 2.5)),
    });
    let initial = problem.save();
    let initial_layout_feasible = Layout::from_snapshot(&initial.layout_snapshot).is_feasible();
    assert_eq!(initial.layout_snapshot.placed_items.len(), 2);
    assert_eq!(initial_layout_feasible, !overlapping);

    let mut config = DEFAULT_SPARROW_CONFIG;
    config.expl_cfg.time_limit = Duration::ZERO;
    config.cmpr_cfg.time_limit = Duration::ZERO;
    config.expl_cfg.separator_config.n_workers = 1;
    config.cmpr_cfg.separator_config.n_workers = 1;

    let mut trace = vec![format!(
        "config\t{case_name}\t42\tExplorationEmptyLarge.json\tworkers=1\tphase_ms=0,0\tstop=after_initial_expl_feas"
    )];
    append_state(&mut trace, case_name, "initial", instance, &initial);

    let mut listener = RecordingListener {
        case_name: case_name.to_owned(),
        stop: Some(Arc::clone(&terminator.ctrlc)),
        ..RecordingListener::default()
    };
    let final_solution = optimize(
        instance.clone(),
        Xoshiro256PlusPlus::seed_from_u64(42),
        &mut listener,
        terminator,
        &config.expl_cfg,
        &config.cmpr_cfg,
        Some(&initial),
    )
    .expect("Rust accepts a restorable partial warm start");

    assert!(listener.stopped_after_initial_expl_feas);
    assert_eq!(listener.events.len(), 4);
    assert_eq!(listener.separation_progress_count, 0);
    assert_eq!(listener.separation_result_count, 0);
    assert_eq!(listener.compression_step_count, 0);
    trace.extend(listener.events);
    append_state(&mut trace, case_name, "final", instance, &final_solution);
    trace.push(format!(
        "termination\t{case_name}\tlistener_cancel_after_expl_feas\tzero_compression_deadline"
    ));
    trace.push(format!("loss\t{case_name}\tnone_no_separator_attempt"));
    trace.push(format!(
        "counts\t{case_name}\tseparation_progress=0\tseparation_results=0\tcompression_steps=0"
    ));
    trace
}

#[test]
fn partial_warm_start_final_states_match_the_checked_in_reference() {
    let (external, _) =
        sparrow::util::io::read_spp_input(Path::new("../Fixtures/ExplorationEmptyLarge.json"))
            .expect("read partial warm-start fixture");
    let defaults = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        defaults.cde_config,
        defaults.poly_simpl_tolerance,
        defaults.narrow_concavity_cutoff_ratio,
    );
    let instance = import_instance(&importer, &external).expect("import fixture");

    let mut terminator = CtrlCTerminator::new();
    let mut actual = run_case("partial_disjoint", &instance, false, &mut terminator);
    actual.extend(run_case(
        "partial_overlapping",
        &instance,
        true,
        &mut terminator,
    ));

    if std::env::var_os("SPARROW_PRINT_PARTIAL_WARM_START_REFERENCE").is_some() {
        for row in actual {
            println!("{row}");
        }
        return;
    }
    let expected = REFERENCE
        .lines()
        .filter(|line| !line.is_empty() && !line.starts_with('#'))
        .map(str::to_owned)
        .collect::<Vec<_>>();
    assert_eq!(
        actual, expected,
        "partial warm-start full-path trace changed"
    );
}
