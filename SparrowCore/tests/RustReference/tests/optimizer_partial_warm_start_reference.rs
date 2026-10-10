use jagua_rs::entities::Layout;
use jagua_rs::geometry::DTransformation;
use jagua_rs::io::import::Importer;
use jagua_rs::probs::spp::entities::{SPPlacement, SPProblem, SPSolution};
use jagua_rs::probs::spp::io::import_instance;
use rand::SeedableRng;
use rand::rngs::Xoshiro256PlusPlus;
use sparrow::config::DEFAULT_SPARROW_CONFIG;
use sparrow::optimizer::optimize;
use sparrow::util::ctrlc_terminator::CtrlCTerminator;
use sparrow::util::listener::{ReportType, SolutionListener};
use std::path::Path;
use std::sync::Arc;
use std::sync::atomic::Ordering;
use std::time::Duration;

#[derive(Default)]
struct RecordingListener {
    reports: Vec<(ReportType, usize)>,
    stop: Option<Arc<std::sync::atomic::AtomicBool>>,
}

impl SolutionListener for RecordingListener {
    fn report(&mut self, report: ReportType, solution: &SPSolution) {
        self.reports
            .push((report.clone(), solution.layout_snapshot.placed_items.len()));
        if report == ReportType::ExplFeas {
            self.stop
                .as_ref()
                .expect("stop flag is wired to CtrlCTerminator")
                .store(true, Ordering::SeqCst);
        }
    }
}

#[test]
fn partial_overlapping_warm_start_is_accepted_and_reported() {
    let (external, _) =
        sparrow::util::io::read_spp_input(Path::new("../Fixtures/ExplorationEmptyLarge.json"))
            .expect("read partial warm-start fixture");
    let defaults = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        defaults.cde_config,
        defaults.poly_simpl_tolerance,
        defaults.narrow_concavity_cutoff_ratio,
    );
    let instance = import_instance(&importer, &external).expect("import fixture");
    let mut problem = SPProblem::new(instance.clone()).expect("create problem");
    problem.change_strip_width(8.0).expect("set initial width");
    problem.place_item(SPPlacement {
        item_idx: 1,
        d_transf: DTransformation::new(0.0, (1.5, 2.5)),
    });
    problem.place_item(SPPlacement {
        item_idx: 2,
        d_transf: DTransformation::new(0.0, (1.5, 2.5)),
    });
    let partial = problem.save();
    assert_eq!(partial.layout_snapshot.placed_items.len(), 2);
    assert!(!Layout::from_snapshot(&partial.layout_snapshot).is_feasible());

    let mut config = DEFAULT_SPARROW_CONFIG;
    config.expl_cfg.time_limit = Duration::ZERO;
    config.cmpr_cfg.time_limit = Duration::ZERO;
    let mut terminator = CtrlCTerminator::new();
    let mut listener = RecordingListener {
        reports: Vec::new(),
        stop: Some(Arc::clone(&terminator.ctrlc)),
    };
    let final_solution = optimize(
        instance,
        Xoshiro256PlusPlus::seed_from_u64(42),
        &mut listener,
        &mut terminator,
        &config.expl_cfg,
        &config.cmpr_cfg,
        Some(&partial),
    )
    .expect("Rust accepts a restorable partial, overlapping warm start");

    assert_eq!(
        listener.reports,
        vec![(ReportType::ExplFeas, 2), (ReportType::Final, 2)]
    );
    assert_eq!(final_solution.layout_snapshot.placed_items.len(), 2);
    assert!(!Layout::from_snapshot(&final_solution.layout_snapshot).is_feasible());
}
