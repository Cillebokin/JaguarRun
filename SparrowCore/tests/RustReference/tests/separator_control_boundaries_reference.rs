use jagua_rs::entities::Layout;
use jagua_rs::io::import::Importer;
use jagua_rs::probs::spp::entities::{SPPlacement, SPProblem};
use jagua_rs::probs::spp::io::import_instance;
use rand::SeedableRng;
use rand::rngs::Xoshiro256PlusPlus;
use sparrow::config::DEFAULT_SPARROW_CONFIG;
use sparrow::optimizer::separator::{Separator, SeparatorConfig};
use sparrow::sample::search::SampleConfig;
use sparrow::util::listener::{ReportType, SeparationProgress, SeparationResult, SolutionListener};
use sparrow::util::terminator::BasicTerminator;
use std::path::Path;

const RUST_TRACE: &str = include_str!("../../Fixtures/SeparatorThresholdIterationRustTrace.tsv");

#[derive(Default)]
struct RecordingListener {
    reports: usize,
    progress: Vec<SeparationProgress>,
    result: Option<SeparationResult>,
}

impl SolutionListener for RecordingListener {
    fn report(
        &mut self,
        _report: ReportType,
        _solution: &jagua_rs::probs::spp::entities::SPSolution,
    ) {
        self.reports += 1;
    }

    fn report_separation_progress(&mut self, progress: SeparationProgress) {
        self.progress.push(progress);
    }

    fn report_separation_result(&mut self, result: SeparationResult) {
        self.result = Some(result);
    }
}

fn make_problem(width: f32, placement_count: usize) -> SPProblem {
    let (external, _) =
        sparrow::util::io::read_spp_input(Path::new("../Fixtures/SeparatorRoundRustTrace.json"))
            .expect("read separator control-boundary input");
    let defaults = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        defaults.cde_config,
        defaults.poly_simpl_tolerance,
        defaults.narrow_concavity_cutoff_ratio,
    );
    let instance =
        import_instance(&importer, &external).expect("import separator control-boundary input");
    let mut problem = SPProblem::new(instance).expect("create separator problem");
    problem
        .change_strip_width(width)
        .expect("set separator strip width");
    let transform = jagua_rs::geometry::DTransformation::new(0.0, (1.0, 1.0));
    for _ in 0..placement_count {
        problem.place_item(SPPlacement {
            item_idx: 0,
            d_transf: transform,
        });
    }
    problem
}

fn make_separator_config(
    strike_limit: usize,
    iteration_limit: usize,
    worker_count: usize,
    container_samples: usize,
) -> SeparatorConfig {
    let mut config = DEFAULT_SPARROW_CONFIG.expl_cfg.separator_config;
    config.strike_limit = strike_limit;
    config.iter_no_imprv_limit = iteration_limit;
    config.n_workers = worker_count;
    config.sample_config = SampleConfig {
        n_container_samples: container_samples,
        n_focussed_samples: 0,
        n_coord_descents: 1,
    };
    config
}

#[test]
fn separator_threshold_sides_and_inner_iteration_limit_match_rust_fixture() {
    let problem = make_problem(9.0, 9);
    let config = make_separator_config(2, 2, 1, 1);
    let mut separator = Separator::new(problem, Xoshiro256PlusPlus::seed_from_u64(0), config);
    let mut listener = RecordingListener::default();
    let (solution, tracker) = separator.separate(&BasicTerminator::new(), &mut listener);
    let result = listener.result.expect("separator result callback");

    let mut actual = String::from(
        "# generator=Sparrow Rust 1.98.1; single-worker control-boundary trace\n\
         # input=SeparatorRoundRustTrace.json; seed=0; width=9; strike_limit=2; iter_no_imprv_limit=2\n\
         # progress ordinal iteration min_loss_f32_bits\n",
    );
    for (ordinal, progress) in listener.progress.iter().enumerate() {
        actual.push_str(&format!(
            "progress {ordinal} {} {:08x}\n",
            progress.iteration,
            progress.min_loss.to_bits(),
        ));
    }
    actual.push_str(&format!("reports {}\n", listener.reports));
    // The Rust return value carries the best tracker snapshot; compare the
    // post-run internal tracker to C++ Separator::Tracker() for retained GLS weights.
    actual.push_str(&format!(
        "result {} {} {} {} {} {:08x} {:08x}\n",
        u8::from(result.success),
        result.total_evals,
        result.total_moves,
        result.iterations,
        solution.layout_snapshot.placed_items.len(),
        separator.ct.get_total_loss().to_bits(),
        separator.ct.get_total_weighted_loss().to_bits(),
    ));
    assert_eq!(actual, RUST_TRACE);
    assert_eq!(tracker.get_total_loss(), separator.ct.get_total_loss());

    let losses = listener
        .progress
        .iter()
        .map(|progress| progress.min_loss)
        .collect::<Vec<_>>();
    assert!(losses[5] < losses[4] * 0.98);
    assert!(losses[11] < losses[10] && losses[11] >= losses[10] * 0.98);
    assert_eq!(listener.progress.last().unwrap().iteration, 16);
}

#[test]
fn zero_strike_and_zero_inner_iteration_limits_do_not_start_a_worker_round() {
    for (strike_limit, iteration_limit) in [(0, 2), (2, 0)] {
        let problem = make_problem(9.0, 9);
        let config = make_separator_config(strike_limit, iteration_limit, 1, 1);
        let mut separator = Separator::new(problem, Xoshiro256PlusPlus::seed_from_u64(42), config);
        let mut listener = RecordingListener::default();
        let (solution, tracker) = separator.separate(&BasicTerminator::new(), &mut listener);
        let result = listener.result.expect("separator result callback");

        assert_eq!(listener.progress.len(), 1);
        assert_eq!(listener.progress[0].iteration, 0);
        assert_eq!(listener.reports, 0);
        assert_eq!(result.total_evals, 0);
        assert_eq!(result.total_moves, 0);
        assert_eq!(result.iterations, 0);
        assert_eq!(solution.layout_snapshot.placed_items.len(), 9);
        assert_eq!(tracker.get_total_loss().to_bits(), 0x4343_bf37);
    }
}

#[test]
fn four_worker_natural_schedule_preserves_separator_invariants() {
    let problem = make_problem(9.0, 0);
    let config = make_separator_config(1, 1, 4, 0);
    let mut separator = Separator::new(problem, Xoshiro256PlusPlus::seed_from_u64(42), config);
    let mut listener = RecordingListener::default();
    let (solution, tracker) = separator.separate(&BasicTerminator::new(), &mut listener);
    let result = listener.result.expect("separator result callback");

    assert_eq!(
        listener
            .progress
            .iter()
            .map(|progress| progress.iteration)
            .collect::<Vec<_>>(),
        [0, 1]
    );
    assert!(listener.reports == 0);
    assert_eq!(result.iterations, 0);
    assert_eq!(result.total_evals, 0);
    assert_eq!(result.total_moves, 0);
    assert!(solution.layout_snapshot.placed_items.is_empty());
    assert_eq!(tracker.get_total_loss(), 0.0);
    assert!(Layout::from_snapshot(&solution.layout_snapshot).is_feasible());
    assert!(result.success);
}
