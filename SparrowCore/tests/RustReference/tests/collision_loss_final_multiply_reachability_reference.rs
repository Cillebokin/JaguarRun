use jagua_rs::entities::{Container, Layout};
use jagua_rs::geometry::fail_fast::SPSurrogateConfig;
use jagua_rs::geometry::geo_traits::Transformable;
use jagua_rs::geometry::primitives::{Point, Rect, SPolygon};
use jagua_rs::geometry::shape_modification::{ShapeModifyConfig, ShapeModifyMode, offset_shape};
use jagua_rs::geometry::{DTransformation, OriginalShape};
use jagua_rs::io::ext_repr::{ExtItem, ExtOrientation, ExtRotation, ExtSPolygon, ExtShape};
use jagua_rs::io::import::Importer;
use sparrow::config::DEFAULT_SPARROW_CONFIG;
use sparrow::eval::sample_eval::{SampleEval, SampleEvaluator};
use sparrow::eval::sep_evaluator::SeparationEvaluator;
use sparrow::quantify::quantify_collision_poly_container;
use sparrow::quantify::tracker::CollisionTracker;
use std::panic::{AssertUnwindSafe, catch_unwind};

fn polygon_f32_bits_hash(polygon: &SPolygon) -> u64 {
    let mut hash = 0xcbf29ce484222325_u64;
    for Point(x, y) in &polygon.vertices {
        for byte in x
            .to_bits()
            .to_le_bytes()
            .into_iter()
            .chain(y.to_bits().to_le_bytes())
        {
            hash ^= u64::from(byte);
            hash = hash.wrapping_mul(0x100000001b3);
        }
    }
    hash
}

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
fn strict_convex_inflate_contour_order_matches_reference() {
    let probe_shape = SPolygon::new(vec![
        Point(-3.0, -2.0),
        Point(8.0, -1.0),
        Point(7.0, 6.0),
        Point(-4.0, 5.0),
    ])
    .expect("ordinary convex probe shape must construct");
    let probe_offset = offset_shape(&probe_shape, ShapeModifyMode::Inflate, 1.25)
        .expect("ordinary convex probe offset must succeed");
    assert_eq!(probe_offset.vertices.len(), 66);
    assert_eq!(probe_offset.area.to_bits(), 0x43003100);
    assert_eq!(probe_offset.bbox.area().to_bits(), 0x431839f6);
    assert_eq!(polygon_f32_bits_hash(&probe_offset), 0x3eeb7f64793c4728);

    let rotated_probe = SPolygon::new(vec![
        Point(8.0, -1.0),
        Point(7.0, 6.0),
        Point(-4.0, 5.0),
        Point(-3.0, -2.0),
    ])
    .expect("rotated ordinary convex probe shape must construct");
    let rotated_offset = offset_shape(&rotated_probe, ShapeModifyMode::Inflate, 1.25)
        .expect("rotated ordinary convex probe offset must succeed");
    assert_eq!(rotated_offset.vertices.len(), 66);
    assert_eq!(rotated_offset.area.to_bits(), 0x43003100);
    assert_eq!(rotated_offset.bbox.area().to_bits(), 0x431839f6);
    assert_eq!(polygon_f32_bits_hash(&rotated_offset), 0x3eeb7f64793c4728);

    let other_probe = SPolygon::new(vec![
        Point(-2.0, -3.0),
        Point(9.0, -1.0),
        Point(6.0, 7.0),
        Point(-5.0, 4.0),
    ])
    .expect("second convex probe shape must construct");
    let other_offset = offset_shape(&other_probe, ShapeModifyMode::Inflate, 1.25)
        .expect("second convex probe offset must succeed");
    assert_eq!(other_offset.vertices.len(), 66);
    assert_eq!(other_offset.area.to_bits(), 0x430f53e0);
    assert_eq!(other_offset.bbox.area().to_bits(), 0x434e36c0);
    assert_eq!(polygon_f32_bits_hash(&other_offset), 0xc89c2d67a1a13ff5);

    let large_square = SPolygon::new(vec![
        Point(1_000_000.0, 1_000_000.0),
        Point(2_000_000.0, 1_000_000.0),
        Point(2_000_000.0, 2_000_000.0),
        Point(1_000_000.0, 2_000_000.0),
    ])
    .expect("large square must construct");
    let sub_ulp_offset = offset_shape(&large_square, ShapeModifyMode::Inflate, 0.01)
        .expect("sub-ULP offset must succeed");
    assert_eq!(sub_ulp_offset.vertices.len(), 4);
    assert_eq!(
        sub_ulp_offset.vertices,
        vec![
            Point(1_000_000.0, 2_000_000.0),
            Point(1_000_000.0, 1_000_000.0),
            Point(2_000_000.0, 1_000_000.0),
            Point(2_000_000.0, 2_000_000.0),
        ]
    );
    assert_eq!(sub_ulp_offset.area.to_bits(), 0x5368d4a5);
    assert_eq!(sub_ulp_offset.bbox.area().to_bits(), 0x5368d4a5);
    assert_eq!(polygon_f32_bits_hash(&sub_ulp_offset), 0x30b539c7ccd489c5);
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
    let original = item.shape_orig.as_ref();
    assert_eq!(original.shape.vertices.len(), 4);
    assert_eq!(polygon_f32_bits_hash(&original.shape), 0x08cf09db1f0044d5);
    let transformed = original
        .shape
        .transform_clone(&original.pre_transform.compose());
    assert_eq!(transformed.vertices.len(), 4);
    assert_eq!(polygon_f32_bits_hash(&transformed), 0x08cf09db1f0044d5);
    let offset = offset_shape(
        &transformed,
        original.modify_mode,
        original.modify_config.offset.expect("offset must be set"),
    )
    .expect("Rust offset stage must succeed");
    assert_eq!(offset.vertices.len(), 68);
    assert_eq!(offset.area.to_bits(), 0x7ef0e310);
    assert_eq!(offset.bbox.area().to_bits(), 0x7f19999a);
    assert_eq!(polygon_f32_bits_hash(&offset), 0xd2af3ad80f009765);
    let converted = original
        .convert_to_internal()
        .expect("Rust internal conversion must succeed");
    assert_eq!(converted.vertices.len(), 50);
    assert_eq!(converted.area.to_bits(), 0x7ef12454);
    assert_eq!(converted.bbox.area().to_bits(), 0x7f19999a);
    assert_eq!(polygon_f32_bits_hash(&converted), 0x43ff76136e8088d1);
    assert_eq!(
        polygon_f32_bits_hash(item.shape_cd.as_ref()),
        0x43ff76136e8088d1
    );
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
