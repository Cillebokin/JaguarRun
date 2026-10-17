use jagua_rs::geometry::DTransformation;
use jagua_rs::io::import::Importer;
use jagua_rs::probs::spp::entities::SPPlacement;
use jagua_rs::probs::spp::entities::SPProblem;
use jagua_rs::probs::spp::io::import_instance;
use sparrow::config::DEFAULT_SPARROW_CONFIG;
use sparrow::quantify::tracker::CollisionTracker;
use std::path::Path;

const REFERENCE: &str = include_str!("../../Fixtures/SparrowPairMatrixTrackerRustReference.tsv");

fn assert_tracker_state(line: &str, phase: &str, tracker: &CollisionTracker) {
    let mut fields = line.split_whitespace();
    assert_eq!(fields.next(), Some(phase));

    for entry in &tracker.pair_collisions.data {
        assert_eq!(
            u32::from_str_radix(fields.next().expect("pair loss bits"), 16)
                .expect("valid pair loss bits"),
            entry.loss.to_bits(),
            "{phase} pair loss"
        );
        assert_eq!(
            u32::from_str_radix(fields.next().expect("pair weight bits"), 16)
                .expect("valid pair weight bits"),
            entry.weight.to_bits(),
            "{phase} pair weight"
        );
    }
    for entry in &tracker.container_collisions {
        assert_eq!(
            u32::from_str_radix(fields.next().expect("container loss bits"), 16)
                .expect("valid container loss bits"),
            entry.loss.to_bits(),
            "{phase} container loss"
        );
        assert_eq!(
            u32::from_str_radix(fields.next().expect("container weight bits"), 16)
                .expect("valid container weight bits"),
            entry.weight.to_bits(),
            "{phase} container weight"
        );
    }

    assert_eq!(
        u32::from_str_radix(fields.next().expect("total loss bits"), 16)
            .expect("valid total loss bits"),
        tracker.get_total_loss().to_bits(),
        "{phase} total loss"
    );
    assert_eq!(
        u32::from_str_radix(fields.next().expect("total weighted loss bits"), 16)
            .expect("valid total weighted loss bits"),
        tracker.get_total_weighted_loss().to_bits(),
        "{phase} total weighted loss"
    );
    assert_eq!(
        fields.next().expect("separated flag"),
        if tracker.get_total_loss() == 0.0 {
            "1"
        } else {
            "0"
        },
        "{phase} separated flag"
    );
    assert!(fields.next().is_none(), "unexpected {phase} fixture fields");
}

#[test]
fn tracker_pair_matrix_initial_update_and_move_states_match_rust_reference() {
    let (external_instance, _) =
        sparrow::util::io::read_spp_input(Path::new("../Fixtures/CollisionTrackerWeightRust.json"))
            .expect("read tracker pair-matrix reference instance");
    let config = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        config.cde_config,
        config.poly_simpl_tolerance,
        config.narrow_concavity_cutoff_ratio,
    );
    let instance = import_instance(&importer, &external_instance)
        .expect("import tracker pair-matrix reference instance");
    let mut problem = SPProblem::new(instance).expect("create tracker pair-matrix problem");
    problem
        .change_strip_width(8.0)
        .expect("widen tracker pair-matrix strip");
    let keys: Vec<_> = (0..3)
        .map(|item_idx| {
            problem.place_item(SPPlacement {
                item_idx,
                d_transf: DTransformation::new(0.0, (1.0, 1.0)),
            })
        })
        .collect();
    let mut tracker = CollisionTracker::new(&problem.layout);
    let mut reference_rows = REFERENCE
        .lines()
        .filter(|line| !line.is_empty() && !line.starts_with('#'));

    assert_tracker_state(
        reference_rows.next().expect("initial reference state"),
        "initial",
        &tracker,
    );

    for (ordinal, entry) in tracker.pair_collisions.data.iter_mut().enumerate() {
        entry.weight = 1.0 + ordinal as f32 * 0.25;
    }
    for (index, entry) in tracker.container_collisions.iter_mut().enumerate() {
        entry.weight = 1.5 + index as f32 * 0.5;
    }
    tracker.update_weights();
    assert_tracker_state(
        reference_rows.next().expect("post-update reference state"),
        "after_weight_update",
        &tracker,
    );

    let old_key = keys[2];
    let old_placement = problem.remove_item(old_key);
    let new_key = problem.place_item(SPPlacement {
        item_idx: old_placement.item_idx,
        d_transf: DTransformation::new(0.0, (4.0, 1.0)),
    });
    tracker.register_item_move(&problem.layout, old_key, new_key);
    assert_tracker_state(
        reference_rows.next().expect("post-move reference state"),
        "after_move",
        &tracker,
    );
    assert!(
        reference_rows.next().is_none(),
        "unexpected extra reference state"
    );
}
