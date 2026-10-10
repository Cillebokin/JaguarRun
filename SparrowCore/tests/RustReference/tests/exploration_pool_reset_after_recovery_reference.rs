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
use std::fmt::Write as _;
use std::path::Path;
use std::time::Duration;

const REFERENCE: &str = include_str!("../../Fixtures/ExplorationPoolResetRustTrace.tsv");

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

#[derive(Clone)]
struct PoseTrace {
    item_index: usize,
    rotation: f32,
    x: f32,
    y: f32,
    bounds: [f32; 4],
}

struct FailureTrace {
    loss: f32,
    poses: Vec<PoseTrace>,
}

struct ReportTrace {
    report_type: &'static str,
    strip_width: f32,
    loss: f32,
    feasible: bool,
}

#[derive(Default)]
struct RecordingListener {
    reports: Vec<ReportTrace>,
    failures: Vec<FailureTrace>,
}

impl SolutionListener for RecordingListener {
    fn report(&mut self, report: ReportType, solution: &SPSolution) {
        let report_type = match report {
            ReportType::ExplFeas => "feasible",
            ReportType::ExplInfeas => "infeasible",
            _ => "other",
        };
        let layout = Layout::from_snapshot(&solution.layout_snapshot);
        let loss = CollisionTracker::new(&layout).get_total_loss();
        let mut poses = solution
            .layout_snapshot
            .placed_items
            .values()
            .map(|placed| {
                let (x, y) = placed.d_transf.translation();
                let bounds = placed.shape.bbox;
                PoseTrace {
                    item_index: placed.item.idx,
                    rotation: placed.d_transf.rotation(),
                    x,
                    y,
                    bounds: [bounds.x_min, bounds.y_min, bounds.x_max, bounds.y_max],
                }
            })
            .collect::<Vec<_>>();
        poses.sort_by_key(|pose| pose.item_index);
        if report == ReportType::ExplInfeas {
            self.failures.push(FailureTrace {
                loss,
                poses: poses.clone(),
            });
        }
        self.reports.push(ReportTrace {
            report_type,
            strip_width: solution.strip_width(),
            loss,
            feasible: report == ReportType::ExplFeas,
        });
    }
}

fn write_poses(output: &mut String, poses: &[PoseTrace]) {
    for pose in poses {
        writeln!(
            output,
            "pose {} {:.9} {:.9} {:.9} {:.9} {:.9} {:.9} {:.9}",
            pose.item_index,
            pose.rotation,
            pose.x,
            pose.y,
            pose.bounds[0],
            pose.bounds[1],
            pose.bounds[2],
            pose.bounds[3],
        )
        .unwrap();
    }
}

#[test]
fn successful_recovery_clears_pool_and_resets_consecutive_failure_limit() {
    let (external, _) =
        sparrow::util::io::read_spp_input(Path::new("../Fixtures/ExplorationPoolReset.json"))
            .expect("read pool-reset fixture");
    let defaults = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        defaults.cde_config,
        defaults.poly_simpl_tolerance,
        defaults.narrow_concavity_cutoff_ratio,
    );
    let instance = import_instance(&importer, &external).expect("import fixture");
    let mut problem = SPProblem::new(instance).expect("create problem");
    problem.change_strip_width(10.0).expect("set initial width");
    for (item_idx, translation) in [(0, (6.6, 2.0)), (1, (4.5, 4.1)), (2, (1.5, 2.0))] {
        problem.place_item(SPPlacement {
            item_idx,
            d_transf: DTransformation::new(0.0, translation),
        });
    }
    let initial = problem.save();
    assert!(Layout::from_snapshot(&initial.layout_snapshot).is_feasible());

    let mut config = defaults.expl_cfg;
    config.shrink_step = 0.25;
    config.large_item_ch_area_cutoff_percentile = 0.473;
    config.solution_pool_distribution_stddev = 1.0;
    config.max_conseq_failed_attempts = Some(2);
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
        limit: 4,
    };
    let mut events = Vec::new();
    exploration_phase_with_trace(
        &mut separator,
        &mut listener,
        &terminator,
        &config,
        |event| events.push(event),
    );

    let actual_insertions = events
        .iter()
        .filter_map(|event| match event {
            ExplorationTraceEvent::PoolInsert {
                failure_id,
                insertion_index,
                pool_order,
            } => Some((*failure_id, *insertion_index, pool_order.clone())),
            _ => None,
        })
        .collect::<Vec<_>>();
    assert_eq!(actual_insertions, vec![(0, 0, vec![0]), (1, 0, vec![1])]);
    let sampled_failure_ids = events
        .iter()
        .filter_map(|event| match event {
            ExplorationTraceEvent::PoolSample { failure_id, .. } => Some(*failure_id),
            _ => None,
        })
        .collect::<Vec<_>>();
    assert_eq!(sampled_failure_ids, vec![0, 1]);
    assert_eq!(
        listener
            .reports
            .iter()
            .map(|report| report.report_type)
            .collect::<Vec<_>>(),
        vec!["feasible", "infeasible", "feasible", "infeasible"]
    );
    assert_eq!(
        listener
            .reports
            .iter()
            .map(|report| report.strip_width)
            .collect::<Vec<_>>(),
        vec![10.0, 7.5, 7.5, 5.625]
    );

    let mut actual = format!(
        "case pool_reset_after_recovery 42 10.000000000 0.250000000 4 1.000000000 {} 3 2\n",
        listener.failures.len(),
    );
    for (ordinal, failure) in listener.failures.iter().enumerate() {
        writeln!(actual, "failure {ordinal} {:.9} 3", failure.loss).unwrap();
        write_poses(&mut actual, &failure.poses);
    }
    for report in &listener.reports {
        writeln!(
            actual,
            "report {} {:.9} {:.9} {}",
            report.report_type,
            report.strip_width,
            report.loss,
            u8::from(report.feasible),
        )
        .unwrap();
    }
    for event in &events {
        match event {
            ExplorationTraceEvent::PoolInsert {
                failure_id,
                insertion_index,
                pool_order,
            } => {
                write!(
                    actual,
                    "insert {failure_id} {insertion_index} {}",
                    pool_order.len(),
                )
                .unwrap();
                for candidate_id in pool_order {
                    write!(actual, " {candidate_id}").unwrap();
                }
                actual.push('\n');
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
                    actual,
                    "sample {failure_id} {:08x} {:08x} {selected_index} {consumed_u64_words} {first_layer_index:02x}",
                    raw_sample.to_bits(),
                    mapped_sample.to_bits(),
                )
                .unwrap();
                for value in following_u64 {
                    write!(actual, " {value:016x}").unwrap();
                }
                actual.push('\n');
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
                    actual,
                    "move {failure_id} {kind} {item_index} {related_item_index} {:.9} {:.9} {:.9} {:.9} {:.9} {:.9}",
                    from.rotation(),
                    from_x,
                    from_y,
                    to.rotation(),
                    to_x,
                    to_y,
                )
                .unwrap();
            }
        }
    }
    let final_solution = separator.prob.save();
    let final_layout = Layout::from_snapshot(&final_solution.layout_snapshot);
    let final_loss = CollisionTracker::new(&final_layout).get_total_loss();
    let mut final_poses = final_solution
        .layout_snapshot
        .placed_items
        .values()
        .map(|placed| {
            let (x, y) = placed.d_transf.translation();
            let bounds = placed.shape.bbox;
            PoseTrace {
                item_index: placed.item.idx,
                rotation: placed.d_transf.rotation(),
                x,
                y,
                bounds: [bounds.x_min, bounds.y_min, bounds.x_max, bounds.y_max],
            }
        })
        .collect::<Vec<_>>();
    final_poses.sort_by_key(|pose| pose.item_index);
    writeln!(actual, "final {final_loss:.9} 3").unwrap();
    write_poses(&mut actual, &final_poses);
    let expected = REFERENCE
        .lines()
        .filter(|line| !line.starts_with('#'))
        .collect::<Vec<_>>()
        .join("\n");
    assert_eq!(
        actual.trim_end(),
        expected,
        "Rust recovery/reset reference changed"
    );
}
