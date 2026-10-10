use jagua_rs::Instant;
use jagua_rs::collision_detection::hazards::HazardEntity;
use jagua_rs::geometry::DTransformation;
use jagua_rs::io::import::Importer;
use jagua_rs::probs::spp::entities::{SPPlacement, SPProblem, SPSolution};
use jagua_rs::probs::spp::io::import_instance;
use rand::SeedableRng;
use rand::rngs::Xoshiro256PlusPlus;
use sparrow::config::DEFAULT_SPARROW_CONFIG;
use sparrow::optimizer::optimize;
use sparrow::util::listener::{ReportType, SolutionListener};
use sparrow::util::terminator::Terminator;
use std::panic::{AssertUnwindSafe, catch_unwind};
use std::path::Path;
use std::sync::Arc;
use std::time::Duration;

#[derive(Default)]
struct RecordingListener {
    reports: Vec<ReportType>,
}

impl SolutionListener for RecordingListener {
    fn report(&mut self, report: ReportType, _solution: &SPSolution) {
        self.reports.push(report);
    }
}

struct StopImmediately;

impl Terminator for StopImmediately {
    fn kill(&self) -> bool {
        true
    }

    fn new_timeout(&mut self, _timeout: Duration) {}

    fn timeout_at(&self) -> Option<Instant> {
        None
    }
}

fn read_instance(name: &str) -> jagua_rs::probs::spp::entities::SPInstance {
    let (external, _) = sparrow::util::io::read_spp_input(Path::new(name))
        .unwrap_or_else(|error| panic!("read warm-start fixture {name}: {error}"));
    let defaults = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        defaults.cde_config,
        defaults.poly_simpl_tolerance,
        defaults.narrow_concavity_cutoff_ratio,
    );
    import_instance(&importer, &external)
        .unwrap_or_else(|error| panic!("import warm-start fixture {name}: {error}"))
}

#[test]
fn warm_start_rejection_boundaries_match_observed_rust_profiles() {
    let base = read_instance("../Fixtures/ExplorationEmptyLarge.json");
    let mut source = SPProblem::new(base.clone()).expect("create source problem");
    source
        .change_strip_width(8.0)
        .expect("set warm-start width");
    source.place_item(SPPlacement {
        item_idx: 1,
        d_transf: DTransformation::new(0.0, (1.5, 2.5)),
    });
    let valid = source.save();

    // Build an over-demand snapshot through Layout so it can also be created
    // in Debug, where SPProblem::place_item guards exhausted demand.
    let mut over_demand_layout = source.layout.clone();
    over_demand_layout.place_item(
        source.instance.item(1),
        DTransformation::new(0.0, (4.5, 2.5)),
    );
    let mut over_demand = valid.clone();
    over_demand.layout_snapshot = over_demand_layout.save();
    let mut over_demand_target = SPProblem::new(base.clone()).expect("create target problem");
    let over_demand_restore = catch_unwind(AssertUnwindSafe(|| {
        over_demand_target.restore(&over_demand);
    }));
    if cfg!(debug_assertions) {
        assert!(over_demand_restore.is_err());
        assert_eq!(over_demand_target.layout.placed_items.len(), 2);
        assert_eq!(over_demand_target.strip.width, over_demand.strip.width);
        assert_eq!(over_demand_target.item_demand_qtys[1], 0);
    } else {
        assert!(over_demand_restore.is_ok());
        assert_eq!(over_demand_target.item_demand_qtys[1], usize::MAX);
    }

    // Same dense index and external ID, but a different imported definition.
    let alternate = read_instance("../Fixtures/WarmStartItemDefinitionMismatch.json");
    let mut alternate_problem = SPProblem::new(alternate).expect("create alternate problem");
    alternate_problem.place_item(SPPlacement {
        item_idx: 1,
        d_transf: DTransformation::new(0.0, (1.5, 2.5)),
    });
    let alternate_item_area = alternate_problem.instance.item(1).area();
    let mismatched_definition = alternate_problem.save();
    let mut config = DEFAULT_SPARROW_CONFIG;
    config.expl_cfg.time_limit = Duration::ZERO;
    config.cmpr_cfg.time_limit = Duration::ZERO;
    let mut listener = RecordingListener::default();
    let mut terminator = StopImmediately;
    let optimized = optimize(
        base.clone(),
        Xoshiro256PlusPlus::seed_from_u64(42),
        &mut listener,
        &mut terminator,
        &config.expl_cfg,
        &config.cmpr_cfg,
        Some(&mismatched_definition),
    );
    assert!(optimized.is_ok());
    assert_eq!(
        listener.reports,
        vec![ReportType::ExplFeas, ReportType::Final]
    );
    let optimized = optimized.unwrap();
    assert_eq!(optimized.layout_snapshot.placed_items.len(), 1);
    assert_eq!(
        optimized
            .layout_snapshot
            .placed_items
            .values()
            .next()
            .unwrap()
            .item
            .area(),
        alternate_item_area
    );

    // The restore API accepts an internally inconsistent CDE snapshot. This
    // state is recorded at the restore boundary and is not sent into optimize.
    let mut malformed_structure = valid;
    malformed_structure
        .layout_snapshot
        .cde_snapshot
        .dynamic_hazards
        .clear();
    let mut malformed_target = SPProblem::new(base).expect("create malformed target problem");
    let original_width = malformed_target.strip.width;
    let malformed_restore = catch_unwind(AssertUnwindSafe(|| {
        malformed_target.restore(&malformed_structure);
    }));
    if cfg!(debug_assertions) {
        assert!(malformed_restore.is_err());
        assert_eq!(malformed_target.layout.placed_items.len(), 0);
        assert_eq!(malformed_target.item_demand_qtys[1], 1);
        assert_eq!(malformed_target.strip.width, original_width);
    } else {
        assert!(malformed_restore.is_ok());
        assert_eq!(malformed_target.layout.placed_items.len(), 1);
        assert_eq!(malformed_target.item_demand_qtys[1], 0);
        assert_eq!(
            malformed_target
                .layout
                .save()
                .cde_snapshot
                .dynamic_hazards
                .len(),
            0
        );
    }

    // Exercise the same malformed snapshot through Sparrow's real warm-start
    // entry point. The tracker fails before reports in both Rust profiles.
    let mut malformed_optimize_listener = RecordingListener::default();
    let mut malformed_optimize_terminator = StopImmediately;
    let malformed_optimized = catch_unwind(AssertUnwindSafe(|| {
        optimize(
            read_instance("../Fixtures/ExplorationEmptyLarge.json"),
            Xoshiro256PlusPlus::seed_from_u64(42),
            &mut malformed_optimize_listener,
            &mut malformed_optimize_terminator,
            &config.expl_cfg,
            &config.cmpr_cfg,
            Some(&malformed_structure),
        )
    }));
    if cfg!(debug_assertions) {
        assert!(malformed_optimized.is_err());
        assert!(malformed_optimize_listener.reports.is_empty());
    } else {
        assert!(malformed_optimized.is_err());
        assert!(malformed_optimize_listener.reports.is_empty());
    }

    // A nonempty but incomplete dynamic-hazard snapshot must remain distinct
    // from the all-hazards-missing boundary above.
    let partial_base = read_instance("../Fixtures/ExplorationEmptyLarge.json");
    let mut partial_source =
        SPProblem::new(partial_base.clone()).expect("create partial-hazard source");
    partial_source
        .change_strip_width(8.0)
        .expect("set partial-hazard strip width");
    partial_source.place_item(SPPlacement {
        item_idx: 1,
        d_transf: DTransformation::new(0.0, (1.5, 2.5)),
    });
    partial_source.place_item(SPPlacement {
        item_idx: 2,
        d_transf: DTransformation::new(0.0, (4.5, 2.5)),
    });
    let mut partial_hazard = partial_source.save();
    assert_eq!(
        partial_hazard
            .layout_snapshot
            .cde_snapshot
            .dynamic_hazards
            .len(),
        2
    );
    partial_hazard
        .layout_snapshot
        .cde_snapshot
        .dynamic_hazards
        .remove(0);

    let mut partial_target =
        SPProblem::new(partial_base.clone()).expect("create partial-hazard target");
    let partial_original_width = partial_target.strip.width;
    let partial_restore = catch_unwind(AssertUnwindSafe(|| {
        partial_target.restore(&partial_hazard);
    }));
    if cfg!(debug_assertions) {
        assert!(partial_restore.is_err());
        assert_eq!(partial_target.layout.placed_items.len(), 0);
        assert_eq!(partial_target.item_demand_qtys[1], 1);
        assert_eq!(partial_target.item_demand_qtys[2], 1);
        assert_eq!(partial_target.strip.width, partial_original_width);
    } else {
        assert!(partial_restore.is_ok());
        assert_eq!(partial_target.layout.placed_items.len(), 2);
        assert_eq!(partial_target.item_demand_qtys[1], 0);
        assert_eq!(partial_target.item_demand_qtys[2], 0);
        assert_eq!(
            partial_target
                .layout
                .save()
                .cde_snapshot
                .dynamic_hazards
                .len(),
            1
        );
    }

    let mut partial_optimize_listener = RecordingListener::default();
    let mut partial_optimize_terminator = StopImmediately;
    let partial_optimized = catch_unwind(AssertUnwindSafe(|| {
        optimize(
            read_instance("../Fixtures/ExplorationEmptyLarge.json"),
            Xoshiro256PlusPlus::seed_from_u64(42),
            &mut partial_optimize_listener,
            &mut partial_optimize_terminator,
            &config.expl_cfg,
            &config.cmpr_cfg,
            Some(&partial_hazard),
        )
    }));
    assert!(partial_optimized.is_err());
    assert!(partial_optimize_listener.reports.is_empty());

    // A snapshot can be internally consistent while its item index is out of
    // range for the problem receiving it. Restore installs the layout and
    // strip before indexing the target demand vector, so the panic leaves that
    // restored state visible in both profiles.
    let mut out_of_range_source =
        SPProblem::new(partial_base.clone()).expect("create out-of-range source");
    out_of_range_source
        .change_strip_width(8.0)
        .expect("set out-of-range snapshot width");
    out_of_range_source.place_item(SPPlacement {
        item_idx: 2,
        d_transf: DTransformation::new(0.0, (4.5, 2.5)),
    });
    let mut out_of_range_solution = out_of_range_source.save();
    let out_of_range_item_idx = partial_base.items.len();
    assert_eq!(out_of_range_solution.layout_snapshot.placed_items.len(), 1);
    for placed_item in out_of_range_solution
        .layout_snapshot
        .placed_items
        .values_mut()
    {
        Arc::make_mut(&mut placed_item.item).idx = out_of_range_item_idx;
    }
    for hazard in &mut out_of_range_solution
        .layout_snapshot
        .cde_snapshot
        .dynamic_hazards
    {
        hazard.entity = match hazard.entity {
            HazardEntity::PlacedItem { dt, pk, .. } => HazardEntity::PlacedItem {
                id: out_of_range_item_idx,
                dt,
                pk,
            },
            other => other,
        };
    }

    let mut out_of_range_target =
        SPProblem::new(partial_base.clone()).expect("create out-of-range target");
    let out_of_range_initial_width = out_of_range_target.strip.width;
    let out_of_range_initial_demands = out_of_range_target.item_demand_qtys.clone();
    let out_of_range_restore = catch_unwind(AssertUnwindSafe(|| {
        out_of_range_target.restore(&out_of_range_solution);
    }));
    assert!(out_of_range_restore.is_err());
    assert_eq!(out_of_range_target.layout.placed_items.len(), 1);
    assert_eq!(
        out_of_range_target
            .layout
            .placed_items
            .values()
            .next()
            .unwrap()
            .item
            .idx,
        out_of_range_item_idx
    );
    assert_eq!(
        out_of_range_target.item_demand_qtys,
        out_of_range_initial_demands
    );
    assert_eq!(
        out_of_range_target.strip.width,
        out_of_range_solution.strip.width
    );
    assert_ne!(out_of_range_target.strip.width, out_of_range_initial_width);

    let mut out_of_range_optimize_listener = RecordingListener::default();
    let mut out_of_range_optimize_terminator = StopImmediately;
    let out_of_range_optimized = catch_unwind(AssertUnwindSafe(|| {
        optimize(
            partial_base,
            Xoshiro256PlusPlus::seed_from_u64(42),
            &mut out_of_range_optimize_listener,
            &mut out_of_range_optimize_terminator,
            &config.expl_cfg,
            &config.cmpr_cfg,
            Some(&out_of_range_solution),
        )
    }));
    assert!(out_of_range_optimized.is_err());
    assert!(out_of_range_optimize_listener.reports.is_empty());
}
