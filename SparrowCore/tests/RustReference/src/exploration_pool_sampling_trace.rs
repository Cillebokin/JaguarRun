use jagua_rs::geometry::DTransformation;
use jagua_rs::io::import::Importer;
use jagua_rs::probs::spp::entities::{SPPlacement, SPProblem, SPSolution};
use jagua_rs::probs::spp::io::import_instance;
use rand::SeedableRng;
use rand::rngs::Xoshiro256PlusPlus;
use sparrow::config::DEFAULT_SPARROW_CONFIG;
use sparrow::optimizer::explore::{ExplorationTraceEvent, exploration_phase_with_trace};
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
struct NoopListener;

impl SolutionListener for NoopListener {
    fn report(&mut self, _report: ReportType, _solution: &SPSolution) {}
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

pub fn exploration_pool_sampling_trace_tsv() -> Result<String, Box<dyn Error>> {
    let fixture_path = Path::new("../Fixtures/ExplorationEqualLossPool.json");
    let (external, _) = sparrow::util::io::read_spp_input(fixture_path)?;
    let defaults = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        defaults.cde_config,
        defaults.poly_simpl_tolerance,
        defaults.narrow_concavity_cutoff_ratio,
    );
    let instance = import_instance(&importer, &external)?;
    let mut problem = SPProblem::new(instance).expect("create equal-loss problem");
    problem.change_strip_width(8.0)?;
    problem.place_item(SPPlacement {
        item_idx: 0,
        d_transf: DTransformation::new(0.0, (1.5, 2.0)),
    });
    problem.place_item(SPPlacement {
        item_idx: 1,
        d_transf: DTransformation::new(0.0, (6.0, 2.0)),
    });

    let mut config = defaults.expl_cfg;
    config.shrink_step = 0.1875;
    config.solution_pool_distribution_stddev = 0.000001;
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
        Xoshiro256PlusPlus::seed_from_u64(0),
        config.separator_config,
    );
    let mut listener = NoopListener;
    let terminator = StopAfterIterations {
        calls: Cell::new(0),
        limit: 6,
    };
    let mut events = Vec::new();
    exploration_phase_with_trace(
        &mut separator,
        &mut listener,
        &terminator,
        &config,
        |event| events.push(event),
    );

    let mut output = String::from(
        "# generator=actual Sparrow Rust exploration_phase with oracle-trace observer\n\
         # input=ExplorationEqualLossPool.json seed=0 stddev=1e-6 iterations=6\n\
         # insert failure_id insertion_index pool_size pool_candidate_ids...\n\
         # sample failure_id raw_normal_bits mapped_sample_bits selected_index consumed_u64_words first_layer next_u64_after_sample[0..3]\n",
    );
    output.push_str("case equal_loss 0 8.000000000 0.187500000 6 0.000001000\n");
    for event in events {
        match event {
            ExplorationTraceEvent::PoolInsert {
                failure_id,
                insertion_index,
                pool_order,
            } => {
                output.push_str(&format!(
                    "insert {failure_id} {insertion_index} {}",
                    pool_order.len(),
                ));
                for candidate_id in pool_order {
                    output.push_str(&format!(" {candidate_id}"));
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
                output.push_str(&format!(
                    "sample {failure_id} {:08x} {:08x} {selected_index} {consumed_u64_words} {first_layer_index:02x}",
                    raw_sample.to_bits(),
                    mapped_sample.to_bits(),
                ));
                for value in following_u64 {
                    output.push_str(&format!(" {value:016x}"));
                }
                output.push('\n');
            }
            ExplorationTraceEvent::DisruptionMove { .. } => {}
        }
    }
    Ok(output)
}

#[allow(dead_code)]
pub fn write_reference(path: &str) -> Result<(), Box<dyn Error>> {
    let output = exploration_pool_sampling_trace_tsv()?;
    if let Some(parent) = Path::new(path).parent() {
        fs::create_dir_all(parent)?;
    }
    fs::write(path, output)?;
    Ok(())
}
