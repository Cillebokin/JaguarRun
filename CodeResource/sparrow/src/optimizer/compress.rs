use crate::config::{CompressionConfig, ShrinkDecayStrategy};
use crate::optimizer::separator::Separator;
use crate::util::listener::{ReportType, SolutionListener};
use crate::util::terminator::Terminator;
use jagua_rs::Instant;
use jagua_rs::probs::spp::entities::SPSolution;
use log::info;
use rand::{Rng, RngExt};
use std::time::Duration;

#[cfg(feature = "oracle-trace")]
use std::cell::RefCell;
#[cfg(feature = "oracle-trace")]
use std::marker::PhantomData;
#[cfg(feature = "oracle-trace")]
use std::rc::Rc;

#[cfg(feature = "oracle-trace")]
thread_local! {
    static COMPRESSION_CLOCK_SEQUENCE: RefCell<Option<ClockSequenceState>> = const { RefCell::new(None) };
}

#[cfg(feature = "oracle-trace")]
struct ClockSequenceState {
    samples: Vec<Duration>,
    next_sample: usize,
    exhausted: bool,
}

/// Installs deterministic compression clock samples for the oracle trace build.
/// Samples are monotonic absolute times; the first sample is the compression start.
#[cfg(feature = "oracle-trace")]
pub struct ScopedCompressionClockSequence {
    _thread_bound: PhantomData<Rc<()>>,
}

#[cfg(feature = "oracle-trace")]
impl ScopedCompressionClockSequence {
    pub fn new(samples: impl IntoIterator<Item = Duration>) -> Self {
        let samples = samples.into_iter().collect::<Vec<_>>();
        assert!(!samples.is_empty(), "compression clock sequence is empty");
        assert!(
            samples.windows(2).all(|pair| pair[0] <= pair[1]),
            "compression clock sequence is not monotonic"
        );
        COMPRESSION_CLOCK_SEQUENCE.with(|sequence| {
            let mut sequence = sequence.borrow_mut();
            assert!(sequence.is_none(), "compression clock sequence is already active");
            *sequence = Some(ClockSequenceState {
                samples,
                next_sample: 0,
                exhausted: false,
            });
        });
        Self {
            _thread_bound: PhantomData,
        }
    }

    pub fn samples_consumed(&self) -> usize {
        COMPRESSION_CLOCK_SEQUENCE.with(|sequence| {
            sequence
                .borrow()
                .as_ref()
                .map_or(0, |state| state.next_sample)
        })
    }

    pub fn exhausted(&self) -> bool {
        COMPRESSION_CLOCK_SEQUENCE.with(|sequence| {
            sequence
                .borrow()
                .as_ref()
                .is_some_and(|state| state.exhausted)
        })
    }
}

#[cfg(feature = "oracle-trace")]
impl Drop for ScopedCompressionClockSequence {
    fn drop(&mut self) {
        COMPRESSION_CLOCK_SEQUENCE.with(|sequence| *sequence.borrow_mut() = None);
    }
}

#[cfg(feature = "oracle-trace")]
fn next_compression_clock_sample() -> Option<Duration> {
    COMPRESSION_CLOCK_SEQUENCE.with(|sequence| {
        let mut sequence = sequence.borrow_mut();
        let state = sequence.as_mut()?;
        if state.next_sample >= state.samples.len() {
            state.exhausted = true;
            return state.samples.last().copied();
        }
        let sample = state.samples[state.next_sample];
        state.next_sample += 1;
        Some(sample)
    })
}

enum CompressionClockStart {
    System(Instant),
    #[cfg(feature = "oracle-trace")]
    Sequence(Duration),
}

impl CompressionClockStart {
    fn new() -> Self {
        #[cfg(feature = "oracle-trace")]
        if let Some(start) = next_compression_clock_sample() {
            return Self::Sequence(start);
        }
        Self::System(Instant::now())
    }

    fn elapsed(&self) -> Duration {
        match self {
            Self::System(start) => start.elapsed(),
            #[cfg(feature = "oracle-trace")]
            Self::Sequence(start) => next_compression_clock_sample()
                .unwrap_or(*start)
                .saturating_sub(*start),
        }
    }
}

/// Algorithm 13 from https://doi.org/10.48550/arXiv.2509.13329
pub fn compression_phase(
    sep: &mut Separator,
    init_sol: &SPSolution,
    sol_listener: &mut impl SolutionListener,
    term: &impl Terminator,
    config: &CompressionConfig,
) -> SPSolution {
    let min_width = super::minimum_strip_width(&sep.prob);
    let mut best_sol = init_sol.clone();
    let start = CompressionClockStart::new();
    let mut n_failed_attempts = 0;

    // Create the function to calculate the shrink step size.
    let shrink_step_size = |n_failed_attempts: i32| -> f32 {
        match config.shrink_decay {
            ShrinkDecayStrategy::TimeBased => {
                let range = config.shrink_range.1 - config.shrink_range.0;
                let elapsed = start.elapsed();
                let ratio = elapsed.as_secs_f32() / config.time_limit.as_secs_f32();
                config.shrink_range.0 + ratio * range
            }
            ShrinkDecayStrategy::FailureBased(r) => {
                config.shrink_range.0 * r.powi(n_failed_attempts)
            }
        }
    };

    // As long as the shrink step size is above the minimum, keep attempting to compress
    while !term.kill()
        && let step = shrink_step_size(n_failed_attempts)
        && step >= config.shrink_range.1
    {
        let width = best_sol.strip_width();
        let next_width = width * (1.0 - step);
        if next_width < min_width || next_width >= width {
            info!("[CMPR] stopping at minimum strip width: {:.3}", min_width);
            break;
        }
        sol_listener.report_compression_progress(step);
        match attempt_to_compress(sep, &best_sol, step, term, sol_listener) {
            Some(compacted_sol) => {
                info!(
                    "[CMPR] success at {:.3}% ({:.3} | {:.3}%)",
                    step * 100.0,
                    compacted_sol.strip_width(),
                    compacted_sol.density() * 100.0
                );
                sol_listener.report(ReportType::CmprFeas, &compacted_sol);
                best_sol = compacted_sol;
            }
            None => {
                info!("[CMPR] failed at {:.3}%", step * 100.0);
                n_failed_attempts += 1;
            }
        }
    }
    info!(
        "[CMPR] finished, compressed from {:.3}% to {:.3}% (+{:.3}%)",
        init_sol.density() * 100.0,
        best_sol.density() * 100.0,
        (best_sol.density() - init_sol.density()) * 100.0
    );
    best_sol
}

fn attempt_to_compress(
    sep: &mut Separator,
    init_sol: &SPSolution,
    r_shrink: f32,
    term: &impl Terminator,
    sol_listener: &mut impl SolutionListener,
) -> Option<SPSolution> {
    // Restore to the initial solution and width
    sep.change_strip_width(init_sol.strip_width(), None);
    sep.rollback(init_sol, None);

    // Shrink the container by the provided amount at a random position
    let new_width = init_sol.strip_width() * (1.0 - r_shrink);
    let split_pos = sep.rng.random_range(0.0..sep.prob.strip_width());
    sep.change_strip_width(new_width, Some(split_pos));

    // Try to separate layout, if all collisions are eliminated, return the solution
    let (compacted_sol, ot) = sep.separate(term, sol_listener);
    match ot.get_total_loss() == 0.0 {
        true => Some(compacted_sol),
        false => None,
    }
}
