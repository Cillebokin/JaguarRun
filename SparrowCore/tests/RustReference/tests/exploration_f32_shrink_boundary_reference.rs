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
use std::cell::Cell;
use std::path::Path;
use std::time::Duration;

const LONG_SHRINK_CHAIN_REFERENCE: &str =
    include_str!("../../Fixtures/ExplorationLongShrinkChainRust.tsv");

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

struct StopAfterIterations {
    checks: Cell<usize>,
    limit: usize,
}

impl Terminator for StopAfterIterations {
    fn kill(&self) -> bool {
        let checks = self.checks.get();
        self.checks.set(checks + 1);
        checks >= self.limit
    }

    fn new_timeout(&mut self, _timeout: Duration) {}

    fn timeout_at(&self) -> Option<jagua_rs::Instant> {
        None
    }
}

#[derive(Default)]
struct RecordingListener {
    reports: Vec<ReportType>,
    widths: Vec<f32>,
}

impl SolutionListener for RecordingListener {
    fn report(&mut self, report: ReportType, solution: &SPSolution) {
        self.reports.push(report);
        self.widths.push(solution.strip_width());
    }
}

#[test]
fn successful_exploration_matches_long_f32_shrink_chain_reference() {
    let instance = load_single_item_instance();
    let mut problem = SPProblem::new(instance).expect("create long-shrink problem");
    problem
        .change_strip_width(64.0)
        .expect("set long-shrink initial width");
    problem.place_item(SPPlacement {
        item_idx: 0,
        d_transf: DTransformation::new(0.0, (1.1, 2.0)),
    });
    assert!(Layout::from_snapshot(&problem.save().layout_snapshot).is_feasible());

    let mut config = DEFAULT_SPARROW_CONFIG.expl_cfg;
    config.shrink_step = 0.1;
    config.max_conseq_failed_attempts = None;
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
        &StopAfterIterations {
            checks: Cell::new(0),
            limit: 128,
        },
        &config,
    );

    assert_eq!(listener.widths.len(), 33);
    assert_eq!(solutions.len(), 33);
    assert!(listener.widths.last().is_some_and(|width| *width >= 2.0));
    assert_eq!(listener.widths.first(), Some(&64.0));
    let actual = listener
        .widths
        .iter()
        .enumerate()
        .map(|(index, width)| format!("width {index} {:08x}", width.to_bits()))
        .collect::<Vec<_>>()
        .join("\n")
        + "\n";
    let expected = LONG_SHRINK_CHAIN_REFERENCE.replace("\r\n", "\n");
    assert_eq!(actual, expected, "Rust long shrink-chain reference changed");
}

fn load_single_item_instance() -> jagua_rs::probs::spp::entities::SPInstance {
    let (external, _) =
        sparrow::util::io::read_spp_input(Path::new("../Fixtures/RustSppSingleItem.json"))
            .expect("read single-item shrink-boundary fixture");
    let defaults = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        defaults.cde_config,
        defaults.poly_simpl_tolerance,
        defaults.narrow_concavity_cutoff_ratio,
    );
    import_instance(&importer, &external).expect("import single-item shrink-boundary fixture")
}

#[test]
fn shrink_step_zero_one_and_half_ulp_are_accepted_and_stop_without_shrinking() {
    for (name, shrink_step) in [
        ("zero", 0.0_f32),
        ("half_ulp", 2.0_f32.powi(-25)),
        ("one", 1.0_f32),
    ] {
        let instance = load_single_item_instance();
        let mut problem = SPProblem::new(instance).expect("create shrink-boundary problem");
        problem
            .change_strip_width(4.0)
            .expect("set shrink-boundary width");
        problem.place_item(SPPlacement {
            item_idx: 0,
            d_transf: DTransformation::new(0.0, (2.0, 2.0)),
        });
        assert!(Layout::from_snapshot(&problem.save().layout_snapshot).is_feasible());

        let mut config = DEFAULT_SPARROW_CONFIG.expl_cfg;
        config.shrink_step = shrink_step;
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
            "{name}: retain the initial feasible solution"
        );
        assert_eq!(listener.reports, vec![ReportType::ExplFeas], "{name}");
        assert_eq!(
            separator.prob.strip_width().to_bits(),
            4.0_f32.to_bits(),
            "{name}: Rust exits before changing the current width"
        );
    }
}
