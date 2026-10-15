use jagua_rs::geometry::DTransformation;
use jagua_rs::geometry::primitives::{Point, SPolygon};
use jagua_rs::geometry::shape_modification::{ShapeModifyMode, offset_shape};
use jagua_rs::io::import::Importer;
use jagua_rs::probs::spp::entities::{SPPlacement, SPProblem};
use jagua_rs::probs::spp::io::import_instance;
use sparrow::config::DEFAULT_SPARROW_CONFIG;
use sparrow::eval::sample_eval::{SampleEval, SampleEvaluator};
use sparrow::eval::sep_evaluator::SeparationEvaluator;
use sparrow::quantify::tracker::CollisionTracker;
use std::path::Path;

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

#[test]
fn concave_inflate_contour_bits_match_reference() {
    let shape = SPolygon::new(vec![
        Point(0.0, 0.0),
        Point(6.0, 0.0),
        Point(6.0, 2.0),
        Point(2.0, 2.0),
        Point(2.0, 6.0),
        Point(0.0, 6.0),
    ])
    .expect("L-shaped concave polygon must construct");
    let offset = offset_shape(&shape, ShapeModifyMode::Inflate, 0.25)
        .expect("L-shaped polygon must inflate");

    assert_eq!(offset.vertices.len(), 86);
    assert_eq!(offset.area.to_bits(), 0x41d175db);
    assert_eq!(offset.bbox.area().to_bits(), 0x42290000);
    assert_eq!(polygon_f32_bits_hash(&offset), 0xbf324096dceae641);
}

#[test]
fn convex_deflate_contour_bits_match_reference() {
    let shape = SPolygon::new(vec![
        Point(0.0, 0.0),
        Point(4.0, 0.0),
        Point(5.0, 2.0),
        Point(2.0, 4.0),
        Point(-1.0, 2.0),
    ])
    .expect("convex pentagon must construct");
    let offset =
        offset_shape(&shape, ShapeModifyMode::Deflate, 0.3).expect("convex pentagon must deflate");

    assert_eq!(offset.vertices.len(), 5);
    assert_eq!(offset.area.to_bits(), 0x413a00b7);
    assert_eq!(offset.bbox.area().to_bits(), 0x418ba0c6);
    assert_eq!(polygon_f32_bits_hash(&offset), 0x1710858bdb9fdb5f);
}

#[test]
fn clockwise_and_collinear_offset_contour_bits_match_reference() {
    let clockwise = SPolygon::new(vec![
        Point(-1.0, 2.0),
        Point(2.0, 4.0),
        Point(5.0, 2.0),
        Point(4.0, 0.0),
        Point(0.0, 0.0),
    ])
    .expect("clockwise convex pentagon must construct");
    let clockwise_offset = offset_shape(&clockwise, ShapeModifyMode::Deflate, 0.3)
        .expect("clockwise convex pentagon must deflate");

    let collinear = SPolygon::new(vec![
        Point(0.0, 0.0),
        Point(2.0, 0.0),
        Point(4.0, 0.0),
        Point(5.0, 2.0),
        Point(2.0, 4.0),
        Point(-1.0, 2.0),
    ])
    .expect("convex polygon with a collinear vertex must construct");
    let collinear_offset = offset_shape(&collinear, ShapeModifyMode::Deflate, 0.3)
        .expect("convex polygon with a collinear vertex must deflate");

    for offset in [&clockwise_offset, &collinear_offset] {
        assert_eq!(offset.vertices.len(), 5);
        assert_eq!(offset.area.to_bits(), 0x413a00b7);
        assert_eq!(offset.bbox.area().to_bits(), 0x418ba0c6);
        assert_eq!(polygon_f32_bits_hash(offset), 0x1710858bdb9fdb5f);
    }
}

#[test]
fn offset_adapter_exponent_boundary_bits_match_reference() {
    let distance = 0.5_f32;
    let threshold = 2.0_f64.powf(12.5);
    let ideal_side = (2.0 * threshold - 2.2 * f64::from(distance)) as f32;
    let side_below = f32::from_bits(ideal_side.to_bits() - 1);
    let side_above = f32::from_bits(ideal_side.to_bits() + 1);
    for (label, side) in [("below", side_below), ("above", side_above)] {
        let shape = SPolygon::new(vec![
            Point(0.0, 0.0),
            Point(side, 0.0),
            Point(side, side),
            Point(0.0, side),
        ])
        .expect("scale-boundary square must construct");
        let offset = offset_shape(&shape, ShapeModifyMode::Inflate, distance)
            .expect("scale-boundary square must inflate");
        assert_eq!(offset.vertices.len(), 68);
        if label == "below" {
            assert_eq!(side.to_bits(), 0x4635008c);
            assert_eq!(offset.area.to_bits(), 0x4cfffedb);
            assert_eq!(offset.bbox.area().to_bits(), 0x4cfffedc);
            assert_eq!(polygon_f32_bits_hash(&offset), 0x2b698cbf3a05d425);
        } else {
            assert_eq!(side.to_bits(), 0x4635008e);
            assert_eq!(offset.area.to_bits(), 0x4cfffee1);
            assert_eq!(offset.bbox.area().to_bits(), 0x4cfffee2);
            assert_eq!(polygon_f32_bits_hash(&offset), 0x4248ff4142d7c2d5);
        }
    }
}

#[test]
fn spp_imported_offset_reaches_separation_evaluator() {
    let (external, _) =
        sparrow::util::io::read_spp_input(Path::new("../Fixtures/ShapeOffsetEvaluatorSpp.json"))
            .expect("read SPP offset/evaluator fixture");
    let config = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        config.cde_config,
        config.poly_simpl_tolerance,
        config.narrow_concavity_cutoff_ratio,
    );
    let mut instance =
        import_instance(&importer, &external).expect("import SPP offset/evaluator fixture");
    instance.base_strip.width = 16.0;
    let mut problem = SPProblem::new(instance).expect("create SPP problem");
    let transform = DTransformation::new(0.0, (5.0, 4.0));
    let first_key = problem.place_item(SPPlacement {
        item_idx: 0,
        d_transf: transform,
    });
    let moving_key = problem.place_item(SPPlacement {
        item_idx: 1,
        d_transf: transform,
    });

    let shape = problem.instance.item(1).shape_cd.as_ref();
    let tracker = CollisionTracker::new(&problem.layout);
    let mut evaluator = SeparationEvaluator::new(
        &problem.layout,
        problem.instance.item(1),
        moving_key,
        &tracker,
    );
    let evaluation = evaluator.evaluate_sample(transform, None);
    let evaluation_loss_bits = match evaluation {
        SampleEval::Collision { loss } => loss.to_bits(),
        other => panic!("identical placed items must collide: {other:?}"),
    };
    assert_eq!(shape.vertices.len(), 10);
    assert_eq!(shape.area.to_bits(), 0x41d1a88c);
    assert_eq!(shape.bbox.area().to_bits(), 0x42290000);
    assert_eq!(polygon_f32_bits_hash(shape), 0x1bed85a4ace8dc97);
    assert_eq!(evaluation_loss_bits, 0x4262ee64);
    assert_eq!(
        tracker.get_pair_loss(first_key, moving_key).to_bits(),
        0x4221ca2f
    );
}
