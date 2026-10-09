use jagua_rs::entities::Layout;
use jagua_rs::io::import::Importer;
use jagua_rs::probs::spp::io::import_instance;
use rand::SeedableRng;
use rand::rngs::Xoshiro256PlusPlus;
use sparrow::config::DEFAULT_SPARROW_CONFIG;
use sparrow::optimizer::lbf::LBFBuilder;
use sparrow::sample::search::SampleConfig;
use std::path::Path;

const REFERENCE: &str = include_str!("../../Fixtures/SparrowLbfGrowthAttemptRustReference.tsv");

fn run_reference() -> String {
    let (external, _) =
        sparrow::util::io::read_spp_input(Path::new("../Fixtures/SparrowLbfGrowthAttempt.json"))
            .expect("read width-growth reference input");
    let defaults = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        defaults.cde_config,
        defaults.poly_simpl_tolerance,
        defaults.narrow_concavity_cutoff_ratio,
    );
    let instance = import_instance(&importer, &external).expect("import growth input");
    let initial_width = instance.base_strip.width;
    assert_eq!(initial_width.to_bits(), 1.0_f32.to_bits());

    let builder = LBFBuilder::new(
        instance,
        Xoshiro256PlusPlus::seed_from_u64(42),
        SampleConfig {
            n_container_samples: 1,
            n_focussed_samples: 0,
            n_coord_descents: 1,
        },
    )
    .construct()
    .expect("Rust must continue growing beyond 32 failed width attempts");
    let solution = builder.prob.save();
    let placement = solution
        .layout_snapshot
        .placed_items
        .values()
        .next()
        .expect("constructed item placement");
    let (x, y) = placement.d_transf.translation();
    let final_width = builder.prob.strip_width();
    let width_after_32 = (0..32).fold(initial_width, |width, _| width * 1.2);
    assert!(final_width > width_after_32);
    assert_eq!(builder.prob.item_demand_qtys.iter().sum::<usize>(), 0);
    let feasible = Layout::from_snapshot(&solution.layout_snapshot).is_feasible();
    assert!(feasible);

    format!(
        "growth_limit_38\t42\t{}\t0\t{:08x}\t{:08x}\t{:08x}\t{:08x}\t{}",
        solution.layout_snapshot.placed_items.len(),
        final_width.to_bits(),
        placement.d_transf.rotation().to_bits(),
        x.to_bits(),
        y.to_bits(),
        feasible,
    )
}

#[test]
fn lbf_growth_beyond_32_attempts_matches_the_locked_rust_reference() {
    let actual = run_reference();
    if std::env::var_os("SPARROW_PRINT_GROWTH_ATTEMPT_REFERENCE").is_some() {
        println!("{actual}");
        return;
    }
    let expected = REFERENCE
        .lines()
        .find(|line| !line.is_empty() && !line.starts_with('#'))
        .expect("growth-attempt Rust fixture row");
    assert_eq!(actual, expected);
}
