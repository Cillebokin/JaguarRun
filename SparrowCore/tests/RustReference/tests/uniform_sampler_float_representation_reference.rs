use jagua_rs::entities::Item;
use jagua_rs::geometry::fail_fast::SPSurrogateConfig;
use jagua_rs::geometry::geo_enums::RotationRange;
use jagua_rs::geometry::primitives::{Point, Rect, SPolygon};
use jagua_rs::geometry::shape_modification::{ShapeModifyConfig, ShapeModifyMode};
use jagua_rs::geometry::{DTransformation, OriginalShape};
use rand::SeedableRng;
use rand::rngs::Xoshiro256PlusPlus;
use sparrow::sample::uniform_sampler::UniformBBoxSampler;

fn origin_item() -> Item {
    let shape = SPolygon::new(vec![
        Point(0.0, 0.0),
        Point(2.0, 0.0),
        Point(2.0, 1.0),
        Point(0.0, 1.0),
    ])
    .expect("valid origin-based rectangle");
    Item::new(
        0,
        1001,
        OriginalShape {
            shape,
            pre_transform: DTransformation::empty(),
            modify_mode: ShapeModifyMode::Inflate,
            modify_config: ShapeModifyConfig::default(),
        },
        RotationRange::None,
        None,
        SPSurrogateConfig::none(),
    )
    .expect("valid sampler item")
}

#[test]
fn signed_zero_singleton_intersection_uses_rust_max_semantics() {
    let item = origin_item();
    assert_eq!(item.shape_cd.bbox.x_min.to_bits(), 0x0000_0000);
    let sample_bbox = Rect {
        x_min: 0.0,
        y_min: 0.0,
        x_max: 0.0,
        y_max: 5.0,
    };
    let container_bbox = Rect {
        x_min: -0.0,
        y_min: 0.0,
        x_max: 10.0,
        y_max: 10.0,
    };
    let sampler = UniformBBoxSampler::new(sample_bbox, &item, container_bbox)
        .expect("singleton origin candidate must be feasible");
    let mut rng = Xoshiro256PlusPlus::seed_from_u64(42);
    let (x, _) = sampler.sample(&mut rng).translation();
    assert_eq!(x.to_bits(), 0x0000_0000);
}
