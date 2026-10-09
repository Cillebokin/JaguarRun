use jagua_rs::Instant;
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

struct StopAfterExploreRound {
    checks: Cell<usize>,
}

impl Terminator for StopAfterExploreRound {
    fn kill(&self) -> bool {
        let checks = self.checks.get();
        self.checks.set(checks + 1);
        checks >= 2
    }

    fn new_timeout(&mut self, _timeout: Duration) {}

    fn timeout_at(&self) -> Option<Instant> {
        None
    }
}

#[derive(Default)]
struct NoopListener;

impl SolutionListener for NoopListener {
    fn report(&mut self, _report: ReportType, _solution: &SPSolution) {}
}

fn load_similarity_boundary_instance() -> jagua_rs::probs::spp::entities::SPInstance {
    let (external, _) = sparrow::util::io::read_spp_input(Path::new(
        "../Fixtures/ExplorationSimilarityBoundary.json",
    ))
    .expect("read similarity-boundary fixture");
    let defaults = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        defaults.cde_config,
        defaults.poly_simpl_tolerance,
        defaults.narrow_concavity_cutoff_ratio,
    );
    import_instance(&importer, &external).expect("import similarity-boundary fixture")
}

fn item_x_positions(seed: u64) -> [f32; 3] {
    let instance = load_similarity_boundary_instance();
    let mut problem = SPProblem::new(instance).expect("create similarity-boundary problem");
    problem
        .change_strip_width(30.0)
        .expect("set similarity-boundary width");
    for (item_idx, translation) in [(0, (5.0, 5.0)), (1, (15.0, 10.0)), (2, (6.0, 5.0))] {
        problem.place_item(SPPlacement {
            item_idx,
            d_transf: DTransformation::new(0.0, translation),
        });
    }

    let mut config = DEFAULT_SPARROW_CONFIG.expl_cfg;
    config.large_item_ch_area_cutoff_percentile = 0.0;
    config.separator_config.strike_limit = 0;
    config.separator_config.n_workers = 1;
    config.separator_config.sample_config = SampleConfig {
        n_container_samples: 0,
        n_focussed_samples: 0,
        n_coord_descents: 0,
    };
    let mut separator = Separator::new(
        problem,
        Xoshiro256PlusPlus::seed_from_u64(seed),
        config.separator_config,
    );
    let mut listener = NoopListener;
    let _ = exploration_phase(
        &mut separator,
        &mut listener,
        &StopAfterExploreRound {
            checks: Cell::new(0),
        },
        &config,
    );

    let mut x_positions = [f32::NAN; 3];
    for placed in separator.prob.layout.placed_items.values() {
        x_positions[placed.item.idx] = placed.d_transf.translation.0.into_inner();
    }
    x_positions
}

#[test]
fn f32_similarity_boundary_excludes_the_one_percent_area_neighbor() {
    let instance = load_similarity_boundary_instance();
    let first_area = instance.items[0].0.shape_cd.area;
    let candidate_area = instance.items[1].0.shape_cd.area;
    let first_diameter = instance.items[0].0.shape_cd.diameter;
    let candidate_diameter = instance.items[1].0.shape_cd.diameter;
    let area_delta = (candidate_area - first_area).abs();
    let area_epsilon = first_area * 0.01;
    let diameter_delta = (candidate_diameter - first_diameter).abs();
    let diameter_epsilon = first_diameter * 0.01;
    assert!(
        area_delta <= area_epsilon,
        "area delta={area_delta}, epsilon={area_epsilon}"
    );
    assert!(diameter_delta > diameter_epsilon);

    let positions = item_x_positions(31);
    assert!(
        (positions[0] - 6.0).abs() < 0.5
            && (positions[1] - 15.0).abs() < 0.5
            && (positions[2] - 5.0).abs() < 0.5,
        "seed 31 must select items 0 and 2 while the f32 area epsilon excludes item 1: {positions:?}"
    );
}
