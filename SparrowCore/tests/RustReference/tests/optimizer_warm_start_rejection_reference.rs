use jagua_rs::Instant;
use jagua_rs::collision_detection::hazards::{Hazard, HazardEntity};
use jagua_rs::entities::Layout;
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

fn placed_item_hazard(solution: &SPSolution, item_idx: usize) -> &Hazard {
    solution
        .layout_snapshot
        .cde_snapshot
        .dynamic_hazards
        .iter()
        .find(|hazard| {
            matches!(
                hazard.entity,
                HazardEntity::PlacedItem { id, .. } if id == item_idx
            )
        })
        .expect("placed-item hazard exists")
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
            partial_base.clone(),
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

    // If an earlier placement is valid, Restore must keep its demand
    // decrement when a later placement index panics.
    let mut ordered_source =
        SPProblem::new(partial_base.clone()).expect("create ordered source problem");
    ordered_source
        .change_strip_width(8.0)
        .expect("set ordered snapshot width");
    ordered_source.place_item(SPPlacement {
        item_idx: 1,
        d_transf: DTransformation::new(0.0, (1.5, 2.5)),
    });
    ordered_source.place_item(SPPlacement {
        item_idx: 2,
        d_transf: DTransformation::new(0.0, (4.5, 2.5)),
    });
    let mut ordered_solution = ordered_source.save();
    assert_eq!(ordered_solution.layout_snapshot.placed_items.len(), 2);
    for placed_item in ordered_solution.layout_snapshot.placed_items.values_mut() {
        if placed_item.item.idx == 2 {
            Arc::make_mut(&mut placed_item.item).idx = out_of_range_item_idx;
        }
    }
    for hazard in &mut ordered_solution
        .layout_snapshot
        .cde_snapshot
        .dynamic_hazards
    {
        hazard.entity = match hazard.entity {
            HazardEntity::PlacedItem { id: 2, dt, pk } => HazardEntity::PlacedItem {
                id: out_of_range_item_idx,
                dt,
                pk,
            },
            other => other,
        };
    }

    let mut ordered_target =
        SPProblem::new(partial_base.clone()).expect("create ordered target problem");
    let ordered_initial_width = ordered_target.strip.width;
    let ordered_restore = catch_unwind(AssertUnwindSafe(|| {
        ordered_target.restore(&ordered_solution);
    }));
    assert!(ordered_restore.is_err());
    assert_eq!(ordered_target.layout.placed_items.len(), 2);
    let ordered_restored_indices: Vec<_> = ordered_target
        .layout
        .placed_items
        .values()
        .map(|placed_item| placed_item.item.idx)
        .collect();
    assert_eq!(ordered_restored_indices, vec![1, out_of_range_item_idx]);
    assert_eq!(ordered_target.item_demand_qtys, vec![1, 0, 1]);
    assert_eq!(ordered_target.strip.width, ordered_solution.strip.width);
    assert_ne!(ordered_target.strip.width, ordered_initial_width);

    let mut ordered_optimize_listener = RecordingListener::default();
    let mut ordered_optimize_terminator = StopImmediately;
    let ordered_optimized = catch_unwind(AssertUnwindSafe(|| {
        optimize(
            partial_base,
            Xoshiro256PlusPlus::seed_from_u64(42),
            &mut ordered_optimize_listener,
            &mut ordered_optimize_terminator,
            &config.expl_cfg,
            &config.cmpr_cfg,
            Some(&ordered_solution),
        )
    }));
    assert!(ordered_optimized.is_err());
    assert!(ordered_optimize_listener.reports.is_empty());

    let probe_base = read_instance("../Fixtures/ExplorationEmptyLarge.json");
    let mut probe_source = SPProblem::new(probe_base.clone()).expect("create restore probe source");
    probe_source
        .change_strip_width(8.0)
        .expect("set restore probe width");
    probe_source.place_item(SPPlacement {
        item_idx: 1,
        d_transf: DTransformation::new(0.0, (1.5, 2.5)),
    });
    let probe_solution = probe_source.save();

    let mut wrong_identity = probe_solution.clone();
    let wrong_identity_hazard = &mut wrong_identity.layout_snapshot.cde_snapshot.dynamic_hazards[0];
    wrong_identity_hazard.entity = match wrong_identity_hazard.entity {
        HazardEntity::PlacedItem { id, dt, pk } => HazardEntity::PlacedItem { id: id + 1, dt, pk },
        other => other,
    };
    let mut wrong_identity_target =
        SPProblem::new(probe_base.clone()).expect("create wrong-identity target");
    let wrong_identity_initial_width = wrong_identity_target.strip.width;
    let wrong_identity_result = catch_unwind(AssertUnwindSafe(|| {
        wrong_identity_target.restore(&wrong_identity);
    }));
    let wrong_identity_after = wrong_identity_target.layout.save();
    if cfg!(debug_assertions) {
        assert!(wrong_identity_result.is_err());
        assert!(wrong_identity_after.placed_items.is_empty());
        assert!(wrong_identity_after.cde_snapshot.dynamic_hazards.is_empty());
        assert_eq!(wrong_identity_target.item_demand_qtys, vec![1, 1, 1]);
        assert_eq!(
            wrong_identity_target.strip.width,
            wrong_identity_initial_width
        );
    } else {
        assert!(wrong_identity_result.is_ok());
        assert_eq!(wrong_identity_after.placed_items.len(), 1);
        assert_eq!(wrong_identity_after.cde_snapshot.dynamic_hazards.len(), 1);
        assert!(matches!(
            wrong_identity_after.cde_snapshot.dynamic_hazards[0].entity,
            HazardEntity::PlacedItem { id: 2, .. }
        ));
        assert_eq!(wrong_identity_target.item_demand_qtys, vec![1, 0, 1]);
        assert_eq!(wrong_identity_target.strip.width, 8.0);
    }
    let mut wrong_identity_listener = RecordingListener::default();
    let mut wrong_identity_terminator = StopImmediately;
    let wrong_identity_optimized = catch_unwind(AssertUnwindSafe(|| {
        optimize(
            probe_base.clone(),
            Xoshiro256PlusPlus::seed_from_u64(42),
            &mut wrong_identity_listener,
            &mut wrong_identity_terminator,
            &config.expl_cfg,
            &config.cmpr_cfg,
            Some(&wrong_identity),
        )
    }));
    assert!(wrong_identity_optimized.is_err());
    assert!(wrong_identity_listener.reports.is_empty());

    let mut extra_hazard = probe_solution;
    extra_hazard
        .layout_snapshot
        .cde_snapshot
        .dynamic_hazards
        .push(extra_hazard.layout_snapshot.cde_snapshot.dynamic_hazards[0].clone());
    let extra_hazard_entity =
        &mut extra_hazard.layout_snapshot.cde_snapshot.dynamic_hazards[1].entity;
    *extra_hazard_entity = match *extra_hazard_entity {
        HazardEntity::PlacedItem { id, dt, pk } => HazardEntity::PlacedItem {
            id: id + 100,
            dt,
            pk,
        },
        other => other,
    };
    let mut extra_hazard_target =
        SPProblem::new(probe_base.clone()).expect("create extra-hazard target");
    let extra_hazard_initial_width = extra_hazard_target.strip.width;
    let extra_hazard_result = catch_unwind(AssertUnwindSafe(|| {
        extra_hazard_target.restore(&extra_hazard);
    }));
    let extra_hazard_after = extra_hazard_target.layout.save();
    if cfg!(debug_assertions) {
        assert!(extra_hazard_result.is_err());
        assert!(extra_hazard_after.placed_items.is_empty());
        assert!(extra_hazard_after.cde_snapshot.dynamic_hazards.is_empty());
        assert_eq!(extra_hazard_target.item_demand_qtys, vec![1, 1, 1]);
        assert_eq!(extra_hazard_target.strip.width, extra_hazard_initial_width);
    } else {
        assert!(extra_hazard_result.is_ok());
        assert_eq!(extra_hazard_after.placed_items.len(), 1);
        assert_eq!(extra_hazard_after.cde_snapshot.dynamic_hazards.len(), 2);
        assert!(matches!(
            extra_hazard_after.cde_snapshot.dynamic_hazards[0].entity,
            HazardEntity::PlacedItem { id: 1, .. }
        ));
        assert!(matches!(
            extra_hazard_after.cde_snapshot.dynamic_hazards[1].entity,
            HazardEntity::PlacedItem { id: 101, .. }
        ));
        assert_eq!(extra_hazard_target.item_demand_qtys, vec![1, 0, 1]);
        assert_eq!(extra_hazard_target.strip.width, 8.0);
    }
    let mut extra_hazard_listener = RecordingListener::default();
    let mut extra_hazard_terminator = StopImmediately;
    let extra_hazard_optimized = catch_unwind(AssertUnwindSafe(|| {
        optimize(
            probe_base.clone(),
            Xoshiro256PlusPlus::seed_from_u64(42),
            &mut extra_hazard_listener,
            &mut extra_hazard_terminator,
            &config.expl_cfg,
            &config.cmpr_cfg,
            Some(&extra_hazard),
        )
    }));
    if cfg!(debug_assertions) {
        assert!(extra_hazard_optimized.is_err());
        assert!(extra_hazard_listener.reports.is_empty());
    } else {
        assert!(matches!(extra_hazard_optimized, Ok(Ok(_))));
        assert_eq!(
            extra_hazard_listener.reports,
            vec![ReportType::ExplFeas, ReportType::Final]
        );
    }

    let mut sparse_source = SPProblem::new(probe_base.clone()).expect("create sparse-slot source");
    sparse_source
        .change_strip_width(8.0)
        .expect("set sparse-slot strip width");
    let removed_key = sparse_source.place_item(SPPlacement {
        item_idx: 1,
        d_transf: DTransformation::new(0.0, (1.5, 2.5)),
    });
    let retained_key = sparse_source.place_item(SPPlacement {
        item_idx: 2,
        d_transf: DTransformation::new(0.0, (4.5, 2.5)),
    });
    sparse_source.layout.remove_item(removed_key);
    let sparse_solution = sparse_source.save();
    assert_eq!(
        sparse_solution
            .layout_snapshot
            .placed_items
            .keys()
            .collect::<Vec<_>>(),
        vec![retained_key]
    );
    let mut sparse_target = SPProblem::new(probe_base).expect("create sparse-slot target");
    let sparse_restore = catch_unwind(AssertUnwindSafe(|| {
        sparse_target.restore(&sparse_solution);
    }));
    assert!(sparse_restore.is_ok());
    assert_eq!(
        sparse_target.layout.placed_items.keys().collect::<Vec<_>>(),
        vec![retained_key]
    );
    assert_eq!(sparse_target.item_demand_qtys, vec![1, 1, 0]);
    assert_eq!(sparse_target.strip.width, 8.0);
}

#[test]
fn restore_snapshot_cde_hazard_corruption_matches_observed_rust_profiles() {
    let base = read_instance("../Fixtures/ExplorationEmptyLarge.json");
    let mut source = SPProblem::new(base.clone()).expect("create hazard source");
    source
        .change_strip_width(8.0)
        .expect("set hazard snapshot width");
    source.place_item(SPPlacement {
        item_idx: 1,
        d_transf: DTransformation::new(0.0, (1.5, 2.5)),
    });
    source.place_item(SPPlacement {
        item_idx: 2,
        d_transf: DTransformation::new(0.0, (4.5, 2.5)),
    });
    let valid = source.save();
    let mut duplicate_entity = valid.clone();
    duplicate_entity
        .layout_snapshot
        .cde_snapshot
        .dynamic_hazards
        .push(placed_item_hazard(&valid, 1).clone());

    let mut wrong_shape = valid.clone();
    let second_shape = placed_item_hazard(&valid, 2).shape.clone();
    let first_hazard = wrong_shape
        .layout_snapshot
        .cde_snapshot
        .dynamic_hazards
        .iter_mut()
        .find(|hazard| matches!(hazard.entity, HazardEntity::PlacedItem { id: 1, .. }))
        .expect("first placed-item hazard exists");
    first_hazard.shape = second_shape;

    let second_key = match placed_item_hazard(&valid, 2).entity {
        HazardEntity::PlacedItem { pk, .. } => pk,
        other => panic!("unexpected second hazard entity: {other:?}"),
    };
    let mut wrong_placed_key = valid.clone();
    let first_key_hazard = wrong_placed_key
        .layout_snapshot
        .cde_snapshot
        .dynamic_hazards
        .iter_mut()
        .find(|hazard| matches!(hazard.entity, HazardEntity::PlacedItem { id: 1, .. }))
        .expect("first placed-item hazard exists");
    first_key_hazard.entity = match first_key_hazard.entity {
        HazardEntity::PlacedItem { id, dt, .. } => HazardEntity::PlacedItem {
            id,
            dt,
            pk: second_key,
        },
        other => panic!("unexpected first hazard entity: {other:?}"),
    };

    let mut wrong_transform = valid.clone();
    let first_transform_hazard = wrong_transform
        .layout_snapshot
        .cde_snapshot
        .dynamic_hazards
        .iter_mut()
        .find(|hazard| matches!(hazard.entity, HazardEntity::PlacedItem { id: 1, .. }))
        .expect("first placed-item hazard exists");
    first_transform_hazard.entity = match first_transform_hazard.entity {
        HazardEntity::PlacedItem { id, pk, .. } => HazardEntity::PlacedItem {
            id,
            dt: DTransformation::new(0.0, (12.5, 2.5)),
            pk,
        },
        other => panic!("unexpected first hazard entity: {other:?}"),
    };

    let mut non_dynamic_hazard = valid.clone();
    let first_hazard = non_dynamic_hazard
        .layout_snapshot
        .cde_snapshot
        .dynamic_hazards
        .iter_mut()
        .find(|hazard| matches!(hazard.entity, HazardEntity::PlacedItem { id: 1, .. }))
        .expect("first placed-item hazard exists");
    first_hazard.dynamic = false;

    let mut static_identity_collision = valid.clone();
    let first_hazard = static_identity_collision
        .layout_snapshot
        .cde_snapshot
        .dynamic_hazards
        .iter_mut()
        .find(|hazard| matches!(hazard.entity, HazardEntity::PlacedItem { id: 1, .. }))
        .expect("first placed-item hazard exists");
    first_hazard.entity = HazardEntity::Exterior;

    let mut direct_layout =
        Layout::new(static_identity_collision.layout_snapshot.container.clone());
    let direct_restore = catch_unwind(AssertUnwindSafe(|| {
        direct_layout.restore(&static_identity_collision.layout_snapshot)
    }));
    if cfg!(debug_assertions) {
        assert!(direct_restore.is_err());
        assert_eq!(direct_layout.placed_items.len(), 2);
        assert!(direct_layout.save().cde_snapshot.dynamic_hazards.is_empty());
    } else {
        assert!(direct_restore.is_ok());
        assert_eq!(direct_layout.placed_items.len(), 2);
        assert_eq!(direct_layout.save().cde_snapshot.dynamic_hazards.len(), 2);
    }

    let malformed_cases = [
        ("duplicate entity", duplicate_entity, 3),
        ("wrong shape", wrong_shape, 2),
        ("wrong placed-item key", wrong_placed_key, 2),
        ("wrong transform", wrong_transform, 2),
        ("non-dynamic hazard in dynamic list", non_dynamic_hazard, 1),
        (
            "dynamic entity collides with static exterior",
            static_identity_collision,
            2,
        ),
    ];
    for (name, malformed, expected_release_dynamic_hazards) in malformed_cases {
        let mut target = SPProblem::new(base.clone()).expect("create hazard target");
        let original_width = target.strip.width;
        let original_demand = target.item_demand_qtys.clone();
        let restored = catch_unwind(AssertUnwindSafe(|| target.restore(&malformed)));

        if cfg!(debug_assertions) {
            assert!(restored.is_err(), "{name} must fail in Rust Debug");
            assert!(target.layout.placed_items.is_empty(), "{name}");
            assert!(
                target.layout.save().cde_snapshot.dynamic_hazards.is_empty(),
                "{name}"
            );
            assert_eq!(target.item_demand_qtys, original_demand, "{name}");
            assert_eq!(target.strip.width, original_width, "{name}");
        } else {
            assert!(restored.is_ok(), "{name} must restore in Rust Release");
            assert_eq!(target.layout.placed_items.len(), 2, "{name}");
            assert_eq!(target.item_demand_qtys, vec![1, 0, 0], "{name}");
            assert_eq!(target.strip.width, 8.0, "{name}");
            assert_eq!(
                target.layout.save().cde_snapshot.dynamic_hazards.len(),
                expected_release_dynamic_hazards,
                "{name}"
            );
        }
    }
}
