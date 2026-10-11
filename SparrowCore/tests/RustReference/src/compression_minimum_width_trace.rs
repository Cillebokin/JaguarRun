use jagua_rs::geometry::DTransformation;
use jagua_rs::io::import::Importer;
use jagua_rs::probs::spp::entities::{SPPlacement, SPProblem, SPSolution};
use jagua_rs::probs::spp::io::import_instance;
use rand::SeedableRng;
use rand::rngs::Xoshiro256PlusPlus;
use sparrow::config::{DEFAULT_SPARROW_CONFIG, ShrinkDecayStrategy};
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
    let problem = make_problem()?;
    let initial = problem.save();
    let defaults = DEFAULT_SPARROW_CONFIG;
    let mut config = defaults.cmpr_cfg;
    config.shrink_range = (step, step);
    config.time_limit = Duration::from_secs(1);
    config.shrink_decay = ShrinkDecayStrategy::FailureBased(1.0);
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
         # probe name shrink_step_f32_bits next_width_f32_bits should_enter_compression actual_progress_count\n",
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
    Ok(output)
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
