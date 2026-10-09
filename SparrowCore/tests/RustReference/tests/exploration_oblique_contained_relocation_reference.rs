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
    include_str!("../../Fixtures/ExplorationObliqueContainedRelocationRustTrace.tsv");

#[derive(Default)]
struct TraceListener {
    failures: Vec<(f32, Vec<PlacementTrace>)>,
}

#[derive(Clone)]
struct PlacementTrace {
    item_index: usize,
    rotation: f32,
    x: f32,
    y: f32,
    bounds: [f32; 4],
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

fn trace() -> Result<String, Box<dyn Error>> {
    let (external, _) = sparrow::util::io::read_spp_input(Path::new(
        "../Fixtures/ExplorationObliqueContainedRelocation.json",
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
    let oblique = 37.0_f32.to_radians();
    let (sin_oblique, cos_oblique) = oblique.sin_cos();
    let child_x = 7.6 + (2.3 * cos_oblique - 0.2 * sin_oblique);
    let child_y = 2.75 + (2.3 * sin_oblique + 0.2 * cos_oblique);
    problem.place_item(SPPlacement {
        item_idx: 0,
        d_transf: DTransformation::new(0.0, (2.8, 2.75)),
    });
    problem.place_item(SPPlacement {
        item_idx: 1,
        d_transf: DTransformation::new(oblique, (7.6, 2.75)),
    });
    problem.place_item(SPPlacement {
        item_idx: 2,
        d_transf: DTransformation::new(0.0, (child_x, child_y)),
    });
    let initial = problem.save();
    let initial_layout = Layout::from_snapshot(&initial.layout_snapshot);
    assert!(
        initial_layout.is_feasible(),
        "initial layout must be feasible"
    );
    assert_eq!(CollisionTracker::new(&initial_layout).get_total_loss(), 0.0);

    let seed = 43_u64;
    let shrink_step = 0.1_f32;
    let iteration_limit = 3_usize;
    let pool_stddev = 0.25_f32;
    let mut config = defaults.expl_cfg;
    config.shrink_step = shrink_step;
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

    assert_eq!(
        listener.failures.len(),
        2,
        "expected two Explore infeasible snapshots"
    );
    let first_failure = &listener.failures[0].1;
    let second_failure = &listener.failures[1].1;
    let before_child = first_failure
        .iter()
        .find(|placement| placement.item_index == 2)
        .expect("initial child placement");
    let after_host = second_failure
        .iter()
        .find(|placement| placement.item_index == 0)
        .expect("disrupted host placement");
    let after_child = second_failure
        .iter()
        .find(|placement| placement.item_index == 2)
        .expect("relocated child placement");
    let after_swapped_host = second_failure
        .iter()
        .find(|placement| placement.item_index == 1)
        .expect("second swapped host placement");
    assert!((after_host.rotation - oblique).abs() < 1.0e-6);
    assert!((after_swapped_host.x - 2.8).abs() < 1.0e-5);
    assert!((after_child.rotation - oblique).abs() < 1.0e-6);
    assert!((after_child.x - before_child.x).abs() > 3.0);
    assert!(
        events.iter().any(|event| matches!(
            event,
            ExplorationTraceEvent::DisruptionMove {
                kind: DisruptionMoveKind::ContainedRelocation,
                item_index: 2,
                ..
            }
        )),
        "trace must contain the actual POI-driven child relocation"
    );

    let mut output = String::from(
        "# generator=actual Sparrow Rust exploration_phase with pool observer\n\
         # input=ExplorationObliqueContainedRelocation.json; valid initial layout; 37-degree host swap and contained-child relocation\n\
         # case seed initial_width shrink_step iteration_limit pool_stddev failure_count item_count\n\
         # failure ordinal total_loss item_count\n\
         # pose item_index rotation_rad x y bbox_x_min bbox_y_min bbox_x_max bbox_y_max\n\
         # move failure_id kind item_index related_item_index from_rotation from_x from_y to_rotation to_x to_y\n\
         # insert failure_id insertion_index pool_size candidate_failure_ids...\n\
         # sample failure_id raw_normal_bits mapped_sample_bits selected_index consumed_u64_words first_layer next_u64_after_sample[0..3]\n",
    );
    writeln!(
        &mut output,
        "case oblique_contained {seed} {initial_width:.9} {shrink_step:.9} {iteration_limit} {pool_stddev:.9} {} {}",
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
        for event in events.iter().filter(|event| match event {
            ExplorationTraceEvent::PoolInsert { failure_id: id, .. }
            | ExplorationTraceEvent::PoolSample { failure_id: id, .. }
            | ExplorationTraceEvent::DisruptionMove { failure_id: id, .. } => *id == failure_id,
        }) {
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
    }
    Ok(output)
}

#[test]
fn oblique_rotation_disruption_and_contained_relocation_match_rust_trace() {
    assert_eq!(
        trace().expect("generate oblique relocation trace"),
        REFERENCE,
        "actual Rust Explore oblique disruption/contained-relocation trace changed"
    );
}
