use jagua_rs::entities::Layout;
use jagua_rs::geometry::DTransformation;
use jagua_rs::geometry::geo_enums::RotationRange;
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
const SINGLE_CANDIDATE_REFERENCE: &str =
    include_str!("../../Fixtures/ExplorationDistinctPoolSingleCandidateRustTrace.tsv");
const THREE_CANDIDATE_REFERENCE: &str =
    include_str!("../../Fixtures/ExplorationDistinctPoolThreeCandidatesRustTrace.tsv");
const FOUR_CANDIDATE_REFERENCE: &str =
    include_str!("../../Fixtures/ExplorationDistinctPoolFourCandidatesRustTrace.tsv");
const FIVE_CANDIDATE_REFERENCE: &str =
    include_str!("../../Fixtures/ExplorationDistinctPoolFiveCandidatesRustTrace.tsv");
const SIX_CANDIDATE_REFERENCE: &str =
    include_str!("../../Fixtures/ExplorationDistinctPoolSixCandidatesRustTrace.tsv");
const SEVEN_CANDIDATE_REFERENCE: &str =
    include_str!("../../Fixtures/ExplorationDistinctPoolSevenCandidatesRustTrace.tsv");
const EIGHT_CANDIDATE_REFERENCE: &str =
    include_str!("../../Fixtures/ExplorationDistinctPoolEightCandidatesRustTrace.tsv");
const NINE_CANDIDATE_REFERENCE: &str =
    include_str!("../../Fixtures/ExplorationDistinctPoolNineCandidatesRustTrace.tsv");
const TEN_CANDIDATE_REFERENCE: &str =
    include_str!("../../Fixtures/ExplorationDistinctPoolTenCandidatesRustTrace.tsv");
const TWELVE_CANDIDATE_REFERENCE: &str =
    include_str!("../../Fixtures/ExplorationDistinctPoolTwelveCandidatesRustTrace.tsv");
const FOUR_CANDIDATE_NONE_ROTATION_REFERENCE: &str =
    include_str!("../../Fixtures/ExplorationDistinctPoolFourCandidatesNoneRotationRustTrace.tsv");

fn assert_trace_matches_reference(actual: &str, reference: &str, label: &str) {
    let normalized_reference = reference.replace("\r\n", "\n");
    assert_eq!(actual, normalized_reference, "{label}");
}

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
    input_filename: &str,
    seed: u64,
    expected_first_swap: (usize, usize),
    expected_first_candidate_count: usize,
    initial_width: f32,
    initial_translations: &[(f32, f32)],
    expected_hull_areas: &[f32],
    expected_distinct_candidate_counts: &[usize],
) -> Result<String, Box<dyn Error>> {
    let input_path = Path::new("../Fixtures").join(input_filename);
    let (external, _) = sparrow::util::io::read_spp_input(&input_path)?;
    let defaults = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        defaults.cde_config,
        defaults.poly_simpl_tolerance,
        defaults.narrow_concavity_cutoff_ratio,
    );
    let instance = import_instance(&importer, &external)?;
    let mut problem = SPProblem::new(instance.clone())?;
    problem.change_strip_width(initial_width)?;
    for (item_idx, (x, y)) in initial_translations.iter().copied().enumerate() {
        problem.place_item(SPPlacement {
            item_idx,
            d_transf: DTransformation::new(0.0, (x, y)),
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
    assert_eq!(placed.len(), initial_translations.len());
    let mut hull_areas = placed
        .iter()
        .map(|placed| placed.shape.surrogate().convex_hull_area)
        .collect::<Vec<_>>();
    hull_areas.sort_by(|left, right| right.total_cmp(left));
    let total_hull_area = hull_areas.iter().sum::<f32>();
    assert_eq!(hull_areas, expected_hull_areas);
    assert_eq!(total_hull_area, expected_hull_areas.iter().sum::<f32>());
    let mut cumulative_hull_area = 0.0_f32;
    for hull_area in &hull_areas {
        cumulative_hull_area += *hull_area;
        assert!(
            cumulative_hull_area <= total_hull_area,
            "100% cutoff must leave every item eligible under strict >"
        );
    }
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

    let input_description = match input_filename {
        "ExplorationDistinctPoolSingleCandidate.json" => {
            "two distinct-size squares provide one alternative for either first choice"
        }
        "ExplorationDistinctPool.json" => {
            "duplicate small squares produce two- or three-item distinct pools depending on the first choice"
        }
        "ExplorationDistinctPoolFourCandidates.json" => {
            "five distinct square sizes provide four distinct alternatives for each first choice"
        }
        "ExplorationDistinctPoolFiveCandidates.json" => {
            "six distinct square sizes provide five distinct alternatives for each first choice"
        }
        "ExplorationDistinctPoolSixCandidates.json" => {
            "seven distinct square sizes provide six distinct alternatives for each first choice"
        }
        "ExplorationDistinctPoolSevenCandidates.json" => {
            "eight distinct square sizes provide seven distinct alternatives for each first choice"
        }
        "ExplorationDistinctPoolEightCandidates.json" => {
            "nine distinct square sizes provide eight distinct alternatives for each first choice"
        }
        "ExplorationDistinctPoolNineCandidates.json" => {
            "ten distinct square sizes provide nine distinct alternatives for each first choice"
        }
        "ExplorationDistinctPoolTenCandidates.json" => {
            "eleven distinct square sizes provide ten distinct alternatives for each first choice"
        }
        "ExplorationDistinctPoolTwelveCandidates.json" => {
            "thirteen distinct square sizes provide twelve distinct alternatives for each first choice"
        }
        "ExplorationDistinctPoolFourCandidatesNoneRotation.json" => {
            "five squares use stepped 360-degree input, which imports as RotationRange::None"
        }
        _ => "fixed Rust/C++ Explore fixture",
    };
    let mut output = format!(
        "# generator=actual Sparrow Rust exploration_phase with pool observer\n\
         # input={input_filename}; {input_description}\n\
         # case seed initial_width shrink_step iteration_limit pool_stddev failure_count item_count\n\
         # failure ordinal total_loss item_count\n\
         # pose item_index rotation_rad x y\n\
         # move failure_id kind item_index related_item_index from_rotation from_x from_y to_rotation to_x to_y\n\
         # insert failure_id insertion_index pool_size candidate_failure_ids...\n\
         # sample failure_id raw_normal_bits mapped_sample_bits selected_index consumed_u64_words first_layer next_u64_after_sample[0..3]\n"
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
fn nonempty_distinct_pool_with_single_candidate_matches_rust_trace() {
    let actual = trace(
        "distinct_pool_single",
        "ExplorationDistinctPoolSingleCandidate.json",
        43,
        (0, 1),
        1,
        10.0,
        &[(6.0, 2.5), (3.5, 2.5)],
        &[9.0, 1.0],
        &[1, 1],
    )
    .expect("generate actual Explore one-candidate distinct-pool trace");
    assert_trace_matches_reference(
        &actual,
        SINGLE_CANDIDATE_REFERENCE,
        "actual Rust Explore one-candidate distinct-pool trace changed",
    );
}

#[test]
fn nonempty_distinct_pool_with_multiple_candidates_matches_rust_trace() {
    let actual = trace(
        "distinct_pool",
        "ExplorationDistinctPool.json",
        43,
        (0, 3),
        2,
        10.0,
        &[(0.75, 2.5), (2.0, 2.5), (4.0, 2.5), (7.5, 2.5)],
        &[16.0, 4.0, 1.0, 1.0],
        &[2, 2, 3, 3],
    )
    .expect("generate actual Explore two-candidate distinct-pool trace");
    assert_trace_matches_reference(
        &actual,
        TWO_CANDIDATE_REFERENCE,
        "actual Rust Explore non-empty distinct-pool trace changed",
    );
}

#[test]
fn nonempty_distinct_pool_with_three_candidates_matches_rust_trace() {
    let actual = trace(
        "distinct_pool_three",
        "ExplorationDistinctPool.json",
        10,
        (3, 0),
        3,
        10.0,
        &[(0.75, 2.5), (2.0, 2.5), (4.0, 2.5), (7.5, 2.5)],
        &[16.0, 4.0, 1.0, 1.0],
        &[2, 2, 3, 3],
    )
    .expect("generate actual Explore three-candidate distinct-pool trace");
    assert_trace_matches_reference(
        &actual,
        THREE_CANDIDATE_REFERENCE,
        "actual Rust Explore three-candidate distinct-pool trace changed",
    );
}

#[test]
fn nonempty_distinct_pool_with_four_candidates_matches_rust_trace() {
    let actual = trace(
        "distinct_pool_four",
        "ExplorationDistinctPoolFourCandidates.json",
        0,
        (3, 1),
        4,
        35.0,
        &[
            (1.0, 9.0),
            (2.75, 9.0),
            (6.0, 9.0),
            (12.25, 9.0),
            (24.5, 9.0),
        ],
        &[256.0, 64.0, 16.0, 4.0, 1.0],
        &[4, 4, 4, 4, 4],
    )
    .expect("generate actual Explore four-candidate distinct-pool trace");
    assert_trace_matches_reference(
        &actual,
        FOUR_CANDIDATE_REFERENCE,
        "actual Rust Explore four-candidate distinct-pool trace changed",
    );
}

#[test]
fn nonempty_distinct_pool_with_five_candidates_matches_rust_trace() {
    let actual = trace(
        "distinct_pool_five",
        "ExplorationDistinctPoolFiveCandidates.json",
        0,
        (3, 1),
        5,
        70.0,
        &[
            (1.0, 18.0),
            (2.75, 18.0),
            (6.0, 18.0),
            (12.25, 18.0),
            (24.5, 18.0),
            (48.75, 18.0),
        ],
        &[1024.0, 256.0, 64.0, 16.0, 4.0, 1.0],
        &[5, 5, 5, 5, 5, 5],
    )
    .expect("generate actual Explore five-candidate distinct-pool trace");
    assert_trace_matches_reference(
        &actual,
        FIVE_CANDIDATE_REFERENCE,
        "actual Rust Explore five-candidate distinct-pool trace changed",
    );
}

#[test]
fn nonempty_distinct_pool_with_six_candidates_matches_rust_trace() {
    let actual = trace(
        "distinct_pool_six",
        "ExplorationDistinctPoolSixCandidates.json",
        0,
        (3, 1),
        6,
        139.0,
        &[
            (1.0, 34.0),
            (2.75, 34.0),
            (6.0, 34.0),
            (12.25, 34.0),
            (24.5, 34.0),
            (48.75, 34.0),
            (97.0, 34.0),
        ],
        &[4096.0, 1024.0, 256.0, 64.0, 16.0, 4.0, 1.0],
        &[6, 6, 6, 6, 6, 6, 6],
    )
    .expect("generate actual Explore six-candidate distinct-pool trace");
    assert_trace_matches_reference(
        &actual,
        SIX_CANDIDATE_REFERENCE,
        "actual Rust Explore six-candidate distinct-pool trace changed",
    );
}

#[test]
fn nonempty_distinct_pool_with_seven_candidates_matches_rust_trace() {
    let actual = trace(
        "distinct_pool_seven",
        "ExplorationDistinctPoolSevenCandidates.json",
        0,
        (3, 1),
        7,
        276.0,
        &[
            (1.0, 66.0),
            (2.75, 66.0),
            (6.0, 66.0),
            (12.25, 66.0),
            (24.5, 66.0),
            (48.75, 66.0),
            (97.0, 66.0),
            (193.25, 66.0),
        ],
        &[16384.0, 4096.0, 1024.0, 256.0, 64.0, 16.0, 4.0, 1.0],
        &[7, 7, 7, 7, 7, 7, 7, 7],
    )
    .expect("generate actual Explore seven-candidate distinct-pool trace");
    assert_trace_matches_reference(
        &actual,
        SEVEN_CANDIDATE_REFERENCE,
        "actual Rust Explore seven-candidate distinct-pool trace changed",
    );
}

#[test]
fn nonempty_distinct_pool_with_eight_candidates_matches_rust_trace() {
    let actual = trace(
        "distinct_pool_eight",
        "ExplorationDistinctPoolEightCandidates.json",
        0,
        (3, 1),
        8,
        548.0,
        &[
            (1.0, 130.0),
            (2.75, 130.0),
            (6.0, 130.0),
            (12.25, 130.0),
            (24.5, 130.0),
            (48.75, 130.0),
            (97.0, 130.0),
            (193.25, 130.0),
            (385.5, 130.0),
        ],
        &[
            65536.0, 16384.0, 4096.0, 1024.0, 256.0, 64.0, 16.0, 4.0, 1.0,
        ],
        &[8, 8, 8, 8, 8, 8, 8, 8, 8],
    )
    .expect("generate actual Explore eight-candidate distinct-pool trace");
    assert_trace_matches_reference(
        &actual,
        EIGHT_CANDIDATE_REFERENCE,
        "actual Rust Explore eight-candidate distinct-pool trace changed",
    );
}

#[test]
fn nonempty_distinct_pool_with_nine_candidates_matches_rust_trace() {
    let actual = trace(
        "distinct_pool_nine",
        "ExplorationDistinctPoolNineCandidates.json",
        0,
        (3, 1),
        9,
        1096.0,
        &[
            (1.0, 258.0),
            (2.75, 258.0),
            (6.0, 258.0),
            (12.25, 258.0),
            (24.5, 258.0),
            (48.75, 258.0),
            (97.0, 258.0),
            (193.25, 258.0),
            (385.5, 258.0),
            (769.75, 258.0),
        ],
        &[
            262144.0, 65536.0, 16384.0, 4096.0, 1024.0, 256.0, 64.0, 16.0, 4.0, 1.0,
        ],
        &[9, 9, 9, 9, 9, 9, 9, 9, 9, 9],
    )
    .expect("generate actual Explore nine-candidate distinct-pool trace");
    assert_trace_matches_reference(
        &actual,
        NINE_CANDIDATE_REFERENCE,
        "actual Rust Explore nine-candidate distinct-pool trace changed",
    );
}

#[test]
fn nonempty_distinct_pool_with_ten_candidates_matches_rust_trace() {
    let actual = trace(
        "distinct_pool_ten",
        "ExplorationDistinctPoolTenCandidates.json",
        0,
        (3, 1),
        10,
        2192.0,
        &[
            (1.0, 514.0),
            (2.75, 514.0),
            (6.0, 514.0),
            (12.25, 514.0),
            (24.5, 514.0),
            (48.75, 514.0),
            (97.0, 514.0),
            (193.25, 514.0),
            (385.5, 514.0),
            (769.75, 514.0),
            (1538.0, 514.0),
        ],
        &[
            1048576.0, 262144.0, 65536.0, 16384.0, 4096.0, 1024.0, 256.0, 64.0, 16.0, 4.0, 1.0,
        ],
        &[10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10],
    )
    .expect("generate actual Explore ten-candidate distinct-pool trace");
    assert_trace_matches_reference(
        &actual,
        TEN_CANDIDATE_REFERENCE,
        "actual Rust Explore ten-candidate distinct-pool trace changed",
    );
}

#[test]
fn nonempty_distinct_pool_with_twelve_candidates_matches_rust_trace() {
    let actual = trace(
        "distinct_pool_twelve",
        "ExplorationDistinctPoolTwelveCandidates.json",
        0,
        (3, 1),
        12,
        95.0,
        &[
            (1.0, 8.0),
            (2.75, 8.0),
            (5.5, 8.0),
            (9.25, 8.0),
            (14.0, 8.0),
            (19.75, 8.0),
            (26.5, 8.0),
            (34.25, 8.0),
            (43.0, 8.0),
            (52.75, 8.0),
            (63.5, 8.0),
            (75.25, 8.0),
            (88.0, 8.0),
        ],
        &[
            169.0, 144.0, 121.0, 100.0, 81.0, 64.0, 49.0, 36.0, 25.0, 16.0, 9.0, 4.0, 1.0,
        ],
        &[12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12],
    )
    .expect("generate actual Explore twelve-candidate distinct-pool trace");
    assert_trace_matches_reference(
        &actual,
        TWELVE_CANDIDATE_REFERENCE,
        "actual Rust Explore twelve-candidate distinct-pool trace changed",
    );
}

#[test]
fn none_rotation_distinct_pool_disruption_matches_rust_trace() {
    let input_filename = "ExplorationDistinctPoolFourCandidatesNoneRotation.json";
    let input_path = Path::new("../Fixtures").join(input_filename);
    let (external, _) =
        sparrow::util::io::read_spp_input(&input_path).expect("read None-rotation Explore fixture");
    let defaults = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        defaults.cde_config,
        defaults.poly_simpl_tolerance,
        defaults.narrow_concavity_cutoff_ratio,
    );
    let instance =
        import_instance(&importer, &external).expect("import None-rotation Explore fixture");
    assert!(
        instance
            .items
            .iter()
            .all(|(item, _)| item.allowed_rotation == RotationRange::None),
        "stepped 360-degree inputs must import as RotationRange::None"
    );

    let actual = trace(
        "distinct_pool_four_none_rotation",
        input_filename,
        0,
        (3, 1),
        4,
        35.0,
        &[
            (1.0, 9.0),
            (2.75, 9.0),
            (6.0, 9.0),
            (12.25, 9.0),
            (24.5, 9.0),
        ],
        &[256.0, 64.0, 16.0, 4.0, 1.0],
        &[4, 4, 4, 4, 4],
    )
    .expect("generate actual Explore None-rotation disruption trace");
    for line in actual.lines() {
        if line.starts_with("pose ") {
            assert_eq!(
                line.split_whitespace().nth(2),
                Some("0.000000000"),
                "None-rotation failure snapshots must stay at zero: {line}"
            );
        } else if line.starts_with("move ") {
            let fields = line.split_whitespace().collect::<Vec<_>>();
            assert_eq!(fields[5], "0.000000000", "None move source angle: {line}");
            assert_eq!(fields[8], "0.000000000", "None move target angle: {line}");
        }
    }
    assert_trace_matches_reference(
        &actual,
        FOUR_CANDIDATE_NONE_ROTATION_REFERENCE,
        "actual Rust Explore None-rotation trace changed",
    );
}
