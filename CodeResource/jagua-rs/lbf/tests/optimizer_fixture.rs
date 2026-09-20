use anyhow::Result;
use jagua_rs::io::import::Importer;
use jagua_rs::probs::spp;
use lbf::config::LBFConfig;
use lbf::io::read_spp_instance;
use lbf::opt::lbf_spp::LBFOptimizerSP;
use rand::SeedableRng;
use rand::prelude::SmallRng;
use std::path::Path;

#[test]
fn fixed_seed_optimizer_fixture_has_stable_metrics() -> Result<()> {
    let input = read_spp_instance(Path::new(
        "../../JaguarCore/tests/fixtures/rust_reference/optimizer_spp_instance.json",
    ))?;
    let config: LBFConfig = serde_json::from_str(include_str!(
        "../../../JaguarCore/tests/fixtures/rust_reference/optimizer_lbf_config.json"
    ))?;
    let importer = Importer::new(
        config.cde_config,
        config.poly_simpl_tolerance,
        config.narrow_concavity_cutoff,
    );
    let instance = spp::io::import_instance(&importer, &input)?;
    let solution = LBFOptimizerSP::new(
        instance,
        config,
        SmallRng::seed_from_u64(42),
    )
    .solve();
    assert_eq!(solution.layout_snapshot.placed_items.len(), 3);
    assert!((solution.strip.width - 4.398323).abs() < 1.0e-5);
    assert!((solution.density() - 0.53050524).abs() < 1.0e-5);
    Ok(())
}
