use jagua_rs::collision_detection::hazards::HazardEntity;
use jagua_rs::entities::Layout;
use jagua_rs::geometry::DTransformation;
use jagua_rs::geometry::geo_traits::TransformableFrom;
use jagua_rs::io::ext_repr::{
    ExtContainer, ExtItem, ExtOrientation, ExtPolygon, ExtQualityZone, ExtRotation, ExtSPolygon,
    ExtShape,
};
use jagua_rs::io::import::Importer;
use rand::Rng;
use rand::SeedableRng;
use rand::rngs::Xoshiro256PlusPlus;
use sparrow::config::DEFAULT_SPARROW_CONFIG;
use sparrow::eval::sample_eval::{SampleEval, SampleEvaluator};
use sparrow::eval::sep_evaluator::SeparationEvaluator;
use sparrow::quantify::tracker::CollisionTracker;
use sparrow::sample::search::{
    SampleConfig, SearchTraceEvent, SearchTracePhase, search_placement_with_trace,
};
use sparrow::sample::uniform_sampler::UniformBBoxSampler;
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

#[test]
fn tracker_initialization_panics_on_hole_and_inferior_quality_hazards() {
    for transform in [
        DTransformation::new(0.0, (9.0, 9.0)),
        DTransformation::new(0.0, (1.5, 1.5)),
    ] {
        let (mut layout, item, _, _) = build_case();
        layout.place_item(&item, transform);
        let result = catch_unwind(AssertUnwindSafe(|| CollisionTracker::new(&layout)));
        assert!(
            result.is_err(),
            "Tracker::new must panic on unsupported hazards"
        );
    }
}

#[test]
fn tracker_move_preserves_rust_partial_state_when_hole_dispatch_panics() {
    let (mut layout, item, mut tracker, old_key) = build_case();
    tracker.container_collisions[0].loss = 7.0;
    let _removed = layout.remove_item(old_key);
    let new_key = layout.place_item(&item, DTransformation::new(0.0, (9.0, 9.0)));

    let result = catch_unwind(AssertUnwindSafe(|| {
        tracker.register_item_move(&layout, old_key, new_key)
    }));
    assert!(
        result.is_err(),
        "moved Tracker item must panic on a hole hazard"
    );
    assert!(tracker.pk_idx_map.contains_key(new_key));
    assert!(!tracker.pk_idx_map.contains_key(old_key));
    assert_eq!(
        tracker.get_container_loss(new_key).to_bits(),
        0.0_f32.to_bits()
    );
    assert_eq!(tracker.get_total_loss().to_bits(), 0.0_f32.to_bits());
}

#[test]
fn unsupported_hazard_unwinds_from_worker_search_before_candidate_callback() {
    let defaults = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        defaults.cde_config,
        defaults.poly_simpl_tolerance,
        defaults.narrow_concavity_cutoff_ratio,
    );
    let outer = || {
        ExtShape::Polygon(ExtPolygon {
            outer: ExtSPolygon(vec![(0.0, 0.0), (20.0, 0.0), (20.0, 20.0), (0.0, 20.0)]),
            inner: Vec::new(),
        })
    };
    let item = importer
        .import_item(
            &ExtItem {
                id: 9701,
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
        .expect("import worker propagation item");
    let sample_container = importer
        .import_container(&ExtContainer {
            id: 9700,
            shape: outer(),
            zones: Vec::new(),
        })
        .expect("import worker propagation sample container");
    let sample_bbox = sample_container.outer_cd.bbox;
    let sampler = UniformBBoxSampler::new(sample_bbox, &item, sample_bbox)
        .expect("worker propagation sample must fit");
    let mut expected_rng = Xoshiro256PlusPlus::seed_from_u64(42);
    let candidate = sampler.sample(&mut expected_rng);
    let expected_rng_after_sample = expected_rng.next_u64();

    let mut candidate_shape = item.shape_cd.as_ref().clone();
    candidate_shape.transform_from(item.shape_cd.as_ref(), &candidate.into());
    let candidate_bbox = candidate_shape.bbox;
    let zone = ExtQualityZone {
        quality: 1,
        shape: ExtShape::Rectangle {
            x_min: candidate_bbox.x_min,
            y_min: candidate_bbox.y_min,
            width: candidate_bbox.x_max - candidate_bbox.x_min,
            height: candidate_bbox.y_max - candidate_bbox.y_min,
        },
    };
    let container = importer
        .import_container(&ExtContainer {
            id: 9702,
            shape: outer(),
            zones: vec![zone],
        })
        .expect("import worker propagation hazard container");
    let candidate_position = candidate.translation();
    let current_position = [
        (2.0_f32, 2.0_f32),
        (18.0_f32, 2.0_f32),
        (2.0_f32, 18.0_f32),
        (18.0_f32, 18.0_f32),
    ]
    .into_iter()
    .max_by(|left, right| {
        let distance = |position: &(f32, f32)| {
            (position.0 - candidate_position.0).powi(2)
                + (position.1 - candidate_position.1).powi(2)
        };
        distance(left).total_cmp(&distance(right))
    })
    .expect("candidate must have a far container corner");
    let mut layout = Layout::new(container);
    let moving_key = layout.place_item(&item, DTransformation::new(0.0, current_position));
    let tracker = CollisionTracker::new(&layout);
    let before_transform = layout.placed_items[moving_key].d_transf;
    let before_loss_bits = tracker.get_total_loss().to_bits();
    assert_eq!(before_loss_bits, 0.0_f32.to_bits());
    let mut rng = Xoshiro256PlusPlus::seed_from_u64(42);
    let mut events = Vec::new();
    let result = catch_unwind(AssertUnwindSafe(|| {
        search_placement_with_trace(
            &layout,
            &item,
            Some(moving_key),
            SeparationEvaluator::new(&layout, &item, moving_key, &tracker),
            SampleConfig {
                n_container_samples: 1,
                n_focussed_samples: 0,
                n_coord_descents: 1,
            },
            &mut rng,
            &mut |event| events.push(event),
        )
    }));

    assert!(
        result.is_err(),
        "unsupported hazard must unwind the worker search"
    );
    assert_eq!(rng.next_u64(), expected_rng_after_sample);
    assert_eq!(layout.placed_items[moving_key].d_transf, before_transform);
    assert_eq!(tracker.get_total_loss().to_bits(), before_loss_bits);
    assert!(matches!(
        events.as_slice(),
        [
            SearchTraceEvent::Evaluation {
                phase: SearchTracePhase::ReferencePlacement,
                evaluation: SampleEval::Clear { .. },
                ..
            },
            SearchTraceEvent::Report {
                phase: SearchTracePhase::ReferencePlacement,
                ..
            }
        ]
    ));
}
