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

const REFERENCE: &str =
    include_str!("../../Fixtures/ExplorationMultipleContainedRelocationsRustTrace.tsv");

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

fn actual_trace() -> Result<(String, Vec<usize>), Box<dyn Error>> {
    let (external, _) = sparrow::util::io::read_spp_input(Path::new(
        "../Fixtures/ExplorationMultipleContainedRelocations.json",
    ))?;
    let defaults = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        defaults.cde_config,
        defaults.poly_simpl_tolerance,
        defaults.narrow_concavity_cutoff_ratio,
    );
    let instance = import_instance(&importer, &external)?;
    let mut problem = SPProblem::new(instance.clone())?;
    let initial_width = 9.9_f32;
    problem.change_strip_width(initial_width)?;
    for (item_idx, translation) in [
        (2.8_f32, 2.75_f32),
        (7.6_f32, 2.75_f32),
        (9.5_f32, 2.1_f32),
        (9.5_f32, 3.35_f32),
    ]
    .into_iter()
    .enumerate()
    {
        problem.place_item(SPPlacement {
            item_idx,
            d_transf: DTransformation::new(0.0, translation),
        });
    }
    let initial = problem.save();
    let initial_layout = Layout::from_snapshot(&initial.layout_snapshot);
    assert!(
        initial_layout.is_feasible(),
        "initial layout must be feasible"
    );
    assert_eq!(CollisionTracker::new(&initial_layout).get_total_loss(), 0.0);

    let seed = 43_u64;
    let shrink_step = 0.1_f32;
    let iteration_limit = 4_usize;
    let pool_stddev = 0.25_f32;
    let mut config = defaults.expl_cfg;
    config.shrink_step = shrink_step;
    config.large_item_ch_area_cutoff_percentile = 0.9;
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

    let first_failure_contained_order = events
        .iter()
        .filter_map(|event| match event {
            ExplorationTraceEvent::DisruptionMove {
                failure_id: 0,
                kind: DisruptionMoveKind::ContainedRelocation,
                item_index,
                ..
            } => Some(*item_index),
            _ => None,
        })
        .collect::<Vec<_>>();

    let mut output = String::from(
        "# generator=actual Sparrow Rust exploration_phase with pool observer\n\
         # input=ExplorationMultipleContainedRelocations.json; two POI-contained children in one disruption failure\n\
         # case seed initial_width shrink_step iteration_limit pool_stddev failure_count item_count\n\
         # failure ordinal total_loss item_count\n\
         # pose item_index rotation_rad x y\n\
         # move failure_id kind item_index related_item_index from_rotation from_x from_y to_rotation to_x to_y\n\
         # insert failure_id insertion_index pool_size candidate_failure_ids...\n\
         # sample failure_id raw_normal_bits mapped_sample_bits selected_index consumed_u64_words first_layer next_u64_after_sample[0..3]\n",
    );
    writeln!(
        &mut output,
        "case multiple_contained {seed} {initial_width:.9} {shrink_step:.9} {iteration_limit} {pool_stddev:.9} {} {}",
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
    Ok((output, first_failure_contained_order))
}

#[test]
fn one_disruption_failure_relocates_multiple_contained_children_in_collector_order() {
    let (actual, first_failure_order) =
        actual_trace().expect("generate actual Rust multiple-contained-relocation trace");
    assert_eq!(first_failure_order, [2, 3]);
    assert_eq!(actual, REFERENCE.replace("\r\n", "\n"));
}
