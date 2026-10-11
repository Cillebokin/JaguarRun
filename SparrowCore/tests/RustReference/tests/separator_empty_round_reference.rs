use jagua_rs::entities::Layout;
use jagua_rs::io::import::Importer;
use jagua_rs::probs::spp::entities::SPProblem;
use jagua_rs::probs::spp::io::import_instance;
use rand::rngs::Xoshiro256PlusPlus;
use rand::{Rng, RngExt, SeedableRng};
use sparrow::config::DEFAULT_SPARROW_CONFIG;
use sparrow::optimizer::separator::Separator;
use sparrow::sample::search::SampleConfig;
use sparrow::util::listener::{ReportType, SeparationProgress, SeparationResult, SolutionListener};
use sparrow::util::terminator::BasicTerminator;
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

#[test]
fn initially_separated_layout_completes_one_empty_worker_round() {
    let (external, _) =
        sparrow::util::io::read_spp_input(Path::new("../Fixtures/RustSppSingleItem.json"))
            .expect("read empty-round reference input");
    let defaults = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        defaults.cde_config,
        defaults.poly_simpl_tolerance,
        defaults.narrow_concavity_cutoff_ratio,
    );
    let instance = import_instance(&importer, &external).expect("import empty-round instance");
    let problem = SPProblem::new(instance).expect("create empty-layout problem");
    let mut config = DEFAULT_SPARROW_CONFIG.expl_cfg.separator_config;
    config.iter_no_imprv_limit = 1;
    config.strike_limit = 1;
    config.n_workers = 1;
    config.sample_config = SampleConfig {
        n_container_samples: 0,
        n_focussed_samples: 0,
        n_coord_descents: 1,
    };

    let mut seed_source = Xoshiro256PlusPlus::seed_from_u64(42);
    let worker_seed = seed_source.random::<u64>();
    let expected_root_next = seed_source.next_u64();
    let expected_worker_next = Xoshiro256PlusPlus::seed_from_u64(worker_seed).next_u64();
    let mut separator = Separator::new(problem, Xoshiro256PlusPlus::seed_from_u64(42), config);
    let terminator = BasicTerminator::new();
    let mut listener = RecordingListener::default();
    let (solution, tracker) = separator.separate(&terminator, &mut listener);

    assert_eq!(listener.reports, 0);
    assert_eq!(
        listener
            .progress
            .iter()
            .map(|event| event.iteration)
            .collect::<Vec<_>>(),
        [0, 1]
    );
    assert!(listener.progress.iter().all(|event| event.min_loss == 0.0));
    let result = listener.result.expect("separator result callback");
    assert!(result.success);
    assert_eq!(result.total_evals, 0);
    assert_eq!(result.total_moves, 0);
    assert_eq!(result.iterations, 0);
    assert!(solution.layout_snapshot.placed_items.is_empty());
    assert!(Layout::from_snapshot(&solution.layout_snapshot).is_feasible());
    assert_eq!(tracker.get_total_loss(), 0.0);

    let root_next = separator.rng.next_u64();
    let worker_next = separator.workers[0].rng.next_u64();
    assert_eq!(root_next, expected_root_next);
    assert_eq!(worker_next, expected_worker_next);
    if std::env::var_os("SPARROW_PRINT_EMPTY_SEPARATOR_REFERENCE").is_some() {
        println!("progress_iterations\t0,1");
        println!(
            "summary\t{}\t{}\t{}\t{}",
            result.success, result.total_evals, result.total_moves, result.iterations
        );
        println!(
            "layout\t{}\t{:08x}",
            solution.layout_snapshot.placed_items.len(),
            tracker.get_total_loss().to_bits()
        );
        println!("root_rng_after\t{root_next:016x}");
        println!("worker_rng_after\t{worker_next:016x}");
    }
}
