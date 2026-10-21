use jagua_rs::collision_detection::hazards::filter::{HazKeyFilter, HazardFilter, NoFilter};
use jagua_rs::collision_detection::CDEConfig;
use jagua_rs::entities::{Container, InferiorQualityZone, Item, Layout};
use jagua_rs::geometry::fail_fast::SPSurrogateConfig;
use jagua_rs::geometry::primitives::{Point, Rect, SPolygon};
use jagua_rs::geometry::shape_modification::{ShapeModifyConfig, ShapeModifyMode};
use jagua_rs::geometry::{geo_traits::TransformableFrom, OriginalShape};
use jagua_rs::geometry::{DTransformation, Transformation};
use jagua_rs::probs::bpp::entities::{BPInstance, Bin};
use jagua_rs::probs::spp::entities::{SPInstance, Strip};
use lbf::config::LBFConfig;
use lbf::opt::lbf_bpp::LBFOptimizerBP;
use lbf::opt::lbf_spp::LBFOptimizerSP;
use lbf::opt::loss::LBFLoss;
use lbf::opt::search::search;
use lbf::samplers::ls_sampler::{LSSampler, SD_TRANSL};
use lbf::samplers::uniform_rect_sampler::UniformRectSampler;
use rand::prelude::SmallRng;
use rand::{Rng, SeedableRng};
use rand_distr::{Distribution, Normal, StandardNormal};
use std::cmp::Ordering;
use std::fs;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::path::Path;
use std::sync::Arc;

fn bits(value: f32) -> String {
    format!("{:08x}", value.to_bits())
}

fn trace_row(
    rows: &mut Vec<String>,
    phase: &str,
    ordinal: usize,
    sample: DTransformation,
    surrogate_collision: bool,
    cost: Option<f32>,
    polygon_checked: Option<bool>,
    polygon_collision: Option<bool>,
    accepted: bool,
    before: Option<Rect>,
    after: Option<Rect>,
) {
    let (x, y) = sample.translation();
    let rect_field = |rect: Option<Rect>, field: fn(Rect) -> f32| {
        rect.map(|value| bits(field(value)))
            .unwrap_or_else(|| "-".to_owned())
    };
    let bool_field = |value: Option<bool>| match value {
        Some(true) => "1",
        Some(false) => "0",
        None => "-",
    };
    rows.push(format!(
        "{phase}\t{ordinal}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}",
        bits(sample.rotation()),
        bits(x),
        bits(y),
        u8::from(surrogate_collision),
        cost.map(bits).unwrap_or_else(|| "-".to_owned()),
        bool_field(polygon_checked),
        bool_field(polygon_collision),
        u8::from(accepted),
        rect_field(before, |r| r.x_min),
        rect_field(before, |r| r.y_min),
        rect_field(before, |r| r.x_max),
        rect_field(before, |r| r.y_max),
        rect_field(after, |r| r.x_min),
        rect_field(after, |r| r.y_min),
        rect_field(after, |r| r.x_max),
        rect_field(after, |r| r.y_max),
    ));
}

fn consumed_u64_words(before: &SmallRng, after: &SmallRng) -> usize {
    let next_after = after.clone().next_u64();
    let mut probe = before.clone();
    for words in 0..=64 {
        if probe.next_u64() == next_after {
            return words;
        }
    }
    panic!("normal sample consumed more than 64 u64 words");
}

fn write_quality_zone_filter_fixture(output_path: &str) -> anyhow::Result<()> {
    let original_shape = |rect: Rect, mode| OriginalShape {
        shape: SPolygon::from(&rect),
        pre_transform: DTransformation::empty(),
        modify_mode: mode,
        modify_config: ShapeModifyConfig::default(),
    };
    let cde_config = CDEConfig {
        quadtree_depth: 4,
        cd_threshold: 8,
        item_surrogate_config: SPSurrogateConfig {
            n_pole_limits: [(4, 0.0), (2, 0.8), (1, 0.9)],
            ff_pole_area_ratio: 0.5,
            n_ff_piers: 0,
        },
    };
    let item = Item::new(
        0,
        9101,
        original_shape(Rect::try_new(0.0, 0.0, 1.0, 1.0)?, ShapeModifyMode::Inflate),
        jagua_rs::geometry::geo_enums::RotationRange::None,
        Some(2),
        cde_config.item_surrogate_config.clone(),
    )?;
    let zones = [
        (0, Rect::try_new(8.0, 8.0, 10.0, 10.0)?),
        (1, Rect::try_new(0.0, 0.0, 3.0, 3.0)?),
        (2, Rect::try_new(15.0, 0.0, 18.0, 3.0)?),
    ]
    .into_iter()
    .map(|(quality, rect)| {
        InferiorQualityZone::new(
            quality,
            vec![original_shape(rect, ShapeModifyMode::Inflate)],
        )
    })
    .collect::<anyhow::Result<Vec<_>>>()?;
    let container = Container::new(
        original_shape(
            Rect::try_new(0.0, 0.0, 20.0, 20.0)?,
            ShapeModifyMode::Deflate,
        ),
        zones,
        cde_config,
    )?;
    let filter = HazKeyFilter::from_irrelevant_qzones(
        item.min_quality
            .expect("fixture item must require quality 2"),
        &container.base_cde.hazards_map,
    );
    let cases = [
        ("hole", 8.5, 8.5),
        ("below-min-quality", 1.0, 1.0),
        ("equal-to-min-quality", 15.5, 0.5),
    ];
    let mut contents = String::from(
        "# generator=pinned jagua-rs lbf quality-filter call path at f5e2b5ffd14931f17a20ce283a2122e877953ceb\n# item=1x1 min_quality=2 container=20x20 zones=quality0-hole,quality1,quality2; transforms use no rotation\n# case surrogate_collision polygon_collision lbf_search_invalid\n",
    );
    for (name, x, y) in cases {
        let transform = Transformation::from_translation((x, y));
        let surrogate_collision = container.base_cde.detect_surrogate_collision(
            item.shape_cd.surrogate(),
            &transform,
            &filter,
        );
        let mut shape = (*item.shape_cd).clone();
        shape.surrogate = None;
        shape.transform_from(&item.shape_cd, &transform);
        let polygon_collision =
            !surrogate_collision && container.base_cde.detect_poly_collision(&shape, &filter);
        let invalid = surrogate_collision || polygon_collision;
        contents.push_str(&format!(
            "{name}\t{}\t{}\t{}\n",
            u8::from(surrogate_collision),
            u8::from(polygon_collision),
            u8::from(invalid)
        ));
    }
    let path = Path::new(output_path);
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent)?;
    }
    fs::write(path, contents)?;
    Ok(())
}

fn write_quality_zone_bpp_fixture(output_path: &str) -> anyhow::Result<()> {
    let original_shape = |rect: Rect, mode| OriginalShape {
        shape: SPolygon::from(&rect),
        pre_transform: DTransformation::empty(),
        modify_mode: mode,
        modify_config: ShapeModifyConfig::default(),
    };
    let item_surrogate_config = SPSurrogateConfig {
        n_pole_limits: [(1, 0.0), (0, 0.0), (0, 0.0)],
        ff_pole_area_ratio: 0.5,
        n_ff_piers: 0,
    };
    let cde_config = CDEConfig {
        quadtree_depth: 4,
        cd_threshold: 8,
        item_surrogate_config: item_surrogate_config.clone(),
    };
    let item = Arc::new(Item::new(
        0,
        9201,
        original_shape(Rect::try_new(0.0, 0.0, 1.0, 1.0)?, ShapeModifyMode::Inflate),
        jagua_rs::geometry::geo_enums::RotationRange::None,
        Some(2),
        item_surrogate_config,
    )?);
    let cases = [
        ("below-min-quality", 1, 9201),
        ("equal-to-min-quality", 2, 9202),
        ("hole", 0, 9203),
    ];
    let mut contents = String::from(
        "# generator=pinned jagua-rs lbf::LBFOptimizerBP::solve at f5e2b5ffd14931f17a20ce283a2122e877953ceb\n# item=1x1 min_quality=2 bin=4x3 qzone=x0..2,y0..3 seed=42 n_samples=64 ls_frac=0.25\n# case placed_items layouts cost overlaps_qzone\n",
    );
    for (name, quality, bin_id) in cases {
        let zone = InferiorQualityZone::new(
            quality,
            vec![original_shape(
                Rect::try_new(0.0, 0.0, 2.0, 3.0)?,
                ShapeModifyMode::Inflate,
            )],
        )?;
        let container = Container::new(
            original_shape(Rect::try_new(0.0, 0.0, 4.0, 3.0)?, ShapeModifyMode::Deflate),
            vec![zone],
            cde_config.clone(),
        )?;
        let instance = BPInstance::new(
            vec![(item.clone(), 1)],
            vec![Bin::new(0, bin_id, container, 1, 19)],
        );
        let config = LBFConfig {
            cde_config: cde_config.clone(),
            poly_simpl_tolerance: None,
            narrow_concavity_cutoff: None,
            prng_seed: Some(42),
            n_samples: 64,
            ls_frac: 0.25,
            ..LBFConfig::default()
        };
        let mut optimizer =
            LBFOptimizerBP::new(instance.clone(), config, SmallRng::seed_from_u64(42));
        let solution = optimizer.solve();
        let placed_items = solution
            .layout_snapshots
            .values()
            .map(|layout| layout.placed_items.len())
            .sum::<usize>();
        let overlaps_qzone = solution
            .layout_snapshots
            .values()
            .flat_map(|layout| layout.placed_items.values())
            .any(|placed| {
                let (x, y) = placed.d_transf.translation();
                x < 2.0 && x + 1.0 > 0.0 && y < 3.0 && y + 1.0 > 0.0
            });
        contents.push_str(&format!(
            "{name}\t{placed_items}\t{}\t{}\t{}\n",
            solution.layout_snapshots.len(),
            solution.cost(&instance),
            u8::from(overlaps_qzone),
        ));
    }
    let path = Path::new(output_path);
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent)?;
    }
    fs::write(path, contents)?;
    Ok(())
}

fn write_small_lbf_fixtures(output_path: &str) -> anyhow::Result<()> {
    let original_shape = |rect: Rect, mode| OriginalShape {
        shape: SPolygon::from(&rect),
        pre_transform: DTransformation::empty(),
        modify_mode: mode,
        modify_config: ShapeModifyConfig::default(),
    };
    let item_surrogate_config = SPSurrogateConfig {
        n_pole_limits: [(1, 0.0), (0, 0.0), (0, 0.0)],
        ff_pole_area_ratio: 0.0,
        n_ff_piers: 0,
    };

    let spp_cde_config = CDEConfig {
        quadtree_depth: 4,
        cd_threshold: 32,
        item_surrogate_config: SPSurrogateConfig::none(),
    };
    let spp_item = Arc::new(Item::new(
        0,
        201,
        original_shape(Rect::try_new(0.0, 0.0, 2.0, 2.0)?, ShapeModifyMode::Inflate),
        jagua_rs::geometry::geo_enums::RotationRange::None,
        None,
        item_surrogate_config.clone(),
    )?);
    let spp_instance = SPInstance::new(
        vec![(spp_item, 2)],
        Strip::new(
            4.0,
            spp_cde_config.clone(),
            ShapeModifyConfig::default(),
            2.0,
        )?,
    );
    let spp_config = LBFConfig {
        cde_config: spp_cde_config,
        poly_simpl_tolerance: None,
        narrow_concavity_cutoff: None,
        prng_seed: Some(42),
        n_samples: 64,
        ls_frac: 0.25,
        ..LBFConfig::default()
    };
    let mut spp_optimizer =
        LBFOptimizerSP::new(spp_instance, spp_config, SmallRng::seed_from_u64(42))?;
    let spp_solution = spp_optimizer.solve()?;
    let spp_placed = spp_solution.layout_snapshot.placed_items.len();
    let spp_remaining = 2usize.saturating_sub(spp_placed);

    let bpp_cde_config = CDEConfig {
        quadtree_depth: 4,
        cd_threshold: 8,
        item_surrogate_config: SPSurrogateConfig::none(),
    };
    let bpp_item = Arc::new(Item::new(
        0,
        301,
        original_shape(Rect::try_new(0.0, 0.0, 2.0, 2.0)?, ShapeModifyMode::Inflate),
        jagua_rs::geometry::geo_enums::RotationRange::None,
        None,
        item_surrogate_config,
    )?);
    let bpp_container = Container::new(
        original_shape(Rect::try_new(0.0, 0.0, 6.0, 6.0)?, ShapeModifyMode::Deflate),
        vec![],
        bpp_cde_config.clone(),
    )?;
    let bpp_instance = BPInstance::new(
        vec![(bpp_item, 1)],
        vec![Bin::new(0, 401, bpp_container, 1, 10)],
    );
    let bpp_config = LBFConfig {
        cde_config: bpp_cde_config,
        poly_simpl_tolerance: None,
        narrow_concavity_cutoff: None,
        prng_seed: Some(42),
        n_samples: 64,
        ls_frac: 0.25,
        ..LBFConfig::default()
    };
    let mut bpp_optimizer = LBFOptimizerBP::new(
        bpp_instance.clone(),
        bpp_config,
        SmallRng::seed_from_u64(42),
    );
    let bpp_solution = bpp_optimizer.solve();
    let bpp_placed = bpp_solution
        .layout_snapshots
        .values()
        .map(|layout| layout.placed_items.len())
        .sum::<usize>();
    let bpp_remaining = 1usize.saturating_sub(bpp_placed);
    let bpp_feasible = bpp_placed == 1 && bpp_remaining == 0;

    let contents = format!(
        "# generator=pinned jagua-rs lbf::LBFOptimizerSP/BP::solve at f5e2b5ffd14931f17a20ce283a2122e877953ceb\n# inputs match SparrowCore::Tests::TestSmallSppFixture and TestSmallBppFixture; item=2x2, seed=42, n_samples=64, ls_frac=0.25; SPP CDE=(depth:4,threshold:32), BPP CDE=(depth:4,threshold:8); item poles=(1,0),(0,0),(0,0)\n# fixture seed samples placed remaining width density [cost feasible]\nlbf_spp_small_cpp 42 {} {} {} {:.17} {:.17}\nlbf_bpp_small_cpp 42 {} {} {} 0 {:.17} {} {}\n",
        spp_optimizer.sample_counter,
        spp_placed,
        spp_remaining,
        spp_solution.strip_width(),
        spp_solution.density(),
        bpp_optimizer.sample_counter,
        bpp_placed,
        bpp_remaining,
        bpp_solution.density(),
        bpp_solution.cost(&bpp_instance),
        if bpp_feasible { "true" } else { "false" },
    );
    let path = Path::new(output_path);
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent)?;
    }
    fs::write(path, contents)?;
    Ok(())
}

fn trace_local_normal_components(rng: &SmallRng, mean: (f32, f32), stddev: f32) -> Vec<String> {
    let distributions = [
        Normal::new(mean.0, stddev).expect("finite local x normal"),
        Normal::new(mean.1, stddev).expect("finite local y normal"),
    ];
    let mut probe = rng.clone();
    let mut rows = Vec::with_capacity(5);
    rows.push(format!(
        "# pre_local_next_u64={:016x}",
        probe.clone().next_u64(),
    ));

    for ordinal in 0..4 {
        let axis_index = ordinal % 2;
        let before = probe.clone();
        let sample_value = distributions[axis_index].sample(&mut probe);
        let consumed = consumed_u64_words(&before, &probe);

        let mut zscore_rng = before.clone();
        let zscore: f32 = StandardNormal.sample(&mut zscore_rng);
        assert_eq!(
            zscore_rng.clone().next_u64(),
            probe.clone().next_u64(),
            "Normal<f32> must consume the same raw RNG path as StandardNormal"
        );

        let mut following = probe.clone();
        let following_words = std::array::from_fn::<_, 4, _>(|_| following.next_u64());
        rows.push(format!(
            "{}\t{}\t{:08x}\t{}\t{:08x}\t{:016x}\t{:016x}\t{:016x}\t{:016x}",
            ordinal / 2,
            if axis_index == 0 { "x" } else { "y" },
            zscore.to_bits(),
            consumed,
            sample_value.to_bits(),
            following_words[0],
            following_words[1],
            following_words[2],
            following_words[3],
        ));
    }
    rows
}

// Instrumented transcription of the pinned lbf::opt::search::search body.
// The probe also runs the unmodified function and compares result, sample count,
// and post-search RNG state before emitting the captured rows.
fn traced_search(
    cde: &jagua_rs::collision_detection::CDEngine,
    item: &Item,
    config: &LBFConfig,
    rng: &mut SmallRng,
    sample_counter: &mut usize,
    filter: &impl HazardFilter,
) -> (Option<(DTransformation, LBFLoss)>, Vec<String>, Vec<String>) {
    let surrogate = item.shape_cd.surrogate();
    let mut buffer = (*item.shape_cd).clone();
    buffer.surrogate = None;

    let mut best: Option<(DTransformation, LBFLoss)> = None;
    let ls_sample_budget = (config.n_samples as f32 * config.ls_frac) as usize;
    let uni_sample_budget = config.n_samples - ls_sample_budget;
    let mut bin_sampler = UniformRectSampler::new(cde.bbox(), item);
    let mut rows = Vec::with_capacity(config.n_samples + 1);

    for ordinal in 0..uni_sample_budget {
        let before = bin_sampler.bbox;
        let sample = bin_sampler.sample(rng);
        let transform = sample.compose();
        let surrogate_collision = cde.detect_surrogate_collision(surrogate, &transform, filter);
        let mut cost_value = None;
        let mut polygon_checked = None;
        let mut polygon_collision = None;
        let mut accepted = false;
        let mut after = Some(before);

        if !surrogate_collision {
            buffer.transform_from(&item.shape_cd, &transform);
            let cost = LBFLoss::from_shape(&buffer);
            cost_value = Some(cost.cost());
            let worth_testing = match (best.as_ref(), &cost) {
                (Some((_, best_cost)), candidate_cost) => {
                    candidate_cost.partial_cmp(best_cost).unwrap() == Ordering::Less
                }
                (None, _) => true,
            };

            if worth_testing {
                polygon_checked = Some(true);
                let collides = cde.detect_poly_collision(&buffer, filter);
                polygon_collision = Some(collides);
                if !collides {
                    accepted = true;
                    best = Some((sample, cost));
                    let tightened = cost.tighten_sample_bbox(bin_sampler.bbox);
                    bin_sampler = UniformRectSampler::new(tightened, item);
                    after = Some(tightened);
                }
            }
        }

        trace_row(
            &mut rows,
            "uniform",
            ordinal,
            sample,
            surrogate_collision,
            cost_value,
            polygon_checked,
            polygon_collision,
            accepted,
            Some(before),
            after,
        );
    }

    *sample_counter += uni_sample_budget;
    let Some((best_sample, best_cost)) = best.as_mut() else {
        return (None, rows, Vec::new());
    };

    let (mean_x, mean_y) = best_sample.translation();
    let bbox = cde.bbox();
    let local_stddev = SD_TRANSL.0 * f32::max(bbox.width(), bbox.height());
    let normal_rows = if matches!(
        &item.allowed_rotation,
        &jagua_rs::geometry::geo_enums::RotationRange::None
    ) {
        trace_local_normal_components(rng, (mean_x, mean_y), local_stddev)
    } else {
        Vec::new()
    };
    let mut ls_sampler = LSSampler::from_defaults(item, *best_sample, cde.bbox());
    for ordinal in 0..ls_sample_budget {
        let sample = ls_sampler.sample(rng);
        let transform = sample.compose();
        let surrogate_collision = cde.detect_surrogate_collision(surrogate, &transform, filter);
        let mut cost_value = None;
        let mut polygon_checked = None;
        let mut polygon_collision = None;
        let mut accepted = false;

        if !surrogate_collision {
            buffer.transform_from(&item.shape_cd, &transform);
            let cost = LBFLoss::from_shape(&buffer);
            cost_value = Some(cost.cost());
            if cost < *best_cost {
                polygon_checked = Some(true);
                let collides = cde.detect_poly_collision(&buffer, filter);
                polygon_collision = Some(collides);
                if !collides {
                    accepted = true;
                    ls_sampler.shift_mean(sample);
                    (*best_sample, *best_cost) = (sample, cost);
                }
            }
        }

        trace_row(
            &mut rows,
            "local",
            ordinal,
            sample,
            surrogate_collision,
            cost_value,
            polygon_checked,
            polygon_collision,
            accepted,
            None,
            None,
        );

        let progress_pct = ordinal as f32 / ls_sample_budget as f32;
        ls_sampler.decay_stddev(progress_pct);
    }

    *sample_counter += ls_sample_budget;
    (best, rows, normal_rows)
}

fn same_result(
    left: &Option<(DTransformation, LBFLoss)>,
    right: &Option<(DTransformation, LBFLoss)>,
) -> bool {
    match (left, right) {
        (None, None) => true,
        (Some((left_t, left_loss)), Some((right_t, right_loss))) => {
            let (left_x, left_y) = left_t.translation();
            let (right_x, right_y) = right_t.translation();
            left_t.rotation().to_bits() == right_t.rotation().to_bits()
                && left_x.to_bits() == right_x.to_bits()
                && left_y.to_bits() == right_y.to_bits()
                && left_loss.cost().to_bits() == right_loss.cost().to_bits()
        }
        _ => false,
    }
}

fn main() -> anyhow::Result<()> {
    let output_path = std::env::args()
        .nth(1)
        .ok_or_else(|| anyhow::anyhow!("expected trace output path"))?;
    let normal_output_path = std::env::args()
        .nth(2)
        .ok_or_else(|| anyhow::anyhow!("expected normal trace output path"))?;
    let rotation_mode_arg = std::env::args().nth(3).unwrap_or_else(|| "none".to_owned());
    let discrete_three_angles = rotation_mode_arg == "discrete-three";
    let rotation_mode = if discrete_three_angles {
        "discrete".to_owned()
    } else {
        rotation_mode_arg.clone()
    };
    let bounds_mode = std::env::args().nth(4).unwrap_or_else(|| "full".to_owned());
    let seed_override = std::env::args()
        .nth(5)
        .map(|value| value.parse::<u64>())
        .transpose()?;
    let sample_override = std::env::args()
        .nth(6)
        .map(|value| value.parse::<usize>())
        .transpose()?;
    let obstacle_mode = std::env::args().nth(7).unwrap_or_else(|| "rect".to_owned());
    let obstacle_bbox_min_x = std::env::args()
        .nth(8)
        .map(|value| value.parse::<f32>())
        .transpose()?
        .unwrap_or(0.0);
    let obstacle_bbox_min_y = std::env::args()
        .nth(9)
        .map(|value| value.parse::<f32>())
        .transpose()?
        .unwrap_or(4.0);
    if bounds_mode == "quality-zone-filter" {
        write_quality_zone_filter_fixture(&output_path)?;
        fs::write(
            &normal_output_path,
            "# direct quality-filter probe; no RNG draws\n",
        )?;
        println!("pinned LBF quality-zone filter cases verified: 3");
        return Ok(());
    }
    if bounds_mode == "quality-zone-bpp-e2e" {
        write_quality_zone_bpp_fixture(&output_path)?;
        fs::write(&normal_output_path, "# top-level BPP quality-zone cases\n")?;
        println!("pinned LBF BPP quality-zone cases solved: 3");
        return Ok(());
    }
    if bounds_mode == "small-fixtures" {
        write_small_lbf_fixtures(&output_path)?;
        fs::write(&normal_output_path, "# small SPP/BPP metric fixtures\n")?;
        println!("pinned LBF small SPP/BPP fixtures solved: 2");
        return Ok(());
    }
    if obstacle_mode != "rect"
        && obstacle_mode != "triangle"
        && obstacle_mode != "concave"
        && obstacle_mode != "concave-offset"
        && obstacle_mode != "concave-rot90"
        && obstacle_mode != "concave-rot90-origin"
        && obstacle_mode != "concave-rot45"
        && obstacle_mode != "concave-rot45-searchpair"
        && obstacle_mode != "concave-rot225-searchpair"
        && obstacle_mode != "concave-notch-rot45-searchpair"
        && obstacle_mode != "concave-notch-rot225-searchpair"
    {
        anyhow::bail!("obstacle mode must be rect, triangle, concave, concave-offset, concave-rot90, concave-rot90-origin, concave-rot45, concave-rot45-searchpair, concave-rot225-searchpair, concave-notch-rot45-searchpair, or concave-notch-rot225-searchpair");
    }
    let allowed_rotation = match rotation_mode_arg.as_str() {
        "none" => jagua_rs::geometry::geo_enums::RotationRange::None,
        "continuous" => jagua_rs::geometry::geo_enums::RotationRange::Continuous,
        "discrete" => jagua_rs::geometry::geo_enums::RotationRange::Discrete(vec![
            0.0,
            std::f32::consts::FRAC_PI_2,
        ]),
        "discrete-three" => jagua_rs::geometry::geo_enums::RotationRange::Discrete(vec![
            0.0,
            std::f32::consts::FRAC_PI_4,
            std::f32::consts::FRAC_PI_2,
        ]),
        _ => anyhow::bail!("rotation mode must be none, continuous, discrete, or discrete-three"),
    };

    let (bounds, fixture_name) = match bounds_mode.as_str() {
        "full" => (Rect::try_new(0.0, 0.0, 30.0, 30.0)?, "plain-square-30x30"),
        "adjacent-x" => (
            Rect::try_new(
                f32::from_bits(0x4e80_0000),
                0.0,
                f32::from_bits(0x4e80_0010),
                512.0,
            )?,
            "16-ulp-f32-x-range",
        ),
        "invalid-bounds" => (
            Rect::try_new(0.0, 0.0, 30.0, 30.0)?,
            "invalid-sampler-bounds",
        ),
        "tighten-invalid" => (
            Rect::try_new(100.0, -1000.0, 200.0, -900.0)?,
            "negative-y-tightens-x-below-minimum",
        ),
        "no-valid-placement" => (
            Rect::try_new(0.0, 0.0, 30.0, 30.0)?,
            "container-30x30-blocker-31x31-no-valid-placement",
        ),
        "no-valid-placement-rect" => (
            Rect::try_new(0.0, 0.0, 40.0, 24.0)?,
            "container-40x24-item-3x2-blocker-43x26-no-valid-placement",
        ),
        "feasible-rect" => (
            Rect::try_new(0.0, 0.0, 40.0, 24.0)?,
            "container-40x24-item-3x2-feasible-search",
        ),
        "feasible-obstacle-rect" => (
            Rect::try_new(0.0, 0.0, 40.0, 24.0)?,
            "container-40x24-item-3x2-blocker-10x10-at-origin-feasible-search",
        ),
        _ => anyhow::bail!("unsupported LBF trace bounds mode: {bounds_mode}"),
    };
    let fixture_name = if obstacle_mode == "concave-offset" {
        "container-40x24-item-3x2-blocker-10x10-concave-at-12x8-feasible-search"
    } else if obstacle_mode == "concave-rot90" {
        "container-40x24-item-3x2-blocker-10x10-concave-rot90-translation-22x8-feasible-search"
    } else if obstacle_mode == "concave-rot90-origin" {
        "container-40x24-item-3x2-blocker-10x10-concave-rot90-origin-bbox-feasible-search"
    } else if obstacle_mode == "concave-rot45" {
        "container-40x24-item-3x2-blocker-10x10-concave-rot45-translation-19.1x8-feasible-search"
    } else if obstacle_mode == "concave-rot45-searchpair" {
        "container-40x24-item-3x2-concave-rot45-same-aabb-search-pair"
    } else if obstacle_mode == "concave-rot225-searchpair" {
        "container-40x24-item-3x2-concave-rot225-same-aabb-search-pair"
    } else if obstacle_mode == "concave-notch-rot45-searchpair" {
        "container-40x24-item-3x2-concave-notch-rot45-same-aabb-search-pair"
    } else if obstacle_mode == "concave-notch-rot225-searchpair" {
        "container-40x24-item-3x2-concave-notch-rot225-same-aabb-search-pair"
    } else {
        fixture_name
    };
    let (
        item_width,
        item_height,
        blocker_width,
        blocker_height,
        default_seed,
        default_samples,
        ls_frac,
    ) = if bounds_mode == "feasible-obstacle-rect" {
        (3.0, 2.0, 10.0, 10.0, 17_u64, 80, 0.30)
    } else if bounds_mode == "no-valid-placement-rect" || bounds_mode == "feasible-rect" {
        (3.0, 2.0, 43.0, 26.0, 17_u64, 80, 0.30)
    } else {
        (1.0, 1.0, 31.0, 31.0, 42_u64, 64, 0.25)
    };
    let seed = seed_override.unwrap_or(default_seed);
    let n_samples = sample_override.unwrap_or(default_samples);
    let cde_config = CDEConfig {
        quadtree_depth: 4,
        cd_threshold: 8,
        item_surrogate_config: SPSurrogateConfig {
            n_pole_limits: [(4, 0.0), (2, 0.8), (1, 0.9)],
            ff_pole_area_ratio: 0.5,
            n_ff_piers: 0,
        },
    };
    let item_surrogate_config = SPSurrogateConfig {
        n_pole_limits: [(4, 0.0), (2, 0.8), (1, 0.9)],
        ff_pole_area_ratio: 0.5,
        n_ff_piers: 0,
    };
    let item = Item::new(
        0,
        9101,
        OriginalShape {
            shape: SPolygon::from(&Rect::try_new(0.0, 0.0, item_width, item_height)?),
            pre_transform: DTransformation::empty(),
            modify_mode: ShapeModifyMode::Inflate,
            modify_config: ShapeModifyConfig::default(),
        },
        allowed_rotation,
        None,
        item_surrogate_config.clone(),
    )?;

    if bounds_mode == "adjacent-x" {
        let sampler = UniformRectSampler::new(bounds, &item);
        let mut rng = SmallRng::seed_from_u64(42);
        let mut rows = Vec::with_capacity(48);
        let mut saw_upper_x = false;
        for ordinal in 0..48 {
            let sample = sampler.sample(&mut rng);
            let (x, y) = sample.translation();
            saw_upper_x |= x.to_bits() == bounds.x_max.to_bits();
            rows.push(format!(
                "{ordinal}\t{}\t{}\t{}",
                bits(sample.rotation()),
                bits(x),
                bits(y),
            ));
        }
        let next_rng = rng.next_u64();
        anyhow::ensure!(
            saw_upper_x,
            "the fixed-seed narrow-range trace must sample the exclusive upper endpoint by rounding"
        );
        let mut contents = format!(
            "# generator=pinned jagua-rs/lbf::samplers::uniform_rect_sampler::UniformRectSampler at f5e2b5ffd14931f17a20ce283a2122e877953ceb\n# fixture={fixture_name} seed=42 samples=48 x_min_bits={} x_max_bits={} y_min_bits={} y_max_bits={}\n# next_rng_u64={next_rng:016x} saw_upper_x=1\n# ordinal rotation_bits x_bits y_bits\n",
            bits(bounds.x_min),
            bits(bounds.x_max),
            bits(bounds.y_min),
            bits(bounds.y_max),
        );
        for row in rows {
            contents.push_str(&row);
            contents.push('\n');
        }
        let path = Path::new(&output_path);
        if let Some(parent) = path.parent() {
            fs::create_dir_all(parent)?;
        }
        fs::write(path, contents)?;
        fs::write(
            normal_output_path,
            "# direct LBF sampler probe; no local phase\n",
        )?;
        println!("pinned LBF narrow sampler trace verified: samples=48 next={next_rng:016x}");
        return Ok(());
    }

    if bounds_mode == "invalid-bounds" {
        std::panic::set_hook(Box::new(|_| {}));
        let cases = [
            (
                "x-singleton",
                Rect {
                    x_min: 1.0,
                    y_min: 0.0,
                    x_max: 1.0,
                    y_max: 2.0,
                },
                "EmptyRange",
            ),
            (
                "y-singleton",
                Rect {
                    x_min: 1.0,
                    y_min: 0.0,
                    x_max: 2.0,
                    y_max: 0.0,
                },
                "EmptyRange",
            ),
            (
                "x-reversed",
                Rect {
                    x_min: 2.0,
                    y_min: 0.0,
                    x_max: 1.0,
                    y_max: 2.0,
                },
                "EmptyRange",
            ),
            (
                "x-nonfinite",
                Rect {
                    x_min: 0.0,
                    y_min: 0.0,
                    x_max: f32::INFINITY,
                    y_max: 2.0,
                },
                "NonFinite",
            ),
        ];
        let mut contents = format!(
            "# generator=pinned rand_distr 0.6.0 Uniform<f32>::new at f5e2b5ffd14931f17a20ce283a2122e877953ceb\n# fixture={fixture_name}; constructor-only cases, no RNG draws\n# case rust_status rust_error\n"
        );
        for (name, invalid_bounds, expected_error) in cases.iter().copied() {
            let outcome = catch_unwind(AssertUnwindSafe(|| {
                UniformRectSampler::new(invalid_bounds, &item)
            }));
            let error = outcome
                .as_ref()
                .err()
                .and_then(|payload| payload.downcast_ref::<String>())
                .cloned()
                .or_else(|| {
                    outcome
                        .as_ref()
                        .err()
                        .and_then(|payload| payload.downcast_ref::<&str>())
                        .map(|message| (*message).to_owned())
                });
            anyhow::ensure!(
                error.is_some(),
                "{name} must panic in the pinned constructor"
            );
            anyhow::ensure!(
                error.as_ref().unwrap().contains(expected_error),
                "{name} panic class differs from {expected_error}"
            );
            contents.push_str(&format!("{name}\tpanic\t{expected_error}\n"));
        }
        let path = Path::new(&output_path);
        if let Some(parent) = path.parent() {
            fs::create_dir_all(parent)?;
        }
        fs::write(path, contents)?;
        fs::write(
            normal_output_path,
            "# constructor errors occur before sampling\n",
        )?;
        println!(
            "pinned LBF sampler constructor errors captured: {}",
            cases.len()
        );
        return Ok(());
    }

    let container = Container::new(
        OriginalShape {
            shape: SPolygon::from(&bounds),
            pre_transform: DTransformation::empty(),
            modify_mode: ShapeModifyMode::Deflate,
            modify_config: ShapeModifyConfig::default(),
        },
        vec![],
        cde_config,
    )?;
    let mut layout = Layout::new(container.clone());
    if bounds_mode.starts_with("no-valid-placement") || bounds_mode == "feasible-obstacle-rect" {
        let blocker_shape = if obstacle_mode == "triangle" {
            SPolygon::new(vec![
                Point::from((0.0, 0.0)),
                Point::from((blocker_width, 0.0)),
                Point::from((0.0, blocker_height)),
            ])?
        } else if obstacle_mode == "concave"
            || obstacle_mode == "concave-offset"
            || obstacle_mode == "concave-rot90"
            || obstacle_mode == "concave-rot90-origin"
            || obstacle_mode == "concave-rot45"
            || obstacle_mode == "concave-rot45-searchpair"
            || obstacle_mode == "concave-rot225-searchpair"
            || obstacle_mode == "concave-notch-rot45-searchpair"
            || obstacle_mode == "concave-notch-rot225-searchpair"
        {
            if obstacle_mode == "concave-notch-rot45-searchpair"
                || obstacle_mode == "concave-notch-rot225-searchpair"
            {
                SPolygon::new(vec![
                    Point::from((0.0, 0.0)),
                    Point::from((blocker_width, 0.0)),
                    Point::from((blocker_width, 1.0)),
                    Point::from((1.0, 1.0)),
                    Point::from((1.0, 9.0)),
                    Point::from((blocker_width, 9.0)),
                    Point::from((blocker_width, blocker_height)),
                    Point::from((0.0, blocker_height)),
                ])?
            } else {
                SPolygon::new(vec![
                    Point::from((0.0, 0.0)),
                    Point::from((blocker_width, 0.0)),
                    Point::from((blocker_width, 3.0)),
                    Point::from((3.0, 3.0)),
                    Point::from((3.0, blocker_height)),
                    Point::from((0.0, blocker_height)),
                ])?
            }
        } else {
            SPolygon::from(&Rect::try_new(0.0, 0.0, blocker_width, blocker_height)?)
        };
        let blocker = Arc::new(Item::new(
            1,
            9102,
            OriginalShape {
                shape: blocker_shape,
                pre_transform: DTransformation::empty(),
                modify_mode: ShapeModifyMode::Inflate,
                modify_config: ShapeModifyConfig::default(),
            },
            jagua_rs::geometry::geo_enums::RotationRange::None,
            None,
            item_surrogate_config,
        )?);
        let notch_transform = |rotation: f32| {
            let points = [
                (0.0, 0.0),
                (blocker_width, 0.0),
                (blocker_width, 1.0),
                (1.0, 1.0),
                (1.0, 9.0),
                (blocker_width, 9.0),
                (blocker_width, blocker_height),
                (0.0, blocker_height),
            ];
            let (sin, cos) = rotation.sin_cos();
            let (min_x, min_y, _, _) = points.iter().fold(
                (
                    f32::INFINITY,
                    f32::INFINITY,
                    f32::NEG_INFINITY,
                    f32::NEG_INFINITY,
                ),
                |(min_x, min_y, max_x, max_y), (x, y)| {
                    let rotated_x = cos * x - sin * y;
                    let rotated_y = sin * x + cos * y;
                    (
                        min_x.min(rotated_x),
                        min_y.min(rotated_y),
                        max_x.max(rotated_x),
                        max_y.max(rotated_y),
                    )
                },
            );
            DTransformation::new(
                rotation,
                (obstacle_bbox_min_x - min_x, obstacle_bbox_min_y - min_y),
            )
        };
        let blocker_transform = match obstacle_mode.as_str() {
            "concave-offset" => DTransformation::new(0.0, (12.0, 8.0)),
            "concave-rot90-origin" => {
                DTransformation::new(std::f32::consts::FRAC_PI_2, (10.0, 0.0))
            }
            "concave-rot90" => DTransformation::new(std::f32::consts::FRAC_PI_2, (22.0, 8.0)),
            "concave-rot45" => DTransformation::new(std::f32::consts::FRAC_PI_4, (19.1, 8.0)),
            "concave-rot45-searchpair" => {
                DTransformation::new(std::f32::consts::FRAC_PI_4, (7.0710678, 4.0))
            }
            "concave-rot225-searchpair" => {
                DTransformation::new(5.0 * std::f32::consts::FRAC_PI_4, (7.0710678, 13.192388))
            }
            "concave-notch-rot45-searchpair" => notch_transform(std::f32::consts::FRAC_PI_4),
            "concave-notch-rot225-searchpair" => notch_transform(5.0 * std::f32::consts::FRAC_PI_4),
            _ => DTransformation::empty(),
        };
        layout.place_item(&blocker, blocker_transform);
        if obstacle_mode == "concave-rot90-origin" {
            let candidates = [
                DTransformation::new(
                    f32::from_bits(0x40a3_b9cb),
                    (f32::from_bits(0x414c_0ba0), f32::from_bits(0x41fa_d89c)),
                ),
                DTransformation::new(
                    f32::from_bits(0x408c_f8bf),
                    (f32::from_bits(0x41fd_ebe3), f32::from_bits(0x4178_620c)),
                ),
                DTransformation::new(
                    f32::from_bits(0x3f49_a0f5),
                    (f32::from_bits(0x41aa_65d9), f32::from_bits(0x3e9e_0ba0)),
                ),
                DTransformation::new(
                    f32::from_bits(0x40bb_a91b),
                    (f32::from_bits(0x4155_14b8), f32::from_bits(0x41d0_0272)),
                ),
                DTransformation::new(
                    f32::from_bits(0x4088_b931),
                    (f32::from_bits(0x3fd3_d359), f32::from_bits(0x4101_f784)),
                ),
            ];
            let classify = |obstacle_transform: DTransformation| {
                let mut probe_layout = Layout::new(container.clone());
                probe_layout.place_item(&blocker, obstacle_transform);
                candidates
                    .iter()
                    .map(|candidate| {
                        let transform = candidate.compose();
                        let surrogate_collision = probe_layout.cde().detect_surrogate_collision(
                            item.shape_cd.surrogate(),
                            &transform,
                            &NoFilter,
                        );
                        let polygon_collision = if surrogate_collision {
                            None
                        } else {
                            let mut buffer = (*item.shape_cd).clone();
                            buffer.surrogate = None;
                            buffer.transform_from(&item.shape_cd, &transform);
                            Some(probe_layout.cde().detect_poly_collision(&buffer, &NoFilter))
                        };
                        (surrogate_collision, polygon_collision)
                    })
                    .collect::<Vec<_>>()
            };
            let baseline = [
                (true, None),
                (false, Some(false)),
                (false, Some(false)),
                (true, None),
                (false, Some(true)),
            ];
            let unrotated = classify(DTransformation::empty());
            let rotated = classify(blocker_transform);
            anyhow::ensure!(
                unrotated == baseline
                    && rotated[..4] == baseline[..4]
                    && rotated[4] == (false, Some(false)),
                "same-bbox 90-degree rotation must preserve candidate classifications 0-3 and clear only ordinal 4: unrotated={unrotated:?} rotated={rotated:?}"
            );
        } else if obstacle_mode == "concave-rot90" || obstacle_mode == "concave-rot45" {
            let probe_translation = if obstacle_mode == "concave-rot90" {
                (13.0, 13.0)
            } else {
                (14.5, 17.1)
            };
            let probe_transform = DTransformation::new(0.0, probe_translation);
            let probe_item = Arc::new(item.clone());
            let mut unrotated_probe = Layout::new(container.clone());
            unrotated_probe.place_item(&blocker, DTransformation::new(0.0, (12.0, 8.0)));
            unrotated_probe.place_item(&probe_item, probe_transform);

            let mut rotated_probe = Layout::new(container.clone());
            rotated_probe.place_item(&blocker, blocker_transform);
            rotated_probe.place_item(&probe_item, probe_transform);
            anyhow::ensure!(
                !unrotated_probe.is_feasible() && rotated_probe.is_feasible(),
                "the orientation probe must collide before rotation and clear after rotation"
            );
        } else if obstacle_mode == "concave-rot45-searchpair"
            || obstacle_mode == "concave-rot225-searchpair"
        {
            let candidates = [
                DTransformation::new(
                    f32::from_bits(0x40a3_b9cb),
                    (f32::from_bits(0x414c_0ba0), f32::from_bits(0x41fa_d89c)),
                ),
                DTransformation::new(
                    f32::from_bits(0x408c_f8bf),
                    (f32::from_bits(0x41fd_ebe3), f32::from_bits(0x4178_620c)),
                ),
                DTransformation::new(
                    f32::from_bits(0x3f49_a0f5),
                    (f32::from_bits(0x41aa_65d9), f32::from_bits(0x3e9e_0ba0)),
                ),
                DTransformation::new(
                    f32::from_bits(0x40bb_a91b),
                    (f32::from_bits(0x4155_14b8), f32::from_bits(0x41d0_0272)),
                ),
                DTransformation::new(
                    f32::from_bits(0x4088_b931),
                    (f32::from_bits(0x3fd3_d359), f32::from_bits(0x4101_f784)),
                ),
            ];
            let classify = |obstacle_transform: DTransformation| {
                let mut probe_layout = Layout::new(container.clone());
                probe_layout.place_item(&blocker, obstacle_transform);
                candidates
                    .iter()
                    .map(|candidate| {
                        let transform = candidate.compose();
                        let surrogate_collision = probe_layout.cde().detect_surrogate_collision(
                            item.shape_cd.surrogate(),
                            &transform,
                            &NoFilter,
                        );
                        let polygon_collision = if surrogate_collision {
                            None
                        } else {
                            let mut buffer = (*item.shape_cd).clone();
                            buffer.surrogate = None;
                            buffer.transform_from(&item.shape_cd, &transform);
                            Some(probe_layout.cde().detect_poly_collision(&buffer, &NoFilter))
                        };
                        (surrogate_collision, polygon_collision)
                    })
                    .collect::<Vec<_>>()
            };
            let bounds = |transform: DTransformation| {
                let (offset_x, offset_y) = transform.translation();
                let (sin, cos) = transform.rotation().sin_cos();
                let points = [
                    (0.0, 0.0),
                    (blocker_width, 0.0),
                    (blocker_width, 3.0),
                    (3.0, 3.0),
                    (3.0, blocker_height),
                    (0.0, blocker_height),
                ];
                points.iter().fold(
                    (
                        f32::INFINITY,
                        f32::INFINITY,
                        f32::NEG_INFINITY,
                        f32::NEG_INFINITY,
                    ),
                    |(min_x, min_y, max_x, max_y), (point_x, point_y)| {
                        let x = cos * point_x - sin * point_y + offset_x;
                        let y = sin * point_x + cos * point_y + offset_y;
                        (min_x.min(x), min_y.min(y), max_x.max(x), max_y.max(y))
                    },
                )
            };
            let rotated_45 = DTransformation::new(std::f32::consts::FRAC_PI_4, (7.0710678, 4.0));
            let rotated_225 =
                DTransformation::new(5.0 * std::f32::consts::FRAC_PI_4, (7.0710678, 13.192388));
            let bounds_45 = bounds(rotated_45);
            let bounds_225 = bounds(rotated_225);
            let bounds_match = (bounds_45.0 - bounds_225.0).abs() <= 0.00002
                && (bounds_45.1 - bounds_225.1).abs() <= 0.00002
                && (bounds_45.2 - bounds_225.2).abs() <= 0.00002
                && (bounds_45.3 - bounds_225.3).abs() <= 0.00002;
            let pair_45 = classify(rotated_45);
            let pair_225 = classify(rotated_225);
            anyhow::ensure!(
                bounds_match
                    && pair_45[..4] == pair_225[..4]
                    && pair_45[4] != pair_225[4],
                "45/225-degree pair must share the blocker AABB and preserve candidates 0-3 while changing candidate 4: bounds45={bounds_45:?} bounds225={bounds_225:?} class45={pair_45:?} class225={pair_225:?}"
            );
        } else if obstacle_mode == "concave-notch-rot45-searchpair"
            || obstacle_mode == "concave-notch-rot225-searchpair"
        {
            let sample_candidates = |obstacle_transform: DTransformation| {
                let mut probe_layout = Layout::new(container.clone());
                probe_layout.place_item(&blocker, obstacle_transform);
                let sampler = UniformRectSampler::new(probe_layout.cde().bbox(), &item);
                let mut probe_rng = SmallRng::seed_from_u64(seed);
                (0..5)
                    .map(|_| sampler.sample(&mut probe_rng))
                    .collect::<Vec<_>>()
            };
            let rotated_45 = notch_transform(std::f32::consts::FRAC_PI_4);
            let rotated_225 = notch_transform(5.0 * std::f32::consts::FRAC_PI_4);
            let candidates_45 = sample_candidates(rotated_45);
            let candidates_225 = sample_candidates(rotated_225);
            let candidates_match =
                candidates_45
                    .iter()
                    .zip(&candidates_225)
                    .all(|(left, right)| {
                        let (left_x, left_y) = left.translation();
                        let (right_x, right_y) = right.translation();
                        bits(left.rotation()) == bits(right.rotation())
                            && bits(left_x) == bits(right_x)
                            && bits(left_y) == bits(right_y)
                    });
            let classify = |obstacle_transform: DTransformation, candidates: &[DTransformation]| {
                let mut probe_layout = Layout::new(container.clone());
                probe_layout.place_item(&blocker, obstacle_transform);
                candidates
                    .iter()
                    .map(|candidate| {
                        let transform = candidate.compose();
                        let surrogate_collision = probe_layout.cde().detect_surrogate_collision(
                            item.shape_cd.surrogate(),
                            &transform,
                            &NoFilter,
                        );
                        let polygon_collision = if surrogate_collision {
                            None
                        } else {
                            let mut buffer = (*item.shape_cd).clone();
                            buffer.surrogate = None;
                            buffer.transform_from(&item.shape_cd, &transform);
                            Some(probe_layout.cde().detect_poly_collision(&buffer, &NoFilter))
                        };
                        (surrogate_collision, polygon_collision)
                    })
                    .collect::<Vec<_>>()
            };
            let bounds = |transform: DTransformation| {
                let (offset_x, offset_y) = transform.translation();
                let (sin, cos) = transform.rotation().sin_cos();
                let points = [
                    (0.0, 0.0),
                    (blocker_width, 0.0),
                    (blocker_width, 1.0),
                    (1.0, 1.0),
                    (1.0, 9.0),
                    (blocker_width, 9.0),
                    (blocker_width, blocker_height),
                    (0.0, blocker_height),
                ];
                points.iter().fold(
                    (
                        f32::INFINITY,
                        f32::INFINITY,
                        f32::NEG_INFINITY,
                        f32::NEG_INFINITY,
                    ),
                    |(min_x, min_y, max_x, max_y), (point_x, point_y)| {
                        let x = cos * point_x - sin * point_y + offset_x;
                        let y = sin * point_x + cos * point_y + offset_y;
                        (min_x.min(x), min_y.min(y), max_x.max(x), max_y.max(y))
                    },
                )
            };
            let bounds_45 = bounds(rotated_45);
            let bounds_225 = bounds(rotated_225);
            let bounds_match = (bounds_45.0 - bounds_225.0).abs() <= 0.00002
                && (bounds_45.1 - bounds_225.1).abs() <= 0.00002
                && (bounds_45.2 - bounds_225.2).abs() <= 0.00002
                && (bounds_45.3 - bounds_225.3).abs() <= 0.00002;
            let pair_45 = classify(rotated_45, &candidates_45);
            let pair_225 = classify(rotated_225, &candidates_225);
            anyhow::ensure!(
                bounds_match && candidates_match,
                "notched 45/225-degree pair must share blocker AABB and initial seed-{seed} candidates: bounds45={bounds_45:?} bounds225={bounds_225:?} candidatesMatch={candidates_match} class45={pair_45:?} class225={pair_225:?}"
            );
        }
    }
    let config = LBFConfig {
        n_samples,
        ls_frac,
        ..LBFConfig::default()
    };
    let uniform_budget = n_samples - (n_samples as f32 * ls_frac) as usize;
    let filter = NoFilter;

    if bounds_mode == "tighten-invalid" {
        std::panic::set_hook(Box::new(|_| {}));
        let initial_sampler = UniformRectSampler::new(layout.cde().bbox(), &item);
        let mut probe_rng = SmallRng::seed_from_u64(42);
        let first_sample = initial_sampler.sample(&mut probe_rng);
        let (x, y) = first_sample.translation();
        let transform = first_sample.compose();
        let surrogate_collision =
            layout
                .cde()
                .detect_surrogate_collision(item.shape_cd.surrogate(), &transform, &filter);
        let mut buffer = (*item.shape_cd).clone();
        buffer.surrogate = None;
        buffer.transform_from(&item.shape_cd, &transform);
        let cost = LBFLoss::from_shape(&buffer);
        let polygon_collision = layout.cde().detect_poly_collision(&buffer, &filter);
        let tightened = cost.tighten_sample_bbox(initial_sampler.bbox);
        anyhow::ensure!(
            !surrogate_collision && !polygon_collision,
            "the first fixed-seed candidate must be accepted before tightening: surrogate={surrogate_collision} polygon={polygon_collision} x={} y={} bbox={:?}",
            bits(x),
            bits(y),
            layout.cde().bbox(),
        );
        anyhow::ensure!(
            tightened.x_max <= tightened.x_min,
            "the first accepted candidate must create an invalid tightened x interval"
        );
        let post_one_sample = probe_rng.next_u64();

        let mut direct_rng = SmallRng::seed_from_u64(42);
        let mut direct_count = 0;
        let direct = catch_unwind(AssertUnwindSafe(|| {
            search(
                layout.cde(),
                &item,
                &config,
                &mut direct_rng,
                &mut direct_count,
                &filter,
            )
        }));
        let mut traced_rng = SmallRng::seed_from_u64(42);
        let mut traced_count = 0;
        let traced = catch_unwind(AssertUnwindSafe(|| {
            traced_search(
                layout.cde(),
                &item,
                &config,
                &mut traced_rng,
                &mut traced_count,
                &filter,
            )
        }));
        let direct_panic = direct
            .as_ref()
            .err()
            .and_then(|payload| payload.downcast_ref::<String>())
            .cloned()
            .or_else(|| {
                direct
                    .as_ref()
                    .err()
                    .and_then(|payload| payload.downcast_ref::<&str>())
                    .map(|message| (*message).to_owned())
            });
        let traced_panic = traced
            .as_ref()
            .err()
            .and_then(|payload| payload.downcast_ref::<String>())
            .cloned()
            .or_else(|| {
                traced
                    .as_ref()
                    .err()
                    .and_then(|payload| payload.downcast_ref::<&str>())
                    .map(|message| (*message).to_owned())
            });
        let direct_next = direct_rng.next_u64();
        let traced_next = traced_rng.next_u64();
        anyhow::ensure!(direct_panic.is_some(), "direct lbf::search must panic");
        anyhow::ensure!(traced_panic.is_some(), "traced lbf::search must panic");
        anyhow::ensure!(
            direct_panic == traced_panic,
            "direct and traced LBF panic payloads differ"
        );
        anyhow::ensure!(
            direct_next == traced_next && direct_next == post_one_sample,
            "LBF panic must preserve the RNG state after the accepted uniform candidate"
        );
        anyhow::ensure!(
            direct_count == 0 && traced_count == 0,
            "sample counters must remain unchanged when tightening panics before loop completion"
        );

        let contents = format!(
            "# generator=pinned jagua-rs/lbf::opt::search::search at f5e2b5ffd14931f17a20ce283a2122e877953ceb\n# fixture={fixture_name} seed=42 n_samples=64 ls_frac=0.25 rotation=none\n# direct_status=panic traced_status=panic panic_kind=EmptyRange first_accepted=1\n# first_rotation_bits={} first_x_bits={} first_y_bits={} first_cost_bits={} tightened_x_min_bits={} tightened_x_max_bits={}\n# direct_next_rng_u64={direct_next:016x} traced_next_rng_u64={traced_next:016x} post_one_sample_rng_u64={post_one_sample:016x} direct_sample_counter={direct_count} traced_sample_counter={traced_count}\n",
            bits(first_sample.rotation()),
            bits(x),
            bits(y),
            bits(cost.cost()),
            bits(tightened.x_min),
            bits(tightened.x_max),
        );
        let path = Path::new(&output_path);
        if let Some(parent) = path.parent() {
            fs::create_dir_all(parent)?;
        }
        fs::write(path, contents)?;
        fs::write(normal_output_path, "# no local normal phase before panic\n")?;
        println!("pinned LBF tightened-bounds panic captured: {direct_next:016x}");
        return Ok(());
    }

    let mut direct_rng = SmallRng::seed_from_u64(seed);
    let mut traced_rng = SmallRng::seed_from_u64(seed);
    let mut direct_count = 0;
    let mut traced_count = 0;

    let direct = search(
        layout.cde(),
        &item,
        &config,
        &mut direct_rng,
        &mut direct_count,
        &filter,
    );
    let (traced, rows, normal_rows) = traced_search(
        layout.cde(),
        &item,
        &config,
        &mut traced_rng,
        &mut traced_count,
        &filter,
    );
    let direct_next = direct_rng.next_u64();
    let traced_next = traced_rng.next_u64();
    anyhow::ensure!(
        same_result(&direct, &traced),
        "traced transcription result differs from pinned lbf::search"
    );
    anyhow::ensure!(
        direct_count == traced_count,
        "traced transcription sample count differs from pinned lbf::search"
    );
    anyhow::ensure!(
        direct_next == traced_next,
        "traced transcription post-search RNG differs from pinned lbf::search"
    );

    let forced_probe_candidates: Vec<(&str, usize, bool)> = match obstacle_mode.as_str() {
        "rect"
            if bounds_mode == "feasible-obstacle-rect"
                && rotation_mode == "discrete"
                && seed == 17
                && n_samples == 80 =>
        {
            let candidates = rows
                .iter()
                .filter_map(|row| {
                    let fields = row.split('\t').collect::<Vec<_>>();
                    if fields.len() == 18
                        && fields[5] == "0"
                        && fields[7] == "-"
                        && fields[8] == "-"
                        && fields[9] == "0"
                    {
                        Some((fields[0], fields[1].parse::<usize>().ok()?, true))
                    } else {
                        None
                    }
                })
                .collect::<Vec<_>>();
            let expected_count = if discrete_three_angles { 23 } else { 21 };
            anyhow::ensure!(
                candidates.len() == expected_count,
                "unexpected number of Rust-unobserved discrete rectangle candidates: expected {expected_count}, found {}",
                candidates.len()
            );
            if discrete_three_angles {
                for angle_bits in ["00000000", "3f490fdb", "3fc90fdb"] {
                    anyhow::ensure!(
                        rows.iter()
                            .any(|row| { row.split('\t').nth(2) == Some(angle_bits) }),
                        "discrete-three-angle fixture must sample rotation bits {angle_bits}"
                    );
                }
            }
            candidates
        }
        "rect"
            if bounds_mode == "feasible-obstacle-rect"
                && rotation_mode == "continuous"
                && ((seed == 17 && n_samples == 80)
                    || (seed == 42 && (n_samples == 80 || n_samples == 96))) =>
        {
            let candidates = rows
                .iter()
                .filter_map(|row| {
                    let fields = row.split('\t').collect::<Vec<_>>();
                    if fields.len() == 18
                        && fields[5] == "0"
                        && fields[7] == "-"
                        && fields[8] == "-"
                        && fields[9] == "0"
                    {
                        Some((fields[0], fields[1].parse::<usize>().ok()?, true))
                    } else {
                        None
                    }
                })
                .collect::<Vec<_>>();
            let expected_count = match (seed, n_samples) {
                (17, 80) => 20,
                (42, 80) => 21,
                (42, 96) => 28,
                _ => unreachable!(),
            };
            anyhow::ensure!(
                candidates.len() == expected_count,
                "unexpected number of Rust-unobserved baseline rectangle candidates: expected {expected_count}, found {}",
                candidates.len()
            );
            candidates
        }
        "rect"
            if bounds_mode == "feasible-obstacle-rect"
                && rotation_mode == "none"
                && seed == 17
                && n_samples == 80 =>
        {
            let candidates = rows
                .iter()
                .filter_map(|row| {
                    let fields = row.split('\t').collect::<Vec<_>>();
                    if fields.len() == 18
                        && fields[5] == "0"
                        && fields[7] == "-"
                        && fields[8] == "-"
                        && fields[9] == "0"
                    {
                        Some((fields[0], fields[1].parse::<usize>().ok()?, true))
                    } else {
                        None
                    }
                })
                .collect::<Vec<_>>();
            anyhow::ensure!(
                candidates.len() == 28,
                "unexpected number of Rust-unobserved fixed-orientation rectangle candidates: expected 28, found {}",
                candidates.len()
            );
            candidates
        }
        "rect"
            if bounds_mode == "feasible-rect"
                && rotation_mode == "none"
                && seed == 17
                && n_samples == 80 =>
        {
            let candidates = rows
                .iter()
                .filter_map(|row| {
                    let fields = row.split('\t').collect::<Vec<_>>();
                    if fields.len() == 18
                        && fields[5] == "0"
                        && fields[7] == "-"
                        && fields[8] == "-"
                        && fields[9] == "0"
                    {
                        Some((fields[0], fields[1].parse::<usize>().ok()?, true))
                    } else {
                        None
                    }
                })
                .collect::<Vec<_>>();
            anyhow::ensure!(
                candidates.len() == 40,
                "unexpected number of Rust-unobserved plain-container candidates: expected 40, found {}",
                candidates.len()
            );
            candidates
        }
        "triangle"
            if bounds_mode == "feasible-obstacle-rect"
                && rotation_mode == "discrete"
                && discrete_three_angles
                && seed == 17
                && n_samples == 80 =>
        {
            let candidates = rows
                .iter()
                .filter_map(|row| {
                    let fields = row.split('\t').collect::<Vec<_>>();
                    if fields.len() == 18
                        && fields[5] == "0"
                        && fields[7] == "-"
                        && fields[8] == "-"
                        && fields[9] == "0"
                    {
                        Some((fields[0], fields[1].parse::<usize>().ok()?, true))
                    } else {
                        None
                    }
                })
                .collect::<Vec<_>>();
            anyhow::ensure!(
                candidates.len() == 26,
                "unexpected number of Rust-unobserved discrete triangle candidates: expected 26, found {}",
                candidates.len()
            );
            for angle_bits in ["00000000", "3f490fdb", "3fc90fdb"] {
                anyhow::ensure!(
                    rows.iter()
                        .any(|row| { row.split('\t').nth(2) == Some(angle_bits) }),
                    "discrete-three-angle triangular fixture must sample rotation bits {angle_bits}"
                );
            }
            candidates
        }
        "triangle"
            if bounds_mode == "feasible-obstacle-rect"
                && rotation_mode == "continuous"
                && seed == 42
                && n_samples == 96 =>
        {
            let candidates = rows
                .iter()
                .filter_map(|row| {
                    let fields = row.split('\t').collect::<Vec<_>>();
                    if fields.len() == 18
                        && fields[5] == "0"
                        && fields[7] == "-"
                        && fields[8] == "-"
                        && fields[9] == "0"
                    {
                        Some((fields[0], fields[1].parse::<usize>().ok()?, true))
                    } else {
                        None
                    }
                })
                .collect::<Vec<_>>();
            anyhow::ensure!(
                candidates.len() == 31,
                "unexpected number of Rust-unobserved triangular obstacle candidates: expected 31, found {}",
                candidates.len()
            );
            candidates
        }
        "concave"
            if bounds_mode == "feasible-obstacle-rect"
                && rotation_mode == "continuous"
                && seed == 42
                && n_samples == 96 =>
        {
            let candidates = rows
                .iter()
                .filter_map(|row| {
                    let fields = row.split('\t').collect::<Vec<_>>();
                    if fields.len() == 18
                        && fields[5] == "0"
                        && fields[7] == "-"
                        && fields[8] == "-"
                        && fields[9] == "0"
                    {
                        Some((fields[0], fields[1].parse::<usize>().ok()?, true))
                    } else {
                        None
                    }
                })
                .collect::<Vec<_>>();
            anyhow::ensure!(
                candidates.len() == 32,
                "unexpected number of Rust-unobserved origin concave-L candidates: expected 32, found {}",
                candidates.len()
            );
            candidates
        }
        "concave-offset"
            if bounds_mode == "feasible-obstacle-rect"
                && rotation_mode == "continuous"
                && (seed == 17 || seed == 42)
                && n_samples == 96 =>
        {
            let candidates = rows
                .iter()
                .filter_map(|row| {
                    let fields = row.split('\t').collect::<Vec<_>>();
                    if fields.len() == 18
                        && fields[5] == "0"
                        && fields[7] == "-"
                        && fields[8] == "-"
                        && fields[9] == "0"
                    {
                        Some((fields[0], fields[1].parse::<usize>().ok()?, true))
                    } else {
                        None
                    }
                })
                .collect::<Vec<_>>();
            let expected_count = match seed {
                17 => 35,
                42 => 42,
                _ => unreachable!(),
            };
            anyhow::ensure!(
                candidates.len() == expected_count,
                "unexpected number of Rust-unobserved offset concave-L candidates: expected {expected_count}, found {}",
                candidates.len()
            );
            candidates
        }
        "concave-rot90"
            if bounds_mode == "feasible-obstacle-rect"
                && rotation_mode == "continuous"
                && (seed == 17 || seed == 42)
                && n_samples == 96 =>
        {
            let candidates = rows
                .iter()
                .filter_map(|row| {
                    let fields = row.split('\t').collect::<Vec<_>>();
                    if fields.len() == 18
                        && fields[5] == "0"
                        && fields[7] == "-"
                        && fields[8] == "-"
                        && fields[9] == "0"
                    {
                        Some((fields[0], fields[1].parse::<usize>().ok()?, true))
                    } else {
                        None
                    }
                })
                .collect::<Vec<_>>();
            let expected_count = match seed {
                17 => 35,
                42 => 42,
                _ => unreachable!(),
            };
            anyhow::ensure!(
                candidates.len() == expected_count,
                "unexpected number of Rust-unobserved 90-degree concave-L candidates: expected {expected_count}, found {}",
                candidates.len()
            );
            candidates
        }
        "concave-rot90-origin"
            if bounds_mode == "feasible-obstacle-rect"
                && rotation_mode == "continuous"
                && seed == 42
                && n_samples == 96 =>
        {
            let candidates = rows
                .iter()
                .filter_map(|row| {
                    let fields = row.split('\t').collect::<Vec<_>>();
                    if fields.len() == 18
                        && fields[5] == "0"
                        && fields[7] == "-"
                        && fields[8] == "-"
                        && fields[9] == "0"
                    {
                        Some((fields[0], fields[1].parse::<usize>().ok()?, true))
                    } else {
                        None
                    }
                })
                .collect::<Vec<_>>();
            anyhow::ensure!(
                candidates.len() == 38,
                "unexpected number of Rust-unobserved origin 90-degree concave-L candidates: expected 38, found {}",
                candidates.len()
            );
            candidates
        }
        "concave-rot45"
            if bounds_mode == "feasible-obstacle-rect"
                && rotation_mode == "continuous"
                && seed == 42
                && n_samples == 96 =>
        {
            let candidates = rows
                .iter()
                .filter_map(|row| {
                    let fields = row.split('\t').collect::<Vec<_>>();
                    if fields.len() == 18
                        && fields[5] == "0"
                        && fields[7] == "-"
                        && fields[8] == "-"
                        && fields[9] == "0"
                    {
                        Some((fields[0], fields[1].parse::<usize>().ok()?, true))
                    } else {
                        None
                    }
                })
                .collect::<Vec<_>>();
            anyhow::ensure!(
                candidates.len() == 42,
                "unexpected number of Rust-unobserved 45-degree concave-L candidates: expected 42, found {}",
                candidates.len()
            );
            candidates
        }
        "concave-rot45-searchpair" | "concave-rot225-searchpair" => {
            let candidates = rows
                .iter()
                .filter_map(|row| {
                    let fields = row.split('\t').collect::<Vec<_>>();
                    if fields.len() == 18
                        && fields[5] == "0"
                        && fields[7] == "-"
                        && fields[8] == "-"
                        && fields[9] == "0"
                    {
                        Some((fields[0], fields[1].parse::<usize>().ok()?, true))
                    } else {
                        None
                    }
                })
                .collect::<Vec<_>>();
            let expected_count = if obstacle_mode == "concave-rot45-searchpair" {
                36
            } else {
                31
            };
            anyhow::ensure!(
                candidates.len() == expected_count,
                "unexpected number of Rust-unobserved concave-L candidates: expected {expected_count}, found {}",
                candidates.len()
            );
            candidates
        }
        "concave-notch-rot45-searchpair" | "concave-notch-rot225-searchpair" => {
            let mut candidates = rows
                .iter()
                .filter_map(|row| {
                    let fields = row.split('\t').collect::<Vec<_>>();
                    if fields.len() == 18
                        && fields[5] == "0"
                        && fields[7] == "-"
                        && fields[8] == "-"
                        && fields[9] == "0"
                    {
                        Some((fields[0], fields[1].parse::<usize>().ok()?, true))
                    } else {
                        None
                    }
                })
                .collect::<Vec<_>>();
            let expected_count = if obstacle_mode == "concave-notch-rot45-searchpair" {
                33
            } else {
                35
            };
            anyhow::ensure!(
                candidates.len() == expected_count,
                "unexpected number of Rust-unobserved C-shape candidates: expected {expected_count}, found {}",
                candidates.len()
            );
            for ordinal in [8, 33] {
                if !candidates.iter().any(|(phase, candidate_ordinal, _)| {
                    *phase == "uniform" && *candidate_ordinal == ordinal
                }) {
                    candidates.push(("uniform", ordinal, false));
                }
            }
            candidates
        }
        _ => Vec::new(),
    };
    let forced_collision_probe = if !forced_probe_candidates.is_empty() {
        let mut probe_fields = String::new();
        for (phase, ordinal, require_loss_pruned) in forced_probe_candidates {
            let row_prefix = format!("{phase}\t{ordinal}\t");
            let row = rows
                .iter()
                .find(|row| row.starts_with(&row_prefix))
                .ok_or_else(|| anyhow::anyhow!("{phase} ordinal {ordinal} is missing"))?;
            let fields = row.split('\t').collect::<Vec<_>>();
            anyhow::ensure!(fields.len() == 18, "unexpected trace row schema: {row}");
            if require_loss_pruned {
                anyhow::ensure!(
                    fields[5] == "0"
                        && fields[7] == "-"
                        && fields[8] == "-"
                        && fields[9] == "0",
                    "forced candidate {phase}/{ordinal} must be loss-pruned before Rust polygon classification: {row}"
                );
            }
            let transform = DTransformation::new(
                f32::from_bits(u32::from_str_radix(fields[2], 16)?),
                (
                    f32::from_bits(u32::from_str_radix(fields[3], 16)?),
                    f32::from_bits(u32::from_str_radix(fields[4], 16)?),
                ),
            )
            .compose();
            let surrogate_collision = layout.cde().detect_surrogate_collision(
                item.shape_cd.surrogate(),
                &transform,
                &filter,
            );
            let polygon_collision = if surrogate_collision {
                None
            } else {
                let mut buffer = (*item.shape_cd).clone();
                buffer.surrogate = None;
                buffer.transform_from(&item.shape_cd, &transform);
                Some(layout.cde().detect_poly_collision(&buffer, &filter))
            };
            let bool_field = |value: Option<bool>| match value {
                Some(true) => "1",
                Some(false) => "0",
                None => "-",
            };
            let probe_prefix = format!("forced_probe_{phase}_{ordinal}");
            probe_fields.push_str(&format!(
                " {probe_prefix}_rotation={} {probe_prefix}_x={} {probe_prefix}_y={} {probe_prefix}_surrogate={} {probe_prefix}_polygon={}",
                fields[2],
                fields[3],
                fields[4],
                u8::from(surrogate_collision),
                bool_field(polygon_collision),
            ));
        }
        probe_fields
    } else {
        String::new()
    };

    if obstacle_mode == "concave"
        || obstacle_mode == "concave-rot90-origin"
        || obstacle_mode == "concave-rot45-searchpair"
        || obstacle_mode == "concave-rot225-searchpair"
    {
        let fixed_prefix = [
            ("40a3b9cb", "414c0ba0", "41fad89c"),
            ("408cf8bf", "41fdebe3", "4178620c"),
            ("3f49a0f5", "41aa65d9", "3e9e0ba0"),
            ("40bba91b", "415514b8", "41d00272"),
        ];
        for (ordinal, expected_bits) in fixed_prefix.iter().enumerate() {
            let row = rows
                .iter()
                .find(|row| row.starts_with(&format!("uniform\t{ordinal}\t")))
                .ok_or_else(|| anyhow::anyhow!("uniform ordinal {ordinal} is missing"))?;
            let fields = row.split('\t').collect::<Vec<_>>();
            anyhow::ensure!(
                fields.len() == 18
                    && (fields[2], fields[3], fields[4]) == *expected_bits,
                "the same-bbox orientation pair must preserve fixed uniform candidate {ordinal}: {row}"
            );
        }
        let candidate_row = rows
            .iter()
            .find(|row| row.starts_with("uniform\t4\t"))
            .ok_or_else(|| anyhow::anyhow!("uniform ordinal 4 is missing"))?;
        let fields = candidate_row.split('\t').collect::<Vec<_>>();
        let expected_collision =
            obstacle_mode == "concave" || obstacle_mode == "concave-rot225-searchpair";
        anyhow::ensure!(
            fields.len() == 18
                && fields[2] == "4088b931"
                && fields[3] == "3fd3d359"
                && fields[4] == "4101f784"
                && fields[5] == "0"
                && fields[7] == "1"
                && fields[8] == if expected_collision { "1" } else { "0" }
                && fields[9] == if expected_collision { "0" } else { "1" },
            "the fixed uniform ordinal 4 must match the selected concave-orientation pair: {candidate_row}"
        );
    }

    if bounds_mode.starts_with("no-valid-placement") {
        anyhow::ensure!(
            direct.is_none(),
            "fully blocked LBF search must return no placement"
        );
        anyhow::ensure!(
            direct_count == uniform_budget && traced_count == uniform_budget,
            "pinned LBF must count only its uniform budget when no placement is found"
        );
        anyhow::ensure!(
            rows.len() == uniform_budget && normal_rows.is_empty(),
            "local sampling must be skipped after all uniform candidates collide"
        );
        for (ordinal, row) in rows.iter().enumerate() {
            let fields = row.split('\t').collect::<Vec<_>>();
            anyhow::ensure!(
                fields.len() == 18
                    && fields[0] == "uniform"
                    && fields[1] == ordinal.to_string()
                    && (fields[5] == "1" || (fields[7] == "1" && fields[8] == "1"))
                    && fields[9] == "0",
                "uniform candidate {ordinal} must collide without acceptance: {row}"
            );
        }
    }
    if bounds_mode.starts_with("feasible-") {
        let local_row_count = rows.iter().filter(|row| row.starts_with("local\t")).count();
        anyhow::ensure!(
            direct.is_some()
                && direct_count == n_samples
                && rows.len() == n_samples
                && rows.len() - local_row_count == uniform_budget
                && local_row_count == (n_samples as f32 * ls_frac) as usize
                && (if rotation_mode == "none" {
                    !normal_rows.is_empty()
                } else {
                    normal_rows.is_empty()
                }),
            "feasible rectangular search must produce both uniform and local candidates"
        );
    }

    let (result_rotation, result_x, result_y, result_cost) = match direct {
        Some((transform, loss)) => {
            let (x, y) = transform.translation();
            (
                bits(transform.rotation()),
                bits(x),
                bits(y),
                bits(loss.cost()),
            )
        }
        None => ("-".into(), "-".into(), "-".into(), "-".into()),
    };
    let trace_row_count = rows.len();
    let orientation_probe = if obstacle_mode == "concave-rot90" {
        " orientation_probe=unrotated-left-collides-rotated-left-clear"
    } else if obstacle_mode == "concave-rot90-origin" {
        " search_candidate_probe=uniform-ordinal-4-same-bbox-rotation-sensitive"
    } else if obstacle_mode == "concave-rot45" {
        " orientation_probe=unrotated-left-collides-rotated45-clear"
    } else if obstacle_mode == "concave-rot45-searchpair"
        || obstacle_mode == "concave-rot225-searchpair"
    {
        " search_candidate_probe=same-aabb-45-vs-225"
    } else if obstacle_mode == "concave-notch-rot45-searchpair"
        || obstacle_mode == "concave-notch-rot225-searchpair"
    {
        " search_candidate_probe=same-aabb-45-vs-225-notched-topology"
    } else {
        ""
    };
    let obstacle_position_note = if obstacle_mode == "concave-notch-rot45-searchpair"
        || obstacle_mode == "concave-notch-rot225-searchpair"
    {
        format!(
            " blocker_aabb_min=({}, {})",
            bits(obstacle_bbox_min_x),
            bits(obstacle_bbox_min_y)
        )
    } else {
        String::new()
    };
    let rotation_angle_note = if discrete_three_angles {
        " rotation_angles=0,pi/4,pi/2"
    } else {
        ""
    };
    let mut contents = format!(
        "# generator=pinned jagua-rs/lbf::opt::search::search at f5e2b5ffd14931f17a20ce283a2122e877953ceb; instrumented transcription verified against direct result, sample count, and next RNG output\n# fixture={fixture_name} seed={seed} n_samples={n_samples} ls_frac={ls_frac} CDE=(depth:4,threshold:8) item={item_width}x{item_height} rotation={rotation_mode} bounds_mode={bounds_mode} obstacle={obstacle_mode}{rotation_angle_note}{orientation_probe}{obstacle_position_note}\n# result_samples={direct_count} next_rng_u64={direct_next:016x} result_rotation={result_rotation} result_x={result_x} result_y={result_y} result_cost={result_cost}{forced_collision_probe}\n# phase ordinal rotation_bits x_bits y_bits surrogate_collision loss_cost_bits polygon_checked polygon_collision accepted before_x_min_bits before_y_min_bits before_x_max_bits before_y_max_bits after_x_min_bits after_y_min_bits after_x_max_bits after_y_max_bits\n"
    );
    if bounds_mode.starts_with("no-valid-placement") {
        contents.push_str(&format!(
            "# result_status=none sample_counter={direct_count} uniform_rows={trace_row_count} local_rows={} next_rng_u64={direct_next:016x}\n",
            normal_rows.len(),
        ));
    }
    if bounds_mode.starts_with("feasible-") {
        let local_row_count = rows.iter().filter(|row| row.starts_with("local\t")).count();
        contents.push_str(&format!(
            "# result_status=some sample_counter={direct_count} uniform_rows={} local_rows={local_row_count} next_rng_u64={direct_next:016x}\n",
            trace_row_count - local_row_count,
        ));
    }
    for row in rows {
        contents.push_str(&row);
        contents.push('\n');
    }
    let path = Path::new(&output_path);
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent)?;
    }
    fs::write(path, contents)?;
    let normal_header = if bounds_mode.starts_with("no-valid-placement") {
        format!("# fixture={fixture_name}; all uniform candidates collide; local search skipped\n")
    } else {
        format!(
            "# generator=pinned rand_distr 0.6.0 Normal<f32>/StandardNormal, SmallRng seed={seed}\n# fixture={fixture_name}; rotation={rotation_mode}; local start follows {uniform_budget} uniform samples\n# columns: local_ordinal axis standard_normal_z_bits consumed_u64_words normal_sample_value_bits next_u64_after_0..3\n"
        )
    };
    let mut normal_contents = normal_header;
    for row in normal_rows {
        normal_contents.push_str(&row);
        normal_contents.push('\n');
    }
    let normal_path = Path::new(&normal_output_path);
    if let Some(parent) = normal_path.parent() {
        fs::create_dir_all(parent)?;
    }
    fs::write(normal_path, normal_contents)?;
    println!("pinned LBF trace verified: samples={direct_count}, rows={trace_row_count}");
    Ok(())
}
