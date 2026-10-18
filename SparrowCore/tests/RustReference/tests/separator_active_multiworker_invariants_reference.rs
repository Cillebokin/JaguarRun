use jagua_rs::entities::Layout;
use jagua_rs::io::import::Importer;
use jagua_rs::probs::spp::entities::{SPPlacement, SPProblem};
use jagua_rs::probs::spp::io::import_instance;
use rand::SeedableRng;
use rand::rngs::Xoshiro256PlusPlus;
use sparrow::config::DEFAULT_SPARROW_CONFIG;
use sparrow::optimizer::separator::Separator;
use sparrow::sample::search::SampleConfig;
use sparrow::util::listener::{ReportType, SeparationProgress, SeparationResult, SolutionListener};
use sparrow::util::terminator::BasicTerminator;
use std::path::Path;

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

fn make_overlapping_problem() -> SPProblem {
    let (external, _) =
        sparrow::util::io::read_spp_input(Path::new("../Fixtures/SeparatorRoundRustTrace.json"))
            .expect("read active multiworker input");
    let defaults = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        defaults.cde_config,
        defaults.poly_simpl_tolerance,
        defaults.narrow_concavity_cutoff_ratio,
    );
    let instance = import_instance(&importer, &external).expect("import active multiworker input");
    let mut problem = SPProblem::new(instance).expect("create active multiworker problem");
    problem
        .change_strip_width(9.0)
        .expect("set active multiworker strip width");
    let transform = jagua_rs::geometry::DTransformation::new(0.0, (1.0, 1.0));
    for _ in 0..2 {
        problem.place_item(SPPlacement {
            item_idx: 0,
            d_transf: transform,
        });
    }
    problem
}

#[test]
fn four_worker_active_round_preserves_state_invariants() {
    let problem = make_overlapping_problem();
    let mut config = DEFAULT_SPARROW_CONFIG.expl_cfg.separator_config;
    config.strike_limit = 1;
    config.iter_no_imprv_limit = 1;
    config.n_workers = 4;
    config.sample_config = SampleConfig {
        n_container_samples: 1,
        n_focussed_samples: 0,
        n_coord_descents: 1,
    };
    let mut separator = Separator::new(problem, Xoshiro256PlusPlus::seed_from_u64(42), config);
    assert!(separator.ct.get_total_loss() > 0.0);

    let mut listener = RecordingListener::default();
    let (solution, tracker) = separator.separate(&BasicTerminator::new(), &mut listener);
    let result = listener.result.expect("active multiworker result callback");
    let layout = Layout::from_snapshot(&solution.layout_snapshot);

    assert_eq!(solution.layout_snapshot.placed_items.len(), 2);
    assert!(listener.progress.len() >= 2);
    assert_eq!(listener.progress[0].iteration, 0);
    assert!(
        listener
            .progress
            .iter()
            .all(|event| event.min_loss.is_finite() && event.min_loss >= 0.0)
    );
    assert!(result.total_evals > 0);
    assert!(result.total_moves > 0);
    assert!(tracker.get_total_loss().is_finite() && tracker.get_total_loss() >= 0.0);
    assert_eq!(result.success, separator.ct.get_total_loss() == 0.0);
    assert_eq!(result.success, layout.is_feasible());
}
