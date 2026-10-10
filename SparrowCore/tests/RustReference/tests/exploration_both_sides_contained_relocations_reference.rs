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

const REFERENCE: &str =
    include_str!("../../Fixtures/ExplorationBothSidesContainedRelocationsRustTrace.tsv");
const OBLIQUE_REFERENCE: &str =
    include_str!("../../Fixtures/ExplorationBothSidesObliqueContainedRelocationsRustTrace.tsv");
const OBLIQUE_TWO_ROUNDS_REFERENCE: &str = include_str!(
    "../../Fixtures/ExplorationBothSidesObliqueContainedRelocationsTwoRoundsRustTrace.tsv"
);
const OBLIQUE_LIMIT4_RELEASE_REFERENCE: &str = include_str!(
    "../../Fixtures/ExplorationBothSidesObliqueContainedRelocationsLimit4RustReleaseTrace.tsv"
);
const OBLIQUE_LIMIT5_RELEASE_REFERENCE: &str = include_str!(
    "../../Fixtures/ExplorationBothSidesObliqueContainedRelocationsLimit5RustReleaseTrace.tsv"
);
const OBLIQUE_LIMIT6_RELEASE_REFERENCE: &str = include_str!(
    "../../Fixtures/ExplorationBothSidesObliqueContainedRelocationsLimit6RustReleaseTrace.tsv"
);
const INDEPENDENT_GEOMETRY_REFERENCE: &str = include_str!(
    "../../Fixtures/ExplorationBothSidesObliqueContainedRelocationsIndependentGeometryRustTrace.tsv"
);
const INDEPENDENT_GEOMETRY_LIMIT3_REFERENCE: &str = include_str!(
    "../../Fixtures/ExplorationBothSidesObliqueContainedRelocationsIndependentGeometryLimit3RustTrace.tsv"
);
const INDEPENDENT_GEOMETRY_LIMIT4_REFERENCE: &str = include_str!(
    "../../Fixtures/ExplorationBothSidesObliqueContainedRelocationsIndependentGeometryLimit4RustTrace.tsv"
);
const INDEPENDENT_GEOMETRY_LIMIT5_REFERENCE: &str = include_str!(
    "../../Fixtures/ExplorationBothSidesObliqueContainedRelocationsIndependentGeometryLimit5RustTrace.tsv"
);
const INDEPENDENT_GEOMETRY_LIMIT6_REFERENCE: &str = include_str!(
    "../../Fixtures/ExplorationBothSidesObliqueContainedRelocationsIndependentGeometryLimit6RustTrace.tsv"
);
const INDEPENDENT_GEOMETRY_SEED11_LIMIT6_REFERENCE: &str = include_str!(
    "../../Fixtures/ExplorationBothSidesObliqueContainedRelocationsIndependentGeometrySeed11Limit6RustTrace.tsv"
);
const INDEPENDENT_GEOMETRY_SEED12_LIMIT6_REFERENCE: &str = include_str!(
    "../../Fixtures/ExplorationBothSidesObliqueContainedRelocationsIndependentGeometrySeed12Limit6RustTrace.tsv"
);
const INDEPENDENT_GEOMETRY_SEED13_LIMIT6_REFERENCE: &str = include_str!(
    "../../Fixtures/ExplorationBothSidesObliqueContainedRelocationsIndependentGeometrySeed13Limit6RustTrace.tsv"
);
const INDEPENDENT_GEOMETRY_SEED14_LIMIT6_REFERENCE: &str = include_str!(
    "../../Fixtures/ExplorationBothSidesObliqueContainedRelocationsIndependentGeometrySeed14Limit6RustTrace.tsv"
);
const SECOND_INDEPENDENT_GEOMETRY_SEED10_LIMIT6_REFERENCE: &str = include_str!(
    "../../Fixtures/ExplorationBothSidesObliqueContainedRelocationsSecondIndependentGeometrySeed10Limit6RustTrace.tsv"
);
const SECOND_INDEPENDENT_GEOMETRY_SEED11_LIMIT6_REFERENCE: &str = include_str!(
    "../../Fixtures/ExplorationBothSidesObliqueContainedRelocationsSecondIndependentGeometrySeed11Limit6RustTrace.tsv"
);
const SECOND_INDEPENDENT_GEOMETRY_SEED12_LIMIT6_REFERENCE: &str = include_str!(
    "../../Fixtures/ExplorationBothSidesObliqueContainedRelocationsSecondIndependentGeometrySeed12Limit6RustTrace.tsv"
);
const SECOND_INDEPENDENT_GEOMETRY_SEED13_LIMIT6_REFERENCE: &str = include_str!(
    "../../Fixtures/ExplorationBothSidesObliqueContainedRelocationsSecondIndependentGeometrySeed13Limit6RustTrace.tsv"
);
const SECOND_INDEPENDENT_GEOMETRY_SEED14_LIMIT6_REFERENCE: &str = include_str!(
    "../../Fixtures/ExplorationBothSidesObliqueContainedRelocationsSecondIndependentGeometrySeed14Limit6RustTrace.tsv"
);
const CONTINUOUS_ROTATION_REFERENCE: &str =
    include_str!("../../Fixtures/ExplorationObliqueContainedRelocationContinuousRustTrace.tsv");

#[derive(Clone)]
struct PlacementTrace {
    item_index: usize,
    rotation: f32,
    x: f32,
    y: f32,
    bounds: [f32; 4],
}

#[derive(Default)]
struct TraceListener {
    failures: Vec<(f32, Vec<PlacementTrace>)>,
}

impl SolutionListener for TraceListener {
    fn report(&mut self, report: ReportType, solution: &SPSolution) {
        if report != ReportType::ExplInfeas {
            return;
        }
        let layout = Layout::from_snapshot(&solution.layout_snapshot);
        let loss = CollisionTracker::new(&layout).get_total_loss();
        let mut placements = solution
            .layout_snapshot
            .placed_items
            .values()
            .map(|placed| {
                let (x, y) = placed.d_transf.translation();
                let bounds = placed.shape.bbox;
                PlacementTrace {
                    item_index: placed.item.idx,
                    rotation: placed.d_transf.rotation(),
                    x,
                    y,
                    bounds: [bounds.x_min, bounds.y_min, bounds.x_max, bounds.y_max],
                }
            })
            .collect::<Vec<_>>();
        placements.sort_by_key(|placement| placement.item_index);
        self.failures.push((loss, placements));
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

fn actual_trace(
    input_filename: &str,
    case_name: &str,
    seed: u64,
    large_item_ch_area_cutoff_percentile: f32,
    iteration_limit: usize,
    initial_poses: &[(f32, f32, f32)],
) -> Result<(String, Vec<(usize, usize)>), Box<dyn Error>> {
    actual_trace_with_width(
        input_filename,
        case_name,
        seed,
        large_item_ch_area_cutoff_percentile,
        iteration_limit,
        9.9,
        initial_poses,
    )
}

fn actual_trace_with_width(
    input_filename: &str,
    case_name: &str,
    seed: u64,
    large_item_ch_area_cutoff_percentile: f32,
    iteration_limit: usize,
    initial_width: f32,
    initial_poses: &[(f32, f32, f32)],
) -> Result<(String, Vec<(usize, usize)>), Box<dyn Error>> {
    let input_path = format!("../Fixtures/{input_filename}");
    let (external, _) = sparrow::util::io::read_spp_input(Path::new(&input_path))?;
    let defaults = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        defaults.cde_config,
        defaults.poly_simpl_tolerance,
        defaults.narrow_concavity_cutoff_ratio,
    );
    let instance = import_instance(&importer, &external)?;
    let mut problem = SPProblem::new(instance.clone())?;
    problem.change_strip_width(initial_width)?;
    for (item_idx, (rotation, x, y)) in initial_poses.iter().copied().enumerate() {
        problem.place_item(SPPlacement {
            item_idx,
            d_transf: DTransformation::new(rotation, (x, y)),
        });
    }
    let initial = problem.save();
    let initial_layout = Layout::from_snapshot(&initial.layout_snapshot);
    assert!(
        initial_layout.is_feasible(),
        "initial layout must be feasible"
    );
    assert_eq!(CollisionTracker::new(&initial_layout).get_total_loss(), 0.0);

    let shrink_step = 0.1_f32;
    let pool_stddev = 0.25_f32;
    let mut config = defaults.expl_cfg;
    config.shrink_step = shrink_step;
    config.large_item_ch_area_cutoff_percentile = large_item_ch_area_cutoff_percentile;
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

    let first_failure_contained_moves = events
        .iter()
        .filter_map(|event| match event {
            ExplorationTraceEvent::DisruptionMove {
                failure_id: 0,
                kind: DisruptionMoveKind::ContainedRelocation,
                item_index,
                related_item_index,
                ..
            } => Some((*item_index, *related_item_index)),
            _ => None,
        })
        .collect::<Vec<_>>();

    let mut output = format!(
        "# generator=actual Sparrow Rust exploration_phase with pool observer\n\
         # input={input_filename}; actual Explore trace\n\
         # case seed initial_width shrink_step iteration_limit pool_stddev failure_count item_count\n\
         # failure ordinal total_loss item_count\n\
         # final total_loss item_count\n\
         # pose item_index rotation_rad x y x_min y_min x_max y_max\n\
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
    for (failure_id, (loss, placements)) in listener.failures.iter().enumerate() {
        writeln!(
            &mut output,
            "failure {failure_id} {loss:.9} {}",
            placements.len(),
        )?;
        for placement in placements {
            writeln!(
                &mut output,
                "pose {} {:.9} {:.9} {:.9} {:.9} {:.9} {:.9} {:.9}",
                placement.item_index,
                placement.rotation,
                placement.x,
                placement.y,
                placement.bounds[0],
                placement.bounds[1],
                placement.bounds[2],
                placement.bounds[3],
            )?;
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

    let final_solution = separator.prob.save();
    let final_layout = Layout::from_snapshot(&final_solution.layout_snapshot);
    let final_loss = CollisionTracker::new(&final_layout).get_total_loss();
    let mut final_placements = final_solution
        .layout_snapshot
        .placed_items
        .values()
        .map(|placed| {
            let (x, y) = placed.d_transf.translation();
            let bounds = placed.shape.bbox;
            PlacementTrace {
                item_index: placed.item.idx,
                rotation: placed.d_transf.rotation(),
                x,
                y,
                bounds: [bounds.x_min, bounds.y_min, bounds.x_max, bounds.y_max],
            }
        })
        .collect::<Vec<_>>();
    final_placements.sort_by_key(|placement| placement.item_index);
    writeln!(
        &mut output,
        "final {final_loss:.9} {}",
        final_placements.len(),
    )?;
    for placement in &final_placements {
        writeln!(
            &mut output,
            "pose {} {:.9} {:.9} {:.9} {:.9} {:.9} {:.9} {:.9}",
            placement.item_index,
            placement.rotation,
            placement.x,
            placement.y,
            placement.bounds[0],
            placement.bounds[1],
            placement.bounds[2],
            placement.bounds[3],
        )?;
    }
    Ok((output, first_failure_contained_moves))
}

#[test]
fn both_swapped_hosts_relocate_multiple_contained_children_in_one_failure() {
    let initial_poses = [
        (0.0, 2.8, 2.75),
        (0.0, 7.6, 2.75),
        (0.0, 6.8, 0.95),
        (0.0, 8.4, 0.95),
        (0.0, 2.8, 2.75),
        (0.0, 2.8, 3.5),
    ];
    let (actual, first_failure_moves) = actual_trace(
        "ExplorationBothSidesContainedRelocations.json",
        "both_sides",
        43,
        0.5,
        4,
        &initial_poses,
    )
    .expect("generate actual Rust two-sided relocation trace");
    assert_eq!(first_failure_moves, [(2, 0), (3, 0), (4, 1), (5, 1)]);
    assert_eq!(
        actual.lines().collect::<Vec<_>>(),
        REFERENCE.lines().collect::<Vec<_>>(),
    );
}

#[test]
fn both_swapped_hosts_relocate_multiple_children_with_oblique_rotation() {
    let angle = 37.0_f32.to_radians();
    let (sin_angle, cos_angle) = angle.sin_cos();
    let child_offset = 2.0_f32;
    let initial_poses = [
        (0.0, 2.8, 2.75),
        (angle, 7.6, 2.75),
        (
            0.0,
            7.6 + child_offset * cos_angle,
            2.75 + child_offset * sin_angle,
        ),
        (
            0.0,
            7.6 - child_offset * cos_angle,
            2.75 - child_offset * sin_angle,
        ),
        (0.0, 2.8, 2.75),
        (0.0, 2.8, 3.5),
    ];
    let (actual, first_failure_moves) = actual_trace(
        "ExplorationBothSidesObliqueContainedRelocations.json",
        "both_sides_oblique",
        43,
        0.5,
        2,
        &initial_poses,
    )
    .expect("generate Rust oblique two-sided relocation trace");
    assert_eq!(
        first_failure_moves,
        [(2, 0), (3, 0), (4, 1), (5, 1)],
        "expected both oblique-swapped hosts to relocate two children: {actual}"
    );
    assert_eq!(
        actual.lines().collect::<Vec<_>>(),
        OBLIQUE_REFERENCE.lines().collect::<Vec<_>>(),
    );
}

#[test]
fn both_swapped_hosts_relocate_multiple_children_across_two_failures() {
    let angle = 37.0_f32.to_radians();
    let (sin_angle, cos_angle) = angle.sin_cos();
    let child_offset = 2.0_f32;
    let initial_poses = [
        (0.0, 2.8, 2.75),
        (angle, 7.6, 2.75),
        (
            0.0,
            7.6 + child_offset * cos_angle,
            2.75 + child_offset * sin_angle,
        ),
        (
            0.0,
            7.6 - child_offset * cos_angle,
            2.75 - child_offset * sin_angle,
        ),
        (0.0, 2.8, 2.75),
        (0.0, 2.8, 3.5),
    ];
    let (actual, first_failure_moves) = actual_trace(
        "ExplorationBothSidesObliqueContainedRelocations.json",
        "both_sides_oblique_two_rounds",
        43,
        0.5,
        3,
        &initial_poses,
    )
    .expect("generate Rust oblique two-round relocation trace");
    assert_eq!(
        first_failure_moves,
        [(2, 0), (3, 0), (4, 1), (5, 1)],
        "expected both oblique-swapped hosts to relocate two children: {actual}"
    );
    assert_eq!(
        actual.lines().collect::<Vec<_>>(),
        OBLIQUE_TWO_ROUNDS_REFERENCE.lines().collect::<Vec<_>>(),
    );
}

#[test]
fn oblique_limit_four_profile_boundary_matches_rust_release_trace() {
    let angle = 37.0_f32.to_radians();
    let (sin_angle, cos_angle) = angle.sin_cos();
    let child_offset = 2.0_f32;
    let initial_poses = [
        (0.0, 2.8, 2.75),
        (angle, 7.6, 2.75),
        (
            0.0,
            7.6 + child_offset * cos_angle,
            2.75 + child_offset * sin_angle,
        ),
        (
            0.0,
            7.6 - child_offset * cos_angle,
            2.75 - child_offset * sin_angle,
        ),
        (0.0, 2.8, 2.75),
        (0.0, 2.8, 3.5),
    ];
    let generation = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        actual_trace(
            "ExplorationBothSidesObliqueContainedRelocations.json",
            "both_sides_oblique_limit4",
            43,
            0.5,
            4,
            &initial_poses,
        )
        .expect("generate Rust oblique limit=4 trace")
    }));

    if cfg!(debug_assertions) {
        let panic = generation.expect_err("Rust Debug should hit the quadtree assertion");
        let message = panic
            .downcast_ref::<String>()
            .map(String::as_str)
            .or_else(|| panic.downcast_ref::<&str>().copied())
            .expect("quadtree assertion should carry a string panic message");
        assert_eq!(
            message,
            "assertion failed: constricted_hazards.iter().filter(|h| h.is_some()).count() > 0"
        );
    } else {
        let (actual, _) = generation.expect("Rust Release should complete the three failures");
        assert_eq!(
            actual.lines().collect::<Vec<_>>(),
            OBLIQUE_LIMIT4_RELEASE_REFERENCE.lines().collect::<Vec<_>>(),
        );
    }
}

#[test]
fn oblique_limit_five_profile_boundary_matches_rust_release_trace() {
    let angle = 37.0_f32.to_radians();
    let (sin_angle, cos_angle) = angle.sin_cos();
    let child_offset = 2.0_f32;
    let initial_poses = [
        (0.0, 2.8, 2.75),
        (angle, 7.6, 2.75),
        (
            0.0,
            7.6 + child_offset * cos_angle,
            2.75 + child_offset * sin_angle,
        ),
        (
            0.0,
            7.6 - child_offset * cos_angle,
            2.75 - child_offset * sin_angle,
        ),
        (0.0, 2.8, 2.75),
        (0.0, 2.8, 3.5),
    ];
    let generation = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        actual_trace(
            "ExplorationBothSidesObliqueContainedRelocations.json",
            "both_sides_oblique_limit5",
            43,
            0.5,
            5,
            &initial_poses,
        )
        .expect("generate Rust oblique limit=5 trace")
    }));

    if cfg!(debug_assertions) {
        let panic = generation.expect_err("Rust Debug should hit the quadtree assertion");
        let message = panic
            .downcast_ref::<String>()
            .map(String::as_str)
            .or_else(|| panic.downcast_ref::<&str>().copied())
            .expect("quadtree assertion should carry a string panic message");
        assert_eq!(
            message,
            "assertion failed: constricted_hazards.iter().filter(|h| h.is_some()).count() > 0"
        );
    } else {
        let (actual, _) = generation.expect("Rust Release should complete the four failures");
        assert_eq!(
            actual.lines().collect::<Vec<_>>(),
            OBLIQUE_LIMIT5_RELEASE_REFERENCE.lines().collect::<Vec<_>>(),
        );
    }
}

#[test]
fn oblique_limit_six_profile_boundary_matches_rust_release_trace() {
    let angle = 37.0_f32.to_radians();
    let (sin_angle, cos_angle) = angle.sin_cos();
    let child_offset = 2.0_f32;
    let initial_poses = [
        (0.0, 2.8, 2.75),
        (angle, 7.6, 2.75),
        (
            0.0,
            7.6 + child_offset * cos_angle,
            2.75 + child_offset * sin_angle,
        ),
        (
            0.0,
            7.6 - child_offset * cos_angle,
            2.75 - child_offset * sin_angle,
        ),
        (0.0, 2.8, 2.75),
        (0.0, 2.8, 3.5),
    ];
    let generation = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        actual_trace(
            "ExplorationBothSidesObliqueContainedRelocations.json",
            "both_sides_oblique_limit6",
            43,
            0.5,
            6,
            &initial_poses,
        )
        .expect("generate Rust oblique limit=6 trace")
    }));

    if cfg!(debug_assertions) {
        let panic = generation.expect_err("Rust Debug should hit the quadtree assertion");
        let message = panic
            .downcast_ref::<String>()
            .map(String::as_str)
            .or_else(|| panic.downcast_ref::<&str>().copied())
            .expect("quadtree assertion should carry a string panic message");
        assert_eq!(
            message,
            "assertion failed: constricted_hazards.iter().filter(|h| h.is_some()).count() > 0"
        );
    } else {
        let (actual, _) = generation.expect("Rust Release should complete the five failures");
        assert_eq!(
            actual.lines().collect::<Vec<_>>(),
            OBLIQUE_LIMIT6_RELEASE_REFERENCE.lines().collect::<Vec<_>>(),
        );
    }
}

#[test]
fn independent_geometry_oblique_bilateral_relocations_have_two_sided_trace() {
    let angle = 37.0_f32.to_radians();
    let (sin_angle, cos_angle) = angle.sin_cos();
    let initial_poses = [
        (0.0, 1.3, 2.75),
        (angle, 4.8, 2.75),
        (
            angle,
            4.8 - 0.85 * cos_angle - 0.8 * sin_angle,
            2.75 - 0.85 * sin_angle + 0.8 * cos_angle,
        ),
        (
            angle,
            4.8 + 0.85 * cos_angle - 0.8 * sin_angle,
            2.75 + 0.85 * sin_angle + 0.8 * cos_angle,
        ),
        (0.0, 1.3, 2.75),
        (0.0, 1.3, 3.45),
    ];
    let (actual, first_failure_moves) = actual_trace_with_width(
        "ExplorationBothSidesObliqueContainedRelocationsIndependentGeometry.json",
        "independent_geometry_oblique_bilateral",
        10,
        0.8,
        2,
        6.7,
        &initial_poses,
    )
    .expect("generate independent-geometry bilateral Rust trace");

    assert_eq!(
        first_failure_moves,
        [(2, 0), (3, 0), (4, 1), (5, 1)],
        "expected two contained relocations per exchanged host: {actual}"
    );
    assert_eq!(
        actual.lines().collect::<Vec<_>>(),
        INDEPENDENT_GEOMETRY_REFERENCE.lines().collect::<Vec<_>>(),
    );
}

#[test]
fn independent_geometry_oblique_bilateral_relocations_match_limit_three_trace() {
    let angle = 37.0_f32.to_radians();
    let (sin_angle, cos_angle) = angle.sin_cos();
    let initial_poses = [
        (0.0, 1.3, 2.75),
        (angle, 4.8, 2.75),
        (
            angle,
            4.8 - 0.85 * cos_angle - 0.8 * sin_angle,
            2.75 - 0.85 * sin_angle + 0.8 * cos_angle,
        ),
        (
            angle,
            4.8 + 0.85 * cos_angle - 0.8 * sin_angle,
            2.75 + 0.85 * sin_angle + 0.8 * cos_angle,
        ),
        (0.0, 1.3, 2.75),
        (0.0, 1.3, 3.45),
    ];
    let (actual, first_failure_moves) = actual_trace_with_width(
        "ExplorationBothSidesObliqueContainedRelocationsIndependentGeometry.json",
        "independent_geometry_oblique_bilateral_limit3",
        10,
        0.8,
        3,
        6.7,
        &initial_poses,
    )
    .expect("generate independent-geometry bilateral Rust limit=3 trace");

    assert_eq!(
        first_failure_moves,
        [(2, 0), (3, 0), (4, 1), (5, 1)],
        "expected two contained relocations per exchanged host: {actual}"
    );
    assert!(
        actual.lines().any(|line| line == "insert 1 1 2 0 1"),
        "the second failure should insert into a two-candidate pool: {actual}"
    );
    assert_eq!(
        actual.lines().collect::<Vec<_>>(),
        INDEPENDENT_GEOMETRY_LIMIT3_REFERENCE
            .lines()
            .collect::<Vec<_>>(),
        "independent-geometry limit=3 Rust trace changed"
    );
}

#[test]
fn independent_geometry_oblique_bilateral_limit_four_trace_matches_rust_profiles() {
    let angle = 37.0_f32.to_radians();
    let (sin_angle, cos_angle) = angle.sin_cos();
    let initial_poses = [
        (0.0, 1.3, 2.75),
        (angle, 4.8, 2.75),
        (
            angle,
            4.8 - 0.85 * cos_angle - 0.8 * sin_angle,
            2.75 - 0.85 * sin_angle + 0.8 * cos_angle,
        ),
        (
            angle,
            4.8 + 0.85 * cos_angle - 0.8 * sin_angle,
            2.75 + 0.85 * sin_angle + 0.8 * cos_angle,
        ),
        (0.0, 1.3, 2.75),
        (0.0, 1.3, 3.45),
    ];
    let (actual, first_failure_moves) = actual_trace_with_width(
        "ExplorationBothSidesObliqueContainedRelocationsIndependentGeometry.json",
        "independent_geometry_oblique_bilateral_limit4",
        10,
        0.8,
        4,
        6.7,
        &initial_poses,
    )
    .expect("generate independent-geometry bilateral Rust limit=4 trace");

    assert_eq!(
        first_failure_moves,
        [(2, 0), (3, 0), (4, 1), (5, 1)],
        "expected two contained relocations per exchanged host: {actual}"
    );
    assert!(
        actual.lines().any(|line| line == "insert 2 2 3 0 1 2"),
        "the third failure should insert into a three-candidate pool: {actual}"
    );
    assert!(
        actual
            .lines()
            .any(|line| line.starts_with("sample 2 3e363c90 3e363c90 0 ")),
        "the third sample should select candidate index 0: {actual}"
    );
    assert_eq!(
        actual.lines().collect::<Vec<_>>(),
        INDEPENDENT_GEOMETRY_LIMIT4_REFERENCE
            .lines()
            .collect::<Vec<_>>(),
        "independent-geometry limit=4 Rust trace changed"
    );
}

#[test]
fn independent_geometry_oblique_bilateral_limit_five_trace_matches_rust_profiles() {
    let angle = 37.0_f32.to_radians();
    let (sin_angle, cos_angle) = angle.sin_cos();
    let initial_poses = [
        (0.0, 1.3, 2.75),
        (angle, 4.8, 2.75),
        (
            angle,
            4.8 - 0.85 * cos_angle - 0.8 * sin_angle,
            2.75 - 0.85 * sin_angle + 0.8 * cos_angle,
        ),
        (
            angle,
            4.8 + 0.85 * cos_angle - 0.8 * sin_angle,
            2.75 + 0.85 * sin_angle + 0.8 * cos_angle,
        ),
        (0.0, 1.3, 2.75),
        (0.0, 1.3, 3.45),
    ];
    let (actual, first_failure_moves) = actual_trace_with_width(
        "ExplorationBothSidesObliqueContainedRelocationsIndependentGeometry.json",
        "independent_geometry_oblique_bilateral_limit5",
        10,
        0.8,
        5,
        6.7,
        &initial_poses,
    )
    .expect("generate independent-geometry bilateral Rust limit=5 trace");

    assert_eq!(
        first_failure_moves,
        [(2, 0), (3, 0), (4, 1), (5, 1)],
        "expected two contained relocations per exchanged host: {actual}"
    );
    assert!(
        actual.lines().any(|line| line == "insert 3 2 4 0 1 3 2"),
        "the fourth failure should produce the four-candidate history pool [0,1,3,2]: {actual}"
    );
    assert!(
        actual
            .lines()
            .any(|line| line.starts_with("sample 3 bdd04def 3dd04def 0 ")),
        "the fourth sample should select candidate index 0: {actual}"
    );
    assert_eq!(
        actual.lines().collect::<Vec<_>>(),
        INDEPENDENT_GEOMETRY_LIMIT5_REFERENCE
            .lines()
            .collect::<Vec<_>>(),
        "independent-geometry limit=5 Rust trace changed"
    );
}

#[test]
fn independent_geometry_oblique_bilateral_limit_six_trace_matches_rust_profiles() {
    let angle = 37.0_f32.to_radians();
    let (sin_angle, cos_angle) = angle.sin_cos();
    let initial_poses = [
        (0.0, 1.3, 2.75),
        (angle, 4.8, 2.75),
        (
            angle,
            4.8 - 0.85 * cos_angle - 0.8 * sin_angle,
            2.75 - 0.85 * sin_angle + 0.8 * cos_angle,
        ),
        (
            angle,
            4.8 + 0.85 * cos_angle - 0.8 * sin_angle,
            2.75 + 0.85 * sin_angle + 0.8 * cos_angle,
        ),
        (0.0, 1.3, 2.75),
        (0.0, 1.3, 3.45),
    ];
    let (actual, first_failure_moves) = actual_trace_with_width(
        "ExplorationBothSidesObliqueContainedRelocationsIndependentGeometry.json",
        "independent_geometry_oblique_bilateral_limit6",
        10,
        0.8,
        6,
        6.7,
        &initial_poses,
    )
    .expect("generate independent-geometry bilateral Rust limit=6 trace");

    assert_eq!(
        first_failure_moves,
        [(2, 0), (3, 0), (4, 1), (5, 1)],
        "expected two contained relocations per exchanged host: {actual}"
    );
    assert!(
        actual.lines().any(|line| line == "insert 4 3 5 0 1 3 4 2"),
        "the fifth failure should produce the five-candidate history pool [0,1,3,4,2]: {actual}"
    );
    assert!(
        actual
            .lines()
            .any(|line| line.starts_with("sample 4 be892da8 3e892da8 1 ")),
        "the fifth sample should select candidate index 1: {actual}"
    );
    assert_eq!(
        actual.lines().collect::<Vec<_>>(),
        INDEPENDENT_GEOMETRY_LIMIT6_REFERENCE
            .lines()
            .collect::<Vec<_>>(),
        "independent-geometry limit=6 Rust trace changed"
    );
}

#[test]
fn independent_geometry_oblique_bilateral_seed_eleven_limit_six_trace_matches_rust_profiles() {
    let angle = 37.0_f32.to_radians();
    let (sin_angle, cos_angle) = angle.sin_cos();
    let initial_poses = [
        (0.0, 1.3, 2.75),
        (angle, 4.8, 2.75),
        (
            angle,
            4.8 - 0.85 * cos_angle - 0.8 * sin_angle,
            2.75 - 0.85 * sin_angle + 0.8 * cos_angle,
        ),
        (
            angle,
            4.8 + 0.85 * cos_angle - 0.8 * sin_angle,
            2.75 + 0.85 * sin_angle + 0.8 * cos_angle,
        ),
        (0.0, 1.3, 2.75),
        (0.0, 1.3, 3.45),
    ];
    let (actual, _) = actual_trace_with_width(
        "ExplorationBothSidesObliqueContainedRelocationsIndependentGeometry.json",
        "independent_geometry_oblique_bilateral_seed11_limit6",
        11,
        0.8,
        6,
        6.7,
        &initial_poses,
    )
    .expect("generate independent-geometry seed=11 bilateral Rust limit=6 trace");

    assert!(
        actual.lines().any(|line| line == "insert 2 1 3 0 2 1"),
        "seed=11 should retain a different third-round pool order: {actual}"
    );
    assert!(
        actual.lines().any(|line| line == "insert 4 3 5 0 2 1 4 3"),
        "the fifth failure should produce the seed=11 pool [0,2,1,4,3]: {actual}"
    );
    assert!(
        actual
            .lines()
            .any(|line| line.starts_with("sample 4 3e4963b1 3e4963b1 0 1 24 ")),
        "the fifth sample should select candidate index 0: {actual}"
    );
    assert!(
        actual.lines().any(|line| line == "final 2.337137699 6"),
        "the final separator state should return to the repeated failure layout: {actual}"
    );
    assert_eq!(
        actual.lines().collect::<Vec<_>>(),
        INDEPENDENT_GEOMETRY_SEED11_LIMIT6_REFERENCE
            .lines()
            .collect::<Vec<_>>(),
        "independent-geometry seed=11 limit=6 Rust trace changed"
    );
}

#[test]
fn independent_geometry_oblique_bilateral_seed_twelve_limit_six_trace_matches_rust_profiles() {
    let angle = 37.0_f32.to_radians();
    let (sin_angle, cos_angle) = angle.sin_cos();
    let initial_poses = [
        (0.0, 1.3, 2.75),
        (angle, 4.8, 2.75),
        (
            angle,
            4.8 - 0.85 * cos_angle - 0.8 * sin_angle,
            2.75 - 0.85 * sin_angle + 0.8 * cos_angle,
        ),
        (
            angle,
            4.8 + 0.85 * cos_angle - 0.8 * sin_angle,
            2.75 + 0.85 * sin_angle + 0.8 * cos_angle,
        ),
        (0.0, 1.3, 2.75),
        (0.0, 1.3, 3.45),
    ];
    let (actual, first_failure_moves) = actual_trace_with_width(
        "ExplorationBothSidesObliqueContainedRelocationsIndependentGeometry.json",
        "independent_geometry_oblique_bilateral_seed12_limit6",
        12,
        0.8,
        6,
        6.7,
        &initial_poses,
    )
    .expect("generate independent-geometry seed=12 bilateral Rust limit=6 trace");

    assert_eq!(
        first_failure_moves,
        [(4, 1), (5, 1), (2, 0), (3, 0)],
        "seed=12 first failure should preserve Rust contained-move order"
    );
    assert!(
        actual.lines().any(|line| line == "insert 2 1 3 0 2 1"),
        "seed=12 should form the third-round history pool [0,2,1]: {actual}"
    );
    assert!(
        actual.lines().any(|line| line == "insert 3 1 4 0 3 2 1"),
        "seed=12 should form the fourth-round history pool [0,3,2,1]: {actual}"
    );
    assert!(
        actual.lines().any(|line| line == "insert 4 1 5 0 4 3 2 1"),
        "seed=12 should form the fifth-round history pool [0,4,3,2,1]: {actual}"
    );
    assert!(
        actual
            .lines()
            .any(|line| line.starts_with("sample 4 be9307f8 3e9307f8 1 3 fe ")),
        "the fifth sample should select index 1 after consuming three u64 words: {actual}"
    );
    assert!(
        actual.lines().any(|line| line == "final 0.321904153 6"),
        "the final separator state should return near the first failure: {actual}"
    );

    let failure_snapshots = actual
        .lines()
        .take_while(|line| !line.starts_with("insert "))
        .filter(|line| line.starts_with("failure ") || line.starts_with("pose "))
        .collect::<Vec<_>>();
    for baseline in [
        INDEPENDENT_GEOMETRY_LIMIT6_REFERENCE,
        INDEPENDENT_GEOMETRY_SEED11_LIMIT6_REFERENCE,
    ] {
        let baseline_failure_snapshots = baseline
            .lines()
            .take_while(|line| !line.starts_with("insert "))
            .filter(|line| line.starts_with("failure ") || line.starts_with("pose "))
            .collect::<Vec<_>>();
        assert_eq!(
            failure_snapshots, baseline_failure_snapshots,
            "seed=12 failure snapshots should match seed=10 and seed=11"
        );
    }

    let final_state = actual
        .lines()
        .skip_while(|line| !line.starts_with("final "))
        .collect::<Vec<_>>();
    let seed10_final_state = INDEPENDENT_GEOMETRY_LIMIT6_REFERENCE
        .lines()
        .skip_while(|line| !line.starts_with("final "))
        .collect::<Vec<_>>();
    let seed11_final_state = INDEPENDENT_GEOMETRY_SEED11_LIMIT6_REFERENCE
        .lines()
        .skip_while(|line| !line.starts_with("final "))
        .collect::<Vec<_>>();
    assert_eq!(
        final_state, seed10_final_state,
        "seed=12 should end at the exact seed=10 final state"
    );
    assert_ne!(
        final_state, seed11_final_state,
        "seed=12 should not end at the seed=11 final state"
    );

    assert_eq!(
        actual.lines().collect::<Vec<_>>(),
        INDEPENDENT_GEOMETRY_SEED12_LIMIT6_REFERENCE
            .lines()
            .collect::<Vec<_>>(),
        "independent-geometry seed=12 limit=6 Rust trace changed"
    );
}

#[test]
fn independent_geometry_oblique_bilateral_seed_thirteen_limit_six_trace_matches_rust_profiles() {
    let angle = 37.0_f32.to_radians();
    let (sin_angle, cos_angle) = angle.sin_cos();
    let initial_poses = [
        (0.0, 1.3, 2.75),
        (angle, 4.8, 2.75),
        (
            angle,
            4.8 - 0.85 * cos_angle - 0.8 * sin_angle,
            2.75 - 0.85 * sin_angle + 0.8 * cos_angle,
        ),
        (
            angle,
            4.8 + 0.85 * cos_angle - 0.8 * sin_angle,
            2.75 + 0.85 * sin_angle + 0.8 * cos_angle,
        ),
        (0.0, 1.3, 2.75),
        (0.0, 1.3, 3.45),
    ];
    let (actual, first_failure_moves) = actual_trace_with_width(
        "ExplorationBothSidesObliqueContainedRelocationsIndependentGeometry.json",
        "independent_geometry_oblique_bilateral_seed13_limit6",
        13,
        0.8,
        6,
        6.7,
        &initial_poses,
    )
    .expect("generate independent-geometry seed=13 bilateral Rust limit=6 trace");

    assert_eq!(
        first_failure_moves,
        [(2, 0), (3, 0), (4, 1), (5, 1)],
        "seed=13 first failure should preserve the seed=10/11 contained-move order"
    );
    assert!(
        actual.lines().any(|line| line == "insert 3 0 4 3 0 1 2"),
        "seed=13 should form the fourth-round pool [3,0,1,2]: {actual}"
    );
    assert!(
        actual.lines().any(|line| line == "insert 4 3 5 3 0 1 4 2"),
        "seed=13 should form the fifth-round pool [3,0,1,4,2]: {actual}"
    );
    assert!(
        actual
            .lines()
            .any(|line| line.starts_with("sample 4 3f0e25db 3f0e25db 2 1 04 ")),
        "seed=13 fifth sample should select candidate index 2: {actual}"
    );
    assert!(
        actual.lines().any(|line| line == "final 0.321904153 6"),
        "seed=13 should end at the recorded nonzero-loss state: {actual}"
    );
    assert_eq!(
        actual.lines().collect::<Vec<_>>(),
        INDEPENDENT_GEOMETRY_SEED13_LIMIT6_REFERENCE
            .lines()
            .collect::<Vec<_>>(),
        "independent-geometry seed=13 limit=6 Rust trace changed"
    );
}

#[test]
fn independent_geometry_oblique_bilateral_seed_fourteen_limit_six_trace_matches_rust_profiles() {
    let angle = 37.0_f32.to_radians();
    let (sin_angle, cos_angle) = angle.sin_cos();
    let initial_poses = [
        (0.0, 1.3, 2.75),
        (angle, 4.8, 2.75),
        (
            angle,
            4.8 - 0.85 * cos_angle - 0.8 * sin_angle,
            2.75 - 0.85 * sin_angle + 0.8 * cos_angle,
        ),
        (
            angle,
            4.8 + 0.85 * cos_angle - 0.8 * sin_angle,
            2.75 + 0.85 * sin_angle + 0.8 * cos_angle,
        ),
        (0.0, 1.3, 2.75),
        (0.0, 1.3, 3.45),
    ];
    let (actual, first_failure_moves) = actual_trace_with_width(
        "ExplorationBothSidesObliqueContainedRelocationsIndependentGeometry.json",
        "independent_geometry_oblique_bilateral_seed14_limit6",
        14,
        0.8,
        6,
        6.7,
        &initial_poses,
    )
    .expect("generate independent-geometry seed=14 bilateral Rust limit=6 trace");

    assert_eq!(
        first_failure_moves,
        [(2, 0), (3, 0), (4, 1), (5, 1)],
        "seed=14 first failure should preserve the seed=10/11/13 contained-move order"
    );
    assert!(
        actual.lines().any(|line| line == "insert 3 2 4 0 1 3 2"),
        "seed=14 should form the fourth-round pool [0,1,3,2]: {actual}"
    );
    assert!(
        actual.lines().any(|line| line == "insert 4 0 5 4 0 1 3 2"),
        "seed=14 should form the fifth-round pool [4,0,1,3,2]: {actual}"
    );
    assert!(
        actual
            .lines()
            .any(|line| line.starts_with("sample 4 3cdc5103 3cdc5103 0 1 ec ")),
        "seed=14 fifth sample should select candidate index 0: {actual}"
    );
    assert!(
        actual.lines().any(|line| line == "final 2.337137699 6"),
        "seed=14 should end at the recorded nonzero-loss state: {actual}"
    );
    assert_eq!(
        actual.lines().collect::<Vec<_>>(),
        INDEPENDENT_GEOMETRY_SEED14_LIMIT6_REFERENCE
            .lines()
            .collect::<Vec<_>>(),
        "independent-geometry seed=14 limit=6 Rust trace changed"
    );
}

#[test]
fn second_independent_geometry_oblique_bilateral_seed_ten_limit_six_trace_matches_rust_profiles()
-> Result<(), Box<dyn Error>> {
    let angle = 37.0_f32.to_radians();
    let (sin_angle, cos_angle) = angle.sin_cos();
    let initial_poses = [
        (0.0, 1.6, 3.5),
        (angle, 6.8, 3.5),
        (angle, 6.8 - 0.9 * cos_angle, 3.5 - 0.9 * sin_angle),
        (angle, 6.8 + 0.9 * cos_angle, 3.5 + 0.9 * sin_angle),
        (0.0, 1.6, 2.6),
        (0.0, 1.6, 4.4),
    ];
    let (actual, first_failure_moves) = actual_trace_with_width(
        "ExplorationBothSidesObliqueContainedRelocationsSecondIndependentGeometry.json",
        "second_independent_geometry_h_i_seed10_limit6",
        10,
        0.8,
        6,
        9.5,
        &initial_poses,
    )?;

    assert_eq!(
        first_failure_moves.len(),
        4,
        "probe should relocate multiple contained items on both hosts: {first_failure_moves:?}\n{actual}"
    );
    assert_eq!(
        first_failure_moves
            .iter()
            .filter(|(_, host)| *host == 0)
            .count(),
        2,
        "probe should relocate two items from the H host: {first_failure_moves:?}\n{actual}"
    );
    assert_eq!(
        first_failure_moves
            .iter()
            .filter(|(_, host)| *host == 1)
            .count(),
        2,
        "probe should relocate two items from the I host: {first_failure_moves:?}\n{actual}"
    );
    assert_eq!(first_failure_moves, [(2, 0), (3, 0), (4, 1), (5, 1)]);
    assert_eq!(
        actual
            .lines()
            .filter(|line| line.starts_with("failure "))
            .count(),
        3,
        "second geometry should record three failure snapshots"
    );
    assert!(
        actual.lines().any(|line| line == "insert 2 1 2 1 2"),
        "second geometry should record the third-round two-entry failure pool"
    );
    assert!(
        actual.lines().any(|line| line == "final 6.765402794 6"),
        "second geometry should retain the observed nonzero-loss final state"
    );
    assert_eq!(
        actual.lines().collect::<Vec<_>>(),
        SECOND_INDEPENDENT_GEOMETRY_SEED10_LIMIT6_REFERENCE
            .lines()
            .collect::<Vec<_>>(),
        "second independent geometry seed=10 limit=6 Rust trace changed"
    );
    Ok(())
}

#[test]
fn second_independent_geometry_oblique_bilateral_seed_eleven_limit_six_trace_matches_rust_profiles()
-> Result<(), Box<dyn Error>> {
    let angle = 37.0_f32.to_radians();
    let (sin_angle, cos_angle) = angle.sin_cos();
    let initial_poses = [
        (0.0, 1.6, 3.5),
        (angle, 6.8, 3.5),
        (angle, 6.8 - 0.9 * cos_angle, 3.5 - 0.9 * sin_angle),
        (angle, 6.8 + 0.9 * cos_angle, 3.5 + 0.9 * sin_angle),
        (0.0, 1.6, 2.6),
        (0.0, 1.6, 4.4),
    ];
    let (actual, first_failure_moves) = actual_trace_with_width(
        "ExplorationBothSidesObliqueContainedRelocationsSecondIndependentGeometry.json",
        "second_independent_geometry_h_i_seed11_limit6",
        11,
        0.8,
        6,
        9.5,
        &initial_poses,
    )?;
    assert_eq!(
        first_failure_moves,
        [(4, 1), (5, 1), (2, 0), (3, 0)],
        "seed=11 should collect contained children in this order"
    );
    assert_eq!(
        first_failure_moves
            .iter()
            .filter(|(_, host)| *host == 0)
            .count(),
        2,
        "seed=11 should relocate two children from the H host"
    );
    assert_eq!(
        first_failure_moves
            .iter()
            .filter(|(_, host)| *host == 1)
            .count(),
        2,
        "seed=11 should relocate two children from the I host"
    );
    assert_eq!(
        actual
            .lines()
            .filter(|line| line.starts_with("failure "))
            .count(),
        3,
        "second geometry seed=11 should record three failure snapshots"
    );
    assert!(
        actual.lines().any(|line| line == "insert 2 1 2 1 2"),
        "seed=11 third-round pool should be [1,2]: {actual}"
    );
    assert!(
        actual
            .lines()
            .any(|line| line.starts_with("sample 2 3d8175ef 3d8175ef 0 1 fa ")),
        "seed=11 third-round sample should select index 0: {actual}"
    );
    assert!(
        actual.lines().any(|line| line == "final 6.765402794 6"),
        "seed=11 should retain the observed nonzero-loss final state: {actual}"
    );
    assert_eq!(
        actual.lines().collect::<Vec<_>>(),
        SECOND_INDEPENDENT_GEOMETRY_SEED11_LIMIT6_REFERENCE
            .lines()
            .collect::<Vec<_>>(),
        "second independent geometry seed=11 limit=6 Rust trace changed"
    );
    Ok(())
}

#[test]
fn second_independent_geometry_oblique_bilateral_seed_twelve_limit_six_trace_matches_rust_profiles()
-> Result<(), Box<dyn Error>> {
    let angle = 37.0_f32.to_radians();
    let (sin_angle, cos_angle) = angle.sin_cos();
    let initial_poses = [
        (0.0, 1.6, 3.5),
        (angle, 6.8, 3.5),
        (angle, 6.8 - 0.9 * cos_angle, 3.5 - 0.9 * sin_angle),
        (angle, 6.8 + 0.9 * cos_angle, 3.5 + 0.9 * sin_angle),
        (0.0, 1.6, 2.6),
        (0.0, 1.6, 4.4),
    ];
    let (actual, first_failure_moves) = actual_trace_with_width(
        "ExplorationBothSidesObliqueContainedRelocationsSecondIndependentGeometry.json",
        "second_independent_geometry_h_i_seed12_limit6",
        12,
        0.8,
        6,
        9.5,
        &initial_poses,
    )?;
    assert_eq!(
        first_failure_moves,
        [(2, 0), (3, 0), (4, 1), (5, 1)],
        "seed=12 should collect contained children in this order"
    );
    assert_eq!(
        actual
            .lines()
            .filter(|line| line.starts_with("failure "))
            .count(),
        3,
        "second geometry seed=12 should record three failure snapshots"
    );
    assert!(
        actual.lines().any(|line| line == "failure 0 1.762814999 6")
            && actual.lines().any(|line| line == "failure 1 2.980286598 6"),
        "seed=12 should preserve the first two observed failure snapshots: {actual}"
    );
    assert!(
        actual.lines().any(|line| line == "insert 2 1 2 1 2"),
        "seed=12 third-round pool should be [1,2]: {actual}"
    );
    assert!(
        actual
            .lines()
            .any(|line| line.starts_with("sample 2 3de70ab2 3de70ab2 0 1 2a ")),
        "seed=12 third-round sample should select index 0 at layer 2a: {actual}"
    );
    assert!(
        actual.lines().any(|line| line == "failure 2 4.296658516 6")
            && actual.lines().any(|line| line == "final 4.296658516 6"),
        "seed=12 should end at the third failure snapshot with nonzero loss: {actual}"
    );
    assert_eq!(
        actual.lines().collect::<Vec<_>>(),
        SECOND_INDEPENDENT_GEOMETRY_SEED12_LIMIT6_REFERENCE
            .lines()
            .collect::<Vec<_>>(),
        "second independent geometry seed=12 limit=6 Rust trace changed"
    );
    Ok(())
}

#[test]
fn second_independent_geometry_oblique_bilateral_seed_thirteen_limit_six_trace_matches_rust_profiles()
-> Result<(), Box<dyn Error>> {
    let angle = 37.0_f32.to_radians();
    let (sin_angle, cos_angle) = angle.sin_cos();
    let initial_poses = [
        (0.0, 1.6, 3.5),
        (angle, 6.8, 3.5),
        (angle, 6.8 - 0.9 * cos_angle, 3.5 - 0.9 * sin_angle),
        (angle, 6.8 + 0.9 * cos_angle, 3.5 + 0.9 * sin_angle),
        (0.0, 1.6, 2.6),
        (0.0, 1.6, 4.4),
    ];
    let (actual, first_failure_moves) = actual_trace_with_width(
        "ExplorationBothSidesObliqueContainedRelocationsSecondIndependentGeometry.json",
        "second_independent_geometry_h_i_seed13_limit6",
        13,
        0.8,
        6,
        9.5,
        &initial_poses,
    )?;
    assert_eq!(
        first_failure_moves,
        [(4, 1), (5, 1), (2, 0), (3, 0)],
        "seed=13 should collect contained children in this order"
    );
    assert_eq!(
        first_failure_moves
            .iter()
            .filter(|(_, host)| *host == 0)
            .count(),
        2,
        "seed=13 should relocate two children from the H host"
    );
    assert_eq!(
        first_failure_moves
            .iter()
            .filter(|(_, host)| *host == 1)
            .count(),
        2,
        "seed=13 should relocate two children from the I host"
    );
    let failures = actual
        .lines()
        .filter(|line| line.starts_with("failure "))
        .collect::<Vec<_>>();
    assert_eq!(
        failures,
        [
            "failure 0 1.762814999 6",
            "failure 1 2.980286598 6",
            "failure 2 6.765402794 6",
        ],
        "seed=13 should retain the three observed H/I failure snapshots"
    );
    assert!(
        actual.lines().any(|line| line == "insert 2 1 2 1 2"),
        "seed=13 third-round pool should be [1,2]: {actual}"
    );
    assert!(
        actual
            .lines()
            .any(|line| line.starts_with("sample 2 3e9a48a9 3e9a48a9 0 1 43 ")),
        "seed=13 third-round sample should select index 0 at layer 43: {actual}"
    );
    assert!(
        actual.lines().any(|line| line == "final 6.765402794 6"),
        "seed=13 should retain the observed nonzero-loss final state: {actual}"
    );
    assert_eq!(
        actual.lines().collect::<Vec<_>>(),
        SECOND_INDEPENDENT_GEOMETRY_SEED13_LIMIT6_REFERENCE
            .lines()
            .collect::<Vec<_>>(),
        "second independent geometry seed=13 limit=6 Rust trace changed"
    );
    Ok(())
}

#[test]
fn second_independent_geometry_oblique_bilateral_seed_fourteen_limit_six_trace_matches_rust_profiles()
-> Result<(), Box<dyn Error>> {
    let angle = 37.0_f32.to_radians();
    let (sin_angle, cos_angle) = angle.sin_cos();
    let initial_poses = [
        (0.0, 1.6, 3.5),
        (angle, 6.8, 3.5),
        (angle, 6.8 - 0.9 * cos_angle, 3.5 - 0.9 * sin_angle),
        (angle, 6.8 + 0.9 * cos_angle, 3.5 + 0.9 * sin_angle),
        (0.0, 1.6, 2.6),
        (0.0, 1.6, 4.4),
    ];
    let (actual, first_failure_moves) = actual_trace_with_width(
        "ExplorationBothSidesObliqueContainedRelocationsSecondIndependentGeometry.json",
        "second_independent_geometry_h_i_seed14_limit6",
        14,
        0.8,
        6,
        9.5,
        &initial_poses,
    )?;
    assert_eq!(
        first_failure_moves,
        [(2, 0), (3, 0), (4, 1), (5, 1)],
        "seed=14 should collect two contained children from each H/I host"
    );
    let failures = actual
        .lines()
        .filter(|line| line.starts_with("failure "))
        .collect::<Vec<_>>();
    assert_eq!(
        failures,
        [
            "failure 0 1.762814999 6",
            "failure 1 2.980286598 6",
            "failure 2 6.765402794 6",
        ],
        "seed=14 should retain the three observed H/I failure snapshots"
    );
    assert!(
        actual.lines().any(|line| line == "insert 2 1 2 1 2"),
        "seed=14 third-round pool should be [1,2]: {actual}"
    );
    assert!(
        actual
            .lines()
            .any(|line| line.starts_with("sample 2 3ee2611d 3ee2611d 0 1 2a ")),
        "seed=14 third-round sample should select index 0 at layer 2a: {actual}"
    );
    assert!(
        actual.lines().any(|line| line == "final 4.296658516 6")
            && actual.lines().any(|line| {
                line == "pose 3 0.645771801 4.944271564 4.041633606 4.489125252 3.586487293 5.399417877 4.496779919"
            }),
        "seed=14 final relocation should reach the observed lower-loss state: {actual}"
    );
    assert_eq!(
        actual.lines().collect::<Vec<_>>(),
        SECOND_INDEPENDENT_GEOMETRY_SEED14_LIMIT6_REFERENCE
            .lines()
            .collect::<Vec<_>>(),
        "second independent geometry seed=14 limit=6 Rust trace changed"
    );
    Ok(())
}

#[test]
fn continuous_rotation_disruption_and_contained_relocation_match_rust_trace()
-> Result<(), Box<dyn Error>> {
    let input_filename = "ExplorationObliqueContainedRelocationContinuous.json";
    let input_path = Path::new("../Fixtures").join(input_filename);
    let (external, _) = sparrow::util::io::read_spp_input(&input_path)?;
    let defaults = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        defaults.cde_config,
        defaults.poly_simpl_tolerance,
        defaults.narrow_concavity_cutoff_ratio,
    );
    let instance = import_instance(&importer, &external)?;
    assert!(
        instance
            .items
            .iter()
            .all(|(item, _)| item.allowed_rotation == RotationRange::Continuous),
        "continuous input must import as RotationRange::Continuous"
    );

    let oblique = 37.0_f32.to_radians();
    let (sin_oblique, cos_oblique) = oblique.sin_cos();
    let initial_poses = [
        (0.0, 2.8, 2.75),
        (oblique, 7.6, 2.75),
        (
            0.0,
            7.6 + (2.3 * cos_oblique - 0.2 * sin_oblique),
            2.75 + (2.3 * sin_oblique + 0.2 * cos_oblique),
        ),
    ];
    let (actual, first_failure_moves) = actual_trace_with_width(
        input_filename,
        "continuous_oblique_contained",
        43,
        0.75,
        3,
        9.9,
        &initial_poses,
    )?;
    assert_eq!(
        first_failure_moves,
        [(2, 0)],
        "continuous Explore should relocate the contained child with its host: {actual}"
    );
    assert!(
        actual.lines().any(|line| {
            line.starts_with("move 0 swap 0 1 ")
                && line.split_whitespace().nth(8) == Some("0.645771801")
        }),
        "Continuous rotation must preserve the 37-degree swapped pose: {actual}"
    );
    assert!(
        actual.lines().any(|line| {
            line.starts_with("move 0 contained 2 0 ")
                && line.split_whitespace().nth(8) == Some("-0.645771801")
        }),
        "Continuous relocation must preserve the observed negative 37-degree child pose: {actual}"
    );
    assert_eq!(
        actual.lines().collect::<Vec<_>>(),
        CONTINUOUS_ROTATION_REFERENCE.lines().collect::<Vec<_>>(),
        "actual Rust Explore Continuous-rotation trace changed"
    );
    Ok(())
}
