use jagua_rs::entities::Layout;
use jagua_rs::geometry::DTransformation;
use jagua_rs::io::import::Importer;
use jagua_rs::probs::spp::entities::{SPPlacement, SPProblem};
use jagua_rs::probs::spp::io::import_instance;
use sparrow::config::DEFAULT_SPARROW_CONFIG;
use sparrow::optimizer::minimum_strip_width_for_trace;
use std::fmt::Write as _;
use std::path::Path;

const REFERENCE: &str =
    include_str!("../../Fixtures/ExplorationMinimumWidthRotationHeightRustReference.tsv");

const CASES: [(&str, &str, f32); 6] = [
    (
        "below_rotation_height_tolerance",
        "../Fixtures/ExplorationMinimumWidthRotationHeightBelow.json",
        3.0,
    ),
    (
        "first_f32_height_accepting_90_degrees",
        "../Fixtures/ExplorationMinimumWidthRotationHeightBoundary.json",
        3.0,
    ),
    (
        "above_rotation_height_tolerance",
        "../Fixtures/ExplorationMinimumWidthRotationHeightAbove.json",
        3.0,
    ),
    (
        "oblique_below_rotation_height_tolerance",
        "../Fixtures/ExplorationMinimumWidthObliqueRotationHeightBelow.json",
        2.0,
    ),
    (
        "oblique_first_f32_height_accepting_37_degrees",
        "../Fixtures/ExplorationMinimumWidthObliqueRotationHeightBoundary.json",
        2.0,
    ),
    (
        "oblique_above_rotation_height_tolerance",
        "../Fixtures/ExplorationMinimumWidthObliqueRotationHeightAbove.json",
        2.0,
    ),
];

fn rotation_height_boundary_trace() -> String {
    let defaults = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        defaults.cde_config,
        defaults.poly_simpl_tolerance,
        defaults.narrow_concavity_cutoff_ratio,
    );
    let mut trace = String::from(
        "# generator=Sparrow Rust optimizer::minimum_strip_width\n\
         # input=8x2 rectangle; axis-aligned allowed rotations=[0,90], oblique allowed rotations=[0,37] degrees; initial width=20\n\
         # axis-aligned seed=0 radians at (5,3), container height f32 bits: 40ffffee,40ffffef,40fffff0\n\
         # oblique seed=0 radians at (5,2), container height f32 bits: 40cd2d52,40cd2d53,40cd2d54\n\
         # each triplet spans bbox.height <= container.height + diameter*1e-6; only the alternative rotation crosses the threshold\n\
         # case container_height_f32_bits minimum_width_f32_bits\n",
    );

    for (case_name, fixture_path, seed_y) in CASES {
        let (external, _) = sparrow::util::io::read_spp_input(Path::new(fixture_path))
            .expect("read discrete-rotation height fixture");
        let instance =
            import_instance(&importer, &external).expect("import discrete-rotation height fixture");
        let mut problem =
            SPProblem::new(instance).expect("create discrete-rotation height problem");
        problem
            .change_strip_width(20.0)
            .expect("set initial strip width");
        problem.place_item(SPPlacement {
            item_idx: 0,
            d_transf: DTransformation::new(0.0, (5.0, seed_y)),
        });

        // The placed 0-degree pose is allowed and stays inside each container;
        // only the alternative discrete pose crosses the height-tolerance threshold.
        let solution = problem.save();
        assert!(Layout::from_snapshot(&solution.layout_snapshot).is_feasible());
        let minimum_width = minimum_strip_width_for_trace(&problem);
        writeln!(
            &mut trace,
            "case {case_name} {:08x} {:08x}",
            external.strip_height.to_bits(),
            minimum_width.to_bits()
        )
        .expect("append reference trace");
    }

    trace
}

#[test]
fn discrete_rotation_height_tolerance_matches_locked_rust_reference() {
    assert_eq!(
        rotation_height_boundary_trace(),
        REFERENCE,
        "Rust Explore minimum-width rotation-height trace changed"
    );
}
