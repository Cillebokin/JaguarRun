use jagua_rs::entities::{Container, Layout};
use jagua_rs::geometry::fail_fast::SPSurrogateConfig;
use jagua_rs::geometry::geo_traits::Transformable;
use jagua_rs::geometry::primitives::{Point, Rect, SPolygon};
use jagua_rs::geometry::shape_modification::{ShapeModifyConfig, ShapeModifyMode};
use jagua_rs::geometry::{DTransformation, OriginalShape};
use jagua_rs::io::ext_repr::{ExtItem, ExtOrientation, ExtRotation, ExtSPolygon, ExtShape};
use jagua_rs::io::import::Importer;
use sparrow::config::DEFAULT_SPARROW_CONFIG;
use sparrow::eval::sample_eval::{SampleEval, SampleEvaluator};
use sparrow::eval::sep_evaluator::SeparationEvaluator;
use sparrow::quantify::quantify_collision_poly_container;
use sparrow::quantify::tracker::CollisionTracker;
use std::panic::{AssertUnwindSafe, catch_unwind};

fn overflow_square_points() -> (f32, Vec<Point>) {
    let bbox_area = f32::MAX * 0.49999;
    let side = bbox_area.sqrt();
    let half_side = side * 0.5;
    (
        bbox_area,
        vec![
            Point(-half_side, -half_side),
            Point(half_side, -half_side),
            Point(half_side, half_side),
            Point(-half_side, half_side),
        ],
    )
}

#[test]
fn direct_polygon_constructor_reaches_container_loss_final_multiply_overflow() {
    let (bbox_area, points) = overflow_square_points();
    let mut shape = SPolygon::new(points).expect("finite square polygon must construct");
    assert!(shape.diameter.is_finite());
    assert!(!shape.centroid().0.is_finite() || !shape.centroid().1.is_finite());
    shape
        .generate_surrogate(SPSurrogateConfig {
            n_pole_limits: [(1, 0.0), (1, 0.8), (1, 0.9)],
            ff_pole_area_ratio: 0.0,
            n_ff_piers: 0,
        })
        .expect("finite square surrogate must construct");

    let overlap = (shape.bbox.area() - 4.0) + 0.0001 * shape.bbox.area();
    let penalty = sparrow::quantify::calc_shape_penalty(&shape, &shape);
    let first_product = 2.0 * overlap.sqrt();
    assert!(overlap.is_finite() && overlap.is_normal());
    assert!(penalty.is_finite());
    assert!(first_product.is_finite());
    assert_eq!(shape.bbox.area().to_bits(), 0x7efffeaf);
    assert_eq!(overlap.to_bits(), 0x7f00029e);
    assert_eq!(penalty.to_bits(), 0x5f35047c);
    assert_eq!(first_product.to_bits(), 0x5fb506cd);
    assert!((first_product * penalty).is_infinite());

    let loss =
        quantify_collision_poly_container(&shape, Rect::try_new(-1.0, -1.0, 1.0, 1.0).unwrap());
    assert_eq!(loss, f32::INFINITY);
    println!(
        "bbox_area={bbox_area} bits={:08x}; overlap={overlap} bits={:08x}; penalty={penalty} bits={:08x}; first_product={first_product} bits={:08x}; final_loss=+Inf",
        shape.bbox.area().to_bits(),
        overlap.to_bits(),
        penalty.to_bits(),
        first_product.to_bits(),
    );
}

#[test]
fn external_item_import_rejects_overflow_square_before_evaluation() {
    let (_, points) = overflow_square_points();
    let ext_item = ExtItem {
        id: 1,
        orientation: ExtOrientation {
            rotation: ExtRotation::Continuous {},
        },
        shape: ExtShape::SimplePolygon(ExtSPolygon(
            points.iter().map(|point| (point.0, point.1)).collect(),
        )),
        min_quality: None,
    };
    let config = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        config.cde_config,
        config.poly_simpl_tolerance,
        config.narrow_concavity_cutoff_ratio,
    );
    let import_outcome = catch_unwind(AssertUnwindSafe(|| importer.import_item(&ext_item, 0)));
    assert!(
        !matches!(import_outcome, Ok(Ok(_))),
        "the non-finite centroid must prevent an external item from reaching S12"
    );
}

#[test]
fn finite_item_separation_offset_reaches_container_loss_final_multiply_overflow() {
    let config = DEFAULT_SPARROW_CONFIG;
    let radius = (f32::MAX * 0.15).sqrt();
    let separation = radius * 2.0;
    let mut cde_config = config.cde_config;
    cde_config.item_surrogate_config.n_pole_limits = [(2, 0.0), (2, 0.8), (2, 0.9)];
    let importer = Importer::new(
        cde_config,
        config.poly_simpl_tolerance,
        config.narrow_concavity_cutoff_ratio,
    )
    .with_min_item_separation(separation)
    .expect("finite minimum item separation must be accepted");
    let item = importer
        .import_item(
            &ExtItem {
                id: 2,
                orientation: ExtOrientation {
                    rotation: ExtRotation::Continuous {},
                },
                shape: ExtShape::Rectangle {
                    x_min: -5.0e11,
                    y_min: -5.0e11,
                    width: 1.0e12,
                    height: 1.0e12,
                },
                min_quality: None,
            },
            0,
        )
        .expect("finite offset must import as an item");
    let shape = item.shape_cd.as_ref();
    let overlap = (shape.bbox.area() - 4.0) + 0.0001 * shape.bbox.area();
    let penalty = sparrow::quantify::calc_shape_penalty(shape, shape);
    let first_product = 2.0 * overlap.sqrt();
    let loss =
        quantify_collision_poly_container(shape, Rect::try_new(-1.0, -1.0, 1.0, 1.0).unwrap());

    assert!(separation.is_finite());
    assert!(shape.bbox.area().is_finite() && shape.bbox.area() > f32::MAX * 0.5);
    assert!(shape.area.is_finite());
    assert!(shape.surrogate().convex_hull_area.is_finite());
    assert!(overlap.is_finite() && overlap.is_normal());
    assert!(penalty.is_finite());
    assert!(first_product.is_finite());
    assert!((first_product * penalty).is_infinite());
    assert_eq!(loss, f32::INFINITY);
    println!(
        "min_item_separation={separation}; bbox_area={} bits={:08x}; shape_area={} bits={:08x}; hull_area={} bits={:08x}; overlap={} bits={:08x}; penalty={} bits={:08x}; first_product={} bits={:08x}; final_loss=+Inf",
        shape.bbox.area(),
        shape.bbox.area().to_bits(),
        shape.area,
        shape.area.to_bits(),
        shape.surrogate().convex_hull_area,
        shape.surrogate().convex_hull_area.to_bits(),
        overlap,
        overlap.to_bits(),
        penalty,
        penalty.to_bits(),
        first_product,
        first_product.to_bits(),
    );
}

#[test]
fn finite_imported_item_reaches_container_loss_final_multiply_overflow() {
    let config = DEFAULT_SPARROW_CONFIG;
    let radius = (f32::MAX * 0.15).sqrt();
    let separation = radius * 2.0;
    let mut cde_config = config.cde_config;
    cde_config.quadtree_depth = 0;
    cde_config.item_surrogate_config.n_pole_limits = [(2, 0.0), (2, 0.8), (2, 0.9)];
    let importer = Importer::new(
        cde_config,
        config.poly_simpl_tolerance,
        config.narrow_concavity_cutoff_ratio,
    )
    .with_min_item_separation(separation)
    .expect("finite minimum item separation must be accepted");
    let item = importer
        .import_item(
            &ExtItem {
                id: 3,
                orientation: ExtOrientation {
                    rotation: ExtRotation::Continuous {},
                },
                shape: ExtShape::Rectangle {
                    x_min: -5.0e11,
                    y_min: -5.0e11,
                    width: 1.0e12,
                    height: 1.0e12,
                },
                min_quality: None,
            },
            0,
        )
        .expect("finite offset must import as an item");
    let container_shape = SPolygon::from(Rect::try_new(0.0, 0.0, 2.0, 2.0).unwrap());
    let container = Container::new(
        OriginalShape {
            shape: container_shape,
            pre_transform: DTransformation::new(0.0, (0.0, 0.0)),
            modify_mode: ShapeModifyMode::Deflate,
            modify_config: ShapeModifyConfig::default(),
        },
        vec![],
        cde_config,
    )
    .expect("small CDE container must construct");
    let mut layout = Layout::new(container);
    let placement = DTransformation::new(0.0, (1.0, 1.0));
    let moving_key = layout.place_item(&item, placement);
    let item_shape = item.shape_cd.as_ref();
    let candidate_shape = item_shape.transform_clone(&placement.compose());
    let container_bbox = layout.container.outer_cd.bbox;
    let intersection = Rect::intersection(candidate_shape.bbox, container_bbox)
        .expect("candidate bbox must intersect the container bbox");
    let overlap =
        (candidate_shape.bbox.area() - intersection.area()) + 0.0001 * candidate_shape.bbox.area();
    let penalty = sparrow::quantify::calc_shape_penalty(&candidate_shape, &candidate_shape);
    let first_product = 2.0 * overlap.sqrt();
    let formula_loss = quantify_collision_poly_container(&candidate_shape, container_bbox);
    let tracker = CollisionTracker::new(&layout);
    let mut evaluator = SeparationEvaluator::new(&layout, &item, moving_key, &tracker);
    let evaluation = evaluator.evaluate_sample(placement, None);

    assert!(separation.is_finite());
    assert!(item_shape.area.is_finite() && item_shape.bbox.area().is_finite());
    assert!(!item_shape.centroid().0.is_finite() || !item_shape.centroid().1.is_finite());
    assert!(item_shape.bbox.area() > f32::MAX * 0.5);
    assert!(overlap.is_finite() && overlap.is_normal());
    assert!(penalty.is_finite() && first_product.is_finite());
    assert_eq!(item_shape.bbox.area().to_bits(), 0x7f19999a);
    assert_eq!(item_shape.area.to_bits(), 0x7ef12454);
    assert_eq!(overlap.to_bits(), 0x7f199d89);
    assert_eq!(penalty.to_bits(), 0x5f2fb00c);
    assert_eq!(first_product.to_bits(), 0x5fc64e82);
    assert!((first_product * penalty).is_infinite());
    assert_eq!(formula_loss, f32::INFINITY);
    assert_eq!(
        evaluation,
        SampleEval::Collision {
            loss: f32::INFINITY
        }
    );
    println!(
        "min_item_separation={separation}; manual_container=2x2; cde_quadtree_depth=0; bbox_area={} bits={:08x}; shape_area={} bits={:08x}; overlap={} bits={:08x}; penalty={} bits={:08x}; first_product={} bits={:08x}; evaluator=Collision(+Inf)",
        item_shape.bbox.area(),
        item_shape.bbox.area().to_bits(),
        item_shape.area,
        item_shape.area.to_bits(),
        overlap,
        overlap.to_bits(),
        penalty,
        penalty.to_bits(),
        first_product,
        first_product.to_bits(),
    );
}
