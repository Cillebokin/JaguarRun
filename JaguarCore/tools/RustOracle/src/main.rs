use geo_buffer::buffer_polygon_rounded;
use geo_types::{LineString, Polygon};
use jagua_rs::collision_detection::hazards::collector::BasicHazardCollector;
use jagua_rs::collision_detection::hazards::filter::{HazKeyFilter, NoFilter};
use jagua_rs::collision_detection::hazards::{Hazard, HazardEntity};
use jagua_rs::collision_detection::{CDEConfig, CDEngine};
use jagua_rs::entities::{Container, InferiorQualityZone, Item, Layout};
use jagua_rs::geometry::convex_hull::convex_hull_from_points;
use jagua_rs::geometry::fail_fast::SPSurrogateConfig;
use jagua_rs::geometry::geo_enums::RotationRange;
use jagua_rs::geometry::geo_traits::{
    AlmostCollidesWith, CollidesWith, DistanceTo, SeparationDistance, Transformable,
    TransformableFrom,
};
use jagua_rs::geometry::primitives::{Circle, Edge, Point, Rect, SPolygon};
use jagua_rs::geometry::shape_modification::{
    ShapeModifyConfig, ShapeModifyMode, offset_shape, shape_modification_valid,
};
use jagua_rs::geometry::{DTransformation, OriginalShape};
use jagua_rs::io::export::int_to_ext_transformation;
use jagua_rs::io::ext_repr::{
    ExtContainer, ExtItem, ExtLayout, ExtOrientation, ExtPlacedItem, ExtPolygon, ExtQualityZone,
    ExtRotation, ExtSPolygon, ExtShape, ExtTransformation,
};
use jagua_rs::io::import::{Importer, import_simple_polygon};
use jagua_rs::io::svg::{SvgDrawOptions, layout_to_svg};
use jagua_rs::probs::bpp::entities::{BPInstance, BPLayoutType, BPPlacement, BPProblem, Bin};
use jagua_rs::probs::bpp::io::ext_repr::ExtBPInstance;
use jagua_rs::probs::mspp::entities::{MSPInstance, MSPPlacement, MSPProblem, Strip};
use jagua_rs::probs::mspp::io::ext_repr::{ExtMSPInstance, ExtMSPStrip};
use jagua_rs::probs::spp::entities::{SPInstance, SPPlacement, SPProblem, Strip as SPStrip};
use jagua_rs::probs::spp::io::ext_repr::{ExtSPInstance, ExtSPSolution};
use jagua_rs::probs::spp::io::import_solution as import_spp_solution;
use std::sync::Arc;

fn polygon(points: &[(f32, f32)]) -> SPolygon {
    SPolygon::new(points.iter().copied().map(Point::from).collect()).unwrap()
}

fn square_item(index: usize, side: f32) -> Arc<Item> {
    let original_shape = OriginalShape {
        shape: polygon(&[(0.0, 0.0), (side, 0.0), (side, side), (0.0, side)]),
        pre_transform: DTransformation::empty(),
        modify_mode: ShapeModifyMode::Inflate,
        modify_config: ShapeModifyConfig::default(),
    };
    Arc::new(
        Item::new(
            index,
            1000 + index as u64,
            original_shape,
            RotationRange::None,
            None,
            SPSurrogateConfig::none(),
        )
        .unwrap(),
    )
}

fn print_polygon(name: &str, shape: &SPolygon) {
    println!("{name}.vertices={}", shape.n_vertices());
    println!("{name}.area={:.12}", shape.area);
    println!(
        "{name}.bbox={:.12},{:.12},{:.12},{:.12}",
        shape.bbox.x_min, shape.bbox.y_min, shape.bbox.x_max, shape.bbox.y_max
    );
    println!(
        "{name}.centroid={:.12},{:.12}",
        shape.centroid().0,
        shape.centroid().1
    );
}

fn print_buffer_components(name: &str, points: &[(f32, f32)], distance: f64) {
    let input = Polygon::new(
        LineString::from(
            points
                .iter()
                .map(|(x, y)| (*x as f64, *y as f64))
                .collect::<Vec<_>>(),
        ),
        vec![],
    );
    let components = buffer_polygon_rounded(&input, distance).0;
    println!("{name}.components={}", components.len());
    for (index, component) in components.iter().enumerate() {
        let points: Vec<_> = component.exterior().points().collect();
        let mut area = 0.0f64;
        let mut x_min = f64::INFINITY;
        let mut y_min = f64::INFINITY;
        let mut x_max = f64::NEG_INFINITY;
        let mut y_max = f64::NEG_INFINITY;
        for point in &points {
            x_min = x_min.min(point.x());
            y_min = y_min.min(point.y());
            x_max = x_max.max(point.x());
            y_max = y_max.max(point.y());
        }
        for pair in points.windows(2) {
            area += pair[0].x() * pair[1].y() - pair[1].x() * pair[0].y();
        }
        println!(
            "{name}.component[{index}].vertices={} area={:.12} bbox={:.12},{:.12},{:.12},{:.12}",
            points.len().saturating_sub(1),
            area.abs() * 0.5,
            x_min,
            y_min,
            x_max,
            y_max
        );
    }
}

fn run_collision_parity_oracle() {
    let mut surrogate_shape = polygon(&[(4.95, 7.95), (5.05, 7.95), (5.05, 8.05), (4.95, 8.05)]);
    surrogate_shape
        .generate_surrogate(SPSurrogateConfig {
            n_pole_limits: [(1, 0.0), (0, 0.0), (0, 0.0)],
            ff_pole_area_ratio: 1.0,
            n_ff_piers: 0,
        })
        .unwrap();
    let notch_container = Arc::new(polygon(&[
        (0.0, 0.0),
        (10.0, 0.0),
        (10.0, 10.0),
        (6.0, 10.0),
        (6.0, 4.0),
        (4.0, 4.0),
        (4.0, 10.0),
        (0.0, 10.0),
    ]));

    for (threshold_name, cd_threshold) in [("high", 8), ("low", 0)] {
        let engine = CDEngine::new(
            Rect::try_new(0.0, 0.0, 10.0, 10.0).unwrap(),
            vec![Hazard::new(
                HazardEntity::Exterior,
                Arc::clone(&notch_container),
                false,
            )],
            CDEConfig {
                quadtree_depth: 4,
                cd_threshold,
                item_surrogate_config: SPSurrogateConfig::none(),
            },
        );
        let detect = engine.detect_surrogate_collision(
            surrogate_shape.surrogate(),
            &jagua_rs::geometry::Transformation::empty(),
            &NoFilter,
        );
        println!("collision.notch.detect.{threshold_name}={detect}");

        let mut collector = BasicHazardCollector::new();
        let stopped =
            engine.collect_surrogate_collisions_until(&surrogate_shape, &mut collector, |_| false);
        println!(
            "collision.notch.collect.{threshold_name}=stopped:{stopped},count:{}",
            collector.len()
        );
    }

    let mut pier_only_shape = polygon(&[
        (0.0, 0.0),
        (10.0, 0.0),
        (10.0, 10.0),
        (6.0, 10.0),
        (6.0, 4.0),
        (4.0, 4.0),
        (4.0, 10.0),
        (0.0, 10.0),
    ]);
    pier_only_shape
        .generate_surrogate(SPSurrogateConfig {
            n_pole_limits: [(1, 0.0), (0, 0.0), (0, 0.0)],
            ff_pole_area_ratio: 0.0,
            n_ff_piers: 1,
        })
        .unwrap();
    println!(
        "collision.notch.pier_only_count={}",
        pier_only_shape.surrogate().ff_piers().len()
    );

    let large_notch_container = Arc::new(polygon(&[
        (0.0, 0.0),
        (40.0, 0.0),
        (40.0, 40.0),
        (30.0, 40.0),
        (30.0, 10.0),
        (10.0, 10.0),
        (10.0, 40.0),
        (0.0, 40.0),
    ]));
    let large_notch_engine = CDEngine::new(
        Rect::try_new(0.0, 0.0, 40.0, 40.0).unwrap(),
        vec![Hazard::new(
            HazardEntity::Exterior,
            large_notch_container,
            false,
        )],
        CDEConfig {
            quadtree_depth: 4,
            cd_threshold: 8,
            item_surrogate_config: SPSurrogateConfig::none(),
        },
    );
    let pier_only_detect = large_notch_engine.detect_surrogate_collision(
        pier_only_shape.surrogate(),
        &jagua_rs::geometry::Transformation::from_translation((15.0, 15.0)),
        &NoFilter,
    );
    println!("collision.notch.detect_pier_only={pier_only_detect}");
}

fn run_b3_surrogate_parity_oracle() {
    let config = SPSurrogateConfig {
        n_pole_limits: [(8, 0.0), (4, 0.75), (2, 0.9)],
        ff_pole_area_ratio: 0.5,
        n_ff_piers: 2,
    };
    let shapes = [
        (
            "u",
            vec![
                (0.0, 0.0),
                (10.0, 0.0),
                (10.0, 10.0),
                (6.0, 10.0),
                (6.0, 4.0),
                (4.0, 4.0),
                (4.0, 10.0),
                (0.0, 10.0),
            ],
        ),
        (
            "l",
            vec![
                (0.0, 0.0),
                (8.0, 0.0),
                (8.0, 3.0),
                (3.0, 3.0),
                (3.0, 8.0),
                (0.0, 8.0),
            ],
        ),
        (
            "c",
            vec![
                (0.0, 0.0),
                (10.0, 0.0),
                (10.0, 2.0),
                (3.0, 2.0),
                (3.0, 8.0),
                (10.0, 8.0),
                (10.0, 10.0),
                (0.0, 10.0),
            ],
        ),
    ];

    for (name, points) in shapes {
        let mut shape = polygon(&points);
        shape.generate_surrogate(config).unwrap();
        let surrogate = shape.surrogate();
        println!("b3.{name}.poles.count={}", surrogate.poles.len());
        println!(
            "b3.{name}.fail_fast_poles.count={}",
            surrogate.ff_poles().len()
        );
        println!("b3.{name}.piers.count={}", surrogate.piers.len());
        println!(
            "b3.{name}.convex_hull_area={:.12}",
            surrogate.convex_hull_area
        );
        println!(
            "b3.{name}.convex_hull_indices={:?}",
            surrogate.convex_hull_indices
        );
        for (index, pole) in surrogate.poles.iter().enumerate() {
            println!(
                "b3.{name}.pole[{index}]={:.12},{:.12},{:.12}",
                pole.center.0, pole.center.1, pole.radius
            );
        }
        for (index, pier) in surrogate.piers.iter().enumerate() {
            println!(
                "b3.{name}.pier[{index}]={:.12},{:.12},{:.12},{:.12},{:.12}",
                pier.start.0,
                pier.start.1,
                pier.end.0,
                pier.end.1,
                pier.length()
            );
        }
    }

    run_collision_parity_oracle();
    run_b3_surrogate_boundary_oracle();
}

fn run_b3_surrogate_boundary_oracle() {
    let square_points = [(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)];
    let square = polygon(&square_points);
    let three_pole_config = SPSurrogateConfig {
        n_pole_limits: [(3, 0.0), (100, 2.0), (100, 2.0)],
        ff_pole_area_ratio: 0.0,
        n_ff_piers: 0,
    };
    let mut baseline_shape = square.clone();
    baseline_shape
        .generate_surrogate(three_pole_config)
        .unwrap();
    let baseline = baseline_shape.surrogate();
    let mut covered_area_after_two = 0.0_f32;
    for pole in baseline.poles.iter().take(2) {
        covered_area_after_two += pole.area();
    }
    let coverage_after_two = covered_area_after_two / square.area;
    let threshold_bits = coverage_after_two.to_bits();
    let below_threshold = f32::from_bits(threshold_bits - 1);
    let above_threshold = f32::from_bits(threshold_bits + 1);
    println!("b3.boundary.baseline_poles={}", baseline.poles.len());
    println!("b3.boundary.coverage_after_two={coverage_after_two:.9}");

    for (name, threshold) in [
        ("below", below_threshold),
        ("equal", coverage_after_two),
        ("above", above_threshold),
    ] {
        println!("b3.pole_threshold.{name}_value={threshold:.9}");
        let config = SPSurrogateConfig {
            n_pole_limits: [(2, threshold), (100, 2.0), (100, 2.0)],
            ff_pole_area_ratio: 0.0,
            n_ff_piers: 0,
        };
        let mut shape = square.clone();
        let result = shape.generate_surrogate(config);
        match result {
            Ok(()) => println!(
                "b3.pole_threshold.{name}_poles={}",
                shape.surrogate().poles.len()
            ),
            Err(error) => println!("b3.pole_threshold.{name}_poles=err:{error}"),
        }
    }

    for (name, ratio) in [
        ("below", below_threshold),
        ("equal", coverage_after_two),
        ("above", above_threshold),
    ] {
        println!("b3.fail_fast_ratio.{name}_value={ratio:.9}");
        let config = SPSurrogateConfig {
            ff_pole_area_ratio: ratio,
            ..three_pole_config
        };
        let mut shape = square.clone();
        let result = shape.generate_surrogate(config);
        match result {
            Ok(()) => println!(
                "b3.fail_fast_ratio.{name}_poles={}",
                shape.surrogate().ff_poles().len()
            ),
            Err(error) => println!("b3.fail_fast_ratio.{name}_poles=err:{error}"),
        }
    }

    let next_after_one = f32::from_bits(1.0_f32.to_bits() + 1);
    let smallest_positive = f32::from_bits(1);
    let invalid_ratios = [
        ("negative", -smallest_positive),
        ("above_one", next_after_one),
        ("nan", f32::NAN),
        ("positive_inf", f32::INFINITY),
        ("negative_inf", f32::NEG_INFINITY),
    ];
    for (name, ratio) in invalid_ratios {
        let config = SPSurrogateConfig {
            ff_pole_area_ratio: ratio,
            ..three_pole_config
        };
        let mut shape = square.clone();
        let result = shape.generate_surrogate(config);
        println!(
            "b3.ratio.{name}={}",
            if result.is_err() { "err" } else { "ok" }
        );
    }
    for (name, ratio) in [("zero", 0.0), ("one", 1.0)] {
        let config = SPSurrogateConfig {
            ff_pole_area_ratio: ratio,
            ..three_pole_config
        };
        let mut shape = square.clone();
        let result = shape.generate_surrogate(config);
        match result {
            Ok(()) => println!("b3.ratio.{name}=ok:{}", shape.surrogate().ff_poles().len()),
            Err(_) => println!("b3.ratio.{name}=err"),
        }
    }

    let offset_square = polygon(&[
        (100.0, 100.0),
        (101.0, 100.0),
        (101.0, 101.0),
        (100.0, 101.0),
    ]);
    let pier_failure_config = SPSurrogateConfig {
        n_pole_limits: [(1, 0.0), (0, 0.0), (0, 0.0)],
        ff_pole_area_ratio: 0.0,
        n_ff_piers: 1,
    };
    let mut pier_failure_shape = offset_square;
    let pier_result = pier_failure_shape.generate_surrogate(pier_failure_config);
    match pier_result {
        Ok(()) => println!("b3.pier_generation_failure=ok"),
        Err(error) => println!("b3.pier_generation_failure=err:{error}"),
    }

    let covering_pole = Circle::try_new(Point(0.5, 0.5), 100.0).unwrap();
    let pole_result = jagua_rs::geometry::fail_fast::compute_pole(&square, &[covering_pole]);
    match pole_result {
        Ok(_) => println!("b3.pole_generation_failure=ok"),
        Err(error) if error.to_string().contains("no pole found") => {
            println!("b3.pole_generation_failure=err:no pole found")
        }
        Err(error) => println!("b3.pole_generation_failure=err:{error}"),
    }
}

fn run_model_boundary_parity_oracle() {
    use std::panic::{AssertUnwindSafe, catch_unwind};

    std::panic::set_hook(Box::new(|_| {}));
    let cde_config = CDEConfig {
        quadtree_depth: 1,
        cd_threshold: 8,
        item_surrogate_config: SPSurrogateConfig::none(),
    };
    let shape_config = ShapeModifyConfig::default();
    let transform = DTransformation::new(0.0, (1.0, 1.0));

    let overflowing_spp = SPInstance::new(
        vec![(square_item(0, 1.0), usize::MAX), (square_item(1, 1.0), 1)],
        SPStrip::new(10.0, cde_config, shape_config, 10.0).unwrap(),
    );
    let total_demand = catch_unwind(|| overflowing_spp.total_item_qty());
    match total_demand {
        Ok(total) => println!("demand.total_overflow={total}"),
        Err(_) => println!("demand.total_overflow=panic"),
    }

    let mut spp = SPProblem::new(SPInstance::new(
        vec![(square_item(0, 1.0), 0)],
        SPStrip::new(10.0, cde_config, shape_config, 10.0).unwrap(),
    ))
    .unwrap();
    let spp_result = catch_unwind(AssertUnwindSafe(|| {
        spp.place_item(SPPlacement {
            item_idx: 0,
            d_transf: transform,
        })
    }));
    println!(
        "demand.spp.overplace={};remaining:{};placed:{}",
        if spp_result.is_err() { "panic" } else { "ok" },
        spp.item_demand_qtys[0],
        spp.layout.placed_items.len()
    );

    let make_container = || {
        Container::new(
            OriginalShape {
                shape: polygon(&[(0.0, 0.0), (10.0, 0.0), (10.0, 10.0), (0.0, 10.0)]),
                pre_transform: DTransformation::empty(),
                modify_mode: ShapeModifyMode::Deflate,
                modify_config: shape_config,
            },
            vec![],
            cde_config,
        )
        .unwrap()
    };
    let mut bpp = BPProblem::new(BPInstance::new(
        vec![(square_item(0, 1.0), 0)],
        vec![Bin::new(0, 5000, make_container(), 1, 1)],
    ));
    let bpp_result = catch_unwind(AssertUnwindSafe(|| {
        bpp.place_item(BPPlacement {
            item_idx: 0,
            d_transf: transform,
            layout_id: BPLayoutType::Closed { bin_id: 0 },
        })
    }));
    println!(
        "demand.bpp.overplace={};remaining:{};placed:{};stock:{}",
        if bpp_result.is_err() { "panic" } else { "ok" },
        bpp.item_demand_qtys[0],
        bpp.n_placed_items(),
        bpp.bin_stock_qtys[0]
    );

    let mut mspp = MSPProblem::new(MSPInstance::new(
        vec![(square_item(0, 1.0), 0)],
        Strip::new(10.0, 10.0, cde_config, shape_config, 10.0).unwrap(),
    ));
    let layout = mspp
        .add_layout_from_strip(mspp.instance.base_strip)
        .unwrap();
    let mspp_result = catch_unwind(AssertUnwindSafe(|| {
        mspp.place_item(MSPPlacement {
            lk: layout,
            item_idx: 0,
            d_transf: transform,
        })
    }));
    println!(
        "demand.mspp.overplace={};remaining:{};placed:{};layouts:{}",
        if mspp_result.is_err() { "panic" } else { "ok" },
        mspp.item_demand_qtys[0],
        mspp.n_placed_items(),
        mspp.layouts.len()
    );
}

fn run_restore_boundary_parity_oracle() {
    use std::panic::{AssertUnwindSafe, catch_unwind};

    std::panic::set_hook(Box::new(|_| {}));
    let cde_config = CDEConfig {
        quadtree_depth: 1,
        cd_threshold: 8,
        item_surrogate_config: SPSurrogateConfig::none(),
    };
    let shape_config = ShapeModifyConfig::default();
    let mut spp = SPProblem::new(SPInstance::new(
        vec![(square_item(0, 1.0), 1)],
        SPStrip::new(10.0, cde_config, shape_config, 10.0).unwrap(),
    ))
    .unwrap();
    spp.place_item(SPPlacement {
        item_idx: 0,
        d_transf: DTransformation::new(0.0, (1.0, 1.0)),
    });
    let saved_spp = spp.save();

    let mut spp_config = saved_spp.clone();
    spp_config.strip.cde_config.cd_threshold = 1;
    let spp_config_result = catch_unwind(AssertUnwindSafe(|| spp.restore(&spp_config)));
    println!(
        "restore.spp.config={};threshold:{};placed:{}",
        if spp_config_result.is_err() {
            "panic"
        } else {
            "ok"
        },
        spp.strip.cde_config.cd_threshold,
        spp.layout.placed_items.len()
    );

    let mut spp_width = saved_spp;
    spp_width.strip.width *= 0.5;
    let spp_width_result = catch_unwind(AssertUnwindSafe(|| spp.restore(&spp_width)));
    println!(
        "restore.spp.width={};strip_width:{};layout_width:{};placed:{}",
        if spp_width_result.is_err() {
            "panic"
        } else {
            "ok"
        },
        spp.strip.width,
        spp.layout.container.outer_orig.bbox().x_max,
        spp.layout.placed_items.len()
    );

    let mut mspp = MSPProblem::new(MSPInstance::new(
        vec![(square_item(0, 1.0), 1)],
        Strip::new(10.0, 10.0, cde_config, shape_config, 10.0).unwrap(),
    ));
    let layout = mspp
        .add_layout_from_strip(mspp.instance.base_strip)
        .unwrap();
    mspp.place_item(MSPPlacement {
        lk: layout,
        item_idx: 0,
        d_transf: DTransformation::new(0.0, (1.0, 1.0)),
    });
    let saved_mspp = mspp.save();

    let mut mspp_config = saved_mspp.clone();
    mspp_config.strips[layout].cde_config.cd_threshold = 1;
    let mspp_config_result = catch_unwind(AssertUnwindSafe(|| mspp.restore(&mspp_config)));
    println!(
        "restore.mspp.config={};threshold:{};placed:{}",
        if mspp_config_result.is_err() {
            "panic"
        } else {
            "ok"
        },
        mspp.strips[layout].cde_config.cd_threshold,
        mspp.n_placed_items()
    );

    let mut mspp_width = saved_mspp;
    mspp_width.strips[layout].width *= 0.5;
    let mspp_width_result = catch_unwind(AssertUnwindSafe(|| mspp.restore(&mspp_width)));
    println!(
        "restore.mspp.width={};strip_width:{};layout_width:{};placed:{}",
        if mspp_width_result.is_err() {
            "panic"
        } else {
            "ok"
        },
        mspp.strips[layout].width,
        mspp.layouts[layout].container.outer_orig.bbox().x_max,
        mspp.n_placed_items()
    );
}

fn run_zero_min_item_separation_oracle() {
    let spp = ExtSPInstance {
        name: "zero-spp".to_owned(),
        min_item_separation: 0.0,
        items: vec![],
        strip_height: 1.0,
    };
    let bpp = ExtBPInstance {
        name: "zero-bpp".to_owned(),
        min_item_separation: 0.0,
        items: vec![],
        bins: vec![],
    };
    let mspp = ExtMSPInstance {
        name: "zero-mspp".to_owned(),
        min_item_separation: 0.0,
        items: vec![],
        strips: ExtMSPStrip {
            height: 1.0,
            max_width: 1.0,
        },
    };

    for (name, serialized) in [
        ("spp", serde_json::to_value(spp).unwrap()),
        ("bpp", serde_json::to_value(bpp).unwrap()),
        ("mspp", serde_json::to_value(mspp).unwrap()),
    ] {
        println!(
            "wire.{name}.has_min_item_separation={}",
            serialized.get("min_item_separation").is_some()
        );
        println!(
            "wire.{name}.min_item_separation={}",
            serialized["min_item_separation"]
        );
    }
}

fn run_import_boundary_oracle() {
    use std::panic::{AssertUnwindSafe, catch_unwind};

    let cases = [
        (
            "nonconsecutive_duplicate",
            vec![(0.0, 0.0), (4.0, 0.0), (4.0, 2.0), (0.0, 0.0), (0.0, 2.0)],
        ),
        (
            "signed_zero_closure",
            vec![(0.0, 0.0), (2.0, 0.0), (2.0, 2.0), (0.0, 2.0), (-0.0, 0.0)],
        ),
        (
            "nan_coordinate",
            vec![(0.0, 0.0), (2.0, 0.0), (2.0, 2.0), (f32::NAN, 2.0)],
        ),
        (
            "infinite_coordinate",
            vec![(0.0, 0.0), (2.0, 0.0), (2.0, 2.0), (f32::INFINITY, 2.0)],
        ),
    ];

    for (name, points) in cases {
        match catch_unwind(AssertUnwindSafe(|| {
            import_simple_polygon(&ExtSPolygon(points))
        })) {
            Ok(Ok(shape)) => println!("import.boundary.{name}=ok:{}", shape.n_vertices()),
            Ok(Err(error)) => println!("import.boundary.{name}=err:{error}"),
            Err(_) => println!("import.boundary.{name}=panic"),
        }
    }

    let endpoint_edge = Edge::try_new(Point(0.0, 0.0), Point(2.0, 0.0)).unwrap();
    let touching_edge = Edge::try_new(Point(2.0, 0.0), Point(2.0, 1.0)).unwrap();
    println!(
        "import.boundary.endpoint_touch_collides={}",
        endpoint_edge.collides_at(&touching_edge).is_some()
    );

    let cde_config = CDEConfig {
        quadtree_depth: 1,
        cd_threshold: 8,
        item_surrogate_config: SPSurrogateConfig::none(),
    };
    let instance = SPInstance::new(
        vec![(square_item(0, 1.0), 1)],
        SPStrip::new(10.0, cde_config, ShapeModifyConfig::default(), 10.0).unwrap(),
    );
    for (name, transformation) in [
        (
            "nan_rotation",
            ExtTransformation {
                rotation: f32::NAN,
                translation: (2.0, 2.0),
            },
        ),
        (
            "infinite_rotation",
            ExtTransformation {
                rotation: f32::INFINITY,
                translation: (2.0, 2.0),
            },
        ),
        (
            "infinite_translation",
            ExtTransformation {
                rotation: 0.0,
                translation: (f32::INFINITY, 2.0),
            },
        ),
    ] {
        let solution = ExtSPSolution {
            strip_width: 10.0,
            layout: ExtLayout {
                container_id: 0,
                placed_items: vec![ExtPlacedItem {
                    item_id: 1000,
                    transformation,
                }],
                density: 0.0,
            },
            density: 0.0,
            run_time_sec: 0,
        };
        match catch_unwind(AssertUnwindSafe(|| {
            import_spp_solution(&instance, &solution)
        })) {
            Ok(Ok(_)) => println!("import.boundary.solution.{name}=ok"),
            Ok(Err(error)) => println!("import.boundary.solution.{name}=err:{error}"),
            Err(_) => println!("import.boundary.solution.{name}=panic"),
        }
    }
}

fn run_offset_boundary_oracle() {
    use std::panic::{AssertUnwindSafe, catch_unwind};

    let square = polygon(&[(0.0, 0.0), (2.0, 0.0), (2.0, 2.0), (0.0, 2.0)]);
    for (name, distance) in [
        ("nan", f32::NAN),
        ("positive_infinity", f32::INFINITY),
        ("negative_infinity", f32::NEG_INFINITY),
    ] {
        let result = catch_unwind(AssertUnwindSafe(|| {
            offset_shape(&square, ShapeModifyMode::Inflate, distance)
        }));
        match result {
            Ok(Ok(shape)) => print_polygon(&format!("offset.boundary.{name}"), &shape),
            Ok(Err(error)) => println!("offset.boundary.{name}=error:{error}"),
            Err(_) => println!("offset.boundary.{name}=panic"),
        }
    }

    match offset_shape(&square, ShapeModifyMode::Deflate, 2.0) {
        Ok(shape) => print_polygon("offset.boundary.collapse", &shape),
        Err(error) => println!("offset.boundary.collapse=error:{error}"),
    }
}

fn run_b1_geometry_oracle() {
    let angles = [-7.0_f32, -1.0, 7.0, 1_000_000.0];
    println!(
        "geometry.normalize_rotation={:.9},{:.9},{:.9},{:.9}",
        jagua_rs::geometry::normalize_rotation(angles[0]),
        jagua_rs::geometry::normalize_rotation(angles[1]),
        jagua_rs::geometry::normalize_rotation(angles[2]),
        jagua_rs::geometry::normalize_rotation(angles[3])
    );
    let normalized_nan = jagua_rs::geometry::normalize_rotation(f32::NAN);
    let normalized_infinity = jagua_rs::geometry::normalize_rotation(f32::INFINITY);
    println!(
        "geometry.normalize_rotation_nonfinite=nan:{},infinity:{},negative_infinity:{}",
        normalized_nan.is_nan(),
        normalized_infinity.is_nan(),
        jagua_rs::geometry::normalize_rotation(f32::NEG_INFINITY).is_nan()
    );
    println!(
        "geometry.normalize_rotation_negative_zero={}",
        jagua_rs::geometry::normalize_rotation(-0.0_f32).is_sign_negative()
    );

    let transform = DTransformation::new(0.37, (1.25, -0.75));
    let decomposed = DTransformation::from(transform.compose());
    let (decomposed_x, decomposed_y) = decomposed.translation();
    println!(
        "geometry.dtransformation={:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9}",
        DTransformation::empty().rotation(),
        DTransformation::empty().translation().0,
        DTransformation::empty().translation().1,
        transform.rotation(),
        transform.translation().0,
        transform.translation().1,
        decomposed.rotation(),
        decomposed_x,
        decomposed_y
    );
    let applied = Point(2.0, -3.0).transform_clone(&transform.compose());
    println!(
        "geometry.transformation_apply={:.9},{:.9}",
        applied.0, applied.1
    );
    let rotate_then_translate = jagua_rs::geometry::Transformation::empty()
        .rotate_translate(0.5, (3.0, -2.0))
        .decompose();
    let translate_then_rotate = jagua_rs::geometry::Transformation::empty()
        .translate_rotate((3.0, -2.0), 0.5)
        .decompose();
    println!(
        "geometry.transformation_order={:.9},{:.9},{:.9};{:.9},{:.9},{:.9}",
        rotate_then_translate.rotation(),
        rotate_then_translate.translation().0,
        rotate_then_translate.translation().1,
        translate_then_rotate.rotation(),
        translate_then_rotate.translation().0,
        translate_then_rotate.translation().1
    );
    let transform_base = jagua_rs::geometry::Transformation::from_translation((1.25, -2.5));
    let transform_other =
        jagua_rs::geometry::Transformation::from_rotation(-0.3).translate((0.25, 1.5));
    let decomposed_operation = DTransformation::new(-0.4, (0.5, 2.25));
    let transformation_paths = vec![
        jagua_rs::geometry::Transformation::empty(),
        jagua_rs::geometry::Transformation::from_translation((3.0, -4.0)),
        jagua_rs::geometry::Transformation::from_rotation(0.375),
        transform_base.clone().rotate(0.2),
        transform_base.clone().translate((3.0, -1.0)),
        transform_base.clone().rotate_translate(0.5, (2.0, -1.0)),
        transform_base.clone().translate_rotate((2.0, -1.0), 0.5),
        transform_base.clone().transform(&transform_other),
        transform_base
            .clone()
            .transform_from_decomposed(&decomposed_operation),
        transform_base.clone().inverse(),
    ];
    let transformation_probes = [Point(0.0, 0.0), Point(1.0, 0.0), Point(0.0, 1.0)];
    let mut matrix_probe_values = Vec::new();
    let mut decomposed_values = Vec::new();
    for path in &transformation_paths {
        for probe in &transformation_probes {
            let transformed = probe.transform_clone(path);
            matrix_probe_values.push(format!("{:.9},{:.9}", transformed.0, transformed.1));
        }
        let decomposed = path.decompose();
        let (translation_x, translation_y) = decomposed.translation();
        decomposed_values.push(format!(
            "{:.9},{:.9},{:.9}",
            decomposed.rotation(),
            translation_x,
            translation_y
        ));
    }
    println!(
        "geometry.transformation_matrix_probes={}",
        matrix_probe_values.join(";")
    );
    println!(
        "geometry.transformation_decompositions={}",
        decomposed_values.join(";")
    );
    let empty_transform_cases = [
        jagua_rs::geometry::Transformation::empty(),
        jagua_rs::geometry::Transformation::from_translation((0.0, 0.0)),
        jagua_rs::geometry::Transformation::from_rotation(0.375),
        jagua_rs::geometry::Transformation::from_rotation(std::f32::consts::TAU),
        jagua_rs::geometry::Transformation::from_translation((-0.0, 0.0)),
    ];
    println!(
        "geometry.transformation_is_empty={}",
        empty_transform_cases
            .iter()
            .map(|transform| transform.is_empty().to_string())
            .collect::<Vec<_>>()
            .join(",")
    );

    let circle = Circle::try_new(Point(2.0, 3.0), 5.0).unwrap();
    let circle_bbox = circle.bbox();
    println!(
        "geometry.circle_basics={:.9},{:.9},{:.9},{:.9},{:.9},{:.9}",
        circle.area(),
        circle_bbox.x_min,
        circle_bbox.y_min,
        circle_bbox.x_max,
        circle_bbox.y_max,
        circle.diameter()
    );
    println!(
        "geometry.circle_contains={}",
        circle.collides_with(&Point(2.0, 3.0))
    );
    println!(
        "geometry.circle_construction=zero_radius:{},negative_radius:{},nonfinite_center:{},nonfinite_radius:{}",
        Circle::try_new(Point(0.0, 0.0), 0.0).is_ok(),
        Circle::try_new(Point(0.0, 0.0), -1.0).is_ok(),
        Circle::try_new(Point(f32::INFINITY, 0.0), 1.0).is_ok(),
        Circle::try_new(Point(0.0, 0.0), f32::INFINITY).is_ok()
    );

    let bounding_single = Circle::try_new(Point(3.0, -4.0), 0.0).unwrap();
    let single_bounding_circle = Circle::bounding_circle([&bounding_single]);
    let outer_first_circles = [
        Circle::try_new(Point(0.0, 0.0), 10.0).unwrap(),
        Circle::try_new(Point(1.0, 0.0), 2.0).unwrap(),
    ];
    let outer_first = Circle::bounding_circle(outer_first_circles.iter());
    let inner_first_circles = [
        Circle::try_new(Point(0.0, 0.0), 1.0).unwrap(),
        Circle::try_new(Point(1.0, 0.0), 10.0).unwrap(),
    ];
    let inner_first = Circle::bounding_circle(inner_first_circles.iter());
    let concentric_nested_circles = [
        Circle::try_new(Point(0.0, 0.0), 1.0).unwrap(),
        Circle::try_new(Point(0.0, 0.0), 2.0).unwrap(),
    ];
    let concentric_nested = Circle::bounding_circle(concentric_nested_circles.iter());
    println!(
        "geometry.circle_bounding_circle_empty_contract={}",
        std::panic::catch_unwind(|| Circle::bounding_circle(std::iter::empty::<&Circle>()))
            .is_err()
    );
    println!(
        "geometry.circle_bounding_circle_finite_values={:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9}",
        single_bounding_circle.center.0,
        single_bounding_circle.center.1,
        single_bounding_circle.radius,
        outer_first.center.0,
        outer_first.center.1,
        outer_first.radius,
        inner_first.center.0,
        inner_first.center.1,
        inner_first.radius
    );
    println!(
        "geometry.circle_bounding_circle_finite_contract={},{},{},{},{}",
        single_bounding_circle == bounding_single,
        outer_first.center == Point(0.0, 0.0) && outer_first.radius == 10.0,
        inner_first.center == Point(5.0, 0.0) && inner_first.radius == 6.0,
        inner_first
            .center
            .distance_to(&inner_first_circles[1].center)
            + inner_first_circles[1].radius
            > inner_first.radius,
        concentric_nested.center.0.is_nan()
            && concentric_nested.center.1.is_nan()
            && concentric_nested.radius.is_nan()
    );

    let edge = Edge::try_new(Point(0.0, 0.0), Point(3.0, 4.0)).unwrap();
    let closest = edge.closest_point_on_edge(&Point(1.0, 2.0));
    println!(
        "geometry.edge_closest={:.9},{:.9},{:.9},{:.9}",
        closest.0,
        closest.1,
        edge.distance_to(&Point(1.0, 2.0)),
        edge.sq_distance_to(&Point(1.0, 2.0))
    );
    println!(
        "geometry.edge_construction=valid:{},zero_length:{},f32_collapsed:{}",
        Edge::try_new(Point(0.0, 0.0), Point(1.0, 0.0)).is_ok(),
        Edge::try_new(Point(1.0, 1.0), Point(1.0, 1.0)).is_ok(),
        Edge::try_new(Point(1.0, 0.0), Point(1.0 + f32::EPSILON * 0.5, 0.0)).is_ok()
    );
    let zero_length_edge = Edge {
        start: Point(1.0, 2.0),
        end: Point(1.0, 2.0),
    };
    let zero_length_closest = zero_length_edge.closest_point_on_edge(&Point(2.0, 3.0));
    let zero_length_bbox = zero_length_edge.bbox();
    println!(
        "geometry.edge_zero_length_operations={:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9}",
        zero_length_closest.0,
        zero_length_closest.1,
        zero_length_edge.length(),
        zero_length_edge.centroid().0,
        zero_length_edge.centroid().1,
        zero_length_bbox.x_min,
        zero_length_bbox.y_min,
        zero_length_bbox.x_max,
        zero_length_bbox.y_max,
        zero_length_edge.sq_distance_to(&Point(2.0, 3.0))
    );
    let zero_length_front = zero_length_edge.extend_at_front(2.0);
    let zero_length_back = zero_length_edge.extend_at_back(2.0);
    let zero_length_scaled = zero_length_edge.scale(3.0);
    let zero_length_reversed = zero_length_edge.reverse();
    let zero_length_crossing = Edge {
        start: Point(0.0, 2.0),
        end: Point(2.0, 2.0),
    };
    println!(
        "geometry.edge_zero_length_finite_contract={},{},{},{},{},{},{},{},{},{}",
        zero_length_front.start.0.is_nan(),
        zero_length_front.start.1.is_nan(),
        zero_length_front.end == zero_length_edge.end,
        zero_length_back.start == zero_length_edge.start,
        zero_length_back.end.0.is_nan(),
        zero_length_back.end.1.is_nan(),
        zero_length_scaled.start == zero_length_edge.start
            && zero_length_scaled.end == zero_length_edge.end,
        zero_length_reversed.start == zero_length_edge.end
            && zero_length_reversed.end == zero_length_edge.start,
        zero_length_edge.collides_with(&zero_length_crossing),
        zero_length_edge
            .collides_at(&zero_length_crossing)
            .is_some()
    );
    println!(
        "geometry.edge_zero_length_scale_reverse={:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9}",
        zero_length_scaled.start.0,
        zero_length_scaled.start.1,
        zero_length_scaled.end.0,
        zero_length_scaled.end.1,
        zero_length_reversed.start.0,
        zero_length_reversed.start.1,
        zero_length_reversed.end.0,
        zero_length_reversed.end.1
    );

    let smallest_normal = f32::MIN_POSITIVE;
    let underflow_length_edge =
        Edge::try_new(Point(0.0, 0.0), Point(smallest_normal, 0.0)).unwrap();
    let underflow_front = underflow_length_edge.extend_at_front(1.0);
    let underflow_back = underflow_length_edge.extend_at_back(1.0);
    println!(
        "geometry.edge_underflow_length_contract={},{},{},{},{},{}",
        true,
        underflow_length_edge.length() == 0.0,
        underflow_front.start.0.is_infinite() && underflow_front.start.0.is_sign_negative(),
        underflow_front.start.1.is_nan(),
        underflow_back.end.0.is_infinite() && underflow_back.end.0.is_sign_positive(),
        underflow_back.end.1.is_nan()
    );

    let nan_edge = Edge::try_new(Point(f32::NAN, 0.0), Point(1.0, 1.0)).unwrap();
    let infinite_edge = Edge::try_new(Point(f32::INFINITY, 0.0), Point(1.0, 1.0)).unwrap();
    let nan_edge_bbox = nan_edge.bbox();
    let infinite_edge_bbox = infinite_edge.bbox();
    println!(
        "geometry.edge_nonfinite_bbox_contract={},{},{},{},{},{},{},{}",
        nan_edge.start.0.is_nan(),
        infinite_edge.start.0.is_infinite(),
        !nan_edge_bbox.x_min.is_nan(),
        !nan_edge_bbox.y_min.is_nan(),
        !nan_edge_bbox.x_max.is_nan(),
        !nan_edge_bbox.y_max.is_nan(),
        nan_edge_bbox.x_min == nan_edge_bbox.x_max,
        infinite_edge_bbox.x_max.is_infinite()
    );
    println!(
        "geometry.edge_nan_bbox_values={:.9},{:.9},{:.9},{:.9}",
        nan_edge_bbox.x_min, nan_edge_bbox.y_min, nan_edge_bbox.x_max, nan_edge_bbox.y_max
    );
    let proper_edge_a = Edge::try_new(Point(0.0, 0.0), Point(10.0, 10.0)).unwrap();
    let proper_edge_b = Edge::try_new(Point(0.0, 10.0), Point(10.0, 0.0)).unwrap();
    let endpoint_edge_a = Edge::try_new(Point(0.0, 0.0), Point(2.0, 0.0)).unwrap();
    let endpoint_edge_b = Edge::try_new(Point(2.0, 0.0), Point(2.0, 1.0)).unwrap();
    let collinear_edge = Edge::try_new(Point(1.0, 0.0), Point(3.0, 0.0)).unwrap();
    println!(
        "geometry.edge_intersects=proper:{},endpoint:{},collinear:{}",
        proper_edge_a.collides_with(&proper_edge_b),
        endpoint_edge_a.collides_with(&endpoint_edge_b),
        endpoint_edge_a.collides_with(&collinear_edge)
    );
    let edge_contact_cases = vec![
        (
            Edge::try_new(Point(0.0, 0.0), Point(4.0, 0.0)).unwrap(),
            Edge::try_new(Point(2.0, -1.0), Point(2.0, 1.0)).unwrap(),
        ),
        (
            Edge::try_new(Point(0.0, 0.0), Point(2.0, 0.0)).unwrap(),
            Edge::try_new(Point(2.0, 0.0), Point(2.0, 1.0)).unwrap(),
        ),
        (
            Edge::try_new(Point(0.0, 0.0), Point(4.0, 0.0)).unwrap(),
            Edge::try_new(Point(2.0, 0.0), Point(2.0, 2.0)).unwrap(),
        ),
        (
            Edge::try_new(Point(0.0, 0.0), Point(2.0, 0.0)).unwrap(),
            Edge::try_new(Point(1.0, 0.0), Point(3.0, 0.0)).unwrap(),
        ),
        (
            Edge::try_new(Point(0.0, 0.0), Point(2.0, 0.0)).unwrap(),
            Edge::try_new(Point(2.0, 0.0), Point(4.0, 0.0)).unwrap(),
        ),
        (
            Edge::try_new(Point(0.0, 0.0), Point(4.0, 0.0)).unwrap(),
            Edge::try_new(Point(0.0, 1.0), Point(4.0, 1.0)).unwrap(),
        ),
        (
            Edge::try_new(Point(0.0, 0.0), Point(2.0, 2.0)).unwrap(),
            Edge::try_new(Point(0.0, 1.0), Point(1.0, 1.5)).unwrap(),
        ),
    ];
    let mut edge_contact_results = Vec::new();
    let mut edge_contact_points = Vec::new();
    for (edge_a, edge_b) in edge_contact_cases {
        if let Some(Point(x, y)) = edge_a.collides_at(&edge_b) {
            edge_contact_results.push("true");
            edge_contact_points.push(format!("{x:.9},{y:.9}"));
        } else {
            edge_contact_results.push("false");
        }
    }
    println!(
        "geometry.edge_collides_at={}",
        edge_contact_results.join(",")
    );
    println!(
        "geometry.edge_collides_at_points={}",
        edge_contact_points.join(";")
    );

    let rect = Rect::try_new(0.0, 0.0, 4.0, 2.0).unwrap();
    let quadrants = rect.quadrants();
    println!(
        "geometry.rect_basics={:.9},{:.9};{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9};{:.9},{:.9}",
        rect.centroid().0,
        rect.centroid().1,
        quadrants[0].x_min,
        quadrants[0].y_min,
        quadrants[0].x_max,
        quadrants[0].y_max,
        quadrants[1].x_min,
        quadrants[1].y_min,
        quadrants[1].x_max,
        quadrants[1].y_max,
        quadrants[2].x_min,
        quadrants[2].y_min,
        quadrants[2].x_max,
        quadrants[2].y_max,
        quadrants[3].x_min,
        quadrants[3].y_min,
        quadrants[3].x_max,
        quadrants[3].y_max,
        rect.width(),
        rect.height()
    );
    let enclosed_rect = Rect::try_new(1.0, 0.5, 3.0, 1.5).unwrap();
    let overlap_rect = Rect::try_new(3.0, 1.0, 5.0, 4.0).unwrap();
    let disjoint_rect = Rect::try_new(5.0, 0.0, 6.0, 1.0).unwrap();
    println!(
        "geometry.rect_relations=surrounding:{},enclosed:{},intersecting:{},disjoint:{}",
        matches!(
            rect.relation_to(enclosed_rect),
            jagua_rs::geometry::geo_enums::GeoRelation::Surrounding
        ),
        matches!(
            enclosed_rect.relation_to(rect),
            jagua_rs::geometry::geo_enums::GeoRelation::Enclosed
        ),
        matches!(
            rect.relation_to(overlap_rect),
            jagua_rs::geometry::geo_enums::GeoRelation::Intersecting
        ),
        matches!(
            rect.relation_to(disjoint_rect),
            jagua_rs::geometry::geo_enums::GeoRelation::Disjoint
        )
    );
    let threshold_rect = Rect::try_new(1.0, 1.0, 3.0, 3.0).unwrap();
    let mut rect_relation_ulp_results = Vec::new();
    for ulp_distance in [1_u32, 2, 3, 4, 8, 16] {
        let x_min = f32::from_bits(1.0_f32.to_bits() - ulp_distance);
        let near_boundary_rect = Rect::try_new(x_min, 1.5, 3.0, 2.5).unwrap();
        let strict_relation = threshold_rect.relation_to(near_boundary_rect);
        let almost_relation = threshold_rect.almost_relation_to(near_boundary_rect);
        rect_relation_ulp_results.push(format!(
            "{},{},{}",
            matches!(
                strict_relation,
                jagua_rs::geometry::geo_enums::GeoRelation::Intersecting
            ),
            matches!(
                almost_relation,
                jagua_rs::geometry::geo_enums::GeoRelation::Surrounding
            ),
            matches!(
                almost_relation,
                jagua_rs::geometry::geo_enums::GeoRelation::Intersecting
            )
        ));
    }
    println!(
        "geometry.rect_relation_ulp_boundary={}",
        rect_relation_ulp_results.join(";")
    );
    let through_rect = Edge::try_new(Point(-1.0, 1.0), Point(5.0, 1.0)).unwrap();
    let outside_rect = Edge::try_new(Point(-1.0, 3.0), Point(5.0, 3.0)).unwrap();
    println!(
        "geometry.rect_edge_intersects=through:{},outside:{}",
        rect.collides_with(&through_rect),
        rect.collides_with(&outside_rect)
    );
    println!(
        "geometry.rect_construction=valid:{},zero_width:{},zero_height:{},nan_max:{},infinite_max:{}",
        Rect::try_new(0.0, 0.0, 1.0, 1.0).is_ok(),
        Rect::try_new(1.0, 0.0, 1.0, 1.0).is_ok(),
        Rect::try_new(0.0, 1.0, 1.0, 1.0).is_ok(),
        Rect::try_new(0.0, 0.0, f32::NAN, 1.0).is_ok(),
        Rect::try_new(0.0, 0.0, f32::INFINITY, 1.0).is_ok()
    );
    let boundary_rect = Rect::try_new(0.0, 0.0, 2.0, 1.0).unwrap();
    let edge_touch_rect = Rect::try_new(2.0, 0.0, 3.0, 1.0).unwrap();
    let corner_touch_rect = Rect::try_new(2.0, 1.0, 3.0, 2.0).unwrap();
    let overlap_boundary_rect = Rect::try_new(1.0, 0.5, 3.0, 2.0).unwrap();
    let disjoint_boundary_rect = Rect::try_new(3.0, 0.0, 4.0, 1.0).unwrap();
    println!(
        "geometry.rect_boundary_intersections={},{},{},{}",
        Rect::intersection(boundary_rect, overlap_boundary_rect).is_some(),
        Rect::intersection(boundary_rect, edge_touch_rect).is_some(),
        Rect::intersection(boundary_rect, corner_touch_rect).is_some(),
        Rect::intersection(boundary_rect, disjoint_boundary_rect).is_some()
    );
    println!(
        "geometry.rect_boundary_resizes={},{},{},{}",
        boundary_rect.resize_by(-1.0, 0.0).is_none(),
        boundary_rect.resize_by(0.0, -0.5).is_none(),
        boundary_rect.resize_by(-1.0, -0.5).is_none(),
        boundary_rect.resize_by(0.0, 0.0) == Some(boundary_rect)
    );
    println!(
        "geometry.rect_zero_scale_rejected={}",
        std::panic::catch_unwind(|| boundary_rect.scale(0.0)).is_err()
    );
    let zero_width_rect = Rect {
        x_min: 1.0,
        y_min: 0.0,
        x_max: 1.0,
        y_max: 2.0,
    };
    let zero_height_rect = Rect {
        x_min: 0.0,
        y_min: 1.0,
        x_max: 2.0,
        y_max: 1.0,
    };
    println!(
        "geometry.rect_degenerate_metrics={:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9}",
        zero_width_rect.width(),
        zero_width_rect.height(),
        zero_width_rect.area(),
        zero_width_rect.diameter(),
        zero_width_rect.centroid().0,
        zero_width_rect.centroid().1,
        zero_height_rect.width(),
        zero_height_rect.height(),
        zero_height_rect.area(),
        zero_height_rect.diameter(),
        zero_height_rect.centroid().0,
        zero_height_rect.centroid().1
    );
    let mut degenerate_rect_shapes = Vec::new();
    for degenerate_rect in [zero_width_rect, zero_height_rect] {
        for corner in degenerate_rect.corners() {
            degenerate_rect_shapes.push(format!("{:.9},{:.9}", corner.0, corner.1));
        }
        for edge in degenerate_rect.edges() {
            degenerate_rect_shapes.push(format!(
                "{:.9},{:.9},{:.9},{:.9}",
                edge.start.0, edge.start.1, edge.end.0, edge.end.1
            ));
        }
    }
    println!(
        "geometry.rect_degenerate_boundary_shapes={}",
        degenerate_rect_shapes.join(";")
    );
    let mut degenerate_rect_sides = Vec::new();
    for degenerate_rect in [zero_width_rect, zero_height_rect] {
        for side in degenerate_rect.sides() {
            degenerate_rect_sides.push(format!(
                "{:.9},{:.9},{:.9},{:.9}",
                side.start.0, side.start.1, side.end.0, side.end.1
            ));
        }
    }
    println!(
        "geometry.rect_degenerate_sides={}",
        degenerate_rect_sides.join(";")
    );
    let area_rect = Rect::try_new(0.0, 0.0, 2.0, 2.0).unwrap();
    println!(
        "geometry.rect_degenerate_intersections={},{},{},{}",
        Rect::intersection(zero_width_rect, area_rect).is_some(),
        Rect::intersection(zero_width_rect, zero_width_rect).is_some(),
        Rect::intersection(zero_height_rect, area_rect).is_some(),
        Rect::intersection(zero_height_rect, zero_height_rect).is_some()
    );
    let lower_left_area_rect = Rect::try_new(-2.0, -1.0, 0.0, 1.0).unwrap();
    let upper_right_area_rect = Rect::try_new(3.0, 2.0, 4.0, 3.0).unwrap();
    let degenerate_bounding_rects = [
        Rect::bounding_rect(zero_width_rect, lower_left_area_rect),
        Rect::bounding_rect(zero_height_rect, upper_right_area_rect),
        Rect::bounding_rect(zero_width_rect, zero_height_rect),
    ];
    println!(
        "geometry.rect_degenerate_bounding_rects={:.9},{:.9},{:.9},{:.9};{:.9},{:.9},{:.9},{:.9};{:.9},{:.9},{:.9},{:.9}",
        degenerate_bounding_rects[0].x_min,
        degenerate_bounding_rects[0].y_min,
        degenerate_bounding_rects[0].x_max,
        degenerate_bounding_rects[0].y_max,
        degenerate_bounding_rects[1].x_min,
        degenerate_bounding_rects[1].y_min,
        degenerate_bounding_rects[1].x_max,
        degenerate_bounding_rects[1].y_max,
        degenerate_bounding_rects[2].x_min,
        degenerate_bounding_rects[2].y_min,
        degenerate_bounding_rects[2].x_max,
        degenerate_bounding_rects[2].y_max
    );
    let recover_width = zero_width_rect.resize_by(1.0, 0.0).unwrap();
    let recover_height = zero_height_rect.resize_by(0.0, 1.0).unwrap();
    println!(
        "geometry.rect_degenerate_resize_recovery={:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9}",
        recover_width.x_min,
        recover_width.y_min,
        recover_width.x_max,
        recover_width.y_max,
        recover_height.x_min,
        recover_height.y_min,
        recover_height.x_max,
        recover_height.y_max
    );
    println!(
        "geometry.rect_degenerate_scale_contract={},{},{},{},{}",
        std::panic::catch_unwind(|| rect.scale(-1.0)).is_err(),
        std::panic::catch_unwind(|| zero_width_rect.scale(1.0)).is_err(),
        std::panic::catch_unwind(|| zero_width_rect.scale(2.0)).is_err(),
        std::panic::catch_unwind(|| zero_height_rect.scale(1.0)).is_err(),
        std::panic::catch_unwind(|| zero_height_rect.scale(2.0)).is_err()
    );
    let one_ulp_rect = Rect::try_new(1.0, 0.0, f32::from_bits(0x3f80_0001), 1.0).unwrap();
    let zero_width_rect = Rect {
        x_min: 1.0,
        y_min: 0.0,
        x_max: 1.0,
        y_max: 1.0,
    };
    let one_ulp_height_rect = Rect::try_new(0.0, 1.0, 1.0, f32::from_bits(0x3f80_0001)).unwrap();
    println!(
        "geometry.rect_quadrant_panic_contract={},{},{},{}",
        std::panic::catch_unwind(|| one_ulp_rect.quadrants()).is_err(),
        std::panic::catch_unwind(|| zero_width_rect.quadrants()).is_err(),
        std::panic::catch_unwind(|| one_ulp_height_rect.quadrants()).is_err(),
        std::panic::catch_unwind(|| zero_height_rect.quadrants()).is_err()
    );

    let rectangle = polygon(&[(0.0, 0.0), (4.0, 0.0), (4.0, 2.0), (0.0, 2.0)]);
    let edges = rectangle
        .edge_iter()
        .map(|edge| {
            format!(
                "{:.9},{:.9}>{:.9},{:.9}",
                edge.start.0, edge.start.1, edge.end.0, edge.end.1
            )
        })
        .collect::<Vec<_>>()
        .join(";");
    println!(
        "geometry.simple_polygon_basics=vertices:{},area:{:.9},diameter:{:.9},bbox:{:.9},{:.9},{:.9},{:.9},centroid:{:.9},{:.9},poi:{:.9},{:.9},{:.9},edges:{}",
        rectangle.n_vertices(),
        rectangle.area,
        rectangle.diameter,
        rectangle.bbox.x_min,
        rectangle.bbox.y_min,
        rectangle.bbox.x_max,
        rectangle.bbox.y_max,
        rectangle.centroid().0,
        rectangle.centroid().1,
        rectangle.poi.center.0,
        rectangle.poi.center.1,
        rectangle.poi.radius,
        edges
    );
    println!(
        "geometry.simple_polygon_contains=inside:{},boundary:{},outside:{}",
        rectangle.collides_with(&Point(1.0, 1.0)),
        rectangle.collides_with(&Point(0.0, 1.0)),
        rectangle.collides_with(&Point(-1.0, 1.0))
    );
    let clockwise = polygon(&[(0.0, 0.0), (0.0, 2.0), (4.0, 2.0), (4.0, 0.0)]);
    println!(
        "geometry.simple_polygon_clockwise_vertices={}",
        clockwise
            .vertices
            .iter()
            .map(|point| format!("{:.0},{:.0}", point.0, point.1))
            .collect::<Vec<_>>()
            .join(";")
    );
    let constructor_result = |points: &[(f32, f32)]| {
        SPolygon::new(points.iter().copied().map(Point::from).collect()).is_ok()
    };
    println!(
        "geometry.simple_polygon_construction=valid:{},too_few:{},duplicate:{},collinear:{},bowtie:{},tiny:{}",
        constructor_result(&[(0.0, 0.0), (4.0, 0.0), (4.0, 2.0), (0.0, 2.0)]),
        constructor_result(&[(0.0, 0.0), (1.0, 0.0)]),
        constructor_result(&[(0.0, 0.0), (2.0, 0.0), (2.0, 2.0), (2.0, 2.0), (0.0, 2.0)]),
        constructor_result(&[(0.0, 0.0), (1.0, 0.0), (2.0, 0.0)]),
        constructor_result(&[(0.0, 0.0), (2.0, 2.0), (0.0, 2.0), (2.0, 0.0)]),
        constructor_result(&[(0.0, 0.0), (0.0, 1.0e-5), (1.0e-5, 1.0e-5), (1.0e-5, 0.0)])
    );

    let polygon_transform = DTransformation::new(0.37, (10.0, -2.0)).compose();
    let transformed = rectangle.transform_clone(&polygon_transform);
    let transformed_vertices = transformed
        .vertices
        .iter()
        .map(|point| format!("{:.9},{:.9}", point.0, point.1))
        .collect::<Vec<_>>()
        .join(";");
    println!(
        "geometry.simple_polygon_transform=vertices:{};bbox:{:.9},{:.9},{:.9},{:.9};centroid:{:.9},{:.9};area:{:.9};diameter:{:.9}",
        transformed_vertices,
        transformed.bbox.x_min,
        transformed.bbox.y_min,
        transformed.bbox.x_max,
        transformed.bbox.y_max,
        transformed.centroid().0,
        transformed.centroid().1,
        transformed.area,
        transformed.diameter
    );

    let accessor_point = Point(1.25, -2.5);
    let accessor_edge = Edge::try_new(Point(0.0, 0.0), Point(3.0, 4.0)).unwrap();
    let accessor_bbox = accessor_edge.bbox();
    let accessor_rect = Rect::try_new(0.0, 0.0, 4.0, 2.0).unwrap();
    let accessor_sides = accessor_rect.sides();
    let accessor_sides_text = accessor_sides
        .iter()
        .map(|side| {
            format!(
                "{:.9},{:.9},{:.9},{:.9}",
                side.start.0, side.start.1, side.end.0, side.end.1
            )
        })
        .collect::<Vec<_>>()
        .join(";");
    println!(
        "geometry.independent_accessors=point:{:.9},{:.9};bbox:{:.9},{:.9},{:.9},{:.9};sides:{}",
        accessor_point.x(),
        accessor_point.y(),
        accessor_bbox.x_min,
        accessor_bbox.y_min,
        accessor_bbox.x_max,
        accessor_bbox.y_max,
        accessor_sides_text
    );

    let transform_from = jagua_rs::geometry::Transformation::from_translation((3.0, -2.0));
    let transform_from_point = Point(2.0, -3.0);
    let mut point_from_target = Point(99.0, 98.0);
    point_from_target.transform_from(&transform_from_point, &transform_from);
    let transform_from_circle = Circle::try_new(Point(1.0, 2.0), 1.5).unwrap();
    let mut circle_from_target = Circle::try_new(Point(99.0, 98.0), 1.5).unwrap();
    circle_from_target.transform_from(&transform_from_circle, &transform_from);
    let transform_from_edge = Edge::try_new(Point(-1.0, 0.0), Point(2.0, 1.0)).unwrap();
    let mut edge_from_target = Edge::try_new(Point(10.0, 10.0), Point(11.0, 12.0)).unwrap();
    edge_from_target.transform_from(&transform_from_edge, &transform_from);
    let transform_from_source_polygon = polygon(&[(0.0, 0.0), (4.0, 0.0), (4.0, 2.0), (0.0, 2.0)]);
    let mut polygon_from_target =
        polygon(&[(10.0, 10.0), (14.0, 10.0), (14.0, 12.0), (10.0, 12.0)]);
    polygon_from_target.transform_from(&transform_from_source_polygon, &transform_from);
    let polygon_from_vertices = polygon_from_target
        .vertices
        .iter()
        .map(|point| format!("{:.9},{:.9}", point.0, point.1))
        .collect::<Vec<_>>()
        .join(";");
    println!(
        "geometry.transform_from=point:{:.9},{:.9};circle:{:.9},{:.9},{:.9};edge:{:.9},{:.9},{:.9},{:.9};polygon_vertices:{};bbox:{:.9},{:.9},{:.9},{:.9};centroid:{:.9},{:.9};area:{:.9};diameter:{:.9};poi:{:.9},{:.9},{:.9}",
        point_from_target.0,
        point_from_target.1,
        circle_from_target.center.0,
        circle_from_target.center.1,
        circle_from_target.radius,
        edge_from_target.start.0,
        edge_from_target.start.1,
        edge_from_target.end.0,
        edge_from_target.end.1,
        polygon_from_vertices,
        polygon_from_target.bbox.x_min,
        polygon_from_target.bbox.y_min,
        polygon_from_target.bbox.x_max,
        polygon_from_target.bbox.y_max,
        polygon_from_target.centroid().0,
        polygon_from_target.centroid().1,
        polygon_from_target.area,
        polygon_from_target.diameter,
        polygon_from_target.poi.center.0,
        polygon_from_target.poi.center.1,
        polygon_from_target.poi.radius
    );

    let surrogate_config = SPSurrogateConfig {
        n_pole_limits: [(1, 0.0), (0, 0.0), (0, 0.0)],
        ff_pole_area_ratio: 1.0,
        n_ff_piers: 1,
    };
    let mut surrogate_reference = polygon(&[(0.0, 0.0), (4.0, 0.0), (4.0, 2.0), (0.0, 2.0)]);
    surrogate_reference
        .generate_surrogate(surrogate_config)
        .unwrap();
    let mut surrogate_target = polygon(&[(0.0, 0.0), (4.0, 0.0), (4.0, 2.0), (0.0, 2.0)]);
    surrogate_target
        .generate_surrogate(surrogate_config)
        .unwrap();
    let surrogate_transform = DTransformation::new(0.37, (3.0, -2.0)).compose();
    surrogate_target.transform_from(&surrogate_reference, &surrogate_transform);
    let transformed_surrogate = surrogate_target.surrogate();
    let mut transformed_surrogate_values = vec![surrogate_target.n_vertices() as f32];
    for vertex in &surrogate_target.vertices {
        transformed_surrogate_values.extend([vertex.0, vertex.1]);
    }
    transformed_surrogate_values.extend([
        surrogate_target.bbox.x_min,
        surrogate_target.bbox.y_min,
        surrogate_target.bbox.x_max,
        surrogate_target.bbox.y_max,
        surrogate_target.centroid().0,
        surrogate_target.centroid().1,
        surrogate_target.area,
        surrogate_target.diameter,
        surrogate_target.poi.center.0,
        surrogate_target.poi.center.1,
        surrogate_target.poi.radius,
        transformed_surrogate.poles.len() as f32,
        transformed_surrogate.ff_poles().len() as f32,
        transformed_surrogate.piers.len() as f32,
        transformed_surrogate.convex_hull_area,
        transformed_surrogate.convex_hull_indices.len() as f32,
    ]);
    for index in &transformed_surrogate.convex_hull_indices {
        transformed_surrogate_values.push(*index as f32);
    }
    for pole in &transformed_surrogate.poles {
        transformed_surrogate_values.extend([pole.center.0, pole.center.1, pole.radius]);
    }
    for pier in &transformed_surrogate.piers {
        transformed_surrogate_values.extend([pier.start.0, pier.start.1, pier.end.0, pier.end.1]);
    }
    let surrogate_fixture_key = if cfg!(debug_assertions) {
        "geometry.transform_from_surrogate_debug"
    } else {
        "geometry.transform_from_surrogate_release"
    };
    println!(
        "{}={}",
        surrogate_fixture_key,
        transformed_surrogate_values
            .iter()
            .map(|value| format!("{value:.9}"))
            .collect::<Vec<_>>()
            .join(",")
    );
    let mut no_surrogate_target = polygon(&[(0.0, 0.0), (4.0, 0.0), (4.0, 2.0), (0.0, 2.0)]);
    no_surrogate_target.transform_from(&surrogate_reference, &surrogate_transform);
    let mut surrogate_target_without_reference =
        polygon(&[(0.0, 0.0), (4.0, 0.0), (4.0, 2.0), (0.0, 2.0)]);
    surrogate_target_without_reference
        .generate_surrogate(surrogate_config)
        .unwrap();
    let no_surrogate_reference = polygon(&[(0.0, 0.0), (4.0, 0.0), (4.0, 2.0), (0.0, 2.0)]);
    let missing_reference_surrogate_panics =
        std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
            surrogate_target_without_reference
                .transform_from(&no_surrogate_reference, &surrogate_transform);
        }))
        .is_err();
    println!(
        "geometry.transform_from_surrogate_state={},{}",
        no_surrogate_target.surrogate.is_none(),
        missing_reference_surrogate_panics
    );

    let dispatch_circle = Circle::try_new(Point(0.0, 0.0), 1.0).unwrap();
    let dispatch_rect = Rect::try_new(0.0, 0.0, 2.0, 2.0).unwrap();
    let dispatch_touching_rect = Rect::try_new(2.0, 0.0, 4.0, 2.0).unwrap();
    let dispatch_separated_rect = Rect::try_new(2.01, 0.0, 4.0, 2.0).unwrap();
    let dispatch_epsilon = f32::EPSILON;
    let dispatch_near_rect = Rect::try_new(-1.0, 0.0, -dispatch_epsilon * 0.25, 2.0).unwrap();
    let dispatch_far_rect = Rect::try_new(-1.0, 0.0, -dispatch_epsilon * 2.0, 2.0).unwrap();
    let dispatch_polygon = polygon(&[(0.0, 0.0), (4.0, 0.0), (4.0, 2.0), (0.0, 2.0)]);
    let dispatch_crossing_edge = Edge::try_new(Point(-1.0, 1.0), Point(3.0, 1.0)).unwrap();
    let circle_inside_point = Point(0.25, 0.0);
    let circle_outside_point = Point(3.0, 0.0);
    let rect_inside_point = Point(1.0, 1.0);
    let rect_outside_point = Point(3.0, 1.0);
    let near_rect_point = Point(-dispatch_epsilon * 0.25, 1.0);
    let far_rect_point = Point(-dispatch_epsilon * 2.0, 1.0);
    println!(
        "geometry.trait_dispatch=point_circle_inside:{},point_circle_outside:{},point_rect_inside:{},point_rect_outside:{},point_polygon_inside:{},point_polygon_outside:{},rect_point_inside:{},rect_point_outside:{},rect_rect_touching:{},rect_rect_separated:{},almost_rect_near:{},almost_rect_far:{},almost_point_near:{},almost_point_far:{},edge_rect:{},rect_edge:{}",
        circle_inside_point.collides_with(&dispatch_circle),
        circle_outside_point.collides_with(&dispatch_circle),
        rect_inside_point.collides_with(&dispatch_rect),
        rect_outside_point.collides_with(&dispatch_rect),
        rect_inside_point.collides_with(&dispatch_polygon),
        Point(5.0, 1.0).collides_with(&dispatch_polygon),
        dispatch_rect.collides_with(&rect_inside_point),
        dispatch_rect.collides_with(&rect_outside_point),
        dispatch_rect.collides_with(&dispatch_touching_rect),
        dispatch_rect.collides_with(&dispatch_separated_rect),
        dispatch_rect.almost_collides_with(&dispatch_near_rect),
        dispatch_rect.almost_collides_with(&dispatch_far_rect),
        dispatch_rect.almost_collides_with(&near_rect_point),
        dispatch_rect.almost_collides_with(&far_rect_point),
        dispatch_crossing_edge.collides_with(&dispatch_rect),
        dispatch_rect.collides_with(&dispatch_crossing_edge)
    );

    let helper_points = vec![
        Point(0.0, 0.0),
        Point(4.0, 0.0),
        Point(4.0, 2.0),
        Point(0.0, 2.0),
    ];
    let helper_diameter = SPolygon::calculate_diameter(helper_points.clone());
    let helper_bbox = SPolygon::generate_bounding_box(&helper_points);
    let helper_area = SPolygon::calculate_area(&helper_points);
    let helper_poi = SPolygon::calculate_poi(&helper_points, helper_diameter).unwrap();
    println!(
        "geometry.simple_polygon_helpers=diameter:{:.9};bbox:{:.9},{:.9},{:.9},{:.9};area:{:.9};poi:{:.9},{:.9},{:.9}",
        helper_diameter,
        helper_bbox.x_min,
        helper_bbox.y_min,
        helper_bbox.x_max,
        helper_bbox.y_max,
        helper_area,
        helper_poi.center.0,
        helper_poi.center.1,
        helper_poi.radius
    );

    let empty_helper_points: Vec<Point> = vec![];
    let singleton_helper_points = vec![Point(1.0, 2.0)];
    let two_helper_points = vec![Point(0.0, 0.0), Point(2.0, 2.0)];
    let collinear_helper_points = vec![Point(0.0, 0.0), Point(1.0, 1.0), Point(2.0, 2.0)];
    let two_helper_diameter = SPolygon::calculate_diameter(two_helper_points.clone());
    let collinear_helper_diameter = SPolygon::calculate_diameter(collinear_helper_points.clone());
    let empty_diameter_panics =
        std::panic::catch_unwind(|| SPolygon::calculate_diameter(empty_helper_points.clone()))
            .is_err();
    let empty_bbox_panics =
        std::panic::catch_unwind(|| SPolygon::generate_bounding_box(&empty_helper_points)).is_err();
    let empty_poi_panics =
        std::panic::catch_unwind(|| SPolygon::calculate_poi(&empty_helper_points, 0.0)).is_err();
    let singleton_diameter_panics =
        std::panic::catch_unwind(|| SPolygon::calculate_diameter(singleton_helper_points.clone()))
            .is_err();
    let singleton_bbox_panics =
        std::panic::catch_unwind(|| SPolygon::generate_bounding_box(&singleton_helper_points))
            .is_err();
    let singleton_poi_panics =
        std::panic::catch_unwind(|| SPolygon::calculate_poi(&singleton_helper_points, 0.0))
            .is_err();
    let two_point_poi = std::panic::catch_unwind(|| {
        SPolygon::calculate_poi(&two_helper_points, two_helper_diameter)
    });
    let (two_point_poi_panics, two_point_poi_is_error) = match two_point_poi {
        Ok(result) => (false, result.is_err()),
        Err(_) => (true, false),
    };
    let collinear_poi = std::panic::catch_unwind(|| {
        SPolygon::calculate_poi(&collinear_helper_points, collinear_helper_diameter)
    });
    let (collinear_poi_panics, collinear_poi_is_error) = match collinear_poi {
        Ok(result) => (false, result.is_err()),
        Err(_) => (true, false),
    };
    println!(
        "geometry.simple_polygon_helper_failures={},{},{},{},{},{},{},{},{},{}",
        empty_diameter_panics,
        empty_bbox_panics,
        empty_poi_panics,
        singleton_diameter_panics,
        singleton_bbox_panics,
        singleton_poi_panics,
        two_point_poi_is_error,
        two_point_poi_panics,
        collinear_poi_is_error,
        collinear_poi_panics
    );
    println!(
        "geometry.simple_polygon_helper_rejections={},{},{},{}",
        SPolygon::new(empty_helper_points.clone()).is_err(),
        SPolygon::new(singleton_helper_points.clone()).is_err(),
        SPolygon::new(two_helper_points.clone()).is_err(),
        SPolygon::new(collinear_helper_points.clone()).is_err()
    );
    let duplicate_pair_points = vec![Point(1.0, 2.0), Point(1.0, 2.0)];
    let duplicate_vertex_points = vec![
        Point(0.0, 0.0),
        Point(2.0, 0.0),
        Point(2.0, 0.0),
        Point(0.0, 2.0),
    ];
    let horizontal_collinear_points = vec![Point(0.0, 0.0), Point(1.0, 0.0), Point(2.0, 0.0)];
    let duplicate_pair_diameter = SPolygon::calculate_diameter(duplicate_pair_points.clone());
    let duplicate_vertex_diameter = SPolygon::calculate_diameter(duplicate_vertex_points.clone());
    let duplicate_vertex_bbox = SPolygon::generate_bounding_box(&duplicate_vertex_points);
    let duplicate_vertex_area = SPolygon::calculate_area(&duplicate_vertex_points);
    let horizontal_bbox_panics =
        std::panic::catch_unwind(|| SPolygon::generate_bounding_box(&horizontal_collinear_points))
            .is_err();
    println!(
        "geometry.simple_polygon_degenerate_metrics={:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9}",
        SPolygon::calculate_area(&empty_helper_points),
        SPolygon::calculate_area(&singleton_helper_points),
        duplicate_pair_diameter,
        SPolygon::calculate_area(&duplicate_pair_points),
        duplicate_vertex_diameter,
        duplicate_vertex_bbox.x_min,
        duplicate_vertex_bbox.y_min,
        duplicate_vertex_bbox.x_max,
        duplicate_vertex_bbox.y_max,
        duplicate_vertex_area,
        collinear_helper_diameter,
        SPolygon::calculate_area(&collinear_helper_points)
    );
    println!(
        "geometry.simple_polygon_degenerate_helper_contract={},{},{},{},{},{},{}",
        duplicate_pair_diameter == 0.0,
        SPolygon::calculate_area(&duplicate_pair_points) == 0.0,
        std::panic::catch_unwind(|| { SPolygon::generate_bounding_box(&duplicate_pair_points) })
            .is_err(),
        horizontal_bbox_panics,
        duplicate_vertex_area == 2.0,
        duplicate_vertex_bbox.x_min == 0.0
            && duplicate_vertex_bbox.y_min == 0.0
            && duplicate_vertex_bbox.x_max == 2.0
            && duplicate_vertex_bbox.y_max == 2.0,
        SPolygon::calculate_area(&collinear_helper_points) == 0.0
    );
    println!(
        "geometry.simple_polygon_degenerate_construction={},{}",
        SPolygon::new(duplicate_vertex_points.clone()).is_err(),
        SPolygon::new(horizontal_collinear_points.clone()).is_err()
    );

    let degenerate_hull_inputs = vec![
        vec![],
        vec![Point(1.0, 2.0)],
        vec![Point(1.0, 2.0), Point(1.0, 2.0)],
        duplicate_vertex_points,
        collinear_helper_points.clone(),
        horizontal_collinear_points,
    ];
    let mut degenerate_hull_counts = Vec::new();
    let mut degenerate_hull_points = Vec::new();
    for points in degenerate_hull_inputs {
        let hull = convex_hull_from_points(points);
        degenerate_hull_counts.push(hull.len().to_string());
        degenerate_hull_points.extend(
            hull.into_iter()
                .map(|point| format!("{:.9},{:.9}", point.0, point.1)),
        );
    }
    println!(
        "geometry.convex_hull_finite_degenerates_counts={}",
        degenerate_hull_counts.join(",")
    );
    println!(
        "geometry.convex_hull_finite_degenerates_points={}",
        degenerate_hull_points.join(",")
    );

    let original = OriginalShape {
        shape: rectangle,
        pre_transform: DTransformation::new(0.5, (10.0, 20.0)),
        modify_mode: ShapeModifyMode::Inflate,
        modify_config: ShapeModifyConfig::default(),
    };
    let internal = original.convert_to_internal().unwrap();
    println!(
        "geometry.original_shape=raw:{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9};internal:{},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9}",
        original.area(),
        original.diameter(),
        original.bbox().x_min,
        original.bbox().y_min,
        original.bbox().x_max,
        original.bbox().y_max,
        original.centroid().0,
        original.centroid().1,
        internal.n_vertices(),
        internal.vertices[0].0,
        internal.vertices[0].1,
        internal.bbox.x_min,
        internal.bbox.y_min,
        internal.bbox.x_max,
        internal.bbox.y_max,
        internal.centroid().0,
        internal.centroid().1,
        internal.area,
        internal.diameter
    );
}

fn main() {
    if std::env::args().any(|arg| arg == "--fail-fast-parity-only") {
        run_b3_surrogate_parity_oracle();
        return;
    }
    if std::env::args().any(|arg| arg == "--restore-boundary-parity-only") {
        run_restore_boundary_parity_oracle();
        return;
    }
    if std::env::args().any(|arg| arg == "--model-boundary-parity-only") {
        run_model_boundary_parity_oracle();
        return;
    }
    if std::env::args().any(|arg| arg == "--offset-boundary-parity-only") {
        run_offset_boundary_oracle();
        return;
    }
    if std::env::args().any(|arg| arg == "--import-boundary-parity-only") {
        run_import_boundary_oracle();
        return;
    }
    if std::env::args().any(|arg| arg == "--zero-separation-parity-only") {
        run_zero_min_item_separation_oracle();
        return;
    }
    if std::env::args().any(|arg| arg == "--collision-parity-only") {
        run_collision_parity_oracle();
        return;
    }
    if std::env::args().any(|arg| arg == "--geometry-primitives-parity-only") {
        run_b1_geometry_oracle();
        return;
    }

    run_b1_geometry_oracle();

    let tied_x_hull = convex_hull_from_points(vec![
        Point(0.0, 1.0),
        Point(1.0, 0.0),
        Point(0.0, 0.0),
        Point(1.0, 1.0),
    ]);
    println!(
        "geometry.convex_hull_tied_x={}",
        tied_x_hull
            .iter()
            .map(|point| format!("{:.0},{:.0}", point.0, point.1))
            .collect::<Vec<_>>()
            .join(";")
    );
    let singleton_hull = convex_hull_from_points(vec![Point(1.0, 2.0)]);
    println!(
        "geometry.convex_hull_singleton_count={}",
        singleton_hull.len()
    );

    let mut transformed_polygon = polygon(&[(0.0, 0.0), (10.0, 0.0), (10.0, 3.0), (0.0, 3.0)]);
    let original_area_bits = transformed_polygon.area.to_bits();
    let original_diameter_bits = transformed_polygon.diameter.to_bits();
    let large_translation = DTransformation::new(0.37, (1_000_000.0, -2_000_000.0)).compose();
    transformed_polygon.transform(&large_translation);
    println!(
        "geometry.polygon_transform_metrics=area:{original_area_bits:08x}->{:08x};diameter:{original_diameter_bits:08x}->{:08x}",
        transformed_polygon.area.to_bits(),
        transformed_polygon.diameter.to_bits()
    );
    let transformed_centroid = transformed_polygon.centroid();
    println!(
        "geometry.polygon_transform_centroid={:08x},{:08x}",
        transformed_centroid.0.to_bits(),
        transformed_centroid.1.to_bits()
    );

    let signed_zero_polygon = polygon(&[(0.0, 0.0), (2.0, 0.0), (2.0, 1.0), (-0.0, 1.0)]);
    println!(
        "geometry.polygon_signed_zero_bbox={:08x},{:08x}",
        signed_zero_polygon.bbox.x_min.to_bits(),
        signed_zero_polygon.bbox.x_max.to_bits()
    );

    let mut translated_collapse = polygon(&[(0.0, 0.0), (1.0, 0.0), (4.0, 1.0), (0.0, 1.0)]);
    let collapse_area_bits = translated_collapse.area.to_bits();
    translated_collapse.transform(&DTransformation::new(0.0, (16_777_216.0, 0.0)).compose());
    let collapse_centroid = translated_collapse.centroid();
    println!(
        "geometry.polygon_translated_collapse=vertices:{},bbox:{:08x},{:08x};area:{:08x}->{:08x};diameter:{:08x};centroid:{:08x},{:08x}",
        translated_collapse.n_vertices(),
        translated_collapse.bbox.x_min.to_bits(),
        translated_collapse.bbox.x_max.to_bits(),
        collapse_area_bits,
        translated_collapse.area.to_bits(),
        translated_collapse.diameter.to_bits(),
        collapse_centroid.0.to_bits(),
        collapse_centroid.1.to_bits()
    );

    let mut degenerate_transform = polygon(&[(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)]);
    let degenerate_result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        degenerate_transform.transform(&DTransformation::new(0.0, (16_777_216.0, 0.0)).compose());
    }));
    println!(
        "geometry.polygon_degenerate_transform={}",
        if degenerate_result.is_err() {
            "panic"
        } else {
            "ok"
        }
    );

    let near_zero_vertex_square = polygon(&[(0.0, 0.0), (2.0, 0.0), (2.0, 2.0), (0.0, 2.0)]);
    println!(
        "geometry.polygon_fpa_near_zero_contains={}",
        near_zero_vertex_square.collides_with(&Point(1.0, f32::EPSILON * 0.25))
    );
    let tiny_side = f32::EPSILON * 0.5;
    let mut tiny_square = polygon(&[
        (0.0, 0.0),
        (tiny_side, 0.0),
        (tiny_side, tiny_side),
        (0.0, tiny_side),
    ]);
    let tiny_center = tiny_side * 0.5;
    println!(
        "geometry.tiny_square_contains_center={}",
        tiny_square.collides_with(&Point(tiny_center, tiny_center))
    );
    let tiny_surrogate_config = SPSurrogateConfig {
        n_pole_limits: [(2, 0.0); 3],
        ff_pole_area_ratio: 0.0,
        n_ff_piers: 1,
    };
    println!(
        "geometry.tiny_square_surrogate={}",
        tiny_square
            .generate_surrogate(tiny_surrogate_config)
            .is_ok()
    );
    if let Some(surrogate) = &tiny_square.surrogate {
        let first_pier = surrogate.ff_piers().first();
        println!(
            "geometry.tiny_square_pier={}",
            first_pier.map_or_else(
                || "none".to_string(),
                |pier| format!(
                    "{:.18},{:.18},{:.18},{:.18}",
                    pier.start.0, pier.start.1, pier.end.0, pier.end.1
                )
            )
        );
    }

    let mut unused_large_pole_limit_shape =
        polygon(&[(0.0, 0.0), (2.0, 0.0), (2.0, 2.0), (0.0, 2.0)]);
    let unused_large_pole_limit_config = SPSurrogateConfig {
        n_pole_limits: [(1, 0.0), (2000, 0.0), (0, 2.0)],
        ff_pole_area_ratio: 0.0,
        n_ff_piers: 0,
    };
    let unused_large_pole_limit_result =
        unused_large_pole_limit_shape.generate_surrogate(unused_large_pole_limit_config);
    println!(
        "geometry.unused_large_pole_limit={}",
        unused_large_pole_limit_result.is_ok()
    );
    if let Some(surrogate) = &unused_large_pole_limit_shape.surrogate {
        println!(
            "geometry.unused_large_pole_limit_poles={}",
            surrogate.poles.len()
        );
    }

    let mut underflowing_coverage_shape =
        polygon(&[(0.0, 0.0), (0.5, 0.0), (0.5, 0.5), (0.0, 0.5)]);
    let underflowing_coverage_config = SPSurrogateConfig {
        n_pole_limits: [(0, 0.0); 3],
        ff_pole_area_ratio: f32::from_bits(1),
        n_ff_piers: 0,
    };
    let underflowing_coverage_result =
        underflowing_coverage_shape.generate_surrogate(underflowing_coverage_config);
    println!(
        "geometry.underflowing_coverage_target={}",
        underflowing_coverage_result.is_ok()
    );
    if let Some(surrogate) = &underflowing_coverage_shape.surrogate {
        println!(
            "geometry.underflowing_coverage_poles={}",
            surrogate.ff_poles().len()
        );
    }

    let mut pier_lattice_shape = polygon(&[(0.13, 0.27), (4.21, 0.39), (3.72, 3.19), (1.46, 2.87)]);
    let pier_lattice_config = SPSurrogateConfig {
        n_pole_limits: [(1, 0.0); 3],
        ff_pole_area_ratio: 0.0,
        n_ff_piers: 1,
    };
    let pier_lattice_result = pier_lattice_shape.generate_surrogate(pier_lattice_config);
    if let Some(pier) = pier_lattice_shape
        .surrogate
        .as_ref()
        .and_then(|surrogate| surrogate.ff_piers().first())
    {
        println!(
            "geometry.pier_lattice_case=ok:{},{},{},{}",
            pier.start.0, pier.start.1, pier.end.0, pier.end.1
        );
    } else {
        println!(
            "geometry.pier_lattice_case=error:{}",
            pier_lattice_result.is_err()
        );
    }

    let transform_a = DTransformation::new(100_000.123456789, (1.23456789, -7.65432109));
    let transform_b = DTransformation::new(-99_999.87654321, (0.333333333, 0.777777777));
    let followed_by = transform_a.compose().transform(&transform_b.compose());
    let followed_by_decomposed = DTransformation::from(&followed_by);
    let (followed_x, followed_y) = followed_by_decomposed.translation();
    println!(
        "geometry.transform_followed_by={:.9},{:.9},{:.9}",
        followed_by_decomposed.rotation(),
        followed_x,
        followed_y
    );
    let inverse = DTransformation::from(&transform_a.compose().inverse());
    let (inverse_x, inverse_y) = inverse.translation();
    println!(
        "geometry.transform_inverse={:.9},{:.9},{:.9}",
        inverse.rotation(),
        inverse_x,
        inverse_y
    );

    let wide_circle = Circle::try_new(Point(0.0, 0.0), 12_345.678).unwrap();
    println!("geometry.circle_area_f32={:.9}", wide_circle.area());
    println!(
        "circle.f32_input_boundaries=center_overflow_rejected={},radius_overflow_rejected={},negative_zero_radius_accepted={}",
        Circle::try_new(Point(f32::INFINITY, 0.0), 1.0).is_err(),
        Circle::try_new(Point(0.0, 0.0), f32::INFINITY).is_err(),
        Circle::try_new(Point(0.0, 0.0), -0.0).is_ok()
    );
    let boundary_circle = Circle::try_new(Point(16_777_216.0, 0.0), 1.0).unwrap();
    let circle_bbox = boundary_circle.bbox();
    println!(
        "geometry.circle_bbox_f32={:.0},{:.0},{:.0},{:.0}",
        circle_bbox.x_min, circle_bbox.y_min, circle_bbox.x_max, circle_bbox.y_max
    );
    println!(
        "geometry.circle_contains_f32={}",
        boundary_circle.collides_with(&Point(16_777_217.0, 0.0))
    );
    let extreme_bound = Rect::try_new(f32::MAX * 0.5, f32::MAX * 0.5, f32::MAX, f32::MAX).unwrap();
    let extreme_centroid = extreme_bound.centroid();
    println!(
        "geometry.rect_extreme_centroid={:08x},{:08x}",
        extreme_centroid.0.to_bits(),
        extreme_centroid.1.to_bits()
    );
    let extreme_quadrants = extreme_bound.quadrants();
    println!(
        "geometry.rect_extreme_q1={:08x},{:08x},{:08x},{:08x}",
        extreme_quadrants[0].x_min.to_bits(),
        extreme_quadrants[0].y_min.to_bits(),
        extreme_quadrants[0].x_max.to_bits(),
        extreme_quadrants[0].y_max.to_bits()
    );

    println!(
        "edge.nan_start_accepted={},infinite_start_accepted={}",
        Edge::try_new(Point(f32::NAN, 0.0), Point(1.0, 1.0)).is_ok(),
        Edge::try_new(Point(f32::INFINITY, 0.0), Point(1.0, 1.0)).is_ok()
    );
    let nan_bbox_edge = Edge::try_new(Point(f32::NAN, 0.0), Point(1.0, 1.0)).unwrap();
    let nan_bbox = nan_bbox_edge.bbox();
    println!(
        "edge.nan_start_bbox_bits={:08x},{:08x},{:08x},{:08x}",
        nan_bbox.x_min.to_bits(),
        nan_bbox.y_min.to_bits(),
        nan_bbox.x_max.to_bits(),
        nan_bbox.y_max.to_bits()
    );
    println!(
        "edge.f32_collapsed_endpoints_rejected={}",
        Edge::try_new(Point(1.0, 0.0), Point(1.0 + f32::EPSILON * 0.5, 0.0)).is_err()
    );

    println!(
        "rect.infinite_max_accepted={},nan_max_accepted={}",
        Rect::try_new(0.0, 0.0, f32::INFINITY, 1.0).is_ok(),
        Rect::try_new(0.0, 0.0, f32::NAN, 1.0).is_ok()
    );
    println!(
        "rect.f32_degenerate_bounds_rejected={},same_infinite_bounds_rejected={}",
        Rect::try_new(1.0, 0.0, 1.0 + f32::EPSILON * 0.5, 1.0).is_err(),
        Rect::try_new(f32::INFINITY, 0.0, f32::INFINITY, 1.0).is_err()
    );
    let hazard_bounds = Rect::try_new(0.0, 0.0, 2.0, 2.0).unwrap();
    let near_zero_bounds = Rect::try_new(-f32::EPSILON * 0.25, 0.5, 1.0, 1.5).unwrap();
    println!(
        "rect.almost_relation_fpa_near_zero_surrounds={}",
        matches!(
            hazard_bounds.almost_relation_to(near_zero_bounds),
            jagua_rs::geometry::geo_enums::GeoRelation::Surrounding
        )
    );
    let near_bounds_point = Point(-f32::EPSILON * 0.25, 1.0);
    let outside_bounds_point = Point(-f32::EPSILON * 2.0, 1.0);
    println!(
        "rect.almost_contains_points=near:{},outside:{}",
        hazard_bounds.almost_collides_with(&near_bounds_point),
        hazard_bounds.almost_collides_with(&outside_bounds_point)
    );
    let crossing_rect_edge = Edge::try_new(Point(-1.0, 1.0), Point(3.0, 1.0)).unwrap();
    let outside_rect_edge = Edge::try_new(Point(-1.0, 3.0), Point(3.0, 3.0)).unwrap();
    let endpoint_inside_rect_edge = Edge::try_new(Point(1.0, 1.0), Point(3.0, 3.0)).unwrap();
    let boundary_rect_edge = Edge::try_new(Point(-1.0, 0.0), Point(3.0, 0.0)).unwrap();
    println!(
        "rect.edge_collision=through:{},outside:{},endpoint_inside:{},boundary:{}",
        hazard_bounds.collides_with(&crossing_rect_edge),
        hazard_bounds.collides_with(&outside_rect_edge),
        hazard_bounds.collides_with(&endpoint_inside_rect_edge),
        hazard_bounds.collides_with(&boundary_rect_edge)
    );
    let nan_start_rect_edge = Edge::try_new(Point(f32::NAN, 3.0), Point(-1.0, 1.0)).unwrap();
    println!(
        "rect.edge_collision_nan_start_outside={}",
        hazard_bounds.collides_with(&nan_start_rect_edge)
    );
    let unit_circle = Circle::try_new(Point(0.0, 0.0), 1.0).unwrap();
    let tangent_circle = Circle::try_new(Point(2.0, 0.0), 1.0).unwrap();
    let separated_circle = Circle::try_new(Point(2.01, 0.0), 1.0).unwrap();
    let tangent_circle_edge = Edge::try_new(Point(-2.0, 1.0), Point(2.0, 1.0)).unwrap();
    let separated_circle_edge = Edge::try_new(Point(-2.0, 1.01), Point(2.0, 1.01)).unwrap();
    let tangent_circle_rect = Rect::try_new(1.0, 0.0, 2.0, 1.0).unwrap();
    let separated_circle_rect = Rect::try_new(1.01, 0.0, 2.0, 1.0).unwrap();
    println!(
        "circle.collision=circles:{},circles_separated:{},edge:{},edge_separated:{},rect:{},rect_separated:{}",
        unit_circle.collides_with(&tangent_circle),
        unit_circle.collides_with(&separated_circle),
        unit_circle.collides_with(&tangent_circle_edge),
        unit_circle.collides_with(&separated_circle_edge),
        unit_circle.collides_with(&tangent_circle_rect),
        unit_circle.collides_with(&separated_circle_rect)
    );
    let inside_circle_point = Point(0.25, 0.0);
    let outside_circle_point = Point(3.0, 0.0);
    let (inside_point_position, inside_point_separation) =
        unit_circle.separation_distance(&inside_circle_point);
    let (outside_point_position, outside_point_separation) =
        unit_circle.separation_distance(&outside_circle_point);
    let separated_circle = Circle::try_new(Point(3.0, 0.0), 1.0).unwrap();
    let (circle_position, circle_separation) = unit_circle.separation_distance(&separated_circle);
    let overlapping_edge = Edge::try_new(Point(-2.0, 0.25), Point(2.0, 0.25)).unwrap();
    let (edge_position, edge_separation) = unit_circle.separation_distance(&overlapping_edge);
    println!(
        "circle.distance=diameter:{:.9},point_inside:{:.9},point_outside:{:.9},point_outside_sq:{:.9},circle_outside:{:.9},circle_outside_sq:{:.9},edge_inside:{:.9},edge_inside_sq:{:.9}",
        unit_circle.diameter(),
        unit_circle.distance_to(&inside_circle_point),
        unit_circle.distance_to(&outside_circle_point),
        unit_circle.sq_distance_to(&outside_circle_point),
        unit_circle.distance_to(&separated_circle),
        unit_circle.sq_distance_to(&separated_circle),
        unit_circle.distance_to(&overlapping_edge),
        unit_circle.sq_distance_to(&overlapping_edge)
    );
    println!(
        "circle.separation=point_inside:{:?}:{:.9},point_outside:{:?}:{:.9},circle_outside:{:?}:{:.9},edge_inside:{:?}:{:.9}",
        inside_point_position,
        inside_point_separation,
        outside_point_position,
        outside_point_separation,
        circle_position,
        circle_separation,
        edge_position,
        edge_separation
    );
    let (squared_inside_point_position, squared_inside_point_separation) =
        unit_circle.sq_separation_distance(&inside_circle_point);
    let (squared_outside_point_position, squared_outside_point_separation) =
        unit_circle.sq_separation_distance(&outside_circle_point);
    let (squared_circle_position, squared_circle_separation) =
        unit_circle.sq_separation_distance(&separated_circle);
    let (squared_edge_position, squared_edge_separation) =
        unit_circle.sq_separation_distance(&overlapping_edge);
    println!(
        "circle.squared_separation=point_inside:{:?}:{:.9},point_outside:{:?}:{:.9},circle_outside:{:?}:{:.9},edge_inside:{:?}:{:.9}",
        squared_inside_point_position,
        squared_inside_point_separation,
        squared_outside_point_position,
        squared_outside_point_separation,
        squared_circle_position,
        squared_circle_separation,
        squared_edge_position,
        squared_edge_separation
    );
    let bounding_circle = Circle::bounding_circle([
        &unit_circle,
        &Circle::try_new(Point(4.0, 0.0), 2.0).unwrap(),
    ]);
    println!(
        "circle.bounding_circle={:.9},{:.9},{:.9},diameter:{:.9}",
        bounding_circle.center.0,
        bounding_circle.center.1,
        bounding_circle.radius,
        bounding_circle.diameter()
    );
    let mut transformed_circle = Circle::try_new(Point(1.0, 0.0), 1.0).unwrap();
    let circle_transform =
        jagua_rs::geometry::Transformation::from_rotation(std::f32::consts::FRAC_PI_2)
            .translate((10.0, 20.0));
    transformed_circle.transform(&circle_transform);
    println!(
        "circle.transform={:.9},{:.9},radius:{:.9}",
        transformed_circle.center.0, transformed_circle.center.1, transformed_circle.radius
    );
    let rounded_circle = Circle::try_new(Point(16_777_217.0, 0.0), 1.00000006).unwrap();
    println!(
        "circle.f32_storage={:08x},{:08x},{:08x}",
        rounded_circle.center.0.to_bits(),
        rounded_circle.center.1.to_bits(),
        rounded_circle.radius.to_bits()
    );
    let rounded_rect = Rect::try_new(1.00000006, -1.00000006, 2.00000012, 3.00000024).unwrap();
    let rounded_edge = Edge::try_new(
        Point(1.00000006, -1.00000006),
        Point(2.00000012, 3.00000024),
    )
    .unwrap();
    println!(
        "primitive.f32_storage=rect:{:08x},{:08x},{:08x},{:08x},edge:{:08x},{:08x},{:08x},{:08x}",
        rounded_rect.x_min.to_bits(),
        rounded_rect.y_min.to_bits(),
        rounded_rect.x_max.to_bits(),
        rounded_rect.y_max.to_bits(),
        rounded_edge.start.0.to_bits(),
        rounded_edge.start.1.to_bits(),
        rounded_edge.end.0.to_bits(),
        rounded_edge.end.1.to_bits()
    );
    let smallest_subnormal = f32::from_bits(1);
    let subnormal_edge = Edge::try_new(
        Point(smallest_subnormal, 0.0),
        Point(smallest_subnormal, 1.0),
    )
    .unwrap();
    let first_subnormal_circle = Circle::try_new(Point(smallest_subnormal, 0.0), 0.0).unwrap();
    let second_subnormal_circle = Circle::try_new(Point(smallest_subnormal, 1.0), 0.0).unwrap();
    let subnormal_bounding_circle =
        Circle::bounding_circle([&first_subnormal_circle, &second_subnormal_circle]);
    println!(
        "primitive.subnormal_midpoint=edge:{:08x},circle:{:08x}",
        subnormal_edge.centroid().0.to_bits(),
        subnormal_bounding_circle.center.0.to_bits()
    );
    let edge_ops = Edge::try_new(Point(0.0, 0.0), Point(3.0, 4.0)).unwrap();
    let front_extended = edge_ops.extend_at_front(2.0);
    let back_extended = edge_ops.extend_at_back(2.0);
    let scaled_edge = edge_ops.scale(2.0);
    let reversed_edge = edge_ops.reverse();
    println!(
        "edge.operations=minmax:{:.1},{:.1},{:.1},{:.1};length:{:.1};centroid:{:.1},{:.1};front:{:.1},{:.1};back:{:.1},{:.1};scaled:{:.1},{:.1},{:.1},{:.1};reversed:{:.1},{:.1},{:.1},{:.1}",
        edge_ops.x_min(),
        edge_ops.y_min(),
        edge_ops.x_max(),
        edge_ops.y_max(),
        edge_ops.length(),
        edge_ops.centroid().0,
        edge_ops.centroid().1,
        front_extended.start.0,
        front_extended.start.1,
        back_extended.end.0,
        back_extended.end.1,
        scaled_edge.start.0,
        scaled_edge.start.1,
        scaled_edge.end.0,
        scaled_edge.end.1,
        reversed_edge.start.0,
        reversed_edge.start.1,
        reversed_edge.end.0,
        reversed_edge.end.1
    );
    let crossing_edge_a = Edge::try_new(Point(0.0, 0.0), Point(10.0, 10.0)).unwrap();
    let crossing_edge_b = Edge::try_new(Point(0.0, 10.0), Point(10.0, 0.0)).unwrap();
    let endpoint_edge = Edge::try_new(Point(0.0, 0.0), Point(2.0, 0.0)).unwrap();
    let touching_edge = Edge::try_new(Point(2.0, 0.0), Point(2.0, 1.0)).unwrap();
    let collinear_edge = Edge::try_new(Point(1.0, 0.0), Point(3.0, 0.0)).unwrap();
    println!(
        "edge.intersection=proper:{:.1},{:.1};endpoint:{:.1},{:.1};collinear:{}",
        crossing_edge_a.collides_at(&crossing_edge_b).unwrap().0,
        crossing_edge_a.collides_at(&crossing_edge_b).unwrap().1,
        endpoint_edge.collides_at(&touching_edge).unwrap().0,
        endpoint_edge.collides_at(&touching_edge).unwrap().1,
        collinear_edge.collides_at(&endpoint_edge).is_some()
    );
    let mut transformed_edge = edge_ops;
    transformed_edge.transform(&circle_transform);
    println!(
        "edge.transform={:.1},{:.1},{:.1},{:.1}",
        transformed_edge.start.0,
        transformed_edge.start.1,
        transformed_edge.end.0,
        transformed_edge.end.1
    );
    let utility_rect = Rect::try_new(0.0, 0.0, 4.0, 2.0).unwrap();
    let diagonal_rect = Rect::from_diagonal_corners(Point(4.0, 5.0), Point(-2.0, -1.0)).unwrap();
    let square_rect = utility_rect.inflate_to_square();
    let resized_rect = utility_rect.resize_by(-0.5, -0.5).unwrap();
    let scaled_rect = utility_rect.scale(1.5);
    let overlap_rect = Rect::try_new(3.0, 1.0, 5.0, 4.0).unwrap();
    let intersection_rect = Rect::intersection(utility_rect, overlap_rect).unwrap();
    let bounding_rect = Rect::bounding_rect(utility_rect, overlap_rect);
    let inside_rect_point = Point(1.0, 1.0);
    let outside_rect_point = Point(-3.0, 4.0);
    let (inside_rect_position, inside_rect_separation) =
        utility_rect.separation_distance(&inside_rect_point);
    let (outside_rect_position, outside_rect_separation) =
        utility_rect.separation_distance(&outside_rect_point);
    let (inside_rect_sq_position, inside_rect_sq_separation) =
        utility_rect.sq_separation_distance(&inside_rect_point);
    let (outside_rect_sq_position, outside_rect_sq_separation) =
        utility_rect.sq_separation_distance(&outside_rect_point);
    println!(
        "rect.operations=diagonal:{:.1},{:.1},{:.1},{:.1};square:{:.1},{:.1},{:.1},{:.1};resize:{:.1},{:.1},{:.1},{:.1};scale:{:.1},{:.1},{:.1},{:.1};measure:{:.1},{:.1},{:.1},{:.9}",
        diagonal_rect.x_min,
        diagonal_rect.y_min,
        diagonal_rect.x_max,
        diagonal_rect.y_max,
        square_rect.x_min,
        square_rect.y_min,
        square_rect.x_max,
        square_rect.y_max,
        resized_rect.x_min,
        resized_rect.y_min,
        resized_rect.x_max,
        resized_rect.y_max,
        scaled_rect.x_min,
        scaled_rect.y_min,
        scaled_rect.x_max,
        scaled_rect.y_max,
        utility_rect.width(),
        utility_rect.height(),
        utility_rect.area(),
        utility_rect.diameter()
    );
    println!(
        "rect.combination=intersection:{:.1},{:.1},{:.1},{:.1};bounding:{:.1},{:.1},{:.1},{:.1};corners:{:.1},{:.1};{:.1},{:.1};{:.1},{:.1};{:.1},{:.1}",
        intersection_rect.x_min,
        intersection_rect.y_min,
        intersection_rect.x_max,
        intersection_rect.y_max,
        bounding_rect.x_min,
        bounding_rect.y_min,
        bounding_rect.x_max,
        bounding_rect.y_max,
        utility_rect.corners()[0].0,
        utility_rect.corners()[0].1,
        utility_rect.corners()[1].0,
        utility_rect.corners()[1].1,
        utility_rect.corners()[2].0,
        utility_rect.corners()[2].1,
        utility_rect.corners()[3].0,
        utility_rect.corners()[3].1
    );
    let utility_edges = utility_rect.edges();
    println!(
        "rect.edges={:.1},{:.1}>{:.1},{:.1};{:.1},{:.1}>{:.1},{:.1};{:.1},{:.1}>{:.1},{:.1};{:.1},{:.1}>{:.1},{:.1}",
        utility_edges[0].start.0,
        utility_edges[0].start.1,
        utility_edges[0].end.0,
        utility_edges[0].end.1,
        utility_edges[1].start.0,
        utility_edges[1].start.1,
        utility_edges[1].end.0,
        utility_edges[1].end.1,
        utility_edges[2].start.0,
        utility_edges[2].start.1,
        utility_edges[2].end.0,
        utility_edges[2].end.1,
        utility_edges[3].start.0,
        utility_edges[3].start.1,
        utility_edges[3].end.0,
        utility_edges[3].end.1
    );
    println!(
        "rect.distance=inside:{:.1},outside:{:.9},outside_sq:{:.1};separation:inside:{:?}:{:.1},outside:{:?}:{:.9};squared:inside:{:?}:{:.1},outside:{:?}:{:.1};invalid_resize:{}",
        utility_rect.distance_to(&inside_rect_point),
        utility_rect.distance_to(&outside_rect_point),
        utility_rect.sq_distance_to(&outside_rect_point),
        inside_rect_position,
        inside_rect_separation,
        outside_rect_position,
        outside_rect_separation,
        inside_rect_sq_position,
        inside_rect_sq_separation,
        outside_rect_sq_position,
        outside_rect_sq_separation,
        utility_rect.resize_by(-2.0, 0.0).is_none()
    );
    let point_a = Point(1.25, -2.5);
    let point_b = Point(4.5, 2.5);
    let point_rounding_a = Point(16_777_216.0, 0.0);
    let point_rounding_b = Point(16_777_217.0, 0.0);
    println!(
        "point.distance=normal:{:.9},normal_sq:{:.9},f32_rounded:{:.1}",
        point_a.distance_to(&point_b),
        point_a.sq_distance_to(&point_b),
        point_rounding_a.distance_to(&point_rounding_b)
    );
    let distance_polygon = polygon(&[(0.0, 0.0), (4.0, 0.0), (4.0, 2.0), (0.0, 2.0)]);
    let polygon_inside_point = Point(1.0, 1.0);
    let polygon_outside_point = Point(-3.0, 4.0);
    let (polygon_inside_position, polygon_inside_separation) =
        distance_polygon.separation_distance(&polygon_inside_point);
    let (polygon_outside_position, polygon_outside_separation) =
        distance_polygon.separation_distance(&polygon_outside_point);
    let (polygon_inside_sq_position, polygon_inside_sq_separation) =
        distance_polygon.sq_separation_distance(&polygon_inside_point);
    let (polygon_outside_sq_position, polygon_outside_sq_separation) =
        distance_polygon.sq_separation_distance(&polygon_outside_point);
    println!(
        "polygon.distance=inside:{:.1},inside_sq:{:.1},outside:{:.9},outside_sq:{:.1};separation:inside:{:?}:{:.1},outside:{:?}:{:.9};squared:inside:{:?}:{:.1},outside:{:?}:{:.1}",
        distance_polygon.distance_to(&polygon_inside_point),
        distance_polygon.sq_distance_to(&polygon_inside_point),
        distance_polygon.distance_to(&polygon_outside_point),
        distance_polygon.sq_distance_to(&polygon_outside_point),
        polygon_inside_position,
        polygon_inside_separation,
        polygon_outside_position,
        polygon_outside_separation,
        polygon_inside_sq_position,
        polygon_inside_sq_separation,
        polygon_outside_sq_position,
        polygon_outside_sq_separation
    );
    let enclosed_bounds = Rect::try_new(-1.0, -1.0, 3.0, 3.0).unwrap();
    let intersecting_bounds = Rect::try_new(1.5, 1.5, 3.0, 3.0).unwrap();
    let disjoint_bounds = Rect::try_new(3.0, 0.0, 4.0, 1.0).unwrap();
    println!(
        "rect.almost_relation_cases=surrounding:{},enclosed:{},intersecting:{},disjoint:{}",
        matches!(
            hazard_bounds.almost_relation_to(near_zero_bounds),
            jagua_rs::geometry::geo_enums::GeoRelation::Surrounding
        ),
        matches!(
            hazard_bounds.almost_relation_to(enclosed_bounds),
            jagua_rs::geometry::geo_enums::GeoRelation::Enclosed
        ),
        matches!(
            hazard_bounds.almost_relation_to(intersecting_bounds),
            jagua_rs::geometry::geo_enums::GeoRelation::Intersecting
        ),
        matches!(
            hazard_bounds.almost_relation_to(disjoint_bounds),
            jagua_rs::geometry::geo_enums::GeoRelation::Disjoint
        )
    );

    let zero_radius_circle = Circle::try_new(Point(0.0, 0.0), 0.0).unwrap();
    println!(
        "circle.zero_radius_valid={},area={:.0}",
        zero_radius_circle.radius >= 0.0,
        zero_radius_circle.area()
    );

    let fit_width = 3.25f32 * 1.00001f32 + 0.1f32;
    println!("strip.fit_width={fit_width:.12}");

    let square_bounds = Rect::try_new(-16_777_216.0, 0.0, 1.0, 2.0)
        .unwrap()
        .inflate_to_square();
    println!(
        "rect.square_bounds={:.0},{:.0},{:.0},{:.0}",
        square_bounds.x_min, square_bounds.y_min, square_bounds.x_max, square_bounds.y_max
    );

    let density_item_area = [16_777_216.0f32, 1.0, 1.0].iter().copied().sum::<f32>();
    println!("density.f32_items_area={density_item_area:.0}");
    println!(
        "density.f32_ratio={:.1}",
        density_item_area / 33_554_432.0f32
    );

    let empty_bpp = BPProblem::new(BPInstance::new(vec![], vec![]));
    println!(
        "density.empty_bpp_problem_nan={}",
        empty_bpp.density().is_nan()
    );

    let float_id_item_json = r#"{"id":1.0,"orientation":{"rotation":{"mode":"continuous"}},"shape":{"type":"simple_polygon","data":[[0,0],[1,0],[0,1]]}}"#;
    let negative_zero_id_item_json = r#"{"id":-0,"orientation":{"rotation":{"mode":"continuous"}},"shape":{"type":"simple_polygon","data":[[0,0],[1,0],[0,1]]}}"#;
    let float_quality_item_json = r#"{"id":1,"orientation":{"rotation":{"mode":"continuous"}},"shape":{"type":"simple_polygon","data":[[0,0],[1,0],[0,1]]},"min_quality":1.0}"#;
    println!(
        "json.float_id_rejected={}",
        serde_json::from_str::<ExtItem>(float_id_item_json).is_err()
    );
    println!(
        "json.negative_zero_id_accepted={}",
        serde_json::from_str::<ExtItem>(negative_zero_id_item_json).is_ok()
    );
    println!(
        "json.float_min_quality_rejected={}",
        serde_json::from_str::<ExtItem>(float_quality_item_json).is_err()
    );
    let duplicate_unknown_item_json = r#"{"id":1,"dxf":"first.dxf","dxf":"second.dxf","orientation":{"rotation":{"mode":"continuous"}},"shape":{"type":"simple_polygon","data":[[0,0],[1,0],[0,1]]}}"#;
    let duplicate_id_item_json = r#"{"id":1,"id":2,"orientation":{"rotation":{"mode":"continuous"}},"shape":{"type":"simple_polygon","data":[[0,0],[1,0],[0,1]]}}"#;
    let duplicate_rotation_mode_item_json = r#"{"id":1,"orientation":{"rotation":{"mode":"continuous","mode":"continuous"}},"shape":{"type":"simple_polygon","data":[[0,0],[1,0],[0,1]]}}"#;
    println!(
        "json.duplicate_unknown_metadata_accepted={}",
        serde_json::from_str::<ExtItem>(duplicate_unknown_item_json).is_ok()
    );
    println!(
        "json.duplicate_id_rejected={}",
        serde_json::from_str::<ExtItem>(duplicate_id_item_json).is_err()
    );
    println!(
        "json.duplicate_rotation_mode_rejected={}",
        serde_json::from_str::<ExtItem>(duplicate_rotation_mode_item_json).is_err()
    );
    let f32_angle_item_json = r#"{"id":1,"orientation":{"rotation":{"mode":"discrete","angles":[123.456789]}},"shape":{"type":"simple_polygon","data":[[0,0],[1,0],[0,1]]}}"#;
    let parsed_f32_angle_item = serde_json::from_str::<ExtItem>(f32_angle_item_json).unwrap();
    if let ExtRotation::Discrete { angles } = parsed_f32_angle_item.orientation.rotation {
        println!("json.angle_f32_bits={:08x}", angles[0].to_bits());
    }

    let cde_config = CDEConfig {
        quadtree_depth: 1,
        cd_threshold: 8,
        item_surrogate_config: SPSurrogateConfig::none(),
    };
    let empty_quality_zone = InferiorQualityZone::new(1, vec![]).unwrap();
    let empty_quality_zone_container = Container::new(
        OriginalShape {
            shape: polygon(&[(0.0, 0.0), (4.0, 0.0), (4.0, 4.0), (0.0, 4.0)]),
            pre_transform: DTransformation::empty(),
            modify_mode: ShapeModifyMode::Deflate,
            modify_config: ShapeModifyConfig::default(),
        },
        vec![empty_quality_zone],
        cde_config,
    );
    println!(
        "container.empty_quality_zone_accepted={}",
        empty_quality_zone_container.is_ok()
    );
    let min_quality_zone = InferiorQualityZone::new(
        1,
        vec![OriginalShape {
            shape: polygon(&[(0.0, 0.0), (4.0, 0.0), (4.0, 4.0), (0.0, 4.0)]),
            pre_transform: DTransformation::empty(),
            modify_mode: ShapeModifyMode::Inflate,
            modify_config: ShapeModifyConfig::default(),
        }],
    )
    .unwrap();
    let min_quality_container = Container::new(
        OriginalShape {
            shape: polygon(&[(0.0, 0.0), (10.0, 0.0), (10.0, 10.0), (0.0, 10.0)]),
            pre_transform: DTransformation::empty(),
            modify_mode: ShapeModifyMode::Deflate,
            modify_config: ShapeModifyConfig::default(),
        },
        vec![min_quality_zone],
        cde_config,
    )
    .unwrap();
    let min_quality_item = Arc::new(
        Item::new(
            0,
            6300,
            OriginalShape {
                shape: polygon(&[(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)]),
                pre_transform: DTransformation::empty(),
                modify_mode: ShapeModifyMode::Inflate,
                modify_config: ShapeModifyConfig::default(),
            },
            RotationRange::None,
            Some(2),
            SPSurrogateConfig::none(),
        )
        .unwrap(),
    );
    let mut min_quality_layout = Layout::new(min_quality_container);
    min_quality_layout.place_item(&min_quality_item, DTransformation::new(0.0, (0.5, 0.5)));
    println!(
        "layout.inferior_quality_min_feasible={}",
        min_quality_layout.is_feasible()
    );
    let empty_discrete_rotation_item = Item::new(
        0,
        6200,
        OriginalShape {
            shape: polygon(&[(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)]),
            pre_transform: DTransformation::empty(),
            modify_mode: ShapeModifyMode::Inflate,
            modify_config: ShapeModifyConfig::default(),
        },
        RotationRange::Discrete(vec![]),
        None,
        SPSurrogateConfig::none(),
    );
    println!(
        "item.empty_discrete_rotation_accepted={}",
        empty_discrete_rotation_item.is_ok()
    );
    let rounded_rotation_item = Item::new(
        1,
        6201,
        OriginalShape {
            shape: polygon(&[(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)]),
            pre_transform: DTransformation::empty(),
            modify_mode: ShapeModifyMode::Inflate,
            modify_config: ShapeModifyConfig::default(),
        },
        RotationRange::Discrete(vec![1.00000006_f32]),
        None,
        SPSurrogateConfig::none(),
    )
    .unwrap();
    let rotation_bits = match &rounded_rotation_item.allowed_rotation {
        RotationRange::Discrete(angles) => angles[0].to_bits(),
        _ => unreachable!(),
    };
    println!("item.rotation_f32_bits={rotation_bits:08x}");
    let rotation_importer = Importer::new(cde_config, None, None);
    let polygon_quality_zone = ExtContainer {
        id: 6300,
        shape: ExtShape::Rectangle {
            x_min: 0.0,
            y_min: 0.0,
            width: 10.0,
            height: 10.0,
        },
        zones: vec![ExtQualityZone {
            quality: 1,
            shape: ExtShape::Polygon(ExtPolygon {
                outer: ExtSPolygon(vec![(2.0, 2.0), (4.0, 2.0), (4.0, 4.0), (2.0, 4.0)]),
                inner: vec![],
            }),
        }],
    };
    let empty_bpp_instance = BPInstance::new(vec![], vec![]);
    let empty_bpp_external = jagua_rs::probs::bpp::io::ext_repr::ExtBPInstance {
        name: String::new(),
        min_item_separation: 0.0,
        items: vec![],
        bins: vec![],
    };
    let empty_mspp_strip =
        Strip::new(1.0, 1.0, cde_config, ShapeModifyConfig::default(), 1.0).unwrap();
    let empty_mspp_instance = MSPInstance::new(vec![], empty_mspp_strip);
    let distinct_width_mspp_strip =
        Strip::new(10.0, 5.0, cde_config, ShapeModifyConfig::default(), 4.0).unwrap();
    let distinct_width_mspp_instance = MSPInstance::new(vec![], distinct_width_mspp_strip);
    println!(
        "mspp.base_strip_width={:.0},max={:.0}",
        distinct_width_mspp_instance.base_strip.width,
        distinct_width_mspp_instance.base_strip.max_width
    );
    let empty_mspp_external = jagua_rs::probs::mspp::io::ext_repr::ExtMSPInstance {
        name: String::new(),
        min_item_separation: 0.0,
        items: vec![],
        strips: jagua_rs::probs::mspp::io::ext_repr::ExtMSPStrip {
            height: 1.0,
            max_width: 1.0,
        },
    };
    let previous_panic_hook = std::panic::take_hook();
    std::panic::set_hook(Box::new(|_| {}));
    let polygon_quality_zone_panics =
        std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
            rotation_importer.import_container(&polygon_quality_zone)
        }))
        .is_err();
    let bpp_solution_import_panics = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        jagua_rs::probs::bpp::io::import_solution(&empty_bpp_instance, &empty_bpp_external)
    }))
    .is_err();
    let mspp_solution_import_panics =
        std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
            jagua_rs::probs::mspp::io::import_solution(&empty_mspp_instance, &empty_mspp_external)
        }))
        .is_err();
    std::panic::set_hook(previous_panic_hook);
    println!("import.polygon_quality_zone_unimplemented={polygon_quality_zone_panics}");
    println!("import.bpp_solution_unimplemented={bpp_solution_import_panics}");
    println!("import.mspp_solution_unimplemented={mspp_solution_import_panics}");

    let svg_container = Container::new(
        OriginalShape {
            shape: polygon(&[(0.0, 0.0), (100.0, 0.0), (100.0, 100.0), (0.0, 100.0)]),
            pre_transform: DTransformation::empty(),
            modify_mode: ShapeModifyMode::Deflate,
            modify_config: ShapeModifyConfig::default(),
        },
        vec![],
        CDEConfig {
            quadtree_depth: 1,
            cd_threshold: 8,
            item_surrogate_config: SPSurrogateConfig::none(),
        },
    )
    .unwrap();
    let svg_collision_item = Arc::new(
        Item::new(
            0,
            9001,
            OriginalShape {
                shape: polygon(&[(-5.0, -10.0), (5.0, -10.0), (5.0, 10.0), (-5.0, 10.0)]),
                pre_transform: DTransformation::empty(),
                modify_mode: ShapeModifyMode::Inflate,
                modify_config: ShapeModifyConfig::default(),
            },
            RotationRange::None,
            None,
            SPSurrogateConfig::none(),
        )
        .unwrap(),
    );
    let mut svg_collision_layout = Layout::new(svg_container);
    svg_collision_layout.place_item(&svg_collision_item, DTransformation::new(0.0, (45.0, 50.0)));
    svg_collision_layout.place_item(&svg_collision_item, DTransformation::new(0.0, (53.0, 50.0)));
    let collision_svg =
        layout_to_svg(&svg_collision_layout, SvgDrawOptions::default(), "oracle").to_string();
    println!(
        "svg.collision_lines_present={}",
        collision_svg.contains("id=\"collision_lines\"")
    );
    println!(
        "svg.collision_highlight_present={}",
        collision_svg.contains("collision_highlight_color") || collision_svg.contains("#00FF00")
    );
    println!(
        "svg.collision_line_endpoints_match={}",
        collision_svg.contains("M45,50 L53,50") || collision_svg.contains("M53,50 L45,50")
    );
    if let Some(collision_line) = collision_svg.lines().find(|line| line.contains("#00FF00")) {
        println!("svg.collision_line={collision_line}");
    }

    let ordered_container = rotation_importer
        .import_container(&ExtContainer {
            id: 6205,
            shape: ExtShape::Polygon(ExtPolygon {
                outer: ExtSPolygon(vec![(0.0, 0.0), (10.0, 0.0), (10.0, 10.0), (0.0, 10.0)]),
                inner: vec![ExtSPolygon(vec![
                    (4.0, 4.0),
                    (6.0, 4.0),
                    (6.0, 6.0),
                    (4.0, 6.0),
                ])],
            }),
            zones: vec![ExtQualityZone {
                quality: 0,
                shape: ExtShape::SimplePolygon(ExtSPolygon(vec![
                    (7.0, 7.0),
                    (9.0, 7.0),
                    (9.0, 9.0),
                    (7.0, 9.0),
                ])),
            }],
        })
        .unwrap();
    let zone_probe = polygon(&[(7.2, 7.2), (7.8, 7.2), (7.8, 7.8), (7.2, 7.8)]);
    let mut zone_collector = BasicHazardCollector::new();
    ordered_container
        .base_cde
        .collect_poly_collisions(&zone_probe, &mut zone_collector);
    let imported_zone_uses_first_hole_index = zone_collector
        .iter()
        .any(|(_, entity)| matches!(entity, HazardEntity::Hole { idx: 0 }));
    println!("import.q0_zone_hazard_index_zero={imported_zone_uses_first_hole_index}");
    let precise_angle = 123.456789_f32;
    let precise_angle_item = rotation_importer
        .import_item(
            &ExtItem {
                id: 6204,
                orientation: ExtOrientation {
                    rotation: ExtRotation::Discrete {
                        angles: vec![precise_angle],
                    },
                },
                shape: ExtShape::Rectangle {
                    x_min: 0.0,
                    y_min: 0.0,
                    width: 1.0,
                    height: 1.0,
                },
                min_quality: None,
            },
            4,
        )
        .unwrap();
    let precise_rotation_bits = match &precise_angle_item.allowed_rotation {
        RotationRange::Discrete(angles) => angles[0].to_bits(),
        _ => unreachable!(),
    };
    println!("import.rotation_noninteger_f32_bits={precise_rotation_bits:08x}");
    let rounded_360 = 360.00000001_f32;
    let discrete_boundary_item = rotation_importer
        .import_item(
            &ExtItem {
                id: 6202,
                orientation: ExtOrientation {
                    rotation: ExtRotation::Discrete {
                        angles: vec![rounded_360],
                    },
                },
                shape: ExtShape::Rectangle {
                    x_min: 0.0,
                    y_min: 0.0,
                    width: 1.0,
                    height: 1.0,
                },
                min_quality: None,
            },
            2,
        )
        .unwrap();
    let stepped_boundary_item = rotation_importer
        .import_item(
            &ExtItem {
                id: 6203,
                orientation: ExtOrientation {
                    rotation: ExtRotation::Stepped { step: rounded_360 },
                },
                shape: ExtShape::Rectangle {
                    x_min: 0.0,
                    y_min: 0.0,
                    width: 1.0,
                    height: 1.0,
                },
                min_quality: None,
            },
            3,
        )
        .unwrap();
    println!(
        "import.rotation_f32_rounded_360=discrete_none:{},stepped_none:{}",
        matches!(discrete_boundary_item.allowed_rotation, RotationRange::None),
        matches!(stepped_boundary_item.allowed_rotation, RotationRange::None)
    );
    let wide_rotation_external = jagua_rs::probs::spp::io::ext_repr::ExtSPInstance {
        name: "wide-rotation".to_string(),
        min_item_separation: 0.0,
        items: vec![jagua_rs::probs::spp::io::ext_repr::ExtItem {
            base: ExtItem {
                id: 6206,
                orientation: ExtOrientation {
                    rotation: ExtRotation::Continuous {},
                },
                shape: ExtShape::Rectangle {
                    x_min: 0.0,
                    y_min: 0.0,
                    width: 1.0,
                    height: 1.0,
                },
                min_quality: None,
            },
            demand: 1,
        }],
        strip_height: 10.0,
    };
    let wide_rotation_instance =
        jagua_rs::probs::spp::io::import_instance(&rotation_importer, &wide_rotation_external)
            .unwrap();
    let wide_rotation_solution = jagua_rs::probs::spp::io::import_solution(
        &wide_rotation_instance,
        &jagua_rs::probs::spp::io::ext_repr::ExtSPSolution {
            strip_width: 10.0,
            layout: ExtLayout {
                container_id: 0,
                placed_items: vec![ExtPlacedItem {
                    item_id: 6206,
                    transformation: ExtTransformation {
                        rotation: 869_592.625,
                        translation: (5.0, 5.0),
                    },
                }],
                density: 0.0,
            },
            density: 0.0,
            run_time_sec: 0,
        },
    )
    .unwrap();
    let wide_rotation_bits = wide_rotation_solution
        .layout_snapshot
        .placed_items
        .values()
        .next()
        .unwrap()
        .d_transf
        .rotation()
        .to_bits();
    println!("import.external_solution_rotation_bits={wide_rotation_bits:08x}");
    let wide_rotation_placement = wide_rotation_solution
        .layout_snapshot
        .placed_items
        .values()
        .next()
        .unwrap();
    let wide_rotation_external_transform = int_to_ext_transformation(
        &wide_rotation_placement.d_transf,
        &wide_rotation_placement.item.shape_orig.pre_transform,
    );
    println!(
        "export.external_solution_rotation_bits={:08x}",
        wide_rotation_external_transform
            .rotation()
            .to_degrees()
            .to_bits()
    );
    let roundtrip_probe_solution = jagua_rs::probs::spp::io::import_solution(
        &wide_rotation_instance,
        &jagua_rs::probs::spp::io::ext_repr::ExtSPSolution {
            strip_width: 10.0,
            layout: ExtLayout {
                container_id: 0,
                placed_items: vec![ExtPlacedItem {
                    item_id: 6206,
                    transformation: ExtTransformation {
                        rotation: f32::from_bits(0xc1fa_7be4),
                        translation: (5.0, 5.0),
                    },
                }],
                density: 0.0,
            },
            density: 0.0,
            run_time_sec: 0,
        },
    )
    .unwrap();
    let roundtrip_probe_placement = roundtrip_probe_solution
        .layout_snapshot
        .placed_items
        .values()
        .next()
        .unwrap();
    println!(
        "import.roundtrip_probe_internal_rotation_bits={:08x}",
        roundtrip_probe_placement.d_transf.rotation().to_bits()
    );
    let internal_translation = roundtrip_probe_placement.d_transf.translation();
    println!(
        "import.roundtrip_probe_internal_translation_bits={:08x},{:08x}",
        internal_translation.0.to_bits(),
        internal_translation.1.to_bits()
    );
    let roundtrip_probe_external_transform = int_to_ext_transformation(
        &roundtrip_probe_placement.d_transf,
        &roundtrip_probe_placement.item.shape_orig.pre_transform,
    );
    let external_translation = roundtrip_probe_external_transform.translation();
    println!(
        "export.roundtrip_probe_translation_bits={:08x},{:08x}",
        external_translation.0.to_bits(),
        external_translation.1.to_bits()
    );
    println!(
        "export.roundtrip_probe_rotation_bits={:08x}",
        roundtrip_probe_external_transform
            .rotation()
            .to_degrees()
            .to_bits()
    );
    let collapsed_rectangle = rotation_importer.import_item(
        &ExtItem {
            id: 6205,
            orientation: ExtOrientation {
                rotation: ExtRotation::Continuous {},
            },
            shape: ExtShape::Rectangle {
                x_min: 16_777_216.5_f32,
                y_min: 0.0,
                width: 0.6_f32,
                height: 2.0,
            },
            min_quality: None,
        },
        5,
    );
    println!(
        "import.rect_f32_endpoints_collapsed_rejected={}",
        collapsed_rectangle.is_err()
    );
    let rounded_bounds_max = 1.00000006_f32;
    let rounded_bounds = Rect::try_new(0.0, 0.0, rounded_bounds_max, 1.0).unwrap();
    println!(
        "collision.f32_bounds_max={:08x}",
        rounded_bounds.x_max.to_bits()
    );
    let collision_root = Rect::try_new(0.0, 0.0, 10.0, 10.0).unwrap();
    let exterior_shape = Arc::new(polygon(&[
        (0.0, 0.0),
        (10.0, 0.0),
        (10.0, 10.0),
        (0.0, 10.0),
    ]));
    let collision_engine = CDEngine::new(
        collision_root,
        vec![Hazard::new(HazardEntity::Exterior, exterior_shape, false)],
        cde_config,
    );
    let exterior_key = collision_engine
        .hazards_map
        .iter()
        .find(|(_, hazard)| matches!(hazard.entity, HazardEntity::Exterior))
        .map(|(key, _)| key)
        .unwrap();
    let ignore_exterior = HazKeyFilter::from_keys([exterior_key]);
    let partly_outside = polygon(&[(-1.0, 1.0), (1.0, 1.0), (1.0, 2.0), (-1.0, 2.0)]);
    println!(
        "collision.out_of_bounds_ignores_exterior_filter={}",
        collision_engine.detect_poly_collision(&partly_outside, &ignore_exterior)
    );
    let depth_zero_collision_config = CDEConfig {
        quadtree_depth: 0,
        cd_threshold: 8,
        item_surrogate_config: SPSurrogateConfig::none(),
    };
    let depth_zero_engine = CDEngine::new(
        Rect::try_new(0.0, 0.0, 10.0, 10.0).unwrap(),
        vec![Hazard::new(
            HazardEntity::Exterior,
            Arc::new(polygon(&[(2.0, 2.0), (8.0, 2.0), (8.0, 8.0), (2.0, 8.0)])),
            false,
        )],
        depth_zero_collision_config,
    );
    let query_containing_exterior = polygon(&[(1.0, 1.0), (9.0, 1.0), (9.0, 9.0), (1.0, 9.0)]);
    println!(
        "collision.query_contains_exterior={}",
        depth_zero_engine.detect_poly_collision(&query_containing_exterior, &NoFilter)
    );
    let depth_four_engine = CDEngine::new(
        Rect::try_new(0.0, 0.0, 10.0, 10.0).unwrap(),
        vec![Hazard::new(
            HazardEntity::Exterior,
            Arc::new(polygon(&[(2.0, 2.0), (8.0, 2.0), (8.0, 8.0), (2.0, 8.0)])),
            false,
        )],
        CDEConfig {
            quadtree_depth: 4,
            cd_threshold: 8,
            item_surrogate_config: SPSurrogateConfig::none(),
        },
    );
    let root_enclosing_query = polygon(&[(0.0, 0.0), (10.0, 0.0), (10.0, 10.0), (0.0, 10.0)]);
    println!(
        "collision.root_contains_exterior={}",
        depth_four_engine.detect_poly_collision(&root_enclosing_query, &NoFilter)
    );
    println!(
        "collision.depth_four_query_contains_exterior={}",
        depth_four_engine.detect_poly_collision(&query_containing_exterior, &NoFilter)
    );
    let empty_spp = SPInstance::new(
        vec![],
        SPStrip::new(4.0, cde_config, ShapeModifyConfig::default(), 1.0).unwrap(),
    );
    println!("spp.empty_instance_items={}", empty_spp.items.len());
    let zero_demand_spp = SPInstance::new(
        vec![(square_item(0, 1.0), 0)],
        SPStrip::new(4.0, cde_config, ShapeModifyConfig::default(), 1.0).unwrap(),
    );
    println!(
        "spp.zero_demand_item_area={:.0}",
        zero_demand_spp.item_area()
    );
    println!(
        "spp.item_idx_zero_demand={:?},unknown={:?}",
        zero_demand_spp.item_idx(1000),
        zero_demand_spp.item_idx(9999)
    );
    println!(
        "spp.positive_infinite_initial_width_accepted={}",
        SPStrip::new(5.0, cde_config, ShapeModifyConfig::default(), f32::INFINITY,).is_ok()
    );
    let mut positive_infinite_spp_strip =
        SPStrip::new(5.0, cde_config, ShapeModifyConfig::default(), 10.0).unwrap();
    positive_infinite_spp_strip.set_width(f32::INFINITY);
    println!("spp.positive_infinite_width_accepted=true");

    let empty_bpp_instance = BPInstance::new(vec![], vec![]);
    println!(
        "bpp.empty_instance_items={},bins={}",
        empty_bpp_instance.items.len(),
        empty_bpp_instance.bins.len()
    );
    let zero_demand_bpp = BPInstance::new(vec![(square_item(0, 1.0), 0)], vec![]);
    println!(
        "bpp.item_idx_zero_demand={:?},unknown={:?}",
        zero_demand_bpp.item_idx(1000),
        zero_demand_bpp.item_idx(9999)
    );
    let duplicate_bin_container = Container::new(
        OriginalShape {
            shape: polygon(&[(0.0, 0.0), (4.0, 0.0), (4.0, 4.0), (0.0, 4.0)]),
            pre_transform: DTransformation::empty(),
            modify_mode: ShapeModifyMode::Deflate,
            modify_config: ShapeModifyConfig::default(),
        },
        vec![],
        cde_config,
    )
    .unwrap();
    let duplicate_external_bins = vec![
        Bin::new(0, 5200, duplicate_bin_container.clone(), 1, 1),
        Bin::new(1, 5200, duplicate_bin_container, 1, 2),
    ];
    let duplicate_bin_instance = BPInstance::new(vec![], duplicate_external_bins);
    println!(
        "bpp.duplicate_external_bin_ids={}",
        duplicate_bin_instance.bins.len()
    );
    let make_bin_container = |width: f32, height: f32| {
        Container::new(
            OriginalShape {
                shape: polygon(&[(0.0, 0.0), (width, 0.0), (width, height), (0.0, height)]),
                pre_transform: DTransformation::empty(),
                modify_mode: ShapeModifyMode::Deflate,
                modify_config: ShapeModifyConfig::default(),
            },
            vec![],
            cde_config,
        )
        .unwrap()
    };
    let item_list_first = square_item(5, 1.0);
    let item_list_second = square_item(6, 1.0);
    let mut duplicate_item_index = (*item_list_first).clone();
    duplicate_item_index.external_id = 1505;
    let duplicate_item_index = Arc::new(duplicate_item_index);
    let mut distinct_items_layout = Layout::new(make_bin_container(30.0, 10.0));
    let item_list_first_key =
        distinct_items_layout.place_item(&item_list_first, DTransformation::new(0.0, (1.0, 1.0)));
    distinct_items_layout.place_item(&item_list_second, DTransformation::new(0.0, (8.0, 1.0)));
    distinct_items_layout.place_item(
        &duplicate_item_index,
        DTransformation::new(0.0, (15.0, 1.0)),
    );
    let distinct_items: Vec<_> = distinct_items_layout
        .items()
        .map(|item| format!("{}:{}", item.idx, item.external_id))
        .collect();
    println!("layout.distinct_items={}", distinct_items.join(","));
    distinct_items_layout.remove_item(item_list_first_key);
    let distinct_items_after_removal: Vec<_> = distinct_items_layout
        .items()
        .map(|item| format!("{}:{}", item.idx, item.external_id))
        .collect();
    println!(
        "layout.distinct_items_after_removal={}",
        distinct_items_after_removal.join(",")
    );

    let slot_item = square_item(0, 1.0);
    let mut slot_map_layout = Layout::new(make_bin_container(20.0, 10.0));
    let first_slot_key =
        slot_map_layout.place_item(&slot_item, DTransformation::new(0.0, (1.0, 1.0)));
    let second_slot_key =
        slot_map_layout.place_item(&slot_item, DTransformation::new(0.0, (5.0, 1.0)));
    slot_map_layout.remove_item(first_slot_key);
    slot_map_layout.remove_item(second_slot_key);
    let reused_slot_key =
        slot_map_layout.place_item(&slot_item, DTransformation::new(0.0, (9.0, 1.0)));
    println!(
        "layout.slot_reuse_order=first:{first_slot_key:?},second:{second_slot_key:?},reused:{reused_slot_key:?}"
    );
    let cde_dynamic_keys: Vec<_> = slot_map_layout
        .cde()
        .hazards_map
        .iter()
        .filter(|(_, hazard)| hazard.dynamic)
        .map(|(key, _)| format!("{key:?}"))
        .collect();
    println!("layout.cde_dynamic_keys={cde_dynamic_keys:?}");
    let slot_map_snapshot = slot_map_layout.save();
    let mut restored_slot_map_layout = Layout::new(slot_map_snapshot.container.clone());
    restored_slot_map_layout
        .restore(&slot_map_snapshot)
        .unwrap();
    let reused_after_restore =
        restored_slot_map_layout.place_item(&slot_item, DTransformation::new(0.0, (13.0, 1.0)));
    println!(
        "layout.restore_free_reuse={reused_after_restore:?},stale_first_active={}",
        restored_slot_map_layout
            .placed_items
            .contains_key(first_slot_key)
    );

    let mut cde_restore_layout = Layout::new(make_bin_container(20.0, 10.0));
    let cde_item_a = square_item(7, 1.0);
    let cde_item_b = square_item(8, 1.0);
    let cde_item_c = square_item(9, 1.0);
    let cde_item_key_a =
        cde_restore_layout.place_item(&cde_item_a, DTransformation::new(0.0, (1.0, 1.0)));
    let cde_item_key_b =
        cde_restore_layout.place_item(&cde_item_b, DTransformation::new(0.0, (5.0, 1.0)));
    let cde_snapshot = cde_restore_layout.save();
    cde_restore_layout.remove_item(cde_item_key_a);
    cde_restore_layout.remove_item(cde_item_key_b);
    cde_restore_layout.place_item(&cde_item_c, DTransformation::new(0.0, (9.0, 1.0)));
    let mut restored_cde = cde_restore_layout.cde().clone();
    restored_cde.restore(&cde_snapshot.cde_snapshot);
    let restored_dynamic_hazard_keys: Vec<_> = restored_cde
        .hazards_map
        .iter()
        .filter(|(_, hazard)| hazard.dynamic)
        .map(|(key, hazard)| format!("{:?}:{key:?}", hazard.entity))
        .collect();
    println!("layout.cde_restore_dynamic_keys={restored_dynamic_hazard_keys:?}");

    let mut swap_after_restore = cde_restore_layout.clone();
    swap_after_restore.restore(&cde_snapshot).unwrap();
    swap_after_restore.swap_container(make_bin_container(21.0, 10.0));
    let swap_after_restore_dynamic_order: Vec<_> = swap_after_restore
        .save()
        .cde_snapshot
        .dynamic_hazards
        .iter()
        .map(|hazard| format!("{:?}", hazard.entity))
        .collect();
    println!("layout.swap_after_restore_dynamic_order={swap_after_restore_dynamic_order:?}");

    let mut cde_with_dynamic_hole = slot_map_layout.cde().clone();
    let mut dynamic_hole_snapshot = cde_with_dynamic_hole.save();
    dynamic_hole_snapshot.dynamic_hazards.push(Hazard::new(
        HazardEntity::Hole { idx: 88 },
        slot_item.shape_cd.clone(),
        true,
    ));
    cde_with_dynamic_hole.restore(&dynamic_hole_snapshot);
    println!(
        "layout.dynamic_non_item_hazard_restored={}",
        cde_with_dynamic_hole
            .save()
            .dynamic_hazards
            .iter()
            .any(|hazard| matches!(hazard.entity, HazardEntity::Hole { idx: 88 }))
    );

    let spp_restore_strip =
        SPStrip::new(10.0, cde_config, ShapeModifyConfig::default(), 20.0).unwrap();
    let mut spp_restore = SPProblem::new(SPInstance::new(
        vec![
            (square_item(0, 1.0), 1),
            (square_item(1, 1.0), 1),
            (square_item(2, 1.0), 1),
        ],
        spp_restore_strip,
    ))
    .unwrap();
    let spp_item_a = spp_restore.place_item(SPPlacement {
        item_idx: 0,
        d_transf: DTransformation::new(0.0, (1.0, 1.0)),
    });
    let spp_item_b = spp_restore.place_item(SPPlacement {
        item_idx: 1,
        d_transf: DTransformation::new(0.0, (5.0, 1.0)),
    });
    let spp_restore_snapshot = spp_restore.save();
    spp_restore.remove_item(spp_item_a);
    spp_restore.remove_item(spp_item_b);
    spp_restore.place_item(SPPlacement {
        item_idx: 2,
        d_transf: DTransformation::new(0.0, (9.0, 1.0)),
    });
    spp_restore.restore(&spp_restore_snapshot);
    let spp_restore_hazard_order: Vec<_> = spp_restore
        .layout
        .cde()
        .hazards_map
        .values()
        .filter(|hazard| hazard.dynamic)
        .map(|hazard| match hazard.entity {
            HazardEntity::PlacedItem { id, .. } => id,
            _ => usize::MAX,
        })
        .collect();
    println!("spp.restore_cde_item_order={spp_restore_hazard_order:?}");

    println!(
        "mspp.positive_infinite_initial_width_accepted={}",
        Strip::new(
            10.0,
            5.0,
            cde_config,
            ShapeModifyConfig::default(),
            f32::INFINITY,
        )
        .is_ok()
    );
    let mut positive_infinite_mspp_strip = Strip::new(
        f32::INFINITY,
        5.0,
        cde_config,
        ShapeModifyConfig::default(),
        10.0,
    )
    .unwrap();
    println!("mspp.positive_infinite_max_width_accepted=true");
    positive_infinite_mspp_strip.set_width(f32::INFINITY);
    println!("mspp.positive_infinite_width_accepted=true");

    let stale_msp_strip =
        Strip::new(10.0, 5.0, cde_config, ShapeModifyConfig::default(), 10.0).unwrap();
    let mut stale_msp = MSPProblem::new(MSPInstance::new(
        vec![(square_item(0, 1.0), 2)],
        stale_msp_strip,
    ));
    let old_msp_key = stale_msp.add_layout_from_strip(stale_msp_strip).unwrap();
    println!("mspp.first_layout_key={old_msp_key:?}");
    stale_msp.place_item(MSPPlacement {
        lk: old_msp_key,
        item_idx: 0,
        d_transf: DTransformation::empty(),
    });
    let old_msp_solution = stale_msp.save();
    stale_msp.remove_layout(old_msp_key);
    let replacement_msp_key = stale_msp.add_layout_from_strip(stale_msp_strip).unwrap();
    stale_msp.place_item(MSPPlacement {
        lk: replacement_msp_key,
        item_idx: 0,
        d_transf: DTransformation::empty(),
    });
    let msp_restore_keys_changed = stale_msp.restore(&old_msp_solution);
    println!(
        "mspp.restore_rekeyed={},old_key_active={}",
        msp_restore_keys_changed,
        stale_msp.layouts.contains_key(old_msp_key)
    );

    let restore_order_base =
        Strip::new(10.0, 5.0, cde_config, ShapeModifyConfig::default(), 10.0).unwrap();
    let mut restore_order_msp = MSPProblem::new(MSPInstance::new(
        vec![(square_item(0, 1.0), 1)],
        restore_order_base,
    ));
    let restore_order_first = restore_order_msp
        .add_layout_from_strip(
            Strip::new(10.0, 5.0, cde_config, ShapeModifyConfig::default(), 4.0).unwrap(),
        )
        .unwrap();
    let restore_order_second = restore_order_msp
        .add_layout_from_strip(
            Strip::new(10.0, 5.0, cde_config, ShapeModifyConfig::default(), 6.0).unwrap(),
        )
        .unwrap();
    let restore_order_solution = restore_order_msp.save();
    restore_order_msp.remove_layout(restore_order_first);
    restore_order_msp.remove_layout(restore_order_second);
    restore_order_msp.restore(&restore_order_solution);
    let restored_strip_widths = restore_order_msp
        .layouts
        .iter()
        .map(|(key, _)| restore_order_msp.strips[key].width)
        .collect::<Vec<_>>();
    println!("mspp.restore_rekeyed_widths={restored_strip_widths:?}");

    let mut stale_bpp = BPProblem::new(BPInstance::new(
        vec![(square_item(0, 1.0), 2)],
        vec![Bin::new(0, 6500, make_bin_container(10.0, 5.0), 2, 1)],
    ));
    let (old_bpp_key, _) = stale_bpp.place_item(BPPlacement {
        layout_id: BPLayoutType::Closed { bin_id: 0 },
        item_idx: 0,
        d_transf: DTransformation::empty(),
    });
    println!("bpp.first_layout_key={old_bpp_key:?}");
    let old_bpp_solution = stale_bpp.save();
    stale_bpp.remove_layout(old_bpp_key);
    stale_bpp.place_item(BPPlacement {
        layout_id: BPLayoutType::Closed { bin_id: 0 },
        item_idx: 0,
        d_transf: DTransformation::empty(),
    });
    let bpp_restore_keys_changed = stale_bpp.restore(&old_bpp_solution);
    println!(
        "bpp.restore_rekeyed={},old_key_active={}",
        bpp_restore_keys_changed,
        stale_bpp.layouts.contains_key(old_bpp_key)
    );

    let mut bpp_restore_order = BPProblem::new(BPInstance::new(
        vec![(square_item(0, 1.0), 2)],
        vec![
            Bin::new(0, 6560, make_bin_container(2.0, 2.0), 1, 10),
            Bin::new(1, 6561, make_bin_container(3.0, 2.0), 1, 25),
        ],
    ));
    let (bpp_restore_first, _) = bpp_restore_order.place_item(BPPlacement {
        layout_id: BPLayoutType::Closed { bin_id: 0 },
        item_idx: 0,
        d_transf: DTransformation::empty(),
    });
    let (bpp_restore_second, _) = bpp_restore_order.place_item(BPPlacement {
        layout_id: BPLayoutType::Closed { bin_id: 1 },
        item_idx: 0,
        d_transf: DTransformation::empty(),
    });
    let bpp_restore_order_solution = bpp_restore_order.save();
    bpp_restore_order.remove_layout(bpp_restore_first);
    bpp_restore_order.remove_layout(bpp_restore_second);
    bpp_restore_order.restore(&bpp_restore_order_solution);
    let restored_bin_order = bpp_restore_order
        .layouts
        .iter()
        .map(|(key, _)| bpp_restore_order.layout_bins[key])
        .collect::<Vec<_>>();
    println!("bpp.restore_rekeyed_bin_order={restored_bin_order:?}");

    let mut bpp_churn = BPProblem::new(BPInstance::new(
        vec![(square_item(0, 1.0), 4)],
        vec![
            Bin::new(0, 5400, make_bin_container(1.0, 1.0), 4, 1),
            Bin::new(1, 5401, make_bin_container(16_777_216.0, 1.0), 1, 1),
        ],
    ));
    let (first_layout, first_item_key) = bpp_churn.place_item(BPPlacement {
        layout_id: BPLayoutType::Closed { bin_id: 0 },
        item_idx: 0,
        d_transf: DTransformation::empty(),
    });
    bpp_churn.place_item(BPPlacement {
        layout_id: BPLayoutType::Closed { bin_id: 0 },
        item_idx: 0,
        d_transf: DTransformation::empty(),
    });
    bpp_churn.place_item(BPPlacement {
        layout_id: BPLayoutType::Closed { bin_id: 1 },
        item_idx: 0,
        d_transf: DTransformation::empty(),
    });
    bpp_churn.remove_item(first_layout, first_item_key);
    bpp_churn.place_item(BPPlacement {
        layout_id: BPLayoutType::Closed { bin_id: 0 },
        item_idx: 0,
        d_transf: DTransformation::empty(),
    });
    println!("bpp.reused_slot_density={:.12}", bpp_churn.density());

    let spp_instance = SPInstance::new(
        vec![
            (square_item(0, 4096.0), 1),
            (square_item(1, 1.0), 1),
            (square_item(2, 1.0), 1),
        ],
        SPStrip::new(2.0, cde_config, ShapeModifyConfig::default(), 1.0).unwrap(),
    );
    println!("spp.item_area={:.0}", spp_instance.item_area());
    println!(
        "spp.auto_base_strip_width={:.0}",
        spp_instance.item_area() / 2.0
    );

    let strip_width_base = Strip::new(
        16_777_216.0,
        1.0,
        cde_config,
        ShapeModifyConfig::default(),
        16_777_216.0,
    )
    .unwrap();
    let mut strip_width_problem = MSPProblem::new(MSPInstance::new(
        vec![(square_item(0, 1.0), 1)],
        strip_width_base,
    ));
    for width in [16_777_216.0, 1.0, 1.0] {
        strip_width_problem
            .add_layout_from_strip(
                Strip::new(
                    16_777_216.0,
                    1.0,
                    cde_config,
                    ShapeModifyConfig::default(),
                    width,
                )
                .unwrap(),
            )
            .unwrap();
    }
    println!(
        "mspp.total_strip_width={:.0}",
        strip_width_problem.total_strip_width()
    );
    let churn_base = Strip::new(
        16_777_216.0,
        1.0,
        cde_config,
        ShapeModifyConfig::default(),
        16_777_216.0,
    )
    .unwrap();
    let mut churn_problem =
        MSPProblem::new(MSPInstance::new(vec![(square_item(0, 1.0), 1)], churn_base));
    let removed_key = churn_problem
        .add_layout_from_strip(
            Strip::new(
                16_777_216.0,
                1.0,
                cde_config,
                ShapeModifyConfig::default(),
                1.0,
            )
            .unwrap(),
        )
        .unwrap();
    churn_problem
        .add_layout_from_strip(
            Strip::new(
                16_777_216.0,
                1.0,
                cde_config,
                ShapeModifyConfig::default(),
                1.0,
            )
            .unwrap(),
        )
        .unwrap();
    churn_problem
        .add_layout_from_strip(
            Strip::new(
                16_777_216.0,
                1.0,
                cde_config,
                ShapeModifyConfig::default(),
                16_777_216.0,
            )
            .unwrap(),
        )
        .unwrap();
    churn_problem.remove_layout(removed_key);
    let reused_churn_key = churn_problem
        .add_layout_from_strip(
            Strip::new(
                16_777_216.0,
                1.0,
                cde_config,
                ShapeModifyConfig::default(),
                1.0,
            )
            .unwrap(),
        )
        .unwrap();
    println!(
        "mspp.reused_slot_total_strip_width={:.0}",
        churn_problem.total_strip_width()
    );
    churn_problem.place_item(MSPPlacement {
        lk: reused_churn_key,
        item_idx: 0,
        d_transf: DTransformation::empty(),
    });
    println!("mspp.reused_slot_density={:.12}", churn_problem.density());
    let slot_ordered_churn_widths = churn_problem
        .layouts
        .iter()
        .map(|(key, _)| churn_problem.strips[key].width)
        .collect::<Vec<_>>();
    println!("mspp.reused_slot_width_order={slot_ordered_churn_widths:?}");
    println!(
        "mspp.initial_width_above_max_accepted={}",
        Strip::new(10.0, 5.0, cde_config, ShapeModifyConfig::default(), 11.0,).is_ok()
    );
    let over_max_base =
        Strip::new(10.0, 5.0, cde_config, ShapeModifyConfig::default(), 10.0).unwrap();
    let mut over_max_problem = MSPProblem::new(MSPInstance::new(
        vec![(square_item(0, 1.0), 1)],
        over_max_base,
    ));
    let over_max_key = over_max_problem
        .add_layout_from_strip(
            Strip::new(10.0, 5.0, cde_config, ShapeModifyConfig::default(), 11.0).unwrap(),
        )
        .unwrap();
    over_max_problem
        .add_layout_from_strip(
            Strip::new(10.0, 5.0, cde_config, ShapeModifyConfig::default(), 9.0).unwrap(),
        )
        .unwrap();
    over_max_problem
        .add_layout_from_strip(
            Strip::new(10.0, 5.0, cde_config, ShapeModifyConfig::default(), 10.0).unwrap(),
        )
        .unwrap();
    println!(
        "mspp.extendable_strips_count={}",
        over_max_problem.extendable_strips().count()
    );
    let over_max_snapshot = over_max_problem.save();
    over_max_problem.restore(&over_max_snapshot);
    println!(
        "mspp.over_max_restore_width={:.0}",
        over_max_problem.strips[over_max_key].width
    );

    let strip = Strip::new(1.0, 1.0, cde_config, ShapeModifyConfig::default(), 1.0).unwrap();
    let empty_mspp = MSPProblem::new(MSPInstance::new(vec![], strip));
    println!(
        "mspp.empty_instance_items={}",
        empty_mspp.instance.items.len()
    );
    println!(
        "density.empty_mspp_problem_nan={}",
        empty_mspp.density().is_nan()
    );
    let zero_demand_mspp = MSPInstance::new(vec![(square_item(0, 1.0), 0)], strip);
    println!("mspp.zero_demand_item_qty={}", zero_demand_mspp.item_qty(0));
    println!(
        "mspp.item_idx_zero_demand={:?},unknown={:?}",
        zero_demand_mspp.item_idx(1000),
        zero_demand_mspp.item_idx(9999)
    );

    let imported = import_simple_polygon(&ExtSPolygon(vec![
        (0.0, 0.0),
        (4.0, 0.0),
        ((4.0_f64 + 1.0e-7) as f32, 0.0),
        (4.0, 2.0),
        (0.0, 2.0),
    ]))
    .unwrap();
    println!("import.f32duplicate.vertices={}", imported.n_vertices());
    println!("import.f32duplicate.area={:.12}", imported.area);

    let square = polygon(&[(0.0, 0.0), (2.0, 0.0), (2.0, 2.0), (0.0, 2.0)]);
    let large_square = polygon(&[
        (1_000_000.0, 1_000_000.0),
        (2_000_000.0, 1_000_000.0),
        (2_000_000.0, 2_000_000.0),
        (1_000_000.0, 2_000_000.0),
    ]);
    match offset_shape(&large_square, ShapeModifyMode::Inflate, 0.01) {
        Ok(shape) => print_polygon("offset.sub_ulp_inflate", &shape),
        Err(error) => println!("offset.sub_ulp_inflate=error:{error}"),
    }
    match offset_shape(&large_square, ShapeModifyMode::Deflate, 0.01) {
        Ok(shape) => print_polygon("offset.sub_ulp_deflate", &shape),
        Err(error) => println!("offset.sub_ulp_deflate=error:{error}"),
    }
    match offset_shape(&square, ShapeModifyMode::Deflate, 2.0) {
        Ok(shape) => print_polygon("offset.deflate_collapse", &shape),
        Err(error) => println!("offset.deflate_collapse=error:{error}"),
    }
    match offset_shape(&square, ShapeModifyMode::Inflate, 0.25) {
        Ok(shape) => print_polygon("offset.square_inflate", &shape),
        Err(error) => println!("offset.square_inflate=error:{error}"),
    }
    let negative_offset = offset_shape(&square, ShapeModifyMode::Inflate, -0.25).unwrap();
    println!(
        "offset.negative_inflate=area:{:.2},bbox:{:.2},{:.2},{:.2},{:.2}",
        negative_offset.area,
        negative_offset.bbox.x_min,
        negative_offset.bbox.y_min,
        negative_offset.bbox.x_max,
        negative_offset.bbox.y_max
    );
    let epsilon_offset_shape = polygon(&[
        (0.0, 0.0),
        (f32::from_bits(0x3b03126f), 0.0),
        (f32::from_bits(0x3b03126f), f32::from_bits(0x3b03126f)),
        (0.0, f32::from_bits(0x3b03126f)),
    ]);
    let epsilon_offset = std::panic::catch_unwind(|| {
        offset_shape(
            &epsilon_offset_shape,
            ShapeModifyMode::Deflate,
            f32::from_bits(0x3a83126e),
        )
    });
    match epsilon_offset {
        Ok(Ok(shape)) => print_polygon("offset.epsilon_before_event", &shape),
        Ok(Err(error)) => println!("offset.epsilon_before_event=error:{error}"),
        Err(_) => println!("offset.epsilon_before_event=panic"),
    }
    print_buffer_components(
        "offset.epsilon_before_event.raw",
        &[
            (0.0, 0.0),
            (f32::from_bits(0x3b03126f), 0.0),
            (f32::from_bits(0x3b03126f), f32::from_bits(0x3b03126f)),
            (0.0, f32::from_bits(0x3b03126f)),
        ],
        -f64::from(f32::from_bits(0x3a83126e)),
    );
    let near_zero_edge_shape = polygon(&[
        (0.0, 0.0),
        (1.0e-8, 0.0),
        (1.0, 0.0),
        (1.0, 1.0),
        (0.0, 1.0),
    ]);
    match offset_shape(&near_zero_edge_shape, ShapeModifyMode::Inflate, 0.0) {
        Ok(shape) => {
            print_polygon("offset.zero_near_edge", &shape);
            for (index, point) in shape.vertices.iter().enumerate() {
                println!(
                    "offset.zero_near_edge.vertex[{index}]={:.12},{:.12}",
                    point.0, point.1
                );
            }
        }
        Err(error) => println!("offset.zero_near_edge=error:{error}"),
    }
    let validation_bounds = polygon(&[(0.0, 0.0), (3.0, 0.0), (3.0, 3.0), (0.0, 3.0)]);
    let edge_tolerance = validation_bounds.diameter * 1e-6;
    let just_outside = polygon(&[(-edge_tolerance, 1.5), (1.0, 1.0), (1.0, 2.0)]);
    println!(
        "shape_modification.edge_threshold_valid={}",
        shape_modification_valid(&just_outside, &validation_bounds, ShapeModifyMode::Inflate)
    );

    let concave = polygon(&[(0.0, 0.0), (4.0, 0.0), (4.0, 4.0), (2.0, 1.0), (0.0, 4.0)]);
    print_polygon("concave", &concave);
    let deflated = offset_shape(&concave, ShapeModifyMode::Deflate, 0.45);
    println!(
        "concave.deflate045={}",
        if deflated.is_ok() { "ok" } else { "error" }
    );
    if let Ok(shape) = deflated {
        print_polygon("concave.deflate045.first", &shape);
    }
    print_buffer_components(
        "concave.deflate045",
        &[(0.0, 0.0), (4.0, 0.0), (4.0, 4.0), (2.0, 1.0), (0.0, 4.0)],
        -0.45,
    );

    print_buffer_components(
        "l.deflate025",
        &[
            (0.0, 0.0),
            (6.0, 0.0),
            (6.0, 2.0),
            (2.0, 2.0),
            (2.0, 6.0),
            (0.0, 6.0),
        ],
        -0.25,
    );

    let comb = polygon(&[
        (0.0, 0.0),
        (22.0, 0.0),
        (22.0, 12.0),
        (16.0, 12.0),
        (16.0, 4.0),
        (14.0, 4.0),
        (14.0, 12.0),
        (8.0, 12.0),
        (8.0, 4.0),
        (6.0, 4.0),
        (6.0, 12.0),
        (0.0, 12.0),
    ]);
    match offset_shape(&comb, ShapeModifyMode::Deflate, 2.1) {
        Ok(shape) => print_polygon("offset.comb_deflate.first", &shape),
        Err(error) => println!("offset.comb_deflate.first=error:{error}"),
    }
    print_buffer_components(
        "offset.comb_deflate",
        &[
            (0.0, 0.0),
            (22.0, 0.0),
            (22.0, 12.0),
            (16.0, 12.0),
            (16.0, 4.0),
            (14.0, 4.0),
            (14.0, 12.0),
            (8.0, 12.0),
            (8.0, 4.0),
            (6.0, 4.0),
            (6.0, 12.0),
            (0.0, 12.0),
        ],
        f64::from(-2.1_f32),
    );

    let notch = polygon(&[
        (0.0, 0.0),
        (10.0, 0.0),
        (10.0, 10.0),
        (6.0, 10.0),
        (6.0, 9.9),
        (4.0, 9.9),
        (4.0, 10.0),
        (0.0, 10.0),
    ]);
    print_polygon("notch", &notch);
    let notch_offset = offset_shape(&notch, ShapeModifyMode::Inflate, 0.25);
    println!(
        "notch.inflate025={}",
        if notch_offset.is_ok() { "ok" } else { "error" }
    );
    if let Ok(shape) = notch_offset {
        print_polygon("notch.inflate025.first", &shape);
    }

    let u_shape = polygon(&[
        (0.0, 0.0),
        (10.0, 0.0),
        (10.0, 10.0),
        (6.0, 10.0),
        (6.0, 4.0),
        (4.0, 4.0),
        (4.0, 10.0),
        (0.0, 10.0),
    ]);
    let single_nonzero_limit_config = SPSurrogateConfig {
        n_pole_limits: [(8, 0.0), (0, 0.0), (0, 0.0)],
        ff_pole_area_ratio: 0.0,
        n_ff_piers: 0,
    };
    let mut single_nonzero_limit_shape = u_shape.clone();
    single_nonzero_limit_shape
        .generate_surrogate(single_nonzero_limit_config)
        .unwrap();
    println!(
        "surrogate.single_nonzero_limit={}",
        single_nonzero_limit_shape.surrogate().poles.len()
    );
    println!(
        "surrogate.config_threshold_f32_bits={:08x}",
        0.75000001_f32.to_bits()
    );
    println!(
        "surrogate.config_fail_fast_ratio_f32_bits={:08x}",
        0.50000001_f32.to_bits()
    );
    let mut infinite_threshold_shape = polygon(&[(0.0, 0.0), (2.0, 0.0), (2.0, 2.0), (0.0, 2.0)]);
    let infinite_threshold_config = SPSurrogateConfig {
        n_pole_limits: [(1, f32::INFINITY), (0, 0.0), (0, 0.0)],
        ff_pole_area_ratio: 0.0,
        n_ff_piers: 0,
    };
    let infinite_threshold_result =
        infinite_threshold_shape.generate_surrogate(infinite_threshold_config);
    let infinite_threshold_poles = infinite_threshold_shape
        .surrogate
        .as_ref()
        .map_or(0, |surrogate| surrogate.poles.len());
    println!(
        "surrogate.infinite_threshold=ok:{},poles:{}",
        infinite_threshold_result.is_ok(),
        infinite_threshold_poles
    );
    let config = SPSurrogateConfig {
        n_pole_limits: [(8, 0.0), (4, 0.75), (2, 0.9)],
        ff_pole_area_ratio: 0.5,
        n_ff_piers: 2,
    };
    let mut surrogate_shape = u_shape.clone();
    surrogate_shape.generate_surrogate(config).unwrap();
    let surrogate = surrogate_shape.surrogate();
    println!("u.surrogate.poles={}", surrogate.poles.len());
    println!("u.surrogate.ff_poles={}", surrogate.ff_poles().len());
    println!("u.surrogate.piers={}", surrogate.piers.len());
    println!("u.surrogate.hull_area={:.12}", surrogate.convex_hull_area);
    println!(
        "u.surrogate.hull_indices={:?}",
        surrogate.convex_hull_indices
    );
    for (index, pole) in surrogate.poles.iter().enumerate().take(4) {
        println!(
            "u.pole[{index}]={:.12},{:.12},{:.12}",
            pole.center.0, pole.center.1, pole.radius
        );
    }
    for (index, pier) in surrogate.piers.iter().enumerate() {
        println!(
            "u.pier[{index}]={:.12},{:.12},{:.12},{:.12},{:.12}",
            pier.start.0,
            pier.start.1,
            pier.end.0,
            pier.end.1,
            pier.length()
        );
    }

    for (name, points) in [
        (
            "l",
            vec![
                (0.0, 0.0),
                (8.0, 0.0),
                (8.0, 3.0),
                (3.0, 3.0),
                (3.0, 8.0),
                (0.0, 8.0),
            ],
        ),
        (
            "c",
            vec![
                (0.0, 0.0),
                (10.0, 0.0),
                (10.0, 2.0),
                (3.0, 2.0),
                (3.0, 8.0),
                (10.0, 8.0),
                (10.0, 10.0),
                (0.0, 10.0),
            ],
        ),
    ] {
        let shape = polygon(&points);
        let mut shape_with_surrogate = shape.clone();
        shape_with_surrogate.generate_surrogate(config).unwrap();
        let surrogate = shape_with_surrogate.surrogate();
        println!("{name}.surrogate.poles={}", surrogate.poles.len());
        println!("{name}.surrogate.ff_poles={}", surrogate.ff_poles().len());
        println!("{name}.surrogate.piers={}", surrogate.piers.len());
        println!(
            "{name}.surrogate.hull_area={:.12}",
            surrogate.convex_hull_area
        );
        println!(
            "{name}.surrogate.hull_indices={:?}",
            surrogate.convex_hull_indices
        );
        for (index, pole) in surrogate.poles.iter().enumerate().take(4) {
            println!(
                "{name}.pole[{index}]={:.12},{:.12},{:.12}",
                pole.center.0, pole.center.1, pole.radius
            );
        }
        for (index, pier) in surrogate.piers.iter().enumerate() {
            println!(
                "{name}.pier[{index}]={:.12},{:.12},{:.12},{:.12},{:.12}",
                pier.start.0,
                pier.start.1,
                pier.end.0,
                pier.end.1,
                pier.length()
            );
        }
    }
}
