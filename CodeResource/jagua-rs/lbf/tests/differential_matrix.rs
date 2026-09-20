use jagua_rs::collision_detection::hazards::filter::{HazKeyFilter, NoFilter};
use jagua_rs::collision_detection::hazards::{Hazard, HazardEntity};
use jagua_rs::collision_detection::{CDEConfig, CDEngine};
use jagua_rs::geometry::geo_traits::CollidesWith;
use jagua_rs::geometry::primitives::{Edge, Point};
use jagua_rs::geometry::primitives::{Rect, SPolygon};
use std::sync::Arc;

#[test]
fn edge_matrix_matches_shared_reference() {
    let content = include_str!("../../../JaguarCore/tests/fixtures/rust_reference/edge_matrix.tsv");
    let mut cases = 0usize;
    for line in content.lines() {
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        let fields: Vec<_> = line.split_whitespace().collect();
        assert_eq!(fields.len(), 10);
        let edge = |offset: usize| {
            Edge::try_new(
                Point(
                    fields[offset].parse().unwrap(),
                    fields[offset + 1].parse().unwrap(),
                ),
                Point(
                    fields[offset + 2].parse().unwrap(),
                    fields[offset + 3].parse().unwrap(),
                ),
            )
            .unwrap()
        };
        assert_eq!(
            edge(1).collides_with(&edge(5)),
            fields[9] == "1",
            "{}",
            fields[0]
        );
        cases += 1;
    }
    assert_eq!(cases, 9);
}

fn rectangle(x_min: f32, y_min: f32, x_max: f32, y_max: f32) -> SPolygon {
    SPolygon::new(vec![
        Point(x_min, y_min),
        Point(x_max, y_min),
        Point(x_max, y_max),
        Point(x_min, y_max),
    ])
    .unwrap()
}

#[test]
fn containment_matrix_matches_rust_cde_semantics() {
    let content =
        include_str!("../../../JaguarCore/tests/fixtures/rust_reference/containment_matrix.tsv");
    let bounds = Rect::try_new(0.0, 0.0, 10.0, 10.0).unwrap();
    let bounds_shape = Arc::new(SPolygon::from(&bounds));
    let hazard_shape = Arc::new(rectangle(2.0, 2.0, 8.0, 8.0));
    let config: CDEConfig = lbf::config::LBFConfig::default().cde_config;
    let mut cases = 0usize;

    for line in content.lines() {
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        let fields: Vec<_> = line.split_whitespace().collect();
        assert_eq!(fields.len(), 11);
        let query = rectangle(
            fields[1].parse().unwrap(),
            fields[2].parse().unwrap(),
            fields[3].parse().unwrap(),
            fields[4].parse().unwrap(),
        );
        let hazard_kind = fields[9];
        let hazard_entity = if hazard_kind == "hole" {
            HazardEntity::Hole { idx: 0 }
        } else {
            HazardEntity::Exterior
        };
        let static_hazards = if hazard_kind == "hole" {
            vec![
                Hazard::new(HazardEntity::Exterior, bounds_shape.clone(), false),
                Hazard::new(hazard_entity, hazard_shape.clone(), false),
            ]
        } else {
            vec![Hazard::new(
                HazardEntity::Exterior,
                hazard_shape.clone(),
                false,
            )]
        };
        let engine = CDEngine::new(bounds, static_hazards, config);
        let actual = if hazard_kind == "hole" {
            let exterior_key = engine
                .hazards_map
                .iter()
                .find(|(_, hazard)| hazard.entity == HazardEntity::Exterior)
                .map(|(key, _)| key)
                .unwrap();
            let filter = HazKeyFilter::from_keys([exterior_key]);
            engine.detect_poly_collision(&query, &filter)
        } else {
            engine.detect_poly_collision(&query, &NoFilter)
        };
        assert_eq!(actual, fields[10] == "1", "{}", fields[0]);
        cases += 1;
    }

    assert_eq!(cases, 9);
}
