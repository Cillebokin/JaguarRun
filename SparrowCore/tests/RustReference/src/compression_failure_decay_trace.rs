use jagua_rs::geometry::DTransformation;
use jagua_rs::io::import::Importer;
use jagua_rs::probs::spp::entities::{SPPlacement, SPProblem, SPSolution};
use jagua_rs::probs::spp::io::import_instance;
use rand::SeedableRng;
use rand::rngs::Xoshiro256PlusPlus;
use sparrow::config::{DEFAULT_SPARROW_CONFIG, ShrinkDecayStrategy};
use sparrow::optimizer::compress::compression_phase;
use sparrow::optimizer::separator::Separator;
use sparrow::sample::search::SampleConfig;
use sparrow::util::listener::{ReportType, SeparationProgress, SeparationResult, SolutionListener};
use sparrow::util::terminator::BasicTerminator;
use std::error::Error;
use std::path::Path;
use std::time::Duration;

#[derive(Default)]
struct TraceListener {
    steps: Vec<f32>,
    results: Vec<bool>,
}

impl SolutionListener for TraceListener {
    fn report(&mut self, _report: ReportType, _solution: &SPSolution) {}

    fn report_separation_progress(&mut self, _progress: SeparationProgress) {}

    fn report_separation_result(&mut self, result: SeparationResult) {
        self.results.push(result.success);
    }

    fn report_compression_progress(&mut self, step: f32) {
        self.steps.push(step);
    }
}

pub fn failure_decay_threshold_trace_tsv() -> Result<String, Box<dyn Error>> {
    let (external_instance, _) =
        sparrow::util::io::read_spp_input(Path::new("../Fixtures/RustSppCompressWorkerMove.json"))?;
    let mut config = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        config.cde_config,
        config.poly_simpl_tolerance,
        config.narrow_concavity_cutoff_ratio,
    );
    let instance = import_instance(&importer, &external_instance)?;
    let mut problem = SPProblem::new(instance)?;
    problem.change_strip_width(4.2)?;
    problem.place_item(SPPlacement {
        item_idx: 0,
        d_transf: DTransformation::new(0.0, (1.05, 0.55)),
    });
    problem.place_item(SPPlacement {
        item_idx: 0,
        d_transf: DTransformation::new(0.0, (3.15, 0.55)),
    });
    let initial_solution = problem.save();

    config.cmpr_cfg.shrink_range = (0.2, f32::from_bits(0x3e23_d70b));
    config.cmpr_cfg.shrink_decay = ShrinkDecayStrategy::FailureBased(f32::from_bits(0x3f4c_cccd));
    config.cmpr_cfg.time_limit = Duration::from_secs(30);
    config.cmpr_cfg.separator_config.iter_no_imprv_limit = 1;
    config.cmpr_cfg.separator_config.strike_limit = 1;
    config.cmpr_cfg.separator_config.n_workers = 1;
    config.cmpr_cfg.separator_config.sample_config = SampleConfig {
        n_container_samples: 8,
        n_focussed_samples: 0,
        n_coord_descents: 1,
    };

    let mut separator = Separator::new(
        problem,
        Xoshiro256PlusPlus::seed_from_u64(42),
        config.cmpr_cfg.separator_config,
    );
    let mut listener = TraceListener::default();
    let terminator = BasicTerminator::new();
    let final_solution = compression_phase(
        &mut separator,
        &initial_solution,
        &mut listener,
        &terminator,
        &config.cmpr_cfg,
    );

    let mut output = String::from(
        "# generator=Rust sparrow::optimizer::compress::compression_phase\n\
         # input=RustSppCompressWorkerMove.json seed=42 warm_start=true workers=1\n\
         # config=failure_based shrink_max=3e4ccccd shrink_min=3e23d70b decay=3f4ccccd\n\
         case failure_decay_threshold\n\
         config 3e4ccccd 3e23d70b 3f4ccccd\n",
    );
    for (index, step) in listener.steps.iter().enumerate() {
        output.push_str(&format!("step {index} {:08x}\n", step.to_bits()));
    }
    for (index, success) in listener.results.iter().enumerate() {
        output.push_str(&format!("result {index} {}\n", u8::from(*success)));
    }
    output.push_str(&format!(
        "width {:08x} {:08x}\n",
        initial_solution.strip_width().to_bits(),
        final_solution.strip_width().to_bits(),
    ));
    Ok(output)
}
