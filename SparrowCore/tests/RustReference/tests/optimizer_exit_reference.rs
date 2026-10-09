use jagua_rs::entities::Layout;
use jagua_rs::io::import::Importer;
use jagua_rs::probs::spp::entities::SPSolution;
use jagua_rs::probs::spp::io::import_instance;
use rand::SeedableRng;
use rand::rngs::Xoshiro256PlusPlus;
use sparrow::config::DEFAULT_SPARROW_CONFIG;
use sparrow::optimizer::optimize;
use sparrow::util::ctrlc_terminator::CtrlCTerminator;
use sparrow::util::listener::{OptimizationPhase, ReportType, SeparationResult, SolutionListener};
use sparrow::util::terminator::{BasicTerminator, Terminator};
use std::path::Path;
use std::sync::Arc;
use std::sync::atomic::{AtomicBool, Ordering};

const FAILURE_REFERENCE: &str =
    include_str!("../../Fixtures/OptimizeConstructionFailureRustReference.tsv");
const STOP_REFERENCE: &str = include_str!("../../Fixtures/OptimizeImmediateStopRustReference.tsv");
const ZERO_TIMEOUT_REFERENCE: &str =
    include_str!("../../Fixtures/OptimizeZeroTimeoutRustReference.tsv");
const POSITIVE_TIMEOUT_REFERENCE: &str =
    include_str!("../../Fixtures/OptimizePositiveTimeoutRustReference.tsv");
const POSITIVE_TIME_BASED_TIMEOUT_REFERENCE: &str =
    include_str!("../../Fixtures/OptimizeTimeBasedPositiveTimeoutRustReference.tsv");
const POSITIVE_TIME_BASED_CURVE_REFERENCE: &str =
    include_str!("../../Fixtures/CompressionTimeBasedPositiveTimeoutRustReference.tsv");

struct TimeBasedCurveTerminator {
    timeout: Option<jagua_rs::Instant>,
    phase_stop: Arc<AtomicBool>,
}

impl Terminator for TimeBasedCurveTerminator {
    fn kill(&self) -> bool {
        self.phase_stop.load(Ordering::SeqCst)
            || self
                .timeout
                .is_some_and(|timeout| jagua_rs::Instant::now() > timeout)
    }

    fn new_timeout(&mut self, timeout: std::time::Duration) {
        self.timeout = Some(jagua_rs::Instant::now() + timeout);
    }

    fn timeout_at(&self) -> Option<jagua_rs::Instant> {
        self.timeout
    }
}

struct TimeBasedCurveListener {
    samples: Vec<(f32, f32)>,
    first_step_at: Option<jagua_rs::Instant>,
    phase_stop: Arc<AtomicBool>,
}

impl SolutionListener for TimeBasedCurveListener {
    fn report(&mut self, _report: ReportType, _solution: &SPSolution) {}

    fn report_compression_progress(&mut self, step: f32) {
        let now = jagua_rs::Instant::now();
        let first_step_at = *self.first_step_at.get_or_insert(now);
        let elapsed = now.duration_since(first_step_at).as_secs_f32();
        self.samples.push((elapsed, step));
        if self.samples.len() < 5 {
            std::thread::sleep(std::time::Duration::from_millis(400));
        } else {
            self.phase_stop.store(true, Ordering::SeqCst);
        }
    }
}

#[derive(Default)]
struct RecordingListener {
    trace: Vec<String>,
    stop: Option<Arc<AtomicBool>>,
    stop_on_initial_exploration_report: bool,
    phase: &'static str,
    callback_count: usize,
    separation_results: usize,
    compression_steps: usize,
    positive_timeout_exploration_delay: Option<std::time::Duration>,
    positive_timeout_compression_delay: Option<std::time::Duration>,
    positive_timeout_exploration_delayed: bool,
    positive_timeout_compression_delayed: bool,
}

fn report_name(report: &ReportType) -> &'static str {
    match report {
        ReportType::ExplFeas => "expl_feas",
        ReportType::ExplInfeas => "expl_infeas",
        ReportType::ExplImproving => "expl_improving",
        ReportType::CmprFeas => "cmpr_feas",
        ReportType::Final => "final",
    }
}

fn append_solution_rows(trace: &mut Vec<String>, label: &str, solution: &SPSolution) {
    trace.push(format!(
        "report\t{label}\t{}\t{:.9}\t{:.9}",
        solution.layout_snapshot.placed_items.len(),
        solution.strip_width(),
        solution.density(),
    ));
    let mut placements = solution
        .layout_snapshot
        .placed_items
        .values()
        .collect::<Vec<_>>();
    placements.sort_by_key(|placement| placement.item.idx);
    for placement in placements {
        let (x, y) = placement.d_transf.translation();
        trace.push(format!(
            "placement\t{label}\t{}\t{:.9}\t{:.9}\t{:.9}",
            placement.item.idx,
            placement.d_transf.rotation(),
            x,
            y,
        ));
    }
}

impl SolutionListener for RecordingListener {
    fn report(&mut self, report: ReportType, solution: &SPSolution) {
        self.callback_count += 1;
        let label = report_name(&report);
        append_solution_rows(&mut self.trace, label, solution);
        if self.positive_timeout_exploration_delay.is_some()
            && self.phase == "exploration"
            && report == ReportType::ExplFeas
            && !self.positive_timeout_exploration_delayed
        {
            std::thread::sleep(self.positive_timeout_exploration_delay.unwrap());
            self.positive_timeout_exploration_delayed = true;
        }
        if self.stop_on_initial_exploration_report && report == ReportType::ExplFeas {
            self.stop
                .as_ref()
                .expect("phase terminator handle")
                .store(true, Ordering::SeqCst);
            self.stop_on_initial_exploration_report = false;
        }
    }

    fn report_phase(&mut self, phase: OptimizationPhase) {
        self.callback_count += 1;
        self.phase = match phase {
            OptimizationPhase::Exploration => "exploration",
            OptimizationPhase::Compression => "compression",
        };
        self.trace.push(format!("phase\t{}", self.phase));
    }

    fn report_separation_progress(
        &mut self,
        _progress: sparrow::util::listener::SeparationProgress,
    ) {
        self.callback_count += 1;
    }

    fn report_separation_result(&mut self, result: SeparationResult) {
        self.callback_count += 1;
        self.separation_results += 1;
        self.trace.push(format!(
            "separation\t{}\t{}\t{}\t{}\t{}",
            self.phase,
            u8::from(result.success),
            result.total_evals,
            result.total_moves,
            result.iterations,
        ));
    }

    fn report_compression_progress(&mut self, shrink_step: f32) {
        self.callback_count += 1;
        self.compression_steps += 1;
        self.trace
            .push(format!("step\tcompression\t{shrink_step:.9}"));
        if self.positive_timeout_compression_delay.is_some()
            && self.phase == "compression"
            && !self.positive_timeout_compression_delayed
        {
            std::thread::sleep(self.positive_timeout_compression_delay.unwrap());
            self.positive_timeout_compression_delayed = true;
        }
    }
}

fn read_instance(path: &str) -> jagua_rs::probs::spp::entities::SPInstance {
    let (external_instance, _) = sparrow::util::io::read_spp_input(Path::new(path))
        .expect("read shared Sparrow SPP fixture");
    let config = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        config.cde_config,
        config.poly_simpl_tolerance,
        config.narrow_concavity_cutoff_ratio,
    );
    import_instance(&importer, &external_instance).expect("import shared SPP fixture")
}

fn assert_reference(actual: &[String], expected: &str, name: &str) {
    let expected = expected
        .lines()
        .filter(|line| !line.is_empty() && !line.starts_with('#'))
        .collect::<Vec<_>>();
    let observed = actual.iter().map(String::as_str).collect::<Vec<_>>();
    assert_eq!(observed, expected, "Rust {name} reference changed");
}

fn assert_time_based_timeout_reference(actual: &[String], expected: &str) {
    let expected = expected
        .lines()
        .filter(|line| !line.is_empty() && !line.starts_with('#'))
        .collect::<Vec<_>>();
    assert_eq!(
        actual.len(),
        expected.len(),
        "TimeBased timeout event count"
    );
    for (observed, expected) in actual.iter().zip(expected) {
        let observed = observed.split_whitespace().collect::<Vec<_>>();
        let expected = expected.split_whitespace().collect::<Vec<_>>();
        assert_eq!(
            observed.len(),
            expected.len(),
            "TimeBased timeout row shape"
        );
        assert_eq!(observed[0..2], expected[0..2], "TimeBased timeout event");
        let event_type = observed[0];
        match event_type {
            "report" | "placement" | "step" | "separation" => {
                for (index, (observed_value, expected_value)) in
                    observed[2..].iter().zip(&expected[2..]).enumerate()
                {
                    let observed_value = observed_value
                        .parse::<f64>()
                        .expect("numeric observed event field");
                    let expected_value = expected_value
                        .parse::<f64>()
                        .expect("numeric reference event field");
                    let field_index = index + 2;
                    let tolerance = match event_type {
                        "report" if field_index == 2 => 0.0,
                        "report" => 1.0e-5,
                        "placement" if field_index == 2 => 0.0,
                        "placement" if field_index == 3 => 1.0e-6,
                        "placement" => 5.0e-5,
                        "step" => 1.0e-6,
                        "separation" => 0.0,
                        _ => unreachable!(),
                    };
                    assert!(
                        (observed_value - expected_value).abs() <= tolerance,
                        "TimeBased timeout field differs: observed={observed_value}, expected={expected_value}, tolerance={tolerance}",
                    );
                }
            }
            _ => assert_eq!(observed, expected, "TimeBased timeout metadata row"),
        }
    }
}

#[test]
fn cold_start_construction_failure_matches_reference() {
    let instance = read_instance("../Fixtures/RustSppConstructionFailure.json");
    let mut terminator = BasicTerminator::new();
    let mut listener = RecordingListener::default();
    let error = match optimize(
        instance,
        Xoshiro256PlusPlus::seed_from_u64(42),
        &mut listener,
        &mut terminator,
        &DEFAULT_SPARROW_CONFIG.expl_cfg,
        &DEFAULT_SPARROW_CONFIG.cmpr_cfg,
        None,
    ) {
        Ok(_) => panic!("the impossible discrete-rotation fixture must fail construction"),
        Err(error) => error,
    };

    let trace = vec![
        "error_type\tConstructionError".to_owned(),
        format!("error_item\t{}", error.item_idx),
        format!("error_message\t{error}"),
        format!("callback_count\t{}", listener.callback_count),
    ];
    assert_eq!(error.item_idx, 0);
    assert_eq!(
        listener.callback_count, 0,
        "construction failure precedes callbacks"
    );
    if std::env::var_os("SPARROW_PRINT_EXIT_REFERENCES").is_some() {
        for event in &trace {
            println!("{event}");
        }
    }
    assert_reference(&trace, FAILURE_REFERENCE, "construction-failure");
}

#[test]
fn immediate_phase_stop_and_minimum_width_exit_match_reference() {
    let instance = read_instance("../Fixtures/RustSppOptimizeImmediateStop.json");
    let mut terminator = CtrlCTerminator::new();
    let mut listener = RecordingListener {
        stop: Some(Arc::clone(&terminator.ctrlc)),
        stop_on_initial_exploration_report: true,
        ..RecordingListener::default()
    };
    let final_solution = optimize(
        instance,
        Xoshiro256PlusPlus::seed_from_u64(42),
        &mut listener,
        &mut terminator,
        &DEFAULT_SPARROW_CONFIG.expl_cfg,
        &DEFAULT_SPARROW_CONFIG.cmpr_cfg,
        None,
    )
    .expect("an immediate exploration stop still returns the constructed solution");

    if std::env::var_os("SPARROW_PRINT_EXIT_REFERENCES").is_some() {
        for event in &listener.trace {
            println!("{event}");
        }
    }
    assert_eq!(final_solution.layout_snapshot.placed_items.len(), 1);
    assert!(Layout::from_snapshot(&final_solution.layout_snapshot).is_feasible());
    assert_eq!(listener.separation_results, 1);
    assert_eq!(listener.compression_steps, 1);
    assert_reference(&listener.trace, STOP_REFERENCE, "immediate-stop");
}

#[test]
fn zero_phase_timeouts_skip_separator_work_and_match_reference() {
    let instance = read_instance("../Fixtures/RustSppOptimizeImmediateStop.json");
    let mut exploration = DEFAULT_SPARROW_CONFIG.expl_cfg;
    exploration.time_limit = std::time::Duration::ZERO;
    let mut compression = DEFAULT_SPARROW_CONFIG.cmpr_cfg;
    compression.time_limit = std::time::Duration::ZERO;
    let mut terminator = BasicTerminator::new();
    terminator.new_timeout(std::time::Duration::ZERO);
    std::thread::sleep(std::time::Duration::from_millis(1));
    assert!(terminator.kill(), "a zero Rust timeout expires immediately");

    let mut listener = RecordingListener::default();
    let final_solution = optimize(
        instance,
        Xoshiro256PlusPlus::seed_from_u64(42),
        &mut listener,
        &mut terminator,
        &exploration,
        &compression,
        None,
    )
    .expect("zero-duration phases still return the constructed solution");

    assert_eq!(final_solution.layout_snapshot.placed_items.len(), 1);
    assert!(Layout::from_snapshot(&final_solution.layout_snapshot).is_feasible());
    assert_eq!(listener.separation_results, 0);
    assert_eq!(listener.compression_steps, 0);

    let mut trace = listener.trace.clone();
    trace.push(format!("separation_count\t{}", listener.separation_results));
    trace.push(format!("compression_steps\t{}", listener.compression_steps));
    if std::env::var_os("SPARROW_PRINT_EXIT_REFERENCES").is_some() {
        for event in &trace {
            println!("{event}");
        }
    }
    assert_reference(&trace, ZERO_TIMEOUT_REFERENCE, "zero-timeout");
}

#[test]
fn positive_phase_timeouts_expire_after_controlled_callbacks_match_reference() {
    let (external_instance, _) = sparrow::util::io::read_spp_input(Path::new(
        "../Fixtures/RustSppOptimizeCompressSingleItem.json",
    ))
    .expect("read shared Sparrow SPP fixture");
    let mut config = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        config.cde_config,
        config.poly_simpl_tolerance,
        config.narrow_concavity_cutoff_ratio,
    );
    let instance = import_instance(&importer, &external_instance).expect("import SPP fixture");
    let mut warm_problem = jagua_rs::probs::spp::entities::SPProblem::new(instance.clone())
        .expect("create warm-start problem");
    warm_problem
        .change_strip_width(4.0)
        .expect("set warm-start strip width");
    warm_problem.place_item(jagua_rs::probs::spp::entities::SPPlacement {
        item_idx: 0,
        d_transf: jagua_rs::geometry::DTransformation::new(0.0, (1.003, 1.01)),
    });
    let warm_start = warm_problem.save();

    let phase_timeout = std::time::Duration::from_millis(250);
    config.expl_cfg.time_limit = phase_timeout;
    config.expl_cfg.shrink_step = 0.2;
    config.expl_cfg.separator_config.iter_no_imprv_limit = 0;
    config.expl_cfg.separator_config.strike_limit = 1;
    config.expl_cfg.separator_config.n_workers = 1;
    config.expl_cfg.separator_config.sample_config = sparrow::sample::search::SampleConfig {
        n_container_samples: 8,
        n_focussed_samples: 0,
        n_coord_descents: 1,
    };
    config.cmpr_cfg.time_limit = phase_timeout;
    config.cmpr_cfg.shrink_range = (0.01, 0.01);
    config.cmpr_cfg.shrink_decay = sparrow::config::ShrinkDecayStrategy::FailureBased(0.5);
    config.cmpr_cfg.separator_config.iter_no_imprv_limit = 0;
    config.cmpr_cfg.separator_config.strike_limit = 1;
    config.cmpr_cfg.separator_config.n_workers = 1;
    config.cmpr_cfg.separator_config.sample_config = sparrow::sample::search::SampleConfig {
        n_container_samples: 8,
        n_focussed_samples: 0,
        n_coord_descents: 1,
    };

    let mut listener = RecordingListener {
        positive_timeout_exploration_delay: Some(std::time::Duration::from_millis(300)),
        positive_timeout_compression_delay: Some(std::time::Duration::from_millis(300)),
        ..RecordingListener::default()
    };
    let mut terminator = BasicTerminator::new();
    let final_solution = optimize(
        instance,
        Xoshiro256PlusPlus::seed_from_u64(3),
        &mut listener,
        &mut terminator,
        &config.expl_cfg,
        &config.cmpr_cfg,
        Some(&warm_start),
    )
    .expect("positive phase timeouts still return a feasible solution");

    assert!(listener.positive_timeout_exploration_delayed);
    assert!(listener.positive_timeout_compression_delayed);
    assert_eq!(final_solution.layout_snapshot.placed_items.len(), 1);
    assert!(
        jagua_rs::entities::Layout::from_snapshot(&final_solution.layout_snapshot).is_feasible()
    );

    let mut trace = listener.trace.clone();
    trace.push(format!("separation_count\t{}", listener.separation_results));
    trace.push(format!("compression_steps\t{}", listener.compression_steps));
    trace.push(format!(
        "timeout_exploration_delay\t{}",
        u8::from(listener.positive_timeout_exploration_delayed),
    ));
    trace.push(format!(
        "timeout_compression_delay\t{}",
        u8::from(listener.positive_timeout_compression_delayed),
    ));
    if std::env::var_os("SPARROW_PRINT_EXIT_REFERENCES").is_some() {
        for event in &trace {
            println!("{event}");
        }
    }
    assert_reference(&trace, POSITIVE_TIMEOUT_REFERENCE, "positive-timeout");
}

#[test]
fn positive_time_based_timeout_finishes_current_attempt_and_matches_reference() {
    let (external_instance, _) = sparrow::util::io::read_spp_input(Path::new(
        "../Fixtures/RustSppOptimizeCompressSingleItem.json",
    ))
    .expect("read shared Sparrow SPP fixture");
    let mut config = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        config.cde_config,
        config.poly_simpl_tolerance,
        config.narrow_concavity_cutoff_ratio,
    );
    let instance = import_instance(&importer, &external_instance).expect("import SPP fixture");
    let mut warm_problem = jagua_rs::probs::spp::entities::SPProblem::new(instance.clone())
        .expect("create warm-start problem");
    warm_problem
        .change_strip_width(4.0)
        .expect("set warm-start strip width");
    warm_problem.place_item(jagua_rs::probs::spp::entities::SPPlacement {
        item_idx: 0,
        d_transf: jagua_rs::geometry::DTransformation::new(0.0, (1.003, 1.01)),
    });
    let warm_start = warm_problem.save();

    config.expl_cfg.time_limit = std::time::Duration::ZERO;
    config.cmpr_cfg.time_limit = std::time::Duration::from_secs(2);
    config.cmpr_cfg.shrink_range = (0.08, 0.02);
    config.cmpr_cfg.shrink_decay = sparrow::config::ShrinkDecayStrategy::TimeBased;
    config.cmpr_cfg.separator_config.iter_no_imprv_limit = 0;
    config.cmpr_cfg.separator_config.strike_limit = 1;
    config.cmpr_cfg.separator_config.n_workers = 1;
    config.cmpr_cfg.separator_config.sample_config = sparrow::sample::search::SampleConfig {
        n_container_samples: 8,
        n_focussed_samples: 0,
        n_coord_descents: 1,
    };

    let mut listener = RecordingListener {
        positive_timeout_compression_delay: Some(std::time::Duration::from_millis(2100)),
        ..RecordingListener::default()
    };
    let mut terminator = BasicTerminator::new();
    let final_solution = optimize(
        instance,
        Xoshiro256PlusPlus::seed_from_u64(3),
        &mut listener,
        &mut terminator,
        &config.expl_cfg,
        &config.cmpr_cfg,
        Some(&warm_start),
    )
    .expect("TimeBased deadline still returns the completed compression attempt");

    assert!(listener.positive_timeout_compression_delayed);
    assert_eq!(listener.compression_steps, 1);
    assert_eq!(listener.separation_results, 1);
    assert!(
        terminator.kill(),
        "the positive compression deadline must expire"
    );
    assert_eq!(final_solution.layout_snapshot.placed_items.len(), 1);
    assert!(Layout::from_snapshot(&final_solution.layout_snapshot).is_feasible());
    assert!(final_solution.strip_width() < 4.0);
    let step = listener
        .trace
        .iter()
        .find(|event| event.starts_with("step\tcompression\t"))
        .expect("the started compression attempt must report its TimeBased step")
        .split_whitespace()
        .nth(2)
        .expect("compression step field")
        .parse::<f32>()
        .expect("numeric TimeBased step");
    assert!(step > 0.07 && step <= 0.08);
    assert!((final_solution.strip_width() - 4.0 * (1.0 - step)).abs() < 1.0e-5);

    let mut trace = listener.trace.clone();
    trace.push(format!("separation_count\t{}", listener.separation_results));
    trace.push(format!("compression_steps\t{}", listener.compression_steps));
    trace.push(format!(
        "timeout_exploration_delay\t{}",
        u8::from(listener.positive_timeout_exploration_delayed),
    ));
    trace.push(format!(
        "timeout_compression_delay\t{}",
        u8::from(listener.positive_timeout_compression_delayed),
    ));
    trace.push(format!("timeout_expired\t{}", u8::from(terminator.kill())));
    if std::env::var_os("SPARROW_PRINT_EXIT_REFERENCES").is_some() {
        for event in &trace {
            println!("{event}");
        }
    }
    assert_time_based_timeout_reference(&trace, POSITIVE_TIME_BASED_TIMEOUT_REFERENCE);
}

#[test]
fn positive_time_based_compression_shrink_curve_matches_reference() {
    let (external_instance, _) = sparrow::util::io::read_spp_input(Path::new(
        "../Fixtures/RustSppOptimizeCompressSingleItem.json",
    ))
    .expect("read shared Sparrow SPP fixture");
    let mut config = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        config.cde_config,
        config.poly_simpl_tolerance,
        config.narrow_concavity_cutoff_ratio,
    );
    let instance = import_instance(&importer, &external_instance).expect("import SPP fixture");
    let mut warm_problem = jagua_rs::probs::spp::entities::SPProblem::new(instance.clone())
        .expect("create warm-start problem");
    warm_problem
        .change_strip_width(4.0)
        .expect("set warm-start strip width");
    warm_problem.place_item(jagua_rs::probs::spp::entities::SPPlacement {
        item_idx: 0,
        d_transf: jagua_rs::geometry::DTransformation::new(0.0, (1.003, 1.01)),
    });
    let warm_start = warm_problem.save();

    config.expl_cfg.time_limit = std::time::Duration::ZERO;
    config.cmpr_cfg.time_limit = std::time::Duration::from_secs(2);
    config.cmpr_cfg.shrink_range = (0.08, 0.02);
    config.cmpr_cfg.shrink_decay = sparrow::config::ShrinkDecayStrategy::TimeBased;
    config.cmpr_cfg.separator_config.iter_no_imprv_limit = 0;
    config.cmpr_cfg.separator_config.strike_limit = 1;
    config.cmpr_cfg.separator_config.n_workers = 1;
    config.cmpr_cfg.separator_config.sample_config = sparrow::sample::search::SampleConfig {
        n_container_samples: 8,
        n_focussed_samples: 0,
        n_coord_descents: 1,
    };

    let phase_stop = Arc::new(AtomicBool::new(false));
    let mut listener = TimeBasedCurveListener {
        samples: Vec::new(),
        first_step_at: None,
        phase_stop: phase_stop.clone(),
    };
    let mut terminator = TimeBasedCurveTerminator {
        timeout: None,
        phase_stop: phase_stop.clone(),
    };
    let final_solution = optimize(
        instance,
        Xoshiro256PlusPlus::seed_from_u64(3),
        &mut listener,
        &mut terminator,
        &config.expl_cfg,
        &config.cmpr_cfg,
        Some(&warm_start),
    )
    .expect("run positive-timeout TimeBased compression reference");

    assert!(phase_stop.load(Ordering::SeqCst));
    assert_eq!(listener.samples.len(), 5);
    assert!(Layout::from_snapshot(&final_solution.layout_snapshot).is_feasible());

    let reference = POSITIVE_TIME_BASED_CURVE_REFERENCE
        .lines()
        .filter(|line| !line.is_empty() && !line.starts_with('#'))
        .collect::<Vec<_>>();
    assert_eq!(reference.len(), 1);
    let fields = reference[0].split_whitespace().collect::<Vec<_>>();
    assert_eq!(fields.len(), 6);
    assert_eq!(fields[0], "curve");
    assert_eq!(listener.samples.len(), fields[1].parse::<usize>().unwrap());
    let max_step = fields[2].parse::<f32>().unwrap();
    let min_step = fields[3].parse::<f32>().unwrap();
    let time_limit = fields[4].parse::<f32>().unwrap();
    let slope_tolerance = fields[5].parse::<f32>().unwrap();
    let expected_slope = (min_step - max_step) / time_limit;
    assert!((listener.samples[0].1 - max_step).abs() <= 0.001);
    for pair in listener.samples.windows(2) {
        let elapsed = pair[1].0 - pair[0].0;
        let slope = (pair[1].1 - pair[0].1) / elapsed;
        assert!(elapsed > 0.0);
        assert!(pair[1].1 < pair[0].1);
        assert!(
            (slope - expected_slope).abs() <= slope_tolerance,
            "TimeBased slope was {slope:.6} per second, expected {expected_slope:.6} ± {slope_tolerance:.6}",
        );
    }
    if std::env::var_os("SPARROW_PRINT_TIME_BASED_CURVE").is_some() {
        for (index, (elapsed, step)) in listener.samples.iter().enumerate() {
            println!("timebased\t{index}\t{elapsed:.6}\t{step:.9}");
        }
    }
}
