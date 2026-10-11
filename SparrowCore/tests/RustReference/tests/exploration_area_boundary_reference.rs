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

struct StopAfterTwoExploreRounds {
    checks: Cell<usize>,
}

impl Terminator for StopAfterTwoExploreRounds {
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

fn load_area_boundary_instance() -> jagua_rs::probs::spp::entities::SPInstance {
    let (external, _) =
        sparrow::util::io::read_spp_input(Path::new("../Fixtures/ExplorationAreaBoundary.json"))
            .expect("read area-boundary fixture");
    let defaults = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        defaults.cde_config,
        defaults.poly_simpl_tolerance,
        defaults.narrow_concavity_cutoff_ratio,
    );
    import_instance(&importer, &external).expect("import area-boundary fixture")
}

fn small_item_x(seed: u64) -> f32 {
    let instance = load_area_boundary_instance();
    let mut problem = SPProblem::new(instance).expect("create area-boundary problem");
    problem
        .change_strip_width(10000.0)
        .expect("set area-boundary width");
    problem.place_item(SPPlacement {
        item_idx: 0,
        d_transf: DTransformation::new(0.0, (2050.0, 2050.0)),
    });
    problem.place_item(SPPlacement {
        item_idx: 1,
        d_transf: DTransformation::new(0.0, (5000.0, 100.0)),
    });
    problem.place_item(SPPlacement {
        item_idx: 2,
        d_transf: DTransformation::new(0.0, (2050.0, 2050.0)),
    });
    assert!(
        !problem.layout.is_feasible(),
        "seed={seed} must start with an overlapping pair so only disruption moves the small item"
    );

    let mut config = DEFAULT_SPARROW_CONFIG.expl_cfg;
    config.shrink_step = 0.0005;
    config.large_item_ch_area_cutoff_percentile = 0.9999999;
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
        &StopAfterTwoExploreRounds {
            checks: Cell::new(0),
        },
        &config,
    );
    separator
        .prob
        .layout
        .placed_items
        .values()
        .find(|placed| placed.item.idx == 1)
        .expect("small item remains placed")
        .d_transf
        .translation
        .0
        .into_inner()
}

#[test]
fn f32_area_cutoff_keeps_the_small_item_eligible_at_the_rounding_boundary() {
    let instance = load_area_boundary_instance();
    let mut total_area = 0.0_f32;
    for (item, quantity) in &instance.items {
        total_area += item.shape_cd.surrogate().convex_hull_area * (*quantity as f32);
    }
    let cutoff_threshold = total_area * 0.9999999_f32;
    let mut ordered_areas: Vec<(f32, f32)> = instance
        .items
        .iter()
        .map(|(item, quantity)| (item.shape_cd.surrogate().convex_hull_area, *quantity as f32))
        .collect();
    ordered_areas.sort_by(|left, right| right.0.total_cmp(&left.0));
    let mut cumulative_area = 0.0_f32;
    let mut cutoff_area = 0.0_f32;
    for (area, quantity) in ordered_areas {
        cumulative_area += area * quantity;
        if cumulative_area > cutoff_threshold {
            cutoff_area = area;
            break;
        }
    }
    assert_eq!(
        cutoff_area, 0.0,
        "fixture must preserve the f32 cutoff boundary"
    );

    let observed_small_x = small_item_x(3);
    assert!(
        (observed_small_x - 5000.0).abs() > 1.0,
        "seed 3 must disrupt the small item when f32 cutoff keeps it eligible: x={observed_small_x}"
    );
}
