use jagua_rs::geometry::fail_fast::SPSurrogateConfig;
use jagua_rs::geometry::primitives::{Point, Rect, SPolygon};
use sparrow::quantify::quantify_collision_poly_container;
use std::panic::{AssertUnwindSafe, catch_unwind};

fn square_with_surrogate() -> SPolygon {
    let mut shape = SPolygon::new(vec![
        Point(-1.0, -1.0),
        Point(1.0, -1.0),
        Point(1.0, 1.0),
        Point(-1.0, 1.0),
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
fn disjoint_container_loss_uses_rust_midpoint_when_f32_endpoint_sum_overflows() {
    let x0 = f32::from_bits(0x7f00_0000);
    let x1 = f32::from_bits(0x7f00_0001);
    let x2 = f32::from_bits(0x7f00_0002);
    let x3 = f32::from_bits(0x7f00_0003);
    let shape_bounds = Rect::try_new(x0, 0.0, x1, 1.0).unwrap();
    let container_bounds = Rect::try_new(x2, 0.0, x3, 1.0).unwrap();

    assert_eq!(shape_bounds.centroid().0.to_bits(), 0x7f00_0000);
    assert_eq!(container_bounds.centroid().0.to_bits(), 0x7f00_0002);

    let mut shape = square_with_surrogate();
    // This low-level fixture changes only the public bbox to isolate the loss formula.
    shape.bbox = shape_bounds;
    let result = catch_unwind(AssertUnwindSafe(|| {
        quantify_collision_poly_container(&shape, container_bounds)
    }));

    if cfg!(debug_assertions) {
        assert!(result.is_err(), "Debug must assert on the infinite overlap");
    } else {
        let loss = result.expect("Release must preserve IEEE container-loss arithmetic");
        assert_eq!(loss.to_bits(), 0x7f80_0000);
    }
}
