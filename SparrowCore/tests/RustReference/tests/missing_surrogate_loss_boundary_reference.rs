use jagua_rs::geometry::fail_fast::SPSurrogateConfig;
use jagua_rs::geometry::primitives::{Point, Rect, SPolygon};
use sparrow::quantify::{quantify_collision_poly_container, quantify_collision_poly_poly};
use std::panic::{AssertUnwindSafe, catch_unwind};

fn is_missing_surrogate_panic(result: Result<f32, Box<dyn std::any::Any + Send>>) -> bool {
    let Err(payload) = result else {
        return false;
    };
    let message = payload
        .downcast_ref::<String>()
        .map(String::as_str)
        .or_else(|| payload.downcast_ref::<&str>().copied());

    message.is_some_and(|message| message.contains("surrogate not generated"))
}

fn square(generate_surrogate: bool) -> SPolygon {
    let mut shape = SPolygon::new(vec![
        Point(-1.0, -1.0),
        Point(1.0, -1.0),
        Point(1.0, 1.0),
        Point(-1.0, 1.0),
    ])
    .expect("finite square must construct");

    if generate_surrogate {
        shape
            .generate_surrogate(SPSurrogateConfig {
                n_pole_limits: [(2, 0.0), (2, 0.8), (2, 0.9)],
                ff_pole_area_ratio: 0.0,
                n_ff_piers: 0,
            })
            .expect("square surrogate must construct");
    }

    shape
}

#[test]
fn missing_surrogate_panics_at_pair_and_container_loss_boundaries() {
    let missing = square(false);
    let present = square(true);
    let container = Rect::try_new(-2.0, -2.0, 2.0, 2.0).unwrap();

    assert!(missing.surrogate.is_none());
    assert!(present.surrogate.is_some());

    let missing_first = catch_unwind(AssertUnwindSafe(|| {
        quantify_collision_poly_poly(&missing, &present)
    }));
    let missing_second = catch_unwind(AssertUnwindSafe(|| {
        quantify_collision_poly_poly(&present, &missing)
    }));
    let missing_container = catch_unwind(AssertUnwindSafe(|| {
        quantify_collision_poly_container(&missing, container)
    }));

    assert!(
        is_missing_surrogate_panic(missing_first),
        "missing first surrogate must reach its expect panic"
    );
    assert!(
        is_missing_surrogate_panic(missing_second),
        "missing second surrogate must reach its expect panic"
    );
    assert!(
        is_missing_surrogate_panic(missing_container),
        "container loss must reach its surrogate expect panic"
    );
    assert!(
        missing.surrogate.is_none(),
        "failed calls must not generate a surrogate"
    );
    assert!(
        present.surrogate.is_some(),
        "failed calls must not alter the valid surrogate"
    );
}
