use jagua_rs::entities::Layout;
use jagua_rs::geometry::DTransformation;
use jagua_rs::geometry::geo_traits::TransformableFrom;
use jagua_rs::io::ext_repr::{ExtContainer, ExtPolygon, ExtQualityZone, ExtSPolygon, ExtShape};
use jagua_rs::io::import::Importer;
use jagua_rs::probs::spp::entities::{SPPlacement, SPProblem};
use jagua_rs::probs::spp::io::import_instance;
use rand::RngExt;
use rand::SeedableRng;
use rand::rngs::Xoshiro256PlusPlus;
use sparrow::config::DEFAULT_SPARROW_CONFIG;
use sparrow::optimizer::optimize;
use sparrow::optimizer::separator::Separator;
use sparrow::sample::search::SampleConfig;
use sparrow::sample::uniform_sampler::UniformBBoxSampler;
use sparrow::util::listener::{
    OptimizationPhase, ReportType, SeparationProgress, SeparationResult, SolutionListener,
};
use sparrow::util::terminator::BasicTerminator;
use std::panic::{AssertUnwindSafe, catch_unwind};
use std::path::Path;

#[derive(Default)]
struct RecordingListener {
    reports: usize,
    progress: Vec<SeparationProgress>,
    result: Option<SeparationResult>,
}

impl SolutionListener for RecordingListener {
    fn report(
        &mut self,
        _report: ReportType,
        _solution: &jagua_rs::probs::spp::entities::SPSolution,
    ) {
        self.reports += 1;
    }

    fn report_separation_progress(&mut self, progress: SeparationProgress) {
        self.progress.push(progress);
    }

    fn report_separation_result(&mut self, result: SeparationResult) {
        self.result = Some(result);
    }
}

fn rects_separated(
    left: &jagua_rs::geometry::primitives::Rect,
    right: &jagua_rs::geometry::primitives::Rect,
    margin: f32,
) -> bool {
    left.x_max + margin <= right.x_min
        || right.x_max + margin <= left.x_min
        || left.y_max + margin <= right.y_min
        || right.y_max + margin <= left.y_min
}

fn candidate_bounds(
    item: &jagua_rs::entities::Item,
    transform: DTransformation,
) -> jagua_rs::geometry::primitives::Rect {
    let mut shape = item.shape_cd.as_ref().clone();
    shape.transform_from(item.shape_cd.as_ref(), &transform.into());
    shape.bbox
}

fn find_retry_safe_seed(
    sampler: &UniformBBoxSampler,
    item: &jagua_rs::entities::Item,
) -> (u64, Vec<jagua_rs::geometry::primitives::Rect>) {
    for separator_seed in 1..100_000_u64 {
        let mut root_rng = Xoshiro256PlusPlus::seed_from_u64(separator_seed);
        let worker_seeds = [root_rng.random::<u64>(), root_rng.random::<u64>()];
        let mut first = Vec::with_capacity(2);
        let mut second = Vec::with_capacity(2);
        for worker_seed in worker_seeds {
            let mut worker_rng = Xoshiro256PlusPlus::seed_from_u64(worker_seed);
            let first_transform = sampler.sample(&mut worker_rng);
            let second_transform = sampler.sample(&mut worker_rng);
            first.push(candidate_bounds(item, first_transform));
            second.push(candidate_bounds(item, second_transform));
        }
        if !rects_separated(&first[0], &first[1], 0.25) {
            continue;
        }
        if second
            .iter()
            .any(|next| first.iter().any(|zone| !rects_separated(zone, next, 2.0)))
        {
            continue;
        }
        return (separator_seed, first);
    }
    panic!("failed to find deterministic two-worker retry fixture seed");
}

#[test]
fn multiworker_hazard_panic_propagates_and_consumes_worker_rng_before_retry() {
    let (external, _) =
        sparrow::util::io::read_spp_input(Path::new("../Fixtures/RustSppSingleItem.json"))
            .expect("read multiworker failure item");
    let defaults = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        defaults.cde_config,
        defaults.poly_simpl_tolerance,
        defaults.narrow_concavity_cutoff_ratio,
    );
    let instance = import_instance(&importer, &external).expect("import item");
    let item = instance.item(0).clone();
    let plain_container = importer
        .import_container(&ExtContainer {
            id: 9800,
            shape: ExtShape::Polygon(ExtPolygon {
                outer: ExtSPolygon(vec![(0.0, 0.0), (20.0, 0.0), (20.0, 20.0), (0.0, 20.0)]),
                inner: Vec::new(),
            }),
            zones: Vec::new(),
        })
        .expect("import plain sampling container");
    let sample_bbox = plain_container.outer_cd.bbox;
    let sampler = UniformBBoxSampler::new(sample_bbox, &item, sample_bbox)
        .expect("each worker must have a fitting sampler");
    let (separator_seed, first_candidates) = find_retry_safe_seed(&sampler, &item);
    assert_eq!(separator_seed, 1, "freeze the paired C++ worker streams");

    let zones = first_candidates
        .iter()
        .enumerate()
        .map(|(index, bounds)| ExtQualityZone {
            quality: index + 1,
            shape: ExtShape::Rectangle {
                x_min: bounds.x_min,
                y_min: bounds.y_min,
                width: bounds.width(),
                height: bounds.height(),
            },
        })
        .collect();
    let hazard_container = importer
        .import_container(&ExtContainer {
            id: 9801,
            shape: ExtShape::Polygon(ExtPolygon {
                outer: ExtSPolygon(vec![(0.0, 0.0), (20.0, 0.0), (20.0, 20.0), (0.0, 20.0)]),
                inner: Vec::new(),
            }),
            zones,
        })
        .expect("import worker-specific hazard zones");

    let mut problem = SPProblem::new(instance).expect("create SPP problem");
    problem
        .change_strip_width(20.0)
        .expect("set wide strip for candidate sampling");
    problem.layout.swap_container(hazard_container);
    problem.place_item(SPPlacement {
        item_idx: 0,
        d_transf: DTransformation::new(0.0, (19.0, 1.0)),
    });
    let initial = problem.save();
    assert!(!Layout::from_snapshot(&initial.layout_snapshot).is_feasible());

    let mut config = defaults.expl_cfg.separator_config;
    config.strike_limit = 1;
    config.iter_no_imprv_limit = 1;
    config.n_workers = 2;
    config.sample_config = SampleConfig {
        n_container_samples: 1,
        n_focussed_samples: 0,
        n_coord_descents: 1,
    };
    let mut separator = Separator::new(
        problem,
        Xoshiro256PlusPlus::seed_from_u64(separator_seed),
        config,
    );
    let initial_loss = separator.ct.get_total_loss().to_bits();
    assert_ne!(initial_loss, 0.0_f32.to_bits());
    let terminator = BasicTerminator::new();
    let mut listener = RecordingListener::default();

    let first_run = catch_unwind(AssertUnwindSafe(|| {
        separator.separate(&terminator, &mut listener)
    }));
    let payload = first_run.expect_err("both first worker samples hit unsupported QZones");
    let message = payload
        .downcast_ref::<String>()
        .map(String::as_str)
        .or_else(|| payload.downcast_ref::<&str>().copied())
        .expect("unsupported-hazard panic payload must be a string");
    assert_eq!(message, "not implemented: unsupported hazard entity");
    assert_eq!(listener.reports, 0);
    assert_eq!(listener.progress.len(), 1);
    assert_eq!(listener.progress[0].iteration, 0);
    assert!(listener.result.is_none());
    assert_eq!(separator.ct.get_total_loss().to_bits(), initial_loss);
    assert_eq!(separator.prob.save().layout_snapshot.placed_items.len(), 1);
    assert_eq!(
        separator
            .prob
            .save()
            .layout_snapshot
            .placed_items
            .values()
            .next()
            .unwrap()
            .d_transf
            .translation(),
        (19.0, 1.0)
    );

    let second_run = catch_unwind(AssertUnwindSafe(|| {
        separator.separate(&terminator, &mut listener)
    }));
    assert!(
        second_run.is_ok(),
        "worker RNG state consumed before the panic must be retained for a retry"
    );
    assert!(
        listener.result.is_some(),
        "successful retry reports completion"
    );
}

#[derive(Default)]
struct PhaseRecordingListener {
    phases: Vec<OptimizationPhase>,
    reports: Vec<ReportType>,
    progress_iterations: Vec<usize>,
    separation_results: usize,
}

impl SolutionListener for PhaseRecordingListener {
    fn report(
        &mut self,
        report: ReportType,
        _solution: &jagua_rs::probs::spp::entities::SPSolution,
    ) {
        self.reports.push(report);
    }

    fn report_phase(&mut self, phase: OptimizationPhase) {
        self.phases.push(phase);
    }

    fn report_separation_progress(&mut self, progress: SeparationProgress) {
        self.progress_iterations.push(progress.iteration);
    }

    fn report_separation_result(&mut self, _result: SeparationResult) {
        self.separation_results += 1;
    }
}

#[test]
fn multiworker_panic_stops_optimize_before_compression_and_completion_callbacks() {
    let (external, _) =
        sparrow::util::io::read_spp_input(Path::new("../Fixtures/RustSppSingleItem.json"))
            .expect("read multiworker optimizer item");
    let defaults = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        defaults.cde_config,
        defaults.poly_simpl_tolerance,
        defaults.narrow_concavity_cutoff_ratio,
    );
    let instance = import_instance(&importer, &external).expect("import item");
    let item = instance.item(0).clone();
    let outer = || {
        ExtShape::Polygon(ExtPolygon {
            outer: ExtSPolygon(vec![(0.0, 0.0), (20.0, 0.0), (20.0, 20.0), (0.0, 20.0)]),
            inner: Vec::new(),
        })
    };
    let plain_container = importer
        .import_container(&ExtContainer {
            id: 9810,
            shape: outer(),
            zones: Vec::new(),
        })
        .expect("import optimizer sample container");
    let sample_bbox = plain_container.outer_cd.bbox;
    let sampler = UniformBBoxSampler::new(sample_bbox, &item, sample_bbox)
        .expect("optimizer workers must have a fitting sampler");

    // Rust skips the construction child stream for a warm start; the current
    // C++ optimizer consumes it unconditionally. Cover both worker-stream
    // starts so this test isolates error propagation and callback boundaries.
    let mut root_rng = Xoshiro256PlusPlus::seed_from_u64(42);
    let phase_seeds = [root_rng.random::<u64>(), root_rng.random::<u64>()];
    let mut zones = Vec::new();
    for phase_seed in phase_seeds {
        let mut separator_rng = Xoshiro256PlusPlus::seed_from_u64(phase_seed);
        for _ in 0..2 {
            let worker_seed = separator_rng.random::<u64>();
            let mut worker_rng = Xoshiro256PlusPlus::seed_from_u64(worker_seed);
            let candidate = sampler.sample(&mut worker_rng);
            let bounds = candidate_bounds(&item, candidate);
            zones.push(ExtQualityZone {
                quality: zones.len() + 1,
                shape: ExtShape::Rectangle {
                    x_min: bounds.x_min,
                    y_min: bounds.y_min,
                    width: bounds.width(),
                    height: bounds.height(),
                },
            });
        }
    }
    let hazard_container = importer
        .import_container(&ExtContainer {
            id: 9811,
            shape: outer(),
            zones,
        })
        .expect("import warm-start multiworker hazard zones");
    let mut problem = SPProblem::new(instance.clone()).expect("create optimizer problem");
    problem
        .change_strip_width(20.0)
        .expect("set warm-start strip width");
    problem.layout.swap_container(hazard_container);
    problem.place_item(SPPlacement {
        item_idx: 0,
        d_transf: DTransformation::new(0.0, (19.0, 1.0)),
    });
    let initial = problem.save();
    assert!(!Layout::from_snapshot(&initial.layout_snapshot).is_feasible());

    let mut config = defaults;
    config.expl_cfg.time_limit = std::time::Duration::from_secs(10);
    config.expl_cfg.separator_config.strike_limit = 1;
    config.expl_cfg.separator_config.iter_no_imprv_limit = 1;
    config.expl_cfg.separator_config.n_workers = 2;
    config.expl_cfg.separator_config.sample_config = SampleConfig {
        n_container_samples: 1,
        n_focussed_samples: 0,
        n_coord_descents: 1,
    };

    let mut listener = PhaseRecordingListener::default();
    let mut terminator = BasicTerminator::new();
    let result = catch_unwind(AssertUnwindSafe(|| {
        optimize(
            instance,
            Xoshiro256PlusPlus::seed_from_u64(42),
            &mut listener,
            &mut terminator,
            &config.expl_cfg,
            &config.cmpr_cfg,
            Some(&initial),
        )
    }));
    let payload = result.expect_err("worker panic must escape the top-level optimizer");
    let message = payload
        .downcast_ref::<String>()
        .map(String::as_str)
        .or_else(|| payload.downcast_ref::<&str>().copied())
        .expect("unsupported-hazard panic payload must be a string");
    assert_eq!(message, "not implemented: unsupported hazard entity");
    assert_eq!(listener.phases, vec![OptimizationPhase::Exploration]);
    assert_eq!(listener.reports, vec![ReportType::ExplFeas]);
    assert_eq!(listener.progress_iterations, vec![0]);
    assert_eq!(listener.separation_results, 0);
}
