use jagua_rs::collision_detection::hazards::HazardEntity;
use jagua_rs::collision_detection::hazards::collector::BasicHazardCollector;
use jagua_rs::entities::{Container, Layout, PItemKey};
use jagua_rs::geometry::DTransformation;
use jagua_rs::io::ext_repr::{
    ExtContainer, ExtItem, ExtOrientation, ExtRotation, ExtSPolygon, ExtShape,
};
use jagua_rs::io::import::Importer;
use sparrow::config::DEFAULT_SPARROW_CONFIG;
use sparrow::quantify::tracker::CollisionTracker;

struct ProbeRng(u64);

impl ProbeRng {
    fn next_u32(&mut self) -> u32 {
        let mut value = self.0;
        value ^= value << 13;
        value ^= value >> 7;
        value ^= value << 17;
        self.0 = value;
        value as u32
    }

    fn unit(&mut self) -> f32 {
        self.next_u32() as f32 / u32::MAX as f32
    }

    fn signed(&mut self, limit: i32) -> i32 {
        (self.next_u32() % (2 * limit + 1) as u32) as i32 - limit
    }
}

fn build_container(importer: &Importer, half_size: f32) -> Container {
    importer
        .import_container(&ExtContainer {
            id: 8800,
            shape: ExtShape::Rectangle {
                x_min: -half_size,
                y_min: -half_size,
                width: half_size * 2.0,
                height: half_size * 2.0,
            },
            zones: Vec::new(),
        })
        .expect("container must import")
}

fn build_item(
    importer: &Importer,
    id: usize,
    rng: &mut ProbeRng,
) -> std::sync::Arc<jagua_rs::entities::Item> {
    let n = 97 + (rng.next_u32() % 32) as usize;
    let base_radius = 14.0 + (rng.next_u32() % 14) as f32;
    let vertices = (0..n)
        .map(|i| {
            let radius = base_radius * (0.55 + 0.9 * rng.unit());
            let angle = std::f32::consts::TAU * i as f32 / n as f32;
            (radius * angle.cos(), radius * angle.sin())
        })
        .collect();
    importer
        .import_item(
            &ExtItem {
                id: id as u64,
                orientation: ExtOrientation {
                    rotation: ExtRotation::Continuous {},
                },
                shape: ExtShape::SimplePolygon(ExtSPolygon(vertices)),
                min_quality: None,
            },
            id,
        )
        .expect("radial item must import")
}

fn collect_hits(layout: &Layout, query: PItemKey, target: PItemKey) -> bool {
    let query_placed = &layout.placed_items[query];
    let target_placed = &layout.placed_items[target];
    let target_entity = HazardEntity::from((target, target_placed));
    let mut collector = BasicHazardCollector::new();
    layout
        .cde()
        .collect_poly_collisions(&query_placed.shape, &mut collector);
    collector.iter().any(|(_, entity)| entity == &target_entity)
}

#[test]
#[ignore = "exploratory deterministic search for a one-way CDE geometry; not a regression fixture"]
fn deterministic_search_for_one_way_cde_layout_discovery() {
    let config = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        config.cde_config,
        config.poly_simpl_tolerance,
        config.narrow_concavity_cutoff_ratio,
    );
    let container = build_container(&importer, 128.0);
    let mut rng = ProbeRng(0x6e6f_6e73_796d_6d65);

    let items = (0..4)
        .map(|idx| build_item(&importer, 8801 + idx, &mut rng))
        .collect::<Vec<_>>();
    println!(
        "imported collision edge counts={:?}",
        items
            .iter()
            .map(|item| item.shape_cd.vertices.len())
            .collect::<Vec<_>>()
    );

    let mut checked_pairs = 0usize;
    for batch in 0..12 {
        let mut layout = Layout::new(container.clone());
        let mut keys = Vec::new();
        for sequence in 0..48 {
            let item_idx = (rng.next_u32() as usize + sequence + batch) % items.len();
            let rotation = rng.unit() * std::f32::consts::TAU;
            let x = (rng.signed(3) * 32) as f32 + (rng.unit() - 0.5) * 2.0;
            let y = (rng.signed(3) * 32) as f32 + (rng.unit() - 0.5) * 2.0;
            keys.push(layout.place_item(&items[item_idx], DTransformation::new(rotation, (x, y))));
        }

        // Build the real Sparrow tracker before querying both directions. In Debug this also
        // exercises tracker_matches_layout's own asymmetric-collision diagnostic path.
        let tracker = CollisionTracker::new(&layout);
        for i in 0..keys.len() {
            for j in (i + 1)..keys.len() {
                let a = keys[i];
                let b = keys[j];
                let a_to_b = collect_hits(&layout, a, b);
                let b_to_a = collect_hits(&layout, b, a);
                checked_pairs += 1;
                if a_to_b != b_to_a {
                    panic!(
                        "one-way layout CDE discovery: batch={batch}, pair=({i},{j}), A_to_B={a_to_b}, B_to_A={b_to_a}, tracker_loss={:?}, A_vertices={:?}, B_vertices={:?}",
                        tracker.get_pair_loss(a, b),
                        layout.placed_items[a].shape.vertices,
                        layout.placed_items[b].shape.vertices
                    );
                }
                let pair_loss = tracker.get_pair_loss(a, b);
                assert_eq!(
                    pair_loss > 0.0,
                    a_to_b,
                    "Tracker shared pair loss must match the symmetric CDE result"
                );
            }
        }
    }

    assert_eq!(checked_pairs, 12 * 48 * 47 / 2);
    println!("actual_layout_pairs_checked={checked_pairs}; one-way_discoveries=0");
}

fn support_projection(
    item: &jagua_rs::entities::Item,
    rotation: f32,
    direction: f32,
    maximum: bool,
) -> f32 {
    let (sin_rotation, cos_rotation) = rotation.sin_cos();
    let (sin_direction, cos_direction) = direction.sin_cos();
    item.shape_cd
        .vertices
        .iter()
        .map(|point| {
            let x = point.0 * cos_rotation - point.1 * sin_rotation;
            let y = point.0 * sin_rotation + point.1 * cos_rotation;
            x * cos_direction + y * sin_direction
        })
        .fold(
            if maximum {
                f32::NEG_INFINITY
            } else {
                f32::INFINITY
            },
            |acc, value| {
                if maximum {
                    acc.max(value)
                } else {
                    acc.min(value)
                }
            },
        )
}

#[test]
#[ignore = "targeted near-contact search for a one-way CDE geometry; not a regression fixture"]
fn deterministic_search_for_one_way_near_contact_layout_discovery() {
    let config = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        config.cde_config,
        config.poly_simpl_tolerance,
        config.narrow_concavity_cutoff_ratio,
    );
    let container = build_container(&importer, 256.0);
    let mut rng = ProbeRng(0x7461_6e67_656e_7421);
    let items = (0..8)
        .map(|idx| build_item(&importer, 9901 + idx, &mut rng))
        .collect::<Vec<_>>();
    let anchors = [-100.0_f32, 0.0, 100.0];
    let overlaps = [0.0001_f32, 0.001, 0.01, 0.1];
    let mut checked_pairs = 0usize;

    for batch in 0..24 {
        let mut layout = Layout::new(container.clone());
        let mut pairs = Vec::new();
        for row in 0..3 {
            for col in 0..3 {
                let a_idx = rng.next_u32() as usize % items.len();
                let b_idx = (rng.next_u32() as usize + 1) % items.len();
                let item_a = &items[a_idx];
                let item_b = &items[b_idx];
                let rotation_a = rng.unit() * std::f32::consts::TAU;
                let rotation_b = rng.unit() * std::f32::consts::TAU;
                let direction = rng.unit() * std::f32::consts::TAU;
                let overlap = overlaps[(batch + row + col) % overlaps.len()];
                let support_a = support_projection(item_a, rotation_a, direction, true);
                let support_b = -support_projection(item_b, rotation_b, direction, false);
                let distance = support_a + support_b - overlap;
                let (sin_direction, cos_direction) = direction.sin_cos();
                let anchor_x = anchors[col] + (rng.unit() - 0.5) * 0.01;
                let anchor_y = anchors[row] + (rng.unit() - 0.5) * 0.01;
                let key_a = layout.place_item(
                    item_a,
                    DTransformation::new(rotation_a, (anchor_x, anchor_y)),
                );
                let key_b = layout.place_item(
                    item_b,
                    DTransformation::new(
                        rotation_b,
                        (
                            anchor_x + cos_direction * distance,
                            anchor_y + sin_direction * distance,
                        ),
                    ),
                );
                pairs.push((key_a, key_b, rotation_a, rotation_b, direction, overlap));
            }
        }

        let tracker = CollisionTracker::new(&layout);
        for (pair_idx, (a, b, rotation_a, rotation_b, direction, overlap)) in
            pairs.into_iter().enumerate()
        {
            let a_to_b = collect_hits(&layout, a, b);
            let b_to_a = collect_hits(&layout, b, a);
            checked_pairs += 1;
            if a_to_b != b_to_a {
                panic!(
                    "one-way near-contact layout discovery: batch={batch}, pair={pair_idx}, A_to_B={a_to_b}, B_to_A={b_to_a}, rotation_a={rotation_a:?}, rotation_b={rotation_b:?}, direction={direction:?}, overlap={overlap:?}, tracker_loss={:?}, A={:?}, B={:?}",
                    tracker.get_pair_loss(a, b),
                    layout.placed_items[a].shape.vertices,
                    layout.placed_items[b].shape.vertices
                );
            }
            let pair_loss = tracker.get_pair_loss(a, b);
            assert_eq!(
                pair_loss > 0.0,
                a_to_b,
                "Tracker pair loss must match the symmetric near-contact CDE result"
            );
        }
    }

    assert_eq!(checked_pairs, 24 * 9);
    println!("near_contact_layout_pairs_checked={checked_pairs}; one-way_discoveries=0");
}
