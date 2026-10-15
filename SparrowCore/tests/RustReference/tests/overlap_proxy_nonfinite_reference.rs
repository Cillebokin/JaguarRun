use jagua_rs::geometry::fail_fast::SPSurrogateConfig;
use jagua_rs::geometry::primitives::{Point, SPolygon};
use jagua_rs::io::ext_repr::{ExtItem as BaseExtItem, ExtOrientation, ExtRotation, ExtShape};
use jagua_rs::io::import::Importer;
use jagua_rs::probs::spp::entities::{SPPlacement, SPProblem};
use jagua_rs::probs::spp::io::ext_repr::{ExtItem as DemandItem, ExtSPInstance};
use jagua_rs::probs::spp::io::import_instance;
use sparrow::config::DEFAULT_SPARROW_CONFIG;
use sparrow::eval::sample_eval::SampleEval;
use sparrow::eval::sample_eval::SampleEvaluator;
use sparrow::eval::sep_evaluator::SeparationEvaluator;
use sparrow::quantify::overlap_proxy::overlap_area_proxy;
use sparrow::quantify::quantify_collision_poly_poly;
use sparrow::quantify::tracker::CollisionTracker;
use std::panic::{AssertUnwindSafe, catch_unwind};

fn square(side: f32) -> SPolygon {
    let half = side * 0.5;
    let mut shape = SPolygon::new(vec![
        Point(-half, -half),
        Point(half, -half),
        Point(half, half),
        Point(-half, half),
    ])
    .expect("finite square must construct");
    shape
        .generate_surrogate(SPSurrogateConfig {
            n_pole_limits: [(2, 0.0), (2, 0.8), (2, 0.9)],
            ff_pole_area_ratio: 0.0,
            n_ff_piers: 0,
        })
        .expect("finite square surrogate must construct");
    shape
}

#[test]
fn finite_tiny_pair_reports_overlap_proxy_profile_behavior() {
    let first = square(1.0e-20);
    let second = first.clone();
    let epsilon = first.diameter.max(second.diameter) * 0.01;
    let raw = catch_unwind(AssertUnwindSafe(|| {
        overlap_area_proxy(first.surrogate(), second.surrogate(), epsilon)
    }));
    let loss = catch_unwind(AssertUnwindSafe(|| {
        quantify_collision_poly_poly(&first, &second)
    }));

    println!(
        "diameter={}({:08x}) area={}({:08x}) poi_radius={}({:08x}) poles={:?} raw={} loss={}",
        first.diameter,
        first.diameter.to_bits(),
        first.area,
        first.area.to_bits(),
        first.poi.radius,
        first.poi.radius.to_bits(),
        first
            .surrogate()
            .poles
            .iter()
            .map(|pole| (pole.radius, pole.radius.to_bits()))
            .collect::<Vec<_>>(),
        raw.as_ref().map_or_else(
            |_| "panic".to_owned(),
            |value| format!("{}({:08x})", value, value.to_bits())
        ),
        loss.as_ref().map_or_else(
            |_| "panic".to_owned(),
            |value| format!("{}({:08x})", value, value.to_bits())
        )
    );

    assert!(
        first
            .vertices
            .iter()
            .all(|point| point.0.is_finite() && point.1.is_finite())
    );
    assert!(first.diameter.is_finite());
    assert!(first.area.is_finite());
    assert_eq!(first.surrogate().poles.len(), 2);
    if cfg!(debug_assertions) {
        assert!(
            raw.is_err(),
            "Debug must assert on non-normal total_overlap"
        );
        assert!(
            loss.is_err(),
            "Debug must propagate the overlap proxy assertion"
        );
    } else {
        let raw = raw.expect("Release must preserve subnormal total_overlap");
        let loss = loss.expect("Release must preserve subnormal pair loss");
        assert!(raw > 0.0 && raw.is_subnormal());
        assert!(loss > 0.0 && loss.is_subnormal());
        assert_eq!(raw.to_bits(), 0x0001_c3bd);
        assert_eq!(loss.to_bits(), 0x0001_62e2);
    }
}

#[test]
fn direct_extreme_epsilon_exposes_nonfinite_proxy_profile_boundary() {
    let shape = square(1.0);
    let raw = catch_unwind(AssertUnwindSafe(|| {
        overlap_area_proxy(shape.surrogate(), shape.surrogate(), f32::MAX)
    }));
    if cfg!(debug_assertions) {
        assert!(raw.is_err(), "Rust Debug must assert on NaN total_overlap");
    } else {
        let raw = raw.expect("Release helper must preserve IEEE arithmetic");
        println!("extreme_epsilon_raw_proxy={raw} bits={:08x}", raw.to_bits());
        assert!(raw.is_nan());
    }
}

#[test]
fn tiny_imported_pair_reaches_subnormal_loss_through_separation_evaluator() {
    let mut config = DEFAULT_SPARROW_CONFIG;
    config.cde_config.item_surrogate_config.n_pole_limits = [(2, 0.0), (2, 0.8), (2, 0.9)];
    config.cde_config.item_surrogate_config.ff_pole_area_ratio = 0.0;
    let importer = Importer::new(
        config.cde_config,
        config.poly_simpl_tolerance,
        config.narrow_concavity_cutoff_ratio,
    );
    let side = 1.0e-20;
    let external = ExtSPInstance {
        name: "tiny-subnormal-pair".to_owned(),
        min_item_separation: 0.0,
        items: vec![DemandItem {
            base: BaseExtItem {
                id: 1,
                orientation: ExtOrientation {
                    rotation: ExtRotation::Continuous {},
                },
                shape: ExtShape::Rectangle {
                    x_min: 0.0,
                    y_min: 0.0,
                    width: side,
                    height: side,
                },
                min_quality: None,
            },
            demand: 2,
        }],
        strip_height: 2.0e-19,
    };
    let mut instance = import_instance(&importer, &external)
        .expect("tiny finite imported item must produce an SPP instance");
    instance.base_strip.width = 2.0e-19;
    let mut problem = SPProblem::new(instance).expect("tiny SPP problem must construct");
    let first_key = problem.place_item(SPPlacement {
        item_idx: 0,
        d_transf: jagua_rs::geometry::DTransformation::new(0.0, (0.5e-19, 0.5e-19)),
    });
    problem.place_item(SPPlacement {
        item_idx: 0,
        d_transf: jagua_rs::geometry::DTransformation::new(0.0, (1.5e-19, 1.5e-19)),
    });
    let tracker = CollisionTracker::new(&problem.layout);
    let moving_item = &problem.layout.placed_items[first_key].item;
    let mut evaluator = SeparationEvaluator::new(&problem.layout, moving_item, first_key, &tracker);
    let result = catch_unwind(AssertUnwindSafe(|| {
        evaluator.evaluate_sample(
            jagua_rs::geometry::DTransformation::new(0.0, (1.5e-19, 1.5e-19)),
            None,
        )
    }));

    if cfg!(debug_assertions) {
        assert!(
            result.is_err(),
            "Rust Debug must assert on subnormal pair loss"
        );
    } else {
        let SampleEval::Collision { loss } = result.expect("release evaluation must return") else {
            panic!("overlapping tiny items must be a collision");
        };
        println!(
            "actual_evaluator_subnormal_loss={loss} bits={:08x}",
            loss.to_bits()
        );
        assert!(loss > 0.0 && loss.is_subnormal());
        assert_eq!(loss.to_bits(), 0x0001_62e2);
    }
}

#[test]
fn tiny_exterior_hazard_reaches_subnormal_container_loss_assertion() {
    let mut config = DEFAULT_SPARROW_CONFIG;
    config.cde_config.item_surrogate_config.n_pole_limits = [(2, 0.0), (2, 0.8), (2, 0.9)];
    config.cde_config.item_surrogate_config.ff_pole_area_ratio = 0.0;
    let importer = Importer::new(
        config.cde_config,
        config.poly_simpl_tolerance,
        config.narrow_concavity_cutoff_ratio,
    );
    let side = 1.0e-20;
    let external = ExtSPInstance {
        name: "tiny-subnormal-exterior".to_owned(),
        min_item_separation: 0.0,
        items: vec![DemandItem {
            base: BaseExtItem {
                id: 2,
                orientation: ExtOrientation {
                    rotation: ExtRotation::Continuous {},
                },
                shape: ExtShape::Rectangle {
                    x_min: 0.0,
                    y_min: 0.0,
                    width: side,
                    height: side,
                },
                min_quality: None,
            },
            demand: 1,
        }],
        strip_height: 2.0e-19,
    };
    let mut instance = import_instance(&importer, &external)
        .expect("tiny finite imported item must produce an SPP instance");
    instance.base_strip.width = 2.0e-19;
    let mut problem = SPProblem::new(instance).expect("tiny SPP problem must construct");
    let key = problem.place_item(SPPlacement {
        item_idx: 0,
        d_transf: jagua_rs::geometry::DTransformation::new(0.0, (1.0e-19, 1.0e-19)),
    });
    let tracker = CollisionTracker::new(&problem.layout);
    let moving_item = &problem.layout.placed_items[key].item;
    let mut evaluator = SeparationEvaluator::new(&problem.layout, moving_item, key, &tracker);
    let result = catch_unwind(AssertUnwindSafe(|| {
        evaluator.evaluate_sample(
            jagua_rs::geometry::DTransformation::new(0.0, (-2.0e-21, -2.0e-21)),
            None,
        )
    }));
    match &result {
        Ok(SampleEval::Collision { loss }) => println!(
            "exterior_probe=collision loss={loss} bits={:08x}",
            loss.to_bits()
        ),
        Ok(SampleEval::Clear { loss }) => println!("exterior_probe=clear loss={loss}"),
        Ok(SampleEval::Invalid) => println!("exterior_probe=invalid"),
        Err(_) => println!("exterior_probe=panic"),
    }

    if cfg!(debug_assertions) {
        assert!(
            result.is_err(),
            "Rust Debug must assert on subnormal container overlap"
        );
    } else {
        let SampleEval::Collision { loss } = result.expect("release evaluation must return") else {
            panic!("candidate crossing the container boundary must be a collision");
        };
        println!(
            "actual_evaluator_subnormal_container_loss={loss} bits={:08x}",
            loss.to_bits()
        );
        assert!(loss > 0.0 && loss.is_subnormal());
    }
}
