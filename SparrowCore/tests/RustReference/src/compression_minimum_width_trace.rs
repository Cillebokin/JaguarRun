use jagua_rs::geometry::DTransformation;
use jagua_rs::io::import::Importer;
use jagua_rs::probs::spp::entities::{SPPlacement, SPProblem, SPSolution};
use jagua_rs::probs::spp::io::import_instance;
use rand::SeedableRng;
use rand::rngs::Xoshiro256PlusPlus;
use sparrow::config::{DEFAULT_SPARROW_CONFIG, ShrinkDecayStrategy};
use sparrow::optimizer::compress::ScopedCompressionClockSequence;
use sparrow::optimizer::compress::compression_phase;
use sparrow::optimizer::minimum_strip_width_for_trace;
use sparrow::optimizer::separator::Separator;
use sparrow::sample::search::SampleConfig;
use sparrow::util::listener::{ReportType, SolutionListener};
use sparrow::util::terminator::Terminator;
use std::cell::Cell;
use std::error::Error;
use std::fs;
use std::path::Path;
use std::time::Duration;

#[derive(Default)]
struct CompressionProgress(usize);

impl SolutionListener for CompressionProgress {
    fn report(&mut self, _report: ReportType, _solution: &SPSolution) {}

    fn report_compression_progress(&mut self, _shrink_step: f32) {
        self.0 += 1;
    }
}

#[derive(Default)]
struct TimeBasedThresholdProgress(Vec<f32>);

impl SolutionListener for TimeBasedThresholdProgress {
    fn report(&mut self, _report: ReportType, _solution: &SPSolution) {}

    fn report_compression_progress(&mut self, shrink_step: f32) {
        self.0.push(shrink_step);
    }
}

struct StopAfterOneAttempt {
    calls: Cell<usize>,
}

impl Terminator for StopAfterOneAttempt {
    fn kill(&self) -> bool {
        let calls = self.calls.get();
        self.calls.set(calls + 1);
        calls > 0
    }

    fn new_timeout(&mut self, _timeout: Duration) {}

    fn timeout_at(&self) -> Option<jagua_rs::Instant> {
        None
    }
}

fn load_instance() -> Result<jagua_rs::probs::spp::entities::SPInstance, Box<dyn Error>> {
    let (external, _) =
        sparrow::util::io::read_spp_input(Path::new("../Fixtures/RustSppSingleItem.json"))?;
    let defaults = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        defaults.cde_config,
        defaults.poly_simpl_tolerance,
        defaults.narrow_concavity_cutoff_ratio,
    );
    Ok(import_instance(&importer, &external)?)
}

fn make_problem() -> Result<SPProblem, Box<dyn Error>> {
    let mut problem = SPProblem::new(load_instance()?)?;
    problem.change_strip_width(4.0)?;
    problem.place_item(SPPlacement {
        item_idx: 0,
        d_transf: DTransformation::new(0.0, (2.0, 2.0)),
    });
    Ok(problem)
}

fn compression_attempt_count(step: f32) -> Result<usize, Box<dyn Error>> {
    compression_attempt_count_with_decay(step, 1.0)
}

fn compression_attempt_count_with_decay(
    step: f32,
    failure_decay: f32,
) -> Result<usize, Box<dyn Error>> {
    compression_attempt_count_for_range(step, step, failure_decay)
}

fn compression_attempt_count_for_range(
    shrink_max: f32,
    shrink_min: f32,
    failure_decay: f32,
) -> Result<usize, Box<dyn Error>> {
    let problem = make_problem()?;
    let initial = problem.save();
    let defaults = DEFAULT_SPARROW_CONFIG;
    let mut config = defaults.cmpr_cfg;
    config.shrink_range = (shrink_max, shrink_min);
    config.time_limit = Duration::from_secs(1);
    config.shrink_decay = ShrinkDecayStrategy::FailureBased(failure_decay);
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
    let mut listener = CompressionProgress::default();
    let terminator = StopAfterOneAttempt {
        calls: Cell::new(0),
    };
    let _ = compression_phase(
        &mut separator,
        &initial,
        &mut listener,
        &terminator,
        &config,
    );
    Ok(listener.0)
}

pub fn zero_duration_time_based_state() -> Result<(u32, u32, usize), Box<dyn Error>> {
    let problem = make_problem()?;
    let initial = problem.save();
    let defaults = DEFAULT_SPARROW_CONFIG;
    let mut config = defaults.cmpr_cfg;
    config.shrink_range = (0.1, 0.01);
    config.time_limit = Duration::ZERO;
    config.shrink_decay = ShrinkDecayStrategy::TimeBased;
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
    let mut listener = CompressionProgress::default();
    let terminator = StopAfterOneAttempt {
        calls: Cell::new(0),
    };
    let final_solution = compression_phase(
        &mut separator,
        &initial,
        &mut listener,
        &terminator,
        &config,
    );
    Ok((
        initial.strip_width().to_bits(),
        final_solution.strip_width().to_bits(),
        listener.0,
    ))
}

pub fn time_based_threshold_trace() -> Result<(Vec<u32>, u32, usize, bool), Box<dyn Error>> {
    let problem = make_problem()?;
    let initial = problem.save();
    let defaults = DEFAULT_SPARROW_CONFIG;
    let mut config = defaults.cmpr_cfg;
    config.shrink_range = (0.125, 0.0625);
    config.time_limit = Duration::from_secs(2);
    config.shrink_decay = ShrinkDecayStrategy::TimeBased;
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
    let mut listener = TimeBasedThresholdProgress::default();
    let terminator = sparrow::util::terminator::BasicTerminator::new();
    let clock = ScopedCompressionClockSequence::new([
        Duration::ZERO,
        Duration::ZERO,
        Duration::from_nanos(2_000_000_001),
        Duration::from_nanos(2_000_000_500),
    ]);
    let final_solution = compression_phase(
        &mut separator,
        &initial,
        &mut listener,
        &terminator,
        &config,
    );
    Ok((
        listener.0.into_iter().map(f32::to_bits).collect(),
        final_solution.strip_width().to_bits(),
        clock.samples_consumed(),
        clock.exhausted(),
    ))
}

type TimeBasedDurationTrace = (
    String,
    u64,
    u32,
    u32,
    u64,
    u64,
    usize,
    u32,
    u32,
    usize,
    bool,
);

pub fn time_based_duration_traces() -> Result<Vec<TimeBasedDurationTrace>, Box<dyn Error>> {
    let cases = [
        (
            "elapsed_seconds_rounding_before_limit",
            60_000_000_000_u64,
            0.125_f32,
            0.0625_f32,
            10_000_000_000_u64,
            69_999_999_999_u64,
        ),
        (
            "time_limit_seconds_rounding",
            59_999_999_999_u64,
            0.8_f32,
            0.0_f32,
            10_000_000_000_u64,
            40_000_000_000_u64,
        ),
        (
            "elapsed_seconds_rounding_above_limit",
            60_000_000_000_u64,
            0.125_f32,
            0.0625_f32,
            10_000_000_000_u64,
            70_000_002_000_u64,
        ),
        (
            "time_limit_seconds_rounding_above_elapsed",
            60_000_002_000_u64,
            1.0_f32,
            0.0625_f32,
            10_000_000_000_u64,
            70_000_000_000_u64,
        ),
        (
            "time_limit_seconds_rounding_equal_elapsed",
            59_999_999_999_u64,
            0.125_f32,
            0.0625_f32,
            10_000_000_000_u64,
            70_000_000_000_u64,
        ),
        (
            "maximum_time_limit_one_f32_ulp_before_deadline",
            9_223_372_036_854_775_807_u64,
            0.125_f32,
            0.0625_f32,
            0_u64,
            9_223_371_012_854_775_807_u64,
        ),
        (
            "one_nanosecond_deadline_keeps_f32_seconds_nonzero",
            1_u64,
            0.125_f32,
            0.0625_f32,
            10_000_000_000_u64,
            10_000_000_001_u64,
        ),
        (
            "one_nanosecond_at_start_uses_maximum_step",
            1_u64,
            0.125_f32,
            0.0625_f32,
            10_000_000_000_u64,
            10_000_000_000_u64,
        ),
        (
            "two_nanosecond_half_elapsed_uses_midpoint_step",
            2_u64,
            0.125_f32,
            0.0625_f32,
            10_000_000_000_u64,
            10_000_000_001_u64,
        ),
        (
            "three_nanosecond_limit_one_nanosecond_elapsed_uses_rounded_third_step",
            3_u64,
            0.125_f32,
            0.0625_f32,
            10_000_000_000_u64,
            10_000_000_001_u64,
        ),
        (
            "four_nanosecond_limit_one_nanosecond_elapsed_uses_quarter_step",
            4_u64,
            0.125_f32,
            0.0625_f32,
            10_000_000_000_u64,
            10_000_000_001_u64,
        ),
        (
            "five_nanosecond_limit_one_nanosecond_elapsed_uses_fifth_step",
            5_u64,
            0.125_f32,
            0.0625_f32,
            10_000_000_000_u64,
            10_000_000_001_u64,
        ),
        (
            "six_nanosecond_limit_one_nanosecond_elapsed_uses_sixth_step",
            6_u64,
            0.125_f32,
            0.0625_f32,
            10_000_000_000_u64,
            10_000_000_001_u64,
        ),
        (
            "seven_nanosecond_limit_one_nanosecond_elapsed_uses_seventh_step",
            7_u64,
            0.125_f32,
            0.0625_f32,
            10_000_000_000_u64,
            10_000_000_001_u64,
        ),
        (
            "eight_nanosecond_limit_one_nanosecond_elapsed_uses_eighth_step",
            8_u64,
            0.125_f32,
            0.0625_f32,
            10_000_000_000_u64,
            10_000_000_001_u64,
        ),
        (
            "nine_nanosecond_limit_one_nanosecond_elapsed_uses_ninth_step",
            9_u64,
            0.125_f32,
            0.0625_f32,
            10_000_000_000_u64,
            10_000_000_001_u64,
        ),
        (
            "ten_nanosecond_limit_one_nanosecond_elapsed_uses_tenth_step",
            10_u64,
            0.125_f32,
            0.0625_f32,
            10_000_000_000_u64,
            10_000_000_001_u64,
        ),
        (
            "eleven_nanosecond_limit_one_nanosecond_elapsed_uses_eleventh_step",
            11_u64,
            0.125_f32,
            0.0625_f32,
            10_000_000_000_u64,
            10_000_000_001_u64,
        ),
        (
            "twelve_nanosecond_limit_one_nanosecond_elapsed_uses_twelfth_step",
            12_u64,
            0.125_f32,
            0.0625_f32,
            10_000_000_000_u64,
            10_000_000_001_u64,
        ),
        (
            "thirteen_nanosecond_limit_one_nanosecond_elapsed_uses_thirteenth_step",
            13_u64,
            0.125_f32,
            0.0625_f32,
            10_000_000_000_u64,
            10_000_000_001_u64,
        ),
        (
            "fourteen_nanosecond_limit_one_nanosecond_elapsed_uses_fourteenth_step",
            14_u64,
            0.125_f32,
            0.0625_f32,
            10_000_000_000_u64,
            10_000_000_001_u64,
        ),
        (
            "fifteen_nanosecond_limit_one_nanosecond_elapsed_uses_fifteenth_step",
            15_u64,
            0.125_f32,
            0.0625_f32,
            10_000_000_000_u64,
            10_000_000_001_u64,
        ),
        (
            "equal_range_above_deadline",
            30_000_000_000_u64,
            0.0625_f32,
            0.0625_f32,
            10_000_000_000_u64,
            70_000_000_000_u64,
        ),
        (
            "unit_equal_range_stops_before_zero_width",
            1_000_000_000_u64,
            1.0_f32,
            1.0_f32,
            10_000_000_000_u64,
            10_000_000_000_u64,
        ),
        (
            "zero_equal_range_stops_on_unchanged_width",
            1_000_000_000_u64,
            0.0_f32,
            0.0_f32,
            10_000_000_000_u64,
            10_000_000_000_u64,
        ),
        (
            "full_range_midpoint_uses_half_step",
            1_000_000_000_u64,
            1.0_f32,
            0.0_f32,
            10_000_000_000_u64,
            10_500_000_000_u64,
        ),
        (
            "unequal_range_twice_deadline_stops",
            30_000_000_000_u64,
            0.125_f32,
            0.0625_f32,
            10_000_000_000_u64,
            70_000_000_000_u64,
        ),
        (
            "zero_min_at_deadline_stops_on_unchanged_width",
            30_000_000_000_u64,
            0.125_f32,
            0.0_f32,
            10_000_000_000_u64,
            40_000_000_000_u64,
        ),
        (
            "one_second_fraction_rounds_to_min_attempt",
            1_000_000_032_u64,
            0.125_f32,
            0.0625_f32,
            10_000_000_000_u64,
            11_000_000_033_u64,
        ),
        (
            "one_second_nanosecond_rounding_crosses_min",
            1_000_000_059_u64,
            0.125_f32,
            0.0625_f32,
            10_000_000_000_u64,
            11_000_000_060_u64,
        ),
        (
            "two_second_nanosecond_rounding_crosses_min",
            2_000_000_119_u64,
            0.125_f32,
            0.0625_f32,
            10_000_000_000_u64,
            12_000_000_120_u64,
        ),
        (
            "four_second_nanosecond_rounding_crosses_min",
            4_000_000_238_u64,
            0.125_f32,
            0.0625_f32,
            10_000_000_000_u64,
            14_000_000_239_u64,
        ),
        (
            "half_second_nanosecond_rounding_crosses_min",
            500_000_016_u64,
            0.125_f32,
            0.0625_f32,
            10_000_000_000_u64,
            10_500_000_017_u64,
        ),
    ];
    cases
        .into_iter()
        .map(
            |(name, time_limit_ns, shrink_max, shrink_min, start_ns, end_ns)| {
                let problem = make_problem()?;
                let initial = problem.save();
                let defaults = DEFAULT_SPARROW_CONFIG;
                let mut config = defaults.cmpr_cfg;
                config.shrink_range = (shrink_max, shrink_min);
                config.time_limit = Duration::from_nanos(time_limit_ns);
                config.shrink_decay = ShrinkDecayStrategy::TimeBased;
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
                let mut listener = TimeBasedThresholdProgress::default();
                let terminator = StopAfterOneAttempt {
                    calls: Cell::new(0),
                };
                let clock = ScopedCompressionClockSequence::new([
                    Duration::from_nanos(start_ns),
                    Duration::from_nanos(end_ns),
                ]);
                let final_solution = compression_phase(
                    &mut separator,
                    &initial,
                    &mut listener,
                    &terminator,
                    &config,
                );
                let step_bits = listener.0.first().copied().unwrap_or(0.0).to_bits();
                Ok((
                    name.to_owned(),
                    time_limit_ns,
                    shrink_max.to_bits(),
                    shrink_min.to_bits(),
                    start_ns,
                    end_ns,
                    listener.0.len(),
                    step_bits,
                    final_solution.strip_width().to_bits(),
                    clock.samples_consumed(),
                    clock.exhausted(),
                ))
            },
        )
        .collect()
}

#[cfg(test)]
#[allow(dead_code)]
pub fn failure_decay_endpoint_attempt_counts() -> Result<Vec<(f32, usize)>, Box<dyn Error>> {
    [0.0_f32, 1.0_f32]
        .into_iter()
        .map(|failure_decay| {
            compression_attempt_count_with_decay(0.01, failure_decay)
                .map(|attempts| (failure_decay, attempts))
        })
        .collect()
}

#[cfg(test)]
#[allow(dead_code)]
pub fn shrink_range_endpoint_attempt_counts() -> Result<Vec<(f32, f32, usize)>, Box<dyn Error>> {
    [(0.0_f32, 0.0_f32), (0.01, 0.0), (1.0, 1.0)]
        .into_iter()
        .map(|(shrink_max, shrink_min)| {
            compression_attempt_count_for_range(shrink_max, shrink_min, 0.9)
                .map(|attempts| (shrink_max, shrink_min, attempts))
        })
        .collect()
}

pub fn compression_minimum_width_trace_tsv() -> Result<String, Box<dyn Error>> {
    let problem = make_problem()?;
    let minimum_width = minimum_strip_width_for_trace(&problem);
    let initial_width = 4.0_f32;
    let estimated_step = 1.0_f32 - minimum_width / initial_width;
    let estimate_bits = estimated_step.to_bits() as i64;
    let mut below: Option<(f32, f32)> = None;
    let mut at_or_above: Option<(f32, f32)> = None;
    for offset in -16_i64..=16_i64 {
        let bits = estimate_bits + offset;
        if !(0..=u32::MAX as i64).contains(&bits) {
            continue;
        }
        let step = f32::from_bits(bits as u32);
        if !step.is_finite() || step <= 0.0 || step >= 1.0 {
            continue;
        }
        let next_width = initial_width * (1.0_f32 - step);
        if next_width < minimum_width && below.is_none_or(|(_, best_next)| next_width > best_next) {
            below = Some((step, next_width));
        }
        if next_width >= minimum_width
            && at_or_above.is_none_or(|(_, best_next)| next_width < best_next)
        {
            at_or_above = Some((step, next_width));
        }
    }
    let below = below.ok_or("no f32 shrink step found below minimum width")?;
    let at_or_above = at_or_above.ok_or("no f32 shrink step found at/above minimum width")?;

    let mut output = String::from(
        "# generator=Sparrow Rust optimizer::minimum_strip_width and compression_phase\n\
         # input=RustSppSingleItem.json initial_width=4.0 strip_height=4.0 2x2-square\n\
         # case minimum_width_f32_bits\n\
         # probe name shrink_step_f32_bits next_width_f32_bits should_enter_compression actual_progress_count\n\
         # time_based_threshold time_limit_ns shrink_max_bits shrink_min_bits start_ns elapsed0_ns elapsed1_ns stop_ns progress_count step0_bits step1_bits final_width_bits clock_samples_consumed clock_exhausted\n\
         # time_based_duration name time_limit_ns shrink_max_bits shrink_min_bits start_ns end_ns progress_count step_bits final_width_bits clock_samples_consumed clock_exhausted\n",
    );
    output.push_str(&format!("case square {:08x}\n", minimum_width.to_bits()));
    for (name, (step, next_width), should_enter) in
        [("below", below, false), ("at_or_above", at_or_above, true)]
    {
        let progress_count = compression_attempt_count(step)?;
        output.push_str(&format!(
            "probe {name} {:08x} {:08x} {} {progress_count}\n",
            step.to_bits(),
            next_width.to_bits(),
            u8::from(should_enter),
        ));
    }
    let no_width_change_step = f32::from_bits(0x3300_0000);
    let no_width_change_next_width = initial_width * (1.0_f32 - no_width_change_step);
    let no_width_change_progress_count = compression_attempt_count(no_width_change_step)?;
    output.push_str(&format!(
        "probe no_width_change_ties_to_even {:08x} {:08x} 0 {no_width_change_progress_count}\n",
        no_width_change_step.to_bits(),
        no_width_change_next_width.to_bits(),
    ));
    let (initial_width_bits, final_width_bits, progress_count) = zero_duration_time_based_state()?;
    output.push_str(&format!(
        "zero_time_time_based {initial_width_bits:08x} {final_width_bits:08x} {progress_count}\n"
    ));
    let (steps, final_width_bits, clock_samples_consumed, clock_exhausted) =
        time_based_threshold_trace()?;
    let [first_step_bits, threshold_step_bits] = steps.as_slice() else {
        return Err(format!(
            "expected two TimeBased threshold attempts, got {}",
            steps.len()
        )
        .into());
    };
    output.push_str(&format!(
        "time_based_threshold 2000000000 3e000000 3d800000 0 0 2000000001 2000000500 {} {first_step_bits:08x} {threshold_step_bits:08x} {final_width_bits:08x} {clock_samples_consumed} {}\n",
        steps.len(),
        u8::from(clock_exhausted),
    ));
    for (
        name,
        time_limit_ns,
        shrink_max_bits,
        shrink_min_bits,
        start_ns,
        end_ns,
        progress_count,
        step_bits,
        final_width_bits,
        clock_samples_consumed,
        clock_exhausted,
    ) in time_based_duration_traces()?
    {
        output.push_str(&format!(
            "time_based_duration {name} {time_limit_ns} {shrink_max_bits:08x} {shrink_min_bits:08x} {start_ns} {end_ns} {progress_count} {step_bits:08x} {final_width_bits:08x} {clock_samples_consumed} {}\n",
            u8::from(clock_exhausted),
        ));
    }
    Ok(output)
}

#[allow(dead_code)]
pub fn compression_infinite_minimum_width_trace() -> Result<String, Box<dyn Error>> {
    let (external, _) = sparrow::util::io::read_spp_input(Path::new(
        "../Fixtures/ExplorationMinimumWidthInfinity.json",
    ))?;
    let defaults = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        defaults.cde_config,
        defaults.poly_simpl_tolerance,
        defaults.narrow_concavity_cutoff_ratio,
    );
    let instance = import_instance(&importer, &external)?;
    let mut problem = SPProblem::new(instance)?;
    problem.change_strip_width(20.0)?;
    problem.place_item(SPPlacement {
        item_idx: 0,
        d_transf: DTransformation::new(0.0, (3.0, 2.0)),
    });
    problem.place_item(SPPlacement {
        item_idx: 1,
        d_transf: DTransformation::new(0.0, (12.0, 2.0)),
    });

    let minimum_width = minimum_strip_width_for_trace(&problem);
    let initial = problem.save();
    let defaults = DEFAULT_SPARROW_CONFIG;
    let mut config = defaults.cmpr_cfg;
    config.shrink_range = (0.25, 0.01);
    config.shrink_decay = ShrinkDecayStrategy::FailureBased(0.5);
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
    let mut listener = CompressionProgress::default();
    let final_solution = compression_phase(
        &mut separator,
        &initial,
        &mut listener,
        &StopAfterOneAttempt {
            calls: Cell::new(0),
        },
        &config,
    );

    Ok(format!(
        "# generator=Sparrow Rust optimizer::minimum_strip_width and compression_phase\n\
         # input=ExplorationMinimumWidthInfinity.json strip_height=4 initial_width=20 shrink_max=0.25 shrink_min=0.01 seed=42\n\
         # scope=initial placement of item 911 uses 0 degrees although its discrete allowed rotation is 90 degrees; only the +Inf Compression early-stop behavior is compared\n\
         # case minimum_width_f32_bits initial_width_f32_bits final_width_f32_bits compression_progress_count\n\
         case all_discrete_rotations_too_tall {:08x} {:08x} {:08x} {}\n",
        minimum_width.to_bits(),
        initial.strip_width().to_bits(),
        final_solution.strip_width().to_bits(),
        listener.0,
    ))
}

#[allow(dead_code)]
pub fn write_reference(path: &str) -> Result<(), Box<dyn Error>> {
    let output = compression_minimum_width_trace_tsv()?;
    if let Some(parent) = Path::new(path).parent() {
        fs::create_dir_all(parent)?;
    }
    fs::write(path, output)?;
    Ok(())
}
