use jagua_rs::entities::Layout;
use jagua_rs::geometry::DTransformation;
use jagua_rs::io::import::Importer;
use jagua_rs::probs::spp::entities::{SPPlacement, SPProblem, SPSolution};
use jagua_rs::probs::spp::io::import_instance;
use rand::SeedableRng;
use rand::rngs::Xoshiro256PlusPlus;
use sparrow::config::DEFAULT_SPARROW_CONFIG;
use sparrow::optimizer::explore::{ExplorationTraceEvent, exploration_phase_with_trace};
use sparrow::optimizer::separator::Separator;
use sparrow::sample::search::SampleConfig;
use sparrow::util::listener::{ReportType, SolutionListener};
use sparrow::util::terminator::Terminator;
use std::cell::Cell;
use std::fmt::Write as _;
use std::panic::{AssertUnwindSafe, catch_unwind};
use std::path::Path;
use std::time::Duration;

const REFERENCE: &str = include_str!("../../Fixtures/ExplorationEmptyLargeRustTrace.tsv");

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

    fn timeout_at(&self) -> Option<jagua_rs::Instant> {
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
fn unplaced_cutoff_item_leaves_empty_large_pool_and_panics_before_moves() {
    let (external, _) =
        sparrow::util::io::read_spp_input(Path::new("../Fixtures/ExplorationEmptyLarge.json"))
            .expect("read empty-large fixture");
    let defaults = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        defaults.cde_config,
        defaults.poly_simpl_tolerance,
        defaults.narrow_concavity_cutoff_ratio,
    );
    let instance = import_instance(&importer, &external).expect("import fixture");
    let mut problem = SPProblem::new(instance).expect("create problem");
    problem.change_strip_width(8.0).expect("set initial width");
    problem.place_item(SPPlacement {
        item_idx: 1,
        d_transf: DTransformation::new(0.0, (1.5, 2.5)),
    });
    problem.place_item(SPPlacement {
        item_idx: 2,
        d_transf: DTransformation::new(0.0, (6.5, 2.5)),
    });
    let initial = problem.save();
    assert!(
        Layout::from_snapshot(&initial.layout_snapshot).is_feasible(),
        "the partial initial layout must be geometrically feasible"
    );
    assert_eq!(initial.layout_snapshot.placed_items.len(), 2);

    let mut config = defaults.expl_cfg;
    config.shrink_step = 0.5;
    config.large_item_ch_area_cutoff_percentile = 0.75;
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
    let terminator = StopAfterIterations {
        calls: Cell::new(0),
        limit: 2,
    };
    let mut events = Vec::new();
    let result = catch_unwind(AssertUnwindSafe(|| {
        exploration_phase_with_trace(
            &mut separator,
            &mut listener,
            &terminator,
            &config,
            |event| events.push(event),
        )
    }));

    let payload = result.expect_err("Rust must panic when no placed item meets cutoff");
    let message = payload
        .downcast_ref::<String>()
        .map(String::as_str)
        .or_else(|| payload.downcast_ref::<&str>().copied())
        .expect("empty-large panic payload must be a string");
    assert_eq!(message, "[DSRP] failed to choose first item");
    assert_eq!(
        listener.reports,
        vec![ReportType::ExplFeas, ReportType::ExplInfeas],
        "the failure report is emitted before disruption panics"
    );
    let mut actual_trace = String::new();
    for event in &events {
        match event {
            ExplorationTraceEvent::PoolInsert {
                failure_id,
                insertion_index,
                pool_order,
            } => {
                write!(
                    &mut actual_trace,
                    "insert {failure_id} {insertion_index} {}",
                    pool_order.len()
                )
                .unwrap();
                for candidate_id in pool_order {
                    write!(&mut actual_trace, " {candidate_id}").unwrap();
                }
                actual_trace.push('\n');
            }
            ExplorationTraceEvent::PoolSample {
                failure_id,
                raw_sample,
                mapped_sample,
                selected_index,
                consumed_u64_words,
                first_layer_index,
                following_u64,
            } => {
                write!(
                    &mut actual_trace,
                    "sample {failure_id} {:08x} {:08x} {selected_index} {consumed_u64_words} {first_layer_index:02x}",
                    raw_sample.to_bits(),
                    mapped_sample.to_bits(),
                )
                .unwrap();
                for value in following_u64 {
                    write!(&mut actual_trace, " {value:016x}").unwrap();
                }
                actual_trace.push('\n');
            }
            ExplorationTraceEvent::DisruptionMove { .. } => {
                panic!("Rust must panic before any disruption move")
            }
        }
    }
    assert_eq!(
        actual_trace,
        REFERENCE.replace("\r\n", "\n"),
        "Rust pre-panic pool/RNG trace changed"
    );

    let after_panic = separator.prob.save();
    assert_eq!(after_panic.strip_width(), 4.0);
    let after_layout = Layout::from_snapshot(&after_panic.layout_snapshot);
    assert!(!after_layout.is_feasible());
    let mut poses = after_panic
        .layout_snapshot
        .placed_items
        .values()
        .map(|placed| {
            let (x, y) = placed.d_transf.translation();
            (placed.item.idx, placed.d_transf.rotation(), x, y)
        })
        .collect::<Vec<_>>();
    poses.sort_by_key(|pose| pose.0);
    assert_eq!(
        poses,
        vec![(1, 0.0, 1.5, 2.5), (2, 0.0, 2.5, 2.5)],
        "the caught panic must leave the separator at its final failed snapshot"
    );
}
