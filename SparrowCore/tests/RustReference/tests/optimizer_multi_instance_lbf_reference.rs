use jagua_rs::entities::Layout;
use jagua_rs::io::import::Importer;
use jagua_rs::probs::spp::io::import_instance;
use rand::SeedableRng;
use rand::rngs::Xoshiro256PlusPlus;
use sparrow::config::DEFAULT_SPARROW_CONFIG;
use sparrow::optimizer::lbf::LBFBuilder;
use sparrow::sample::search::SampleConfig;
use std::path::Path;

const REFERENCE: &str = include_str!("../../Fixtures/SparrowLbfMultiInstanceRustReference.tsv");
const CASES: [(&str, &str); 4] = [
    ("single_item", "../Fixtures/RustSppSingleItem.json"),
    (
        "three_square_cold_start",
        "../Fixtures/RustSppOptimizeColdStart.json",
    ),
    ("mixed_shape", "../Fixtures/ExplorationPool.json"),
    (
        "three_square_boundary",
        "../Fixtures/RustSppThreeSquareBoundary.json",
    ),
];

fn run_reference() -> Vec<String> {
    let defaults = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        defaults.cde_config,
        defaults.poly_simpl_tolerance,
        defaults.narrow_concavity_cutoff_ratio,
    );
    let mut trace = Vec::new();
    for (case, input) in CASES {
        let (external, _) = sparrow::util::io::read_spp_input(Path::new(input))
            .unwrap_or_else(|error| panic!("read {case} fixture: {error}"));
        let instance = import_instance(&importer, &external)
            .unwrap_or_else(|error| panic!("import {case} fixture: {error}"));
        let builder = LBFBuilder::new(
            instance,
            Xoshiro256PlusPlus::seed_from_u64(42),
            SampleConfig {
                n_container_samples: 64,
                n_focussed_samples: 0,
                n_coord_descents: 1,
            },
        )
        .construct()
        .unwrap_or_else(|error| panic!("construct {case} fixture: {error}"));
        let solution = builder.prob.save();
        let placed = solution.layout_snapshot.placed_items.len();
        let remaining = builder.prob.item_demand_qtys.iter().sum::<usize>();
        let feasible = Layout::from_snapshot(&solution.layout_snapshot).is_feasible();
        trace.push(format!(
            "case\t{case}\t42\t{placed}\t{remaining}\t{:.9}\t{:.9}\t{feasible}",
            builder.prob.strip_width(),
            builder.prob.density(),
        ));
        let mut placements = solution
            .layout_snapshot
            .placed_items
            .values()
            .collect::<Vec<_>>();
        placements.sort_by_key(|placement| placement.item.idx);
        for placement in placements {
            let (x, y) = placement.d_transf.translation();
            trace.push(format!(
                "placement\t{case}\t{}\t{:.9}\t{:.9}\t{:.9}",
                placement.item.idx,
                placement.d_transf.rotation(),
                x,
                y,
            ));
        }
    }
    trace
}

#[test]
fn lbf_construction_matches_multi_instance_reference() {
    let actual = run_reference();
    if std::env::var_os("SPARROW_PRINT_MULTI_INSTANCE_PARITY").is_some() {
        for row in &actual {
            println!("{row}");
        }
    }
    let expected = REFERENCE
        .lines()
        .filter(|line| !line.is_empty() && !line.starts_with('#'))
        .collect::<Vec<_>>();
    let actual = actual.iter().map(String::as_str).collect::<Vec<_>>();
    assert_eq!(actual, expected, "multi-instance Rust reference changed");
}
