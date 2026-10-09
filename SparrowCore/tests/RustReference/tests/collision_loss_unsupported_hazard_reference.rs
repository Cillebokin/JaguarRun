use jagua_rs::collision_detection::hazards::HazardEntity;
use jagua_rs::entities::Layout;
use jagua_rs::geometry::DTransformation;
use jagua_rs::geometry::geo_traits::TransformableFrom;
use jagua_rs::io::ext_repr::{
    ExtContainer, ExtItem, ExtOrientation, ExtPolygon, ExtQualityZone, ExtRotation, ExtSPolygon,
    ExtShape,
};
use jagua_rs::io::import::Importer;
use sparrow::config::DEFAULT_SPARROW_CONFIG;
use sparrow::eval::sample_eval::SampleEvaluator;
use sparrow::eval::sep_evaluator::SeparationEvaluator;
use sparrow::quantify::tracker::CollisionTracker;
use std::panic::{AssertUnwindSafe, catch_unwind};

fn build_case() -> (
    Layout,
    std::sync::Arc<jagua_rs::entities::Item>,
    CollisionTracker,
    jagua_rs::entities::PItemKey,
) {
    let config = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        config.cde_config,
        config.poly_simpl_tolerance,
        config.narrow_concavity_cutoff_ratio,
    );
    let container = importer
        .import_container(&ExtContainer {
            id: 9600,
            shape: ExtShape::Polygon(ExtPolygon {
                outer: ExtSPolygon(vec![(0.0, 0.0), (20.0, 0.0), (20.0, 20.0), (0.0, 20.0)]),
                inner: vec![ExtSPolygon(vec![
                    (8.0, 8.0),
                    (10.0, 8.0),
                    (10.0, 10.0),
                    (8.0, 10.0),
                ])],
            }),
            zones: vec![ExtQualityZone {
                quality: 1,
                shape: ExtShape::Rectangle {
                    x_min: 0.0,
                    y_min: 0.0,
                    width: 3.0,
                    height: 3.0,
                },
            }],
        })
        .expect("import unsupported-hazard reference container");
    let item = importer
        .import_item(
            &ExtItem {
                id: 9601,
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
            0,
        )
        .expect("import unsupported-hazard reference item");
    let mut layout = Layout::new(container);
    let moving_key = layout.place_item(&item, DTransformation::new(0.0, (12.5, 12.5)));
    let tracker = CollisionTracker::new(&layout);
    (layout, item, tracker, moving_key)
}

#[derive(Clone, Copy)]
enum ExpectedHazard {
    Hole,
    InferiorQualityZone,
}

fn assert_unsupported_hazard_panics(transform: DTransformation, expected: ExpectedHazard) {
    let (layout, item, tracker, moving_key) = build_case();
    let mut collector =
        jagua_rs::collision_detection::hazards::collector::BasicHazardCollector::new();
    let mut candidate = item.shape_cd.as_ref().clone();
    candidate.transform_from(item.shape_cd.as_ref(), &transform.compose());
    let mut found_expected = false;
    let _ = layout
        .cde()
        .collect_poly_collisions_until(&candidate, &mut collector, |hazard| {
            found_expected |= match (expected, hazard) {
                (ExpectedHazard::Hole, HazardEntity::Hole { .. }) => true,
                (ExpectedHazard::InferiorQualityZone, HazardEntity::InferiorQualityZone { .. }) => {
                    true
                }
                _ => false,
            };
            false
        });
    assert!(
        found_expected,
        "reference candidate must hit the expected hazard"
    );

    let mut evaluator = SeparationEvaluator::new(&layout, &item, moving_key, &tracker);
    let result = catch_unwind(AssertUnwindSafe(|| {
        evaluator.evaluate_sample(transform, None)
    }));
    assert!(result.is_err(), "unsupported hazard must panic in Rust");
}

#[test]
fn hole_hazard_panics_in_collision_loss_evaluation() {
    assert_unsupported_hazard_panics(DTransformation::new(0.0, (9.0, 9.0)), ExpectedHazard::Hole);
}

#[test]
fn inferior_quality_zone_hazard_panics_in_collision_loss_evaluation() {
    assert_unsupported_hazard_panics(
        DTransformation::new(0.0, (1.5, 1.5)),
        ExpectedHazard::InferiorQualityZone,
    );
}
