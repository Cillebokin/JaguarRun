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
use sparrow::optimizer::minimum_strip_width_for_trace;
use sparrow::optimizer::separator::Separator;
use sparrow::sample::search::SampleConfig;
use sparrow::util::listener::{ReportType, SolutionListener};
use sparrow::util::terminator::Terminator;
use std::cell::Cell;
use std::path::Path;
use std::time::Duration;

const REFERENCE: &str =
    include_str!("../../Fixtures/ExplorationMinimumWidthInfinityRustReference.tsv");

struct StopAfterOneExploreRound {
    checks: Cell<usize>,
}

impl Terminator for StopAfterOneExploreRound {
    fn kill(&self) -> bool {
        let checks = self.checks.get();
        self.checks.set(checks + 1);
        checks > 0
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

fn exploration_minimum_width_infinity_trace() -> String {
    let (external, _) = sparrow::util::io::read_spp_input(Path::new(
        "../Fixtures/ExplorationMinimumWidthInfinity.json",
    ))
    .expect("read minimum-width infinity fixture");
    let defaults = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        defaults.cde_config,
        defaults.poly_simpl_tolerance,
        defaults.narrow_concavity_cutoff_ratio,
    );
    let instance =
        import_instance(&importer, &external).expect("import minimum-width infinity fixture");
    let mut problem = SPProblem::new(instance).expect("create minimum-width problem");
    problem
        .change_strip_width(20.0)
        .expect("set initial exploration width");
    problem.place_item(SPPlacement {
        item_idx: 0,
        d_transf: DTransformation::new(0.0, (3.0, 2.0)),
    });
    problem.place_item(SPPlacement {
        item_idx: 1,
        d_transf: DTransformation::new(0.0, (12.0, 2.0)),
    });
    assert!(Layout::from_snapshot(&problem.save().layout_snapshot).is_feasible());

    let minimum_width = minimum_strip_width_for_trace(&problem);
    let mut config = DEFAULT_SPARROW_CONFIG.expl_cfg;
    config.shrink_step = 0.25;
    config.separator_config.strike_limit = 0;
    config.separator_config.n_workers = 1;
    config.separator_config.sample_config = SampleConfig {
        n_container_samples: 0,
        n_focussed_samples: 0,
        n_coord_descents: 0,
    };
    let mut separator = Separator::new(
        problem,
        Xoshiro256PlusPlus::seed_from_u64(42),
        config.separator_config,
    );
    let mut listener = RecordingListener::default();
    let solutions = exploration_phase(
        &mut separator,
        &mut listener,
        &StopAfterOneExploreRound {
            checks: Cell::new(0),
        },
        &config,
    );
    assert_eq!(
        solutions.len(),
        1,
        "Rust retains only the initial feasible solution"
    );
    assert_eq!(listener.reports, vec![ReportType::ExplFeas]);

    format!(
        "# generator=Sparrow Rust optimizer::minimum_strip_width and exploration_phase\n\
         # input=ExplorationMinimumWidthInfinity.json strip_height=4 initial_width=20 shrink_step=0.25 seed=42\n\
         # case minimum_width_f32_bits best_width_f32_bits separator_width_f32_bits\n\
         case all_discrete_rotations_too_tall {:08x} {:08x} {:08x}\n",
        minimum_width.to_bits(),
        solutions
            .last()
            .expect("initial feasible solution")
            .strip_width()
            .to_bits(),
        separator.prob.strip_width().to_bits(),
    )
}

#[test]
fn all_discrete_rotations_too_tall_preserves_infinite_minimum_width() {
    let expected = REFERENCE.replace("\r\n", "\n");
    assert_eq!(
        exploration_minimum_width_infinity_trace(),
        expected,
        "Rust Explore minimum-width infinity trace changed"
    );
}
