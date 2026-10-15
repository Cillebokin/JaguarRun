use jagua_rs::geometry::DTransformation;
use jagua_rs::geometry::fail_fast::SPSurrogateConfig;
use jagua_rs::geometry::geo_traits::Transformable;
use jagua_rs::geometry::primitives::{Point, Rect, SPolygon};
use sparrow::quantify::quantify_collision_poly_container;
use std::panic::{AssertUnwindSafe, catch_unwind};

fn finite_large_square() -> SPolygon {
    let side = 1.2e19_f32;
    let half = side * 0.5;
    let mut shape = SPolygon::new(vec![
        Point(-half, -half),
        Point(half, -half),
        Point(half, half),
        Point(-half, half),
    ])
    .expect("finite source square must construct");
    shape
        .generate_surrogate(SPSurrogateConfig {
            n_pole_limits: [(2, 0.0), (2, 0.8), (2, 0.9)],
            ff_pole_area_ratio: 0.0,
            n_ff_piers: 0,
        })
        .expect("finite source square surrogate must construct");
    shape
}

#[test]
fn finite_candidate_translation_can_overflow_bbox_area_in_container_loss() {
    let shape = finite_large_square();
    assert_eq!(shape.area.to_bits(), 0x7ed8_aac7);
    assert_eq!(shape.diameter.to_bits(), 0x5f6b_838c);

    let translation = 1.5_f32 * 2.0_f32.powi(86);
    assert_eq!(translation.to_bits(), 0x6ac0_0000);
    let candidate =
        shape.transform_clone(&DTransformation::new(0.0, (translation, translation)).compose());
    assert_eq!(candidate.bbox.width().to_bits(), 0x5f80_0000);
    assert_eq!(candidate.bbox.height().to_bits(), 0x5f80_0000);
    assert_eq!(candidate.bbox.area(), f32::INFINITY);

    let container = Rect::try_new(-1.0, -1.0, 1.0, 1.0).unwrap();
    let loss = catch_unwind(AssertUnwindSafe(|| {
        quantify_collision_poly_container(&candidate, container)
    }));
    if cfg!(debug_assertions) {
        assert!(
            loss.is_err(),
            "Debug must assert on infinite container overlap"
        );
    } else {
        let loss = loss.expect("Release must retain IEEE container-loss arithmetic");
        assert_eq!(loss, f32::INFINITY);
        assert_eq!(loss.to_bits(), 0x7f80_0000);
    }
}
