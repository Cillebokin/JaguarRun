use jagua_rs::Instant;
use jagua_rs::entities::Layout;
use jagua_rs::geometry::DTransformation;
use jagua_rs::io::import::Importer;
use jagua_rs::probs::spp::entities::{SPPlacement, SPProblem, SPSolution};
use jagua_rs::probs::spp::io::import_instance;
use rand::SeedableRng;
use rand::rngs::Xoshiro256PlusPlus;
use sparrow::config::DEFAULT_SPARROW_CONFIG;
use sparrow::optimizer::explore::exploration_phase;
use sparrow::optimizer::separator::Separator;
use sparrow::sample::search::SampleConfig;
use sparrow::util::listener::{ReportType, SolutionListener};
use sparrow::util::terminator::Terminator;
use std::path::Path;
use std::time::Duration;

struct NeverStop;

impl Terminator for NeverStop {
    fn kill(&self) -> bool {
        false
    }

    fn new_timeout(&mut self, _timeout: Duration) {}

    fn timeout_at(&self) -> Option<Instant> {
        None
    }
}

#[derive(Default)]
struct RecordingListener {
    reports: Vec<ReportType>,
}

impl SolutionListener for RecordingListener {
    fn report(&mut self, report: ReportType, _solution: &SPSolution) {
        self.reports.push(report);
    }
}

#[test]
fn some_zero_failure_limit_stops_after_the_first_failed_attempt() {
    let (external, _) =
        sparrow::util::io::read_spp_input(Path::new("../Fixtures/RustSppThreeSquareBoundary.json"))
            .expect("read zero-failure-limit fixture");
    let defaults = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        defaults.cde_config,
        defaults.poly_simpl_tolerance,
        defaults.narrow_concavity_cutoff_ratio,
    );
    let instance =
        import_instance(&importer, &external).expect("import zero-failure-limit fixture");
    let mut problem = SPProblem::new(instance).expect("create zero-failure-limit problem");
    problem
        .change_strip_width(6.8)
        .expect("set initial zero-failure-limit strip");
    for x in [1.2, 3.4, 5.6] {
        problem.place_item(SPPlacement {
            item_idx: 0,
            d_transf: DTransformation::new(0.0, (x, 2.0)),
        });
    }
    assert!(Layout::from_snapshot(&problem.save().layout_snapshot).is_feasible());

    let mut exploration_config = defaults.expl_cfg;
    exploration_config.shrink_step = 0.45;
    exploration_config.max_conseq_failed_attempts = Some(0);
    exploration_config.separator_config.strike_limit = 0;
    exploration_config.separator_config.n_workers = 1;
    exploration_config.separator_config.sample_config = SampleConfig {
        n_container_samples: 0,
        n_focussed_samples: 0,
        n_coord_descents: 1,
    };
    let mut separator = Separator::new(
        problem,
        Xoshiro256PlusPlus::seed_from_u64(42),
        exploration_config.separator_config,
    );
    let mut listener = RecordingListener::default();
    let solutions = exploration_phase(
        &mut separator,
        &mut listener,
        &NeverStop,
        &exploration_config,
    );

    assert_eq!(
        solutions.len(),
        1,
        "the initial feasible report is retained"
    );
    assert_eq!(
        listener.reports,
        vec![ReportType::ExplFeas, ReportType::ExplInfeas],
        "Some(0) attempts the first shrink, reports its failure, then stops"
    );
}
