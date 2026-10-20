use jagua_rs::collision_detection::CDEConfig;
use jagua_rs::collision_detection::hazards::filter::{HazardFilter, NoFilter};
use jagua_rs::entities::{Container, Item, Layout};
use jagua_rs::geometry::DTransformation;
use jagua_rs::geometry::fail_fast::SPSurrogateConfig;
use jagua_rs::geometry::primitives::{Rect, SPolygon};
use jagua_rs::geometry::shape_modification::{ShapeModifyConfig, ShapeModifyMode};
use jagua_rs::geometry::{OriginalShape, geo_traits::TransformableFrom};
use lbf::config::LBFConfig;
use lbf::opt::loss::LBFLoss;
use lbf::opt::search::search;
use lbf::samplers::ls_sampler::{LSSampler, SD_TRANSL};
use lbf::samplers::uniform_rect_sampler::UniformRectSampler;
use rand::prelude::SmallRng;
use rand::{Rng, SeedableRng};
use rand_distr::{Distribution, Normal, StandardNormal};
use std::cmp::Ordering;
use std::fs;
use std::path::Path;

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
    let normal_rows = trace_local_normal_components(rng, (mean_x, mean_y), local_stddev);
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

    let bounds = Rect::try_new(0.0, 0.0, 30.0, 30.0)?;
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
            shape: SPolygon::from(&Rect::try_new(0.0, 0.0, 1.0, 1.0)?),
            pre_transform: DTransformation::empty(),
            modify_mode: ShapeModifyMode::Inflate,
            modify_config: ShapeModifyConfig::default(),
        },
        jagua_rs::geometry::geo_enums::RotationRange::None,
        None,
        item_surrogate_config,
    )?;
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
    let layout = Layout::new(container);
    let config = LBFConfig {
        n_samples: 64,
        ls_frac: 0.25,
        ..LBFConfig::default()
    };
    let filter = NoFilter;
    let mut direct_rng = SmallRng::seed_from_u64(42);
    let mut traced_rng = SmallRng::seed_from_u64(42);
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
    let mut contents = format!(
        "# generator=pinned jagua-rs/lbf::opt::search::search at f5e2b5ffd14931f17a20ce283a2122e877953ceb; instrumented transcription verified against direct result, sample count, and next RNG output\n# fixture=plain-square-30x30 seed=42 n_samples=64 ls_frac=0.25 CDE=(depth:4,threshold:8) item=1x1 rotation=none\n# result_samples={direct_count} next_rng_u64={direct_next:016x} result_rotation={result_rotation} result_x={result_x} result_y={result_y} result_cost={result_cost}\n# phase ordinal rotation_bits x_bits y_bits surrogate_collision loss_cost_bits polygon_checked polygon_collision accepted before_x_min_bits before_y_min_bits before_x_max_bits before_y_max_bits after_x_min_bits after_y_min_bits after_x_max_bits after_y_max_bits\n"
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
    let mut normal_contents = format!(
        "# generator=pinned rand_distr 0.6.0 Normal<f32>/StandardNormal, SmallRng seed=42\n# fixture=plain-square-30x30; local start follows 48 no-rotation uniform samples\n# columns: local_ordinal axis standard_normal_z_bits consumed_u64_words normal_sample_value_bits next_u64_after_0..3\n"
    );
    for row in normal_rows {
        normal_contents.push_str(&row);
        normal_contents.push('\n');
    }
    let normal_path = Path::new(&normal_output_path);
    if let Some(parent) = normal_path.parent() {
        fs::create_dir_all(parent)?;
    }
    fs::write(normal_path, normal_contents)?;
    println!(
        "pinned LBF trace verified: samples={direct_count}, rows={}",
        64
    );
    Ok(())
}
