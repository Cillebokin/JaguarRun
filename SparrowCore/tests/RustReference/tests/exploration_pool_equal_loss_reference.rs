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
use sparrow::quantify::tracker::CollisionTracker;
use sparrow::sample::search::SampleConfig;
use sparrow::util::listener::{ReportType, SolutionListener};
use sparrow::util::terminator::Terminator;
use std::cell::Cell;
use std::path::Path;
use std::time::Duration;

#[derive(Clone, Debug, PartialEq)]
struct Failure {
    loss: f32,
    bounds: Vec<(usize, [f32; 4])>,
}

#[derive(Default)]
struct CaptureFailures(Vec<Failure>);

impl SolutionListener for CaptureFailures {
    fn report(&mut self, report: ReportType, solution: &SPSolution) {
        if report != ReportType::ExplInfeas {
            return;
        }
        let layout = Layout::from_snapshot(&solution.layout_snapshot);
        let mut bounds = solution
            .layout_snapshot
            .placed_items
            .values()
            .map(|placed| {
                let bbox = placed.shape.bbox;
                (
                    placed.item.idx,
                    [bbox.x_min, bbox.y_min, bbox.x_max, bbox.y_max],
                )
            })
            .collect::<Vec<_>>();
        bounds.sort_by_key(|placement| placement.0);
        self.0.push(Failure {
            loss: CollisionTracker::new(&layout).get_total_loss(),
            bounds,
        });
    }
}

struct StopAfterIterations {
    calls: Cell<usize>,
    limit: usize,
}

impl Terminator for StopAfterIterations {
    fn kill(&self) -> bool {
        let calls = self.calls.get();
        self.calls.set(calls + 1);
        calls >= self.limit
    }

    fn new_timeout(&mut self, _timeout: Duration) {}

    fn timeout_at(&self) -> Option<Instant> {
        None
    }
}

fn expected_failures() -> Vec<Failure> {
    let mut rows = include_str!("../../Fixtures/ExplorationEqualLossPoolRustTrace.tsv")
        .lines()
        .filter(|line| !line.trim().is_empty() && !line.starts_with('#'));
    let mut header = rows.next().expect("reference header").split_whitespace();
    assert_eq!(header.next(), Some("case"));
    assert_eq!(header.next(), Some("equal_loss"));
    assert_eq!(header.next(), Some("0"));
    assert_eq!(header.next(), Some("8.000000000"));
    assert_eq!(header.next(), Some("0.187500000"));
    assert_eq!(header.next(), Some("6"));
    assert_eq!(header.next(), Some("0.000001000"));
    let failure_count: usize = header.next().unwrap().parse().unwrap();
    let item_count: usize = header.next().unwrap().parse().unwrap();
    assert_eq!(item_count, 2);
    assert!(header.next().is_none());

    let mut failures = Vec::with_capacity(failure_count);
    for expected_ordinal in 0..failure_count {
        let mut row = rows
            .next()
            .expect("reference failure row")
            .split_whitespace();
        assert_eq!(row.next(), Some("failure"));
        assert_eq!(
            row.next().unwrap().parse::<usize>().unwrap(),
            expected_ordinal
        );
        let loss = row.next().unwrap().parse::<f32>().unwrap();
        let mut bounds = Vec::with_capacity(item_count);
        for _ in 0..item_count {
            let item_index = row.next().unwrap().parse::<usize>().unwrap();
            let rect = [
                row.next().unwrap().parse::<f32>().unwrap(),
                row.next().unwrap().parse::<f32>().unwrap(),
                row.next().unwrap().parse::<f32>().unwrap(),
                row.next().unwrap().parse::<f32>().unwrap(),
            ];
            bounds.push((item_index, rect));
        }
        assert!(row.next().is_none());
        failures.push(Failure { loss, bounds });
    }
    assert!(rows.next().is_none());
    failures
}

#[test]
fn equal_loss_pool_order_and_selected_layout_match_the_locked_rust_trace() {
    let (external, _) =
        sparrow::util::io::read_spp_input(Path::new("../Fixtures/ExplorationEqualLossPool.json"))
            .expect("read equal-loss pool fixture");
    let defaults = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        defaults.cde_config,
        defaults.poly_simpl_tolerance,
        defaults.narrow_concavity_cutoff_ratio,
    );
    let instance = import_instance(&importer, &external).expect("import equal-loss fixture");
    let mut problem = SPProblem::new(instance).expect("create equal-loss problem");
    problem
        .change_strip_width(8.0)
        .expect("set initial equal-loss width");
    problem.place_item(SPPlacement {
        item_idx: 0,
        d_transf: DTransformation::new(0.0, (1.5, 2.0)),
    });
    problem.place_item(SPPlacement {
        item_idx: 1,
        d_transf: DTransformation::new(0.0, (6.0, 2.0)),
    });

    let mut config = defaults.expl_cfg;
    config.shrink_step = 0.1875;
    config.solution_pool_distribution_stddev = 0.000001;
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
        Xoshiro256PlusPlus::seed_from_u64(0),
        config.separator_config,
    );
    let mut listener = CaptureFailures::default();
    let terminator = StopAfterIterations {
        calls: Cell::new(0),
        limit: 6,
    };
    exploration_phase(&mut separator, &mut listener, &terminator, &config);

    let expected = expected_failures();
    assert_eq!(
        listener.0, expected,
        "failure states must match the Rust trace"
    );
    assert_eq!(expected.len(), 4);
    assert!(
        expected
            .iter()
            .all(|failure| failure.loss.to_bits() == expected[0].loss.to_bits()),
        "all reference pool candidates must have exactly equal f32 losses"
    );
    assert_ne!(expected[0].bounds, expected[1].bounds);

    let mut pool: Vec<(usize, f32)> = Vec::new();
    let mut insert_positions = Vec::new();
    for (candidate_id, failure) in expected.iter().enumerate() {
        let insertion_index =
            match pool.binary_search_by(|(_, loss)| loss.partial_cmp(&failure.loss).unwrap()) {
                Ok(index) | Err(index) => index,
            };
        insert_positions.push(insertion_index);
        pool.insert(insertion_index, (candidate_id, failure.loss));
    }
    assert_eq!(insert_positions, [0, 0, 1, 2]);
    assert_eq!(
        pool.iter()
            .map(|(candidate_id, _)| *candidate_id)
            .collect::<Vec<_>>(),
        [1, 2, 3, 0],
        "replaying Rust Vec binary search places each equal-loss candidate at the right-biased match"
    );
    assert_eq!(
        pool[0].0, 1,
        "the next pool selection uses failure layout 1"
    );
    assert_eq!(
        expected[3].bounds, expected[0].bounds,
        "disrupting the selected second layout yields the fourth failure state"
    );
}
