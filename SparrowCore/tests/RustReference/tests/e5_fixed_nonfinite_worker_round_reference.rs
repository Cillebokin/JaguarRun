use jagua_rs::geometry::DTransformation;
use jagua_rs::io::import::Importer;
use jagua_rs::probs::spp::entities::{SPPlacement, SPProblem};
use jagua_rs::probs::spp::io::import_instance;
use ordered_float::OrderedFloat;
use rand::SeedableRng;
use rand::rngs::Xoshiro256PlusPlus;
use sparrow::config::DEFAULT_SPARROW_CONFIG;
use sparrow::optimizer::separator::Separator;
use sparrow::sample::search::SampleConfig;
use std::path::Path;

const REFERENCE: &str =
    include_str!("../../Fixtures/OptimizeMixedNonFiniteFixedWorkerRoundsRustReference.tsv");

fn make_separator(seed: u64, strip_width: f32, samples: SampleConfig) -> Separator {
    let (external, _) =
        sparrow::util::io::read_spp_input(Path::new("../Fixtures/RustSppCompressWorkerMove.json"))
            .expect("read fixed-round worker fixture");
    let defaults = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        defaults.cde_config,
        defaults.poly_simpl_tolerance,
        defaults.narrow_concavity_cutoff_ratio,
    );
    let instance =
        import_instance(&importer, &external).expect("import fixed-round worker fixture");
    let mut problem = SPProblem::new(instance).expect("create fixed-round worker problem");
    problem
        .change_strip_width(strip_width)
        .expect("set strip width");
    problem.place_item(SPPlacement {
        item_idx: 0,
        d_transf: DTransformation::new(0.0, (1.05, 0.55)),
    });
    problem.place_item(SPPlacement {
        item_idx: 0,
        d_transf: DTransformation::new(0.0, (1.55, 0.55)),
    });

    let mut config = defaults.expl_cfg.separator_config;
    config.n_workers = 3;
    config.sample_config = samples;
    Separator::new(problem, Xoshiro256PlusPlus::seed_from_u64(seed), config)
}

fn run_fixed_real_worker_rounds() -> Vec<String> {
    let mut separator = make_separator(
        4,
        3.0,
        SampleConfig {
            n_container_samples: 1,
            n_focussed_samples: 0,
            n_coord_descents: 1,
        },
    );
    let mut trace = Vec::with_capacity(450);
    for round in 0..450 {
        let master_solution = separator.prob.save();
        for worker in &mut separator.workers {
            worker.load(&master_solution, &separator.ct);
            worker.move_items();
        }
        let keys = separator
            .workers
            .iter()
            .map(|worker| worker.ct.get_total_weighted_loss())
            .collect::<Vec<_>>();
        let raw_losses = separator
            .workers
            .iter()
            .map(|worker| worker.ct.get_total_loss())
            .collect::<Vec<_>>();
        let (best_index, best_solution, best_tracker) = separator
            .workers
            .iter_mut()
            .enumerate()
            .min_by_key(|(_, worker)| OrderedFloat(worker.ct.get_total_weighted_loss()))
            .map(|(index, worker)| (index, worker.prob.save(), worker.ct.clone()))
            .expect("worker pool is non-empty");
        let mixed = keys.iter().any(|key| key.is_nan()) && keys.iter().any(|key| !key.is_nan());
        if mixed || (round >= 443 && round <= 449) {
            trace.push(format!(
                "round\t{}\tkeys\t{:08x}\t{:08x}\t{:08x}\traw\t{:08x}\t{:08x}\t{:08x}\tselected\t{}",
                round + 1,
                keys[0].to_bits(),
                keys[1].to_bits(),
                keys[2].to_bits(),
                raw_losses[0].to_bits(),
                raw_losses[1].to_bits(),
                raw_losses[2].to_bits(),
                best_index,
            ));
        }
        separator.prob.restore(&best_solution);
        separator.ct = best_tracker;
        separator.ct.update_weights();
    }
    trace.push(format!(
        "terminal\tsteps\t450\tloss\t{:08x}\tweighted\t{:08x}",
        separator.ct.get_total_loss().to_bits(),
        separator.ct.get_total_weighted_loss().to_bits(),
    ));
    trace
}

#[test]
fn fixed_real_worker_rounds_reproduce_mixed_nonfinite_aggregate() {
    let first = run_fixed_real_worker_rounds();
    let second = run_fixed_real_worker_rounds();
    assert_eq!(
        first, second,
        "fixed worker-round trace must be deterministic"
    );
    let expected = REFERENCE
        .lines()
        .filter(|line| !line.is_empty() && !line.starts_with('#'))
        .collect::<Vec<_>>()
        .join("\n");
    assert_eq!(
        first.join("\n"),
        expected,
        "fixed worker-round trace changed"
    );
}
