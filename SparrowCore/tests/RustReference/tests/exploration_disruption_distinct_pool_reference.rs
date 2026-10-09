use jagua_rs::entities::Layout;
use jagua_rs::geometry::DTransformation;
use jagua_rs::io::import::Importer;
use jagua_rs::probs::spp::entities::{SPPlacement, SPProblem, SPSolution};
use jagua_rs::probs::spp::io::import_instance;
use rand::SeedableRng;
use rand::rngs::Xoshiro256PlusPlus;
use sparrow::config::DEFAULT_SPARROW_CONFIG;
use sparrow::optimizer::explore::{
    DisruptionMoveKind, ExplorationTraceEvent, exploration_phase_with_trace,
};
use sparrow::optimizer::separator::Separator;
use sparrow::quantify::tracker::CollisionTracker;
use sparrow::sample::search::SampleConfig;
use sparrow::util::listener::{ReportType, SolutionListener};
use sparrow::util::terminator::Terminator;
use std::cell::Cell;
use std::error::Error;
use std::fmt::Write as _;
use std::path::Path;
use std::time::Duration;

const TWO_CANDIDATE_REFERENCE: &str =
    include_str!("../../Fixtures/ExplorationDistinctPoolRustTrace.tsv");
const THREE_CANDIDATE_REFERENCE: &str =
    include_str!("../../Fixtures/ExplorationDistinctPoolThreeCandidatesRustTrace.tsv");

#[derive(Default)]
struct TraceListener {
    failures: Vec<(f32, Vec<(usize, f32, f32, f32)>)>,
}

impl SolutionListener for TraceListener {
    fn report(&mut self, report: ReportType, solution: &SPSolution) {
        if report != ReportType::ExplInfeas {
            return;
        }
        let layout = Layout::from_snapshot(&solution.layout_snapshot);
        let loss = CollisionTracker::new(&layout).get_total_loss();
        let mut poses = solution
            .layout_snapshot
            .placed_items
            .values()
            .map(|placed| {
                let (x, y) = placed.d_transf.translation();
                (placed.item.idx, placed.d_transf.rotation(), x, y)
            })
            .collect::<Vec<_>>();
        poses.sort_by_key(|pose| pose.0);
        self.failures.push((loss, poses));
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

    fn timeout_at(&self) -> Option<jagua_rs::Instant> {
        None
    }
}

fn trace(
    case_name: &str,
    seed: u64,
    expected_first_swap: (usize, usize),
    expected_first_candidate_count: usize,
) -> Result<String, Box<dyn Error>> {
    let (external, _) =
        sparrow::util::io::read_spp_input(Path::new("../Fixtures/ExplorationDistinctPool.json"))?;
    let defaults = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        defaults.cde_config,
        defaults.poly_simpl_tolerance,
        defaults.narrow_concavity_cutoff_ratio,
    );
    let instance = import_instance(&importer, &external)?;
    let mut problem = SPProblem::new(instance.clone())?;
    let initial_width = 10.0_f32;
    problem.change_strip_width(initial_width)?;
    for (item_idx, x) in [0.75_f32, 2.0, 4.0, 7.5].into_iter().enumerate() {
        problem.place_item(SPPlacement {
            item_idx,
            d_transf: DTransformation::new(0.0, (x, 2.5_f32)),
        });
    }
    let initial = problem.save();
    let initial_layout = Layout::from_snapshot(&initial.layout_snapshot);
    assert!(
        initial_layout.is_feasible(),
        "initial layout must be feasible"
    );
    assert_eq!(CollisionTracker::new(&initial_layout).get_total_loss(), 0.0);

    let placed = initial
        .layout_snapshot
        .placed_items
        .values()
        .collect::<Vec<_>>();
    assert_eq!(placed.len(), 4, "fixture must contain four placed items");
    let mut hull_areas = placed
        .iter()
        .map(|placed| placed.shape.surrogate().convex_hull_area)
        .collect::<Vec<_>>();
    hull_areas.sort_by(|left, right| right.total_cmp(left));
    let total_hull_area = hull_areas.iter().sum::<f32>();
    assert_eq!(hull_areas, [16.0, 4.0, 1.0, 1.0]);
    assert_eq!(total_hull_area, 22.0);
    let mut cumulative_hull_area = 0.0_f32;
    for hull_area in &hull_areas {
        cumulative_hull_area += *hull_area;
        assert!(
            cumulative_hull_area <= total_hull_area,
            "100% cutoff must leave all four items eligible under strict >"
        );
    }
    let expected_distinct_candidate_counts = [2, 2, 3, 3];
    for first_index in 0..placed.len() {
        let mut distinct_candidate_count = 0;
        for candidate_index in 0..placed.len() {
            if candidate_index == first_index {
                continue;
            }
            let first = placed[first_index];
            let candidate = placed[candidate_index];
            let area_epsilon = first.shape.area * 0.01;
            let diameter_epsilon = first.shape.diameter * 0.01;
            if (candidate.shape.area - first.shape.area).abs() > area_epsilon
                && (candidate.shape.diameter - first.shape.diameter).abs() > diameter_epsilon
            {
                distinct_candidate_count += 1;
            }
        }
        assert_eq!(
            distinct_candidate_count, expected_distinct_candidate_counts[first_index],
            "the fixture must distinguish the strict distinct pool from all other items"
        );
    }

    let shrink_step = 0.1_f32;
    let iteration_limit = 4_usize;
    let pool_stddev = 0.25_f32;
    let mut config = defaults.expl_cfg;
    config.shrink_step = shrink_step;
    config.large_item_ch_area_cutoff_percentile = 1.0;
    config.solution_pool_distribution_stddev = pool_stddev;
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
        Xoshiro256PlusPlus::seed_from_u64(seed),
        config.separator_config,
    );
    let mut listener = TraceListener::default();
    let terminator = StopAfterIterations {
        calls: Cell::new(0),
        limit: iteration_limit,
    };
    let mut events = Vec::new();
    exploration_phase_with_trace(
        &mut separator,
        &mut listener,
        &terminator,
        &config,
        |event| events.push(event),
    );

    let first_failure_swaps = events
        .iter()
        .filter_map(|event| match event {
            ExplorationTraceEvent::DisruptionMove {
                failure_id: 0,
                kind: DisruptionMoveKind::Swap,
                item_index,
                related_item_index,
                ..
            } => Some((*item_index, *related_item_index)),
            _ => None,
        })
        .collect::<Vec<_>>();
    assert_eq!(
        first_failure_swaps.len(),
        2,
        "first failure must complete one distinct-pool swap"
    );
    assert_eq!(first_failure_swaps[0], expected_first_swap);
    assert_eq!(
        expected_distinct_candidate_counts[first_failure_swaps[0].0],
        expected_first_candidate_count,
        "the actual first disruption must exercise the requested distinct-pool size"
    );
    assert_ne!(
        first_failure_swaps[0].0, first_failure_swaps[0].1,
        "second item must come from the non-empty distinct pool"
    );
    assert_eq!(first_failure_swaps[0].0, first_failure_swaps[1].1);
    assert_eq!(first_failure_swaps[0].1, first_failure_swaps[1].0);

    let mut output = String::from(
        "# generator=actual Sparrow Rust exploration_phase with pool observer\n\
         # input=ExplorationDistinctPool.json; duplicate small squares produce two- or three-item distinct pools depending on the first choice\n\
         # case seed initial_width shrink_step iteration_limit pool_stddev failure_count item_count\n\
         # failure ordinal total_loss item_count\n\
         # pose item_index rotation_rad x y\n\
         # move failure_id kind item_index related_item_index from_rotation from_x from_y to_rotation to_x to_y\n\
         # insert failure_id insertion_index pool_size candidate_failure_ids...\n\
         # sample failure_id raw_normal_bits mapped_sample_bits selected_index consumed_u64_words first_layer next_u64_after_sample[0..3]\n",
    );
    writeln!(
        &mut output,
        "case {case_name} {seed} {initial_width:.9} {shrink_step:.9} {iteration_limit} {pool_stddev:.9} {} {}",
        listener.failures.len(),
        instance.items.len(),
    )?;
    for (failure_id, (loss, poses)) in listener.failures.iter().enumerate() {
        writeln!(
            &mut output,
            "failure {failure_id} {loss:.9} {}",
            poses.len(),
        )?;
        for (item_index, rotation, x, y) in poses {
            writeln!(&mut output, "pose {item_index} {rotation:.9} {x:.9} {y:.9}")?;
        }
    }
    for event in &events {
        match event {
            ExplorationTraceEvent::PoolInsert {
                failure_id,
                insertion_index,
                pool_order,
            } => {
                write!(
                    &mut output,
                    "insert {failure_id} {insertion_index} {}",
                    pool_order.len(),
                )?;
                for candidate_id in pool_order {
                    write!(&mut output, " {candidate_id}")?;
                }
                output.push('\n');
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
                    &mut output,
                    "sample {failure_id} {:08x} {:08x} {selected_index} {consumed_u64_words} {first_layer_index:02x}",
                    raw_sample.to_bits(),
                    mapped_sample.to_bits(),
                )?;
                for value in following_u64 {
                    write!(&mut output, " {value:016x}")?;
                }
                output.push('\n');
            }
            ExplorationTraceEvent::DisruptionMove {
                failure_id,
                kind,
                item_index,
                related_item_index,
                from,
                to,
            } => {
                let (from_x, from_y) = from.translation();
                let (to_x, to_y) = to.translation();
                let kind = match kind {
                    DisruptionMoveKind::Swap => "swap",
                    DisruptionMoveKind::ContainedRelocation => "contained",
                };
                writeln!(
                    &mut output,
                    "move {failure_id} {kind} {item_index} {related_item_index} {:.9} {:.9} {:.9} {:.9} {:.9} {:.9}",
                    from.rotation(),
                    from_x,
                    from_y,
                    to.rotation(),
                    to_x,
                    to_y,
                )?;
            }
        }
    }
    Ok(output)
}

#[test]
fn nonempty_distinct_pool_with_multiple_candidates_matches_rust_trace() {
    let actual = trace("distinct_pool", 43, (0, 3), 2)
        .expect("generate actual Explore two-candidate distinct-pool trace");
    assert_eq!(
        actual, TWO_CANDIDATE_REFERENCE,
        "actual Rust Explore non-empty distinct-pool trace changed"
    );
}

#[test]
fn nonempty_distinct_pool_with_three_candidates_matches_rust_trace() {
    let actual = trace("distinct_pool_three", 10, (3, 0), 3)
        .expect("generate actual Explore three-candidate distinct-pool trace");
    assert_eq!(
        actual, THREE_CANDIDATE_REFERENCE,
        "actual Rust Explore three-candidate distinct-pool trace changed"
    );
}
