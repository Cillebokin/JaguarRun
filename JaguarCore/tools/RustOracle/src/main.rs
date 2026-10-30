use geo::algorithm::buffer::{Buffer, BufferStyle, LineJoin};
use geo_buffer::buffer_polygon_rounded;
use geo_types::{LineString, Polygon};
use jagua_rs::collision_detection::hazards::collector::BasicHazardCollector;
use jagua_rs::collision_detection::hazards::filter::{HazKeyFilter, NoFilter};
use jagua_rs::collision_detection::hazards::{Hazard, HazardEntity};
use jagua_rs::collision_detection::quadtree::QTHazPresence;
use jagua_rs::collision_detection::{CDEConfig, CDEngine};
use jagua_rs::entities::{Container, InferiorQualityZone, Item, Layout};
use jagua_rs::geometry::convex_hull::convex_hull_from_points;
use jagua_rs::geometry::fail_fast::SPSurrogateConfig;
use jagua_rs::geometry::geo_enums::RotationRange;
use jagua_rs::geometry::geo_traits::{
    AlmostCollidesWith, CollidesWith, DistanceTo, SeparationDistance, Transformable,
    TransformableFrom,
};
use jagua_rs::geometry::primitives::{Circle, Edge, Point, Rect, SPolygon};
use jagua_rs::geometry::shape_modification::{
    ShapeModifyConfig, ShapeModifyMode, close_narrow_concavities, offset_shape,
    shape_modification_valid, simplify_shape,
};
use jagua_rs::geometry::{DTransformation, OriginalShape};
use jagua_rs::io::export::int_to_ext_transformation;
use jagua_rs::io::ext_repr::{
    ExtContainer, ExtItem, ExtLayout, ExtOrientation, ExtPlacedItem, ExtPolygon, ExtQualityZone,
    ExtRotation, ExtSPolygon, ExtShape, ExtTransformation,
};
use jagua_rs::io::import::{Importer, import_simple_polygon};
use jagua_rs::io::svg::{SvgDrawOptions, layout_to_svg};
use jagua_rs::probs::bpp::entities::{BPInstance, BPLayoutType, BPPlacement, BPProblem, Bin};
use jagua_rs::probs::bpp::io::ext_repr::{ExtBPInstance, ExtBPSolution};
use jagua_rs::probs::bpp::io::import_instance as import_bpp_instance;
use jagua_rs::probs::mspp::entities::{MSPInstance, MSPPlacement, MSPProblem, Strip};
use jagua_rs::probs::mspp::io::ext_repr::{ExtMSPInstance, ExtMSPSolution, ExtMSPStrip};
use jagua_rs::probs::mspp::io::import_instance as import_mspp_instance;
use jagua_rs::probs::spp::entities::{SPInstance, SPPlacement, SPProblem, Strip as SPStrip};
use jagua_rs::probs::spp::io::ext_repr::{ExtSPInstance, ExtSPSolution};
use jagua_rs::probs::spp::io::import_instance as import_spp_instance;
use jagua_rs::probs::spp::io::import_solution as import_spp_solution;
use std::sync::Arc;

fn polygon(points: &[(f32, f32)]) -> SPolygon {
    SPolygon::new(points.iter().copied().map(Point::from).collect()).unwrap()
}

fn rectangle(x_min: f32, y_min: f32, x_max: f32, y_max: f32) -> SPolygon {
    polygon(&[
        (x_min, y_min),
        (x_max, y_min),
        (x_max, y_max),
        (x_min, y_max),
    ])
}

fn square_item(index: usize, side: f32) -> Arc<Item> {
    let original_shape = OriginalShape {
        shape: polygon(&[(0.0, 0.0), (side, 0.0), (side, side), (0.0, side)]),
        pre_transform: DTransformation::empty(),
        modify_mode: ShapeModifyMode::Inflate,
        modify_config: ShapeModifyConfig::default(),
    };
    Arc::new(
        Item::new(
            index,
            1000 + index as u64,
            original_shape,
            RotationRange::None,
            None,
            SPSurrogateConfig::none(),
        )
        .unwrap(),
    )
}

fn print_polygon(name: &str, shape: &SPolygon) {
    println!("{name}.vertices={}", shape.n_vertices());
    println!("{name}.area={:.12}", shape.area);
    println!(
        "{name}.bbox={:.12},{:.12},{:.12},{:.12}",
        shape.bbox.x_min, shape.bbox.y_min, shape.bbox.x_max, shape.bbox.y_max
    );
    println!(
        "{name}.centroid={:.12},{:.12}",
        shape.centroid().0,
        shape.centroid().1
    );
}

fn print_buffer_components(name: &str, points: &[(f32, f32)], distance: f64) {
    let input = Polygon::new(
        LineString::from(
            points
                .iter()
                .map(|(x, y)| (*x as f64, *y as f64))
                .collect::<Vec<_>>(),
        ),
        vec![],
    );
    let components = buffer_polygon_rounded(&input, distance).0;
    println!("{name}.components={}", components.len());
    for (index, component) in components.iter().enumerate() {
        let points: Vec<_> = component.exterior().points().collect();
        let mut area = 0.0f64;
        let mut x_min = f64::INFINITY;
        let mut y_min = f64::INFINITY;
        let mut x_max = f64::NEG_INFINITY;
        let mut y_max = f64::NEG_INFINITY;
        for point in &points {
            x_min = x_min.min(point.x());
            y_min = y_min.min(point.y());
            x_max = x_max.max(point.x());
            y_max = y_max.max(point.y());
        }
        for pair in points.windows(2) {
            area += pair[0].x() * pair[1].y() - pair[1].x() * pair[0].y();
        }
        println!(
            "{name}.component[{index}].vertices={} area={:.12} bbox={:.12},{:.12},{:.12},{:.12}",
            points.len().saturating_sub(1),
            area.abs() * 0.5,
            x_min,
            y_min,
            x_max,
            y_max
        );
    }
}

fn print_pinned_offset_components(name: &str, points: &[(f32, f32)], distance: f32) {
    let input = Polygon::new(
        LineString::from(
            points
                .iter()
                .map(|(x, y)| (f64::from(*x), f64::from(*y)))
                .collect::<Vec<_>>(),
        ),
        vec![],
    );
    let buffered = input
        .buffer_with_style(BufferStyle::new(-f64::from(distance)).line_join(LineJoin::Round(0.1)));
    println!("{name}.components={}", buffered.0.len());
    for (index, component) in buffered.0.iter().enumerate() {
        let exterior = ExtSPolygon(
            component
                .exterior()
                .points()
                .map(|point| (point.x() as f32, point.y() as f32))
                .collect(),
        );
        let imported = import_simple_polygon(&exterior).unwrap();
        let mut values = vec![
            imported.n_vertices().to_string(),
            format!("{:.9}", imported.area),
        ];
        for point in &imported.vertices {
            values.push(format!("{:.9}", point.0));
            values.push(format!("{:.9}", point.1));
        }
        println!("{name}.component{index}={}", values.join(","));
    }
}

fn dense_tooth_gear_points(tooth_count: usize) -> Vec<(f32, f32)> {
    let side = 4.0 * tooth_count as f32;
    let mut points = vec![(0.0, 0.0)];
    for tooth in 0..tooth_count {
        let start = 4.0 * tooth as f32;
        points.push((start + 1.0, 0.0));
        points.push((start + 2.0, -3.0));
        points.push((start + 3.0, 0.0));
    }
    points.push((side, 0.0));
    for tooth in 0..tooth_count {
        let start = 4.0 * tooth as f32;
        points.push((side, start + 1.0));
        points.push((side + 3.0, start + 2.0));
        points.push((side, start + 3.0));
    }
    points.push((side, side));
    for tooth in 0..tooth_count {
        let start = side - 4.0 * tooth as f32;
        points.push((start - 1.0, side));
        points.push((start - 2.0, side + 3.0));
        points.push((start - 3.0, side));
    }
    points.push((0.0, side));
    for tooth in 0..tooth_count {
        let start = side - 4.0 * tooth as f32;
        points.push((0.0, start - 1.0));
        points.push((-3.0, start - 2.0));
        points.push((0.0, start - 3.0));
    }
    points
}

fn b11_cde_config() -> CDEConfig {
    CDEConfig {
        quadtree_depth: 4,
        cd_threshold: 8,
        item_surrogate_config: SPSurrogateConfig::none(),
    }
}

fn run_boundary_b11_oracle() {
    let root_bounds = Rect::try_new(0.0, 0.0, 10.0, 10.0).unwrap();
    let root_shape = Arc::new(SPolygon::from(&root_bounds));
    let root_engine = CDEngine::new(
        root_bounds,
        vec![Hazard::new(HazardEntity::Exterior, root_shape, false)],
        b11_cde_config(),
    );
    let exterior_key = root_engine
        .hazards_map
        .iter()
        .find(|(_, hazard)| matches!(hazard.entity, HazardEntity::Exterior))
        .map(|(key, _)| key)
        .unwrap();
    let ignore_exterior = HazKeyFilter::from_keys([exterior_key]);
    let partially_out_of_bounds = polygon(&[(-1.0, 1.0), (1.0, 1.0), (1.0, 2.0), (-1.0, 2.0)]);
    println!(
        "boundary.root_out_of_bounds_ignores_exterior={}",
        root_engine.detect_poly_collision(&partially_out_of_bounds, &ignore_exterior)
    );

    let edge = |start: (f32, f32), end: (f32, f32)| {
        Edge::try_new(Point::from(start), Point::from(end)).unwrap()
    };
    println!(
        "boundary.edge.proper={}",
        edge((0.0, 0.0), (4.0, 4.0)).collides_with(&edge((0.0, 4.0), (4.0, 0.0)))
    );
    println!(
        "boundary.edge.tangent={}",
        edge((0.0, 0.0), (4.0, 0.0)).collides_with(&edge((2.0, 0.0), (2.0, 3.0)))
    );
    println!(
        "boundary.edge.endpoint={}",
        edge((0.0, 0.0), (4.0, 0.0)).collides_with(&edge((4.0, 0.0), (4.0, 4.0)))
    );
    println!(
        "boundary.edge.collinear_overlap={}",
        edge((0.0, 0.0), (4.0, 0.0)).collides_with(&edge((2.0, 0.0), (6.0, 0.0)))
    );
    let gap = edge((0.0, 0.0), (4.0, 0.0));
    println!(
        "boundary.edge.small_gap={}",
        gap.collides_with(&edge((4.0 + 5.0e-10, 0.0), (8.0, 1.0)))
    );
    println!(
        "boundary.edge.large_gap={}",
        gap.collides_with(&edge((4.0 + 5.0e-7, 0.0), (8.0, 1.0)))
    );

    let bounds = Rect::try_new(0.0, 0.0, 20.0, 20.0).unwrap();
    let exterior = Arc::new(SPolygon::from(&bounds));
    let hazard = Arc::new(polygon(&[(4.0, 4.0), (6.0, 4.0), (6.0, 6.0), (4.0, 6.0)]));
    let engine = CDEngine::new(
        bounds,
        vec![
            Hazard::new(HazardEntity::Exterior, exterior, false),
            Hazard::new(HazardEntity::Hole { idx: 0 }, hazard, false),
        ],
        b11_cde_config(),
    );
    let rectangle = |x_min: f32, y_min: f32, x_max: f32, y_max: f32| {
        polygon(&[
            (x_min, y_min),
            (x_max, y_min),
            (x_max, y_max),
            (x_min, y_max),
        ])
    };
    println!(
        "boundary.polygon.corner_touch={}",
        engine.detect_poly_collision(&rectangle(6.0, 6.0, 8.0, 8.0), &NoFilter)
    );
    println!(
        "boundary.polygon.small_gap={}",
        engine.detect_poly_collision(
            &rectangle(6.0 + 5.0e-10, 6.0, 8.0 + 5.0e-10, 8.0),
            &NoFilter
        )
    );
    println!(
        "boundary.polygon.large_gap={}",
        engine.detect_poly_collision(&rectangle(6.0 + 5.0e-7, 6.0, 8.0 + 5.0e-7, 8.0), &NoFilter)
    );
    println!(
        "boundary.polygon.interior={}",
        engine.detect_poly_collision(
            &rectangle(4.0 + 5.0e-10, 4.0 + 5.0e-10, 6.0 - 5.0e-10, 6.0 - 5.0e-10),
            &NoFilter
        )
    );
}

fn run_matrix_b11_oracle() {
    let edge_reference = include_str!("../../../Tests/Fixtures/RustReference/edge_matrix.tsv");
    for line in edge_reference
        .lines()
        .filter(|line| !line.is_empty() && !line.starts_with('#'))
    {
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
        println!("edge.{}={}", fields[0], edge(1).collides_with(&edge(5)));
    }

    let containment_reference =
        include_str!("../../../Tests/Fixtures/RustReference/containment_matrix.tsv");
    let bounds = Rect::try_new(0.0, 0.0, 10.0, 10.0).unwrap();
    let bounds_shape = Arc::new(SPolygon::from(&bounds));
    let hazard_shape = Arc::new(polygon(&[(2.0, 2.0), (8.0, 2.0), (8.0, 8.0), (2.0, 8.0)]));
    for line in containment_reference
        .lines()
        .filter(|line| !line.is_empty() && !line.starts_with('#'))
    {
        let fields: Vec<_> = line.split_whitespace().collect();
        assert_eq!(fields.len(), 11);
        let query = rectangle(
            fields[1].parse().unwrap(),
            fields[2].parse().unwrap(),
            fields[3].parse().unwrap(),
            fields[4].parse().unwrap(),
        );
        let (hazards, hole_case) = if fields[9] == "hole" {
            (
                vec![
                    Hazard::new(HazardEntity::Exterior, bounds_shape.clone(), false),
                    Hazard::new(HazardEntity::Hole { idx: 0 }, hazard_shape.clone(), false),
                ],
                true,
            )
        } else {
            (
                vec![Hazard::new(
                    HazardEntity::Exterior,
                    hazard_shape.clone(),
                    false,
                )],
                false,
            )
        };
        let engine = CDEngine::new(bounds, hazards, b11_cde_config());
        let actual = if hole_case {
            let exterior_key = engine
                .hazards_map
                .iter()
                .find(|(_, hazard)| matches!(hazard.entity, HazardEntity::Exterior))
                .map(|(key, _)| key)
                .unwrap();
            let ignore_exterior = HazKeyFilter::from_keys([exterior_key]);
            engine.detect_poly_collision(&query, &ignore_exterior)
        } else {
            engine.detect_poly_collision(&query, &NoFilter)
        };
        println!("containment.{}={actual}", fields[0]);
    }
}

fn run_trajectory_b11_oracle() {
    struct XorShift64Star {
        state: u64,
    }
    impl XorShift64Star {
        fn next(&mut self) -> u64 {
            self.state ^= self.state >> 12;
            self.state ^= self.state << 25;
            self.state ^= self.state >> 27;
            self.state = self.state.wrapping_mul(2_685_821_657_736_338_717);
            self.state
        }
    }
    fn mix(checksum: &mut u64, value: u64) {
        *checksum ^= value;
        *checksum = checksum.wrapping_mul(1_099_511_628_211);
    }

    let config = CDEConfig {
        quadtree_depth: 4,
        cd_threshold: 32,
        item_surrogate_config: SPSurrogateConfig {
            n_pole_limits: [(1, 0.0), (0, 0.0), (0, 0.0)],
            ff_pole_area_ratio: 0.0,
            n_ff_piers: 0,
        },
    };
    let importer = Importer::new(config, None, None);
    let item: ExtItem = serde_json::from_value(serde_json::json!({
        "id": 8801,
        "orientation": {"rotation": {"mode": "discrete", "angles": [0, 90, 180, 270]}},
        "shape": {"type": "rectangle", "data": {"x_min": 0, "y_min": 0, "width": 2, "height": 2}}
    }))
    .unwrap();
    let item = importer.import_item(&item, 0).unwrap();
    let container: ExtContainer = serde_json::from_value(serde_json::json!({
        "id": 8800,
        "shape": {"type": "rectangle", "data": {"x_min": 0, "y_min": 0, "width": 52, "height": 26}}
    }))
    .unwrap();
    let mut layout = Layout::new(importer.import_container(&container).unwrap());
    let mut rng = XorShift64Star {
        state: 0x4A47524312345678,
    };
    let mut active = Vec::new();
    let mut checksum = 1_469_598_103_934_665_603u64;
    let (mut placements, mut removals, mut restores) = (0usize, 0usize, 0usize);
    for step in 0..256usize {
        let decision = rng.next();
        if active.is_empty() || decision % 3 == 0 {
            let serial = placements;
            let column = serial % 16;
            let row = (serial / 16) % 8;
            let rotation = (rng.next() % 4) as usize;
            let key = layout.place_item(
                &item,
                DTransformation::new(
                    rotation as f32 * std::f32::consts::FRAC_PI_2,
                    (1.0 + (column * 2) as f32, 1.0 + (row * 2) as f32),
                ),
            );
            active.push((key, serial, column, row, rotation));
            placements += 1;
            for value in [
                0,
                step as u64,
                serial as u64,
                column as u64,
                row as u64,
                rotation as u64,
                active.len() as u64,
            ] {
                mix(&mut checksum, value);
            }
        } else if decision % 3 == 1 {
            let selected = (rng.next() % active.len() as u64) as usize;
            let removed_serial = active[selected].1;
            let (key, _, _, _, _) = active.remove(selected);
            layout.remove_item(key);
            removals += 1;
            for value in [
                1,
                step as u64,
                selected as u64,
                removed_serial as u64,
                active.len() as u64,
            ] {
                mix(&mut checksum, value);
            }
        } else {
            let snapshot = layout.save();
            layout.restore(&snapshot).unwrap();
            restores += 1;
            for value in [2, step as u64, active.len() as u64] {
                mix(&mut checksum, value);
            }
        }
        assert_eq!(layout.placed_items.len(), active.len());
    }
    for value in [255, 256, active.len() as u64] {
        mix(&mut checksum, value);
    }
    for (_, serial, column, row, rotation) in &active {
        for value in [
            *serial as u64,
            *column as u64,
            *row as u64,
            *rotation as u64,
        ] {
            mix(&mut checksum, value);
        }
    }
    println!("placements={placements}");
    println!("removals={removals}");
    println!("restores={restores}");
    println!("active={}", active.len());
    println!("checksum={checksum:016x}");
    println!("rng_state={:016x}", rng.state);
}

fn run_b4a_entire_branch_oracle() {
    let root = Rect::try_new(0.0, 0.0, 10.0, 10.0).unwrap();
    let mut engine = CDEngine::new(
        root,
        vec![Hazard::new(
            HazardEntity::Exterior,
            Arc::new(polygon(&[
                (0.0, 0.0),
                (10.0, 0.0),
                (10.0, 10.0),
                (0.0, 10.0),
            ])),
            false,
        )],
        CDEConfig {
            quadtree_depth: 4,
            cd_threshold: 8,
            item_surrogate_config: SPSurrogateConfig::none(),
        },
    );
    engine.register_hazard(Hazard::new(
        HazardEntity::Hole { idx: 0 },
        Arc::new(polygon(&[(2.0, 2.0), (8.0, 2.0), (8.0, 8.0), (2.0, 8.0)])),
        false,
    ));

    let hole_key = engine
        .hazards_map
        .iter()
        .find(|(_, hazard)| matches!(hazard.entity, HazardEntity::Hole { idx: 0 }))
        .map(|(key, _)| key)
        .unwrap();
    let query = polygon(&[(4.1, 4.1), (4.3, 4.1), (4.3, 4.3), (4.1, 4.3)]);
    let virtual_root = engine.get_virtual_root(query.bbox);
    let entire_is_present = virtual_root
        .hazards
        .iter()
        .any(|hazard| hazard.hkey == hole_key && matches!(&hazard.presence, QTHazPresence::Entire));
    assert!(
        entire_is_present,
        "probe must reach an Entire hazard in the virtual root"
    );
    println!("b4a.virtual_root_contains_filtered_entire={entire_is_present}");
    println!(
        "b4a.detect_without_filter={}",
        engine.detect_poly_collision(&query, &NoFilter)
    );

    let ignore_hole = HazKeyFilter::from_keys([hole_key]);
    let previous_hook = std::panic::take_hook();
    std::panic::set_hook(Box::new(|_| {}));
    let filtered_result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        engine.detect_poly_collision(&query, &ignore_hole)
    }));
    std::panic::set_hook(previous_hook);
    let filtered_outcome = match filtered_result {
        Ok(true) => "collision",
        Ok(false) => "clear",
        Err(_) => "panic",
    };
    println!("b4a.detect_filtering_entire={filtered_outcome}");
}

fn run_collision_parity_oracle() {
    let mut surrogate_shape = polygon(&[(4.95, 7.95), (5.05, 7.95), (5.05, 8.05), (4.95, 8.05)]);
    surrogate_shape
        .generate_surrogate(SPSurrogateConfig {
            n_pole_limits: [(1, 0.0), (0, 0.0), (0, 0.0)],
            ff_pole_area_ratio: 1.0,
            n_ff_piers: 0,
        })
        .unwrap();
    let notch_container = Arc::new(polygon(&[
        (0.0, 0.0),
        (10.0, 0.0),
        (10.0, 10.0),
        (6.0, 10.0),
        (6.0, 4.0),
        (4.0, 4.0),
        (4.0, 10.0),
        (0.0, 10.0),
    ]));

    for (threshold_name, cd_threshold) in [("high", 8), ("low", 0)] {
        let engine = CDEngine::new(
            Rect::try_new(0.0, 0.0, 10.0, 10.0).unwrap(),
            vec![Hazard::new(
                HazardEntity::Exterior,
                Arc::clone(&notch_container),
                false,
            )],
            CDEConfig {
                quadtree_depth: 4,
                cd_threshold,
                item_surrogate_config: SPSurrogateConfig::none(),
            },
        );
        let detect = engine.detect_surrogate_collision(
            surrogate_shape.surrogate(),
            &jagua_rs::geometry::Transformation::empty(),
            &NoFilter,
        );
        println!("collision.notch.detect.{threshold_name}={detect}");

        let mut collector = BasicHazardCollector::new();
        let stopped =
            engine.collect_surrogate_collisions_until(&surrogate_shape, &mut collector, |_| false);
        println!(
            "collision.notch.collect.{threshold_name}=stopped:{stopped},count:{}",
            collector.len()
        );
    }

    let mut pier_only_shape = polygon(&[
        (0.0, 0.0),
        (10.0, 0.0),
        (10.0, 10.0),
        (6.0, 10.0),
        (6.0, 4.0),
        (4.0, 4.0),
        (4.0, 10.0),
        (0.0, 10.0),
    ]);
    pier_only_shape
        .generate_surrogate(SPSurrogateConfig {
            n_pole_limits: [(1, 0.0), (0, 0.0), (0, 0.0)],
            ff_pole_area_ratio: 0.0,
            n_ff_piers: 1,
        })
        .unwrap();
    println!(
        "collision.notch.pier_only_count={}",
        pier_only_shape.surrogate().ff_piers().len()
    );

    let large_notch_container = Arc::new(polygon(&[
        (0.0, 0.0),
        (40.0, 0.0),
        (40.0, 40.0),
        (30.0, 40.0),
        (30.0, 10.0),
        (10.0, 10.0),
        (10.0, 40.0),
        (0.0, 40.0),
    ]));
    let large_notch_engine = CDEngine::new(
        Rect::try_new(0.0, 0.0, 40.0, 40.0).unwrap(),
        vec![Hazard::new(
            HazardEntity::Exterior,
            large_notch_container,
            false,
        )],
        CDEConfig {
            quadtree_depth: 4,
            cd_threshold: 8,
            item_surrogate_config: SPSurrogateConfig::none(),
        },
    );
    let pier_only_detect = large_notch_engine.detect_surrogate_collision(
        pier_only_shape.surrogate(),
        &jagua_rs::geometry::Transformation::from_translation((15.0, 15.0)),
        &NoFilter,
    );
    println!("collision.notch.detect_pier_only={pier_only_detect}");
}

fn run_b3_surrogate_parity_oracle() {
    let config = SPSurrogateConfig {
        n_pole_limits: [(8, 0.0), (4, 0.75), (2, 0.9)],
        ff_pole_area_ratio: 0.5,
        n_ff_piers: 2,
    };
    let shapes = [
        (
            "u",
            vec![
                (0.0, 0.0),
                (10.0, 0.0),
                (10.0, 10.0),
                (6.0, 10.0),
                (6.0, 4.0),
                (4.0, 4.0),
                (4.0, 10.0),
                (0.0, 10.0),
            ],
        ),
        (
            "l",
            vec![
                (0.0, 0.0),
                (8.0, 0.0),
                (8.0, 3.0),
                (3.0, 3.0),
                (3.0, 8.0),
                (0.0, 8.0),
            ],
        ),
        (
            "c",
            vec![
                (0.0, 0.0),
                (10.0, 0.0),
                (10.0, 2.0),
                (3.0, 2.0),
                (3.0, 8.0),
                (10.0, 8.0),
                (10.0, 10.0),
                (0.0, 10.0),
            ],
        ),
    ];

    for (name, points) in shapes {
        let mut shape = polygon(&points);
        shape.generate_surrogate(config).unwrap();
        let surrogate = shape.surrogate();
        println!("b3.{name}.poles.count={}", surrogate.poles.len());
        println!(
            "b3.{name}.fail_fast_poles.count={}",
            surrogate.ff_poles().len()
        );
        println!("b3.{name}.piers.count={}", surrogate.piers.len());
        println!(
            "b3.{name}.convex_hull_area={:.12}",
            surrogate.convex_hull_area
        );
        println!(
            "b3.{name}.convex_hull_indices={:?}",
            surrogate.convex_hull_indices
        );
        for (index, pole) in surrogate.poles.iter().enumerate() {
            println!(
                "b3.{name}.pole[{index}]={:.12},{:.12},{:.12}",
                pole.center.0, pole.center.1, pole.radius
            );
        }
        for (index, pier) in surrogate.piers.iter().enumerate() {
            println!(
                "b3.{name}.pier[{index}]={:.12},{:.12},{:.12},{:.12},{:.12}",
                pier.start.0,
                pier.start.1,
                pier.end.0,
                pier.end.1,
                pier.length()
            );
        }
    }

    run_collision_parity_oracle();
    run_b3_surrogate_boundary_oracle();
}

fn run_b3_surrogate_boundary_oracle() {
    let square_points = [(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)];
    let square = polygon(&square_points);
    let three_pole_config = SPSurrogateConfig {
        n_pole_limits: [(3, 0.0), (100, 2.0), (100, 2.0)],
        ff_pole_area_ratio: 0.0,
        n_ff_piers: 0,
    };
    let mut baseline_shape = square.clone();
    baseline_shape
        .generate_surrogate(three_pole_config)
        .unwrap();
    let baseline = baseline_shape.surrogate();
    let mut covered_area_after_two = 0.0_f32;
    for pole in baseline.poles.iter().take(2) {
        covered_area_after_two += pole.area();
    }
    let coverage_after_two = covered_area_after_two / square.area;
    let threshold_bits = coverage_after_two.to_bits();
    let below_threshold = f32::from_bits(threshold_bits - 1);
    let above_threshold = f32::from_bits(threshold_bits + 1);
    println!("b3.boundary.baseline_poles={}", baseline.poles.len());
    println!("b3.boundary.coverage_after_two={coverage_after_two:.9}");

    for (name, threshold) in [
        ("below", below_threshold),
        ("equal", coverage_after_two),
        ("above", above_threshold),
    ] {
        println!("b3.pole_threshold.{name}_value={threshold:.9}");
        let config = SPSurrogateConfig {
            n_pole_limits: [(2, threshold), (100, 2.0), (100, 2.0)],
            ff_pole_area_ratio: 0.0,
            n_ff_piers: 0,
        };
        let mut shape = square.clone();
        let result = shape.generate_surrogate(config);
        match result {
            Ok(()) => println!(
                "b3.pole_threshold.{name}_poles={}",
                shape.surrogate().poles.len()
            ),
            Err(error) => println!("b3.pole_threshold.{name}_poles=err:{error}"),
        }
    }

    for (name, ratio) in [
        ("below", below_threshold),
        ("equal", coverage_after_two),
        ("above", above_threshold),
    ] {
        println!("b3.fail_fast_ratio.{name}_value={ratio:.9}");
        let config = SPSurrogateConfig {
            ff_pole_area_ratio: ratio,
            ..three_pole_config
        };
        let mut shape = square.clone();
        let result = shape.generate_surrogate(config);
        match result {
            Ok(()) => println!(
                "b3.fail_fast_ratio.{name}_poles={}",
                shape.surrogate().ff_poles().len()
            ),
            Err(error) => println!("b3.fail_fast_ratio.{name}_poles=err:{error}"),
        }
    }

    let next_after_one = f32::from_bits(1.0_f32.to_bits() + 1);
    let smallest_positive = f32::from_bits(1);
    let invalid_ratios = [
        ("negative", -smallest_positive),
        ("above_one", next_after_one),
        ("nan", f32::NAN),
        ("positive_inf", f32::INFINITY),
        ("negative_inf", f32::NEG_INFINITY),
    ];
    for (name, ratio) in invalid_ratios {
        let config = SPSurrogateConfig {
            ff_pole_area_ratio: ratio,
            ..three_pole_config
        };
        let mut shape = square.clone();
        let result = shape.generate_surrogate(config);
        println!(
            "b3.ratio.{name}={}",
            if result.is_err() { "err" } else { "ok" }
        );
    }
    for (name, ratio) in [("zero", 0.0), ("one", 1.0)] {
        let config = SPSurrogateConfig {
            ff_pole_area_ratio: ratio,
            ..three_pole_config
        };
        let mut shape = square.clone();
        let result = shape.generate_surrogate(config);
        match result {
            Ok(()) => println!("b3.ratio.{name}=ok:{}", shape.surrogate().ff_poles().len()),
            Err(_) => println!("b3.ratio.{name}=err"),
        }
    }

    let offset_square = polygon(&[
        (100.0, 100.0),
        (101.0, 100.0),
        (101.0, 101.0),
        (100.0, 101.0),
    ]);
    let pier_failure_config = SPSurrogateConfig {
        n_pole_limits: [(1, 0.0), (0, 0.0), (0, 0.0)],
        ff_pole_area_ratio: 0.0,
        n_ff_piers: 1,
    };
    let mut pier_failure_shape = offset_square;
    let pier_result = pier_failure_shape.generate_surrogate(pier_failure_config);
    match pier_result {
        Ok(()) => println!("b3.pier_generation_failure=ok"),
        Err(error) => println!("b3.pier_generation_failure=err:{error}"),
    }

    let covering_pole = Circle::try_new(Point(0.5, 0.5), 100.0).unwrap();
    let pole_result = jagua_rs::geometry::fail_fast::compute_pole(&square, &[covering_pole]);
    match pole_result {
        Ok(_) => println!("b3.pole_generation_failure=ok"),
        Err(error) if error.to_string().contains("no pole found") => {
            println!("b3.pole_generation_failure=err:no pole found")
        }
        Err(error) => println!("b3.pole_generation_failure=err:{error}"),
    }
}

fn run_model_boundary_parity_oracle() {
    use std::panic::{AssertUnwindSafe, catch_unwind};

    std::panic::set_hook(Box::new(|_| {}));
    let cde_config = CDEConfig {
        quadtree_depth: 1,
        cd_threshold: 8,
        item_surrogate_config: SPSurrogateConfig::none(),
    };
    let shape_config = ShapeModifyConfig::default();
    let transform = DTransformation::new(0.0, (1.0, 1.0));

    let overflowing_spp = SPInstance::new(
        vec![(square_item(0, 1.0), usize::MAX), (square_item(1, 1.0), 1)],
        SPStrip::new(10.0, cde_config, shape_config, 10.0).unwrap(),
    );
    let total_demand = catch_unwind(|| overflowing_spp.total_item_qty());
    match total_demand {
        Ok(total) => println!("demand.total_overflow={total}"),
        Err(_) => println!("demand.total_overflow=panic"),
    }

    let mut spp = SPProblem::new(SPInstance::new(
        vec![(square_item(0, 1.0), 0)],
        SPStrip::new(10.0, cde_config, shape_config, 10.0).unwrap(),
    ))
    .unwrap();
    let spp_result = catch_unwind(AssertUnwindSafe(|| {
        spp.place_item(SPPlacement {
            item_idx: 0,
            d_transf: transform,
        })
    }));
    println!(
        "demand.spp.overplace={};remaining:{};placed:{}",
        if spp_result.is_err() { "panic" } else { "ok" },
        spp.item_demand_qtys[0],
        spp.layout.placed_items.len()
    );

    let make_container = || {
        Container::new(
            OriginalShape {
                shape: polygon(&[(0.0, 0.0), (10.0, 0.0), (10.0, 10.0), (0.0, 10.0)]),
                pre_transform: DTransformation::empty(),
                modify_mode: ShapeModifyMode::Deflate,
                modify_config: shape_config,
            },
            vec![],
            cde_config,
        )
        .unwrap()
    };
    let mut bpp = BPProblem::new(BPInstance::new(
        vec![(square_item(0, 1.0), 0)],
        vec![Bin::new(0, 5000, make_container(), 1, 1)],
    ));
    let bpp_result = catch_unwind(AssertUnwindSafe(|| {
        bpp.place_item(BPPlacement {
            item_idx: 0,
            d_transf: transform,
            layout_id: BPLayoutType::Closed { bin_id: 0 },
        })
    }));
    println!(
        "demand.bpp.overplace={};remaining:{};placed:{};stock:{}",
        if bpp_result.is_err() { "panic" } else { "ok" },
        bpp.item_demand_qtys[0],
        bpp.n_placed_items(),
        bpp.bin_stock_qtys[0]
    );

    let mut mspp = MSPProblem::new(MSPInstance::new(
        vec![(square_item(0, 1.0), 0)],
        Strip::new(10.0, 10.0, cde_config, shape_config, 10.0).unwrap(),
    ));
    let layout = mspp
        .add_layout_from_strip(mspp.instance.base_strip)
        .unwrap();
    let mspp_result = catch_unwind(AssertUnwindSafe(|| {
        mspp.place_item(MSPPlacement {
            lk: layout,
            item_idx: 0,
            d_transf: transform,
        })
    }));
    println!(
        "demand.mspp.overplace={};remaining:{};placed:{};layouts:{}",
        if mspp_result.is_err() { "panic" } else { "ok" },
        mspp.item_demand_qtys[0],
        mspp.n_placed_items(),
        mspp.layouts.len()
    );
}

fn run_restore_boundary_parity_oracle() {
    use std::panic::{AssertUnwindSafe, catch_unwind};

    std::panic::set_hook(Box::new(|_| {}));
    let cde_config = CDEConfig {
        quadtree_depth: 1,
        cd_threshold: 8,
        item_surrogate_config: SPSurrogateConfig::none(),
    };
    let shape_config = ShapeModifyConfig::default();
    let mut spp = SPProblem::new(SPInstance::new(
        vec![(square_item(0, 1.0), 1)],
        SPStrip::new(10.0, cde_config, shape_config, 10.0).unwrap(),
    ))
    .unwrap();
    spp.place_item(SPPlacement {
        item_idx: 0,
        d_transf: DTransformation::new(0.0, (1.0, 1.0)),
    });
    let saved_spp = spp.save();

    let mut spp_config = saved_spp.clone();
    spp_config.strip.cde_config.cd_threshold = 1;
    let spp_config_result = catch_unwind(AssertUnwindSafe(|| spp.restore(&spp_config)));
    println!(
        "restore.spp.config={};threshold:{};placed:{}",
        if spp_config_result.is_err() {
            "panic"
        } else {
            "ok"
        },
        spp.strip.cde_config.cd_threshold,
        spp.layout.placed_items.len()
    );

    let mut spp_width = saved_spp;
    spp_width.strip.width *= 0.5;
    let spp_width_result = catch_unwind(AssertUnwindSafe(|| spp.restore(&spp_width)));
    println!(
        "restore.spp.width={};strip_width:{};layout_width:{};placed:{}",
        if spp_width_result.is_err() {
            "panic"
        } else {
            "ok"
        },
        spp.strip.width,
        spp.layout.container.outer_orig.bbox().x_max,
        spp.layout.placed_items.len()
    );

    let mut mspp = MSPProblem::new(MSPInstance::new(
        vec![(square_item(0, 1.0), 1)],
        Strip::new(10.0, 10.0, cde_config, shape_config, 10.0).unwrap(),
    ));
    let layout = mspp
        .add_layout_from_strip(mspp.instance.base_strip)
        .unwrap();
    mspp.place_item(MSPPlacement {
        lk: layout,
        item_idx: 0,
        d_transf: DTransformation::new(0.0, (1.0, 1.0)),
    });
    let saved_mspp = mspp.save();

    let mut mspp_config = saved_mspp.clone();
    mspp_config.strips[layout].cde_config.cd_threshold = 1;
    let mspp_config_result = catch_unwind(AssertUnwindSafe(|| mspp.restore(&mspp_config)));
    println!(
        "restore.mspp.config={};threshold:{};placed:{}",
        if mspp_config_result.is_err() {
            "panic"
        } else {
            "ok"
        },
        mspp.strips[layout].cde_config.cd_threshold,
        mspp.n_placed_items()
    );

    let mut mspp_width = saved_mspp;
    mspp_width.strips[layout].width *= 0.5;
    let mspp_width_result = catch_unwind(AssertUnwindSafe(|| mspp.restore(&mspp_width)));
    println!(
        "restore.mspp.width={};strip_width:{};layout_width:{};placed:{}",
        if mspp_width_result.is_err() {
            "panic"
        } else {
            "ok"
        },
        mspp.strips[layout].width,
        mspp.layouts[layout].container.outer_orig.bbox().x_max,
        mspp.n_placed_items()
    );
}

fn run_layout_lifecycle_parity_oracle() {
    let cde_config = CDEConfig {
        quadtree_depth: 4,
        cd_threshold: 8,
        item_surrogate_config: SPSurrogateConfig::none(),
    };
    let make_container = |width: f32, height: f32| {
        Container::new(
            OriginalShape {
                shape: polygon(&[(0.0, 0.0), (width, 0.0), (width, height), (0.0, height)]),
                pre_transform: DTransformation::empty(),
                modify_mode: ShapeModifyMode::Deflate,
                modify_config: ShapeModifyConfig::default(),
            },
            vec![],
            cde_config,
        )
        .unwrap()
    };

    let container = make_container(20.0, 10.0);
    let cloned_container = container.clone();
    let recreated_container = make_container(20.0, 10.0);
    println!(
        "container.clone_same_base={}",
        Arc::ptr_eq(&container.base_cde, &cloned_container.base_cde)
    );
    println!(
        "container.recreated_same_base={}",
        Arc::ptr_eq(&container.base_cde, &recreated_container.base_cde)
    );

    let item = square_item(0, 1.0);
    let mut roundtrip_layout = Layout::new(container.clone());
    let saved_key = roundtrip_layout.place_item(&item, DTransformation::new(0.0, (1.0, 1.0)));
    let snapshot = roundtrip_layout.save();
    roundtrip_layout.remove_item(saved_key);
    let mutated_key =
        roundtrip_layout.place_item(&square_item(1, 1.0), DTransformation::new(0.0, (5.0, 1.0)));
    let restored = roundtrip_layout.restore(&snapshot).is_ok();
    let restored_placement = roundtrip_layout.placed_items.get(saved_key);
    println!("roundtrip.restore_succeeded={restored}");
    println!(
        "roundtrip.placement_count={}",
        roundtrip_layout.placed_items.len()
    );
    println!(
        "roundtrip.dynamic_hazard_count={}",
        roundtrip_layout.cde().save().dynamic_hazards.len()
    );
    println!(
        "roundtrip.saved_key_present={}",
        roundtrip_layout.placed_items.contains_key(saved_key)
    );
    println!(
        "roundtrip.mutated_key_stale={}",
        !roundtrip_layout.placed_items.contains_key(mutated_key)
    );
    println!(
        "roundtrip.restored_item={}",
        restored_placement.map_or_else(
            || "missing".to_owned(),
            |placed| {
                let (x, y) = placed.d_transf.translation();
                format!("{}:{x:.1}:{y:.1}", placed.item.idx)
            }
        )
    );

    let mut shared_base_target = Layout::new(cloned_container);
    let shared_base_restore = shared_base_target.restore(&snapshot).is_ok();
    println!("restore.shared_base_succeeded={shared_base_restore}");
    println!(
        "restore.shared_base_placement_count={}",
        shared_base_target.placed_items.len()
    );

    let mut mismatch_target = Layout::new(recreated_container);
    let first_mismatch_item = square_item(7, 1.0);
    let second_mismatch_item = square_item(8, 1.0);
    let first_mismatch_key =
        mismatch_target.place_item(&first_mismatch_item, DTransformation::new(0.0, (2.0, 2.0)));
    let second_mismatch_key =
        mismatch_target.place_item(&second_mismatch_item, DTransformation::new(0.0, (8.0, 2.0)));
    let before_mismatch = mismatch_target.save();
    let mismatch_rejected = mismatch_target.restore(&snapshot).is_err();
    let after_mismatch = mismatch_target.save();
    let mismatch_placements_unchanged = [(first_mismatch_key, 7), (second_mismatch_key, 8)]
        .into_iter()
        .all(|(key, expected_index)| {
            let (Some(before), Some(after)) = (
                before_mismatch.placed_items.get(key),
                after_mismatch.placed_items.get(key),
            ) else {
                return false;
            };
            before.item.idx == expected_index
                && after.item.idx == expected_index
                && before.d_transf == after.d_transf
                && Arc::ptr_eq(&before.item, &after.item)
                && Arc::ptr_eq(&before.shape, &after.shape)
        });
    let mismatch_hazards_unchanged = before_mismatch
        .cde_snapshot
        .dynamic_hazards
        .iter()
        .zip(&after_mismatch.cde_snapshot.dynamic_hazards)
        .all(|(before, after)| {
            before.entity == after.entity
                && before.dynamic == after.dynamic
                && Arc::ptr_eq(&before.shape, &after.shape)
        });
    let mismatch_state_unchanged = mismatch_rejected
        && before_mismatch.placed_items.len() == after_mismatch.placed_items.len()
        && before_mismatch.cde_snapshot.dynamic_hazards.len()
            == after_mismatch.cde_snapshot.dynamic_hazards.len()
        && mismatch_placements_unchanged
        && mismatch_hazards_unchanged
        && Arc::ptr_eq(
            &before_mismatch.container.base_cde,
            &after_mismatch.container.base_cde,
        );
    println!("restore.different_base_rejected={mismatch_rejected}");
    println!("restore.different_base_state_unchanged={mismatch_state_unchanged}");

    let mut swap_layout = Layout::new(container.clone());
    let swap_first_item = square_item(12, 1.0);
    let swap_second_item = square_item(13, 1.0);
    let swap_first_key =
        swap_layout.place_item(&swap_first_item, DTransformation::new(0.0, (1.0, 1.0)));
    let swap_second_key =
        swap_layout.place_item(&swap_second_item, DTransformation::new(0.0, (5.0, 1.0)));
    let old_container_snapshot = swap_layout.save();
    swap_layout.swap_container(make_container(21.0, 10.0));
    let after_swap = swap_layout.save();
    println!("swap.placement_count={}", after_swap.placed_items.len());
    println!(
        "swap.dynamic_hazard_count={}",
        after_swap.cde_snapshot.dynamic_hazards.len()
    );
    let old_snapshot_rejected = swap_layout.restore(&old_container_snapshot).is_err();
    let after_old_snapshot = swap_layout.save();
    let swap_placements_unchanged = [(swap_first_key, 12), (swap_second_key, 13)]
        .into_iter()
        .all(|(key, expected_index)| {
            let (Some(before), Some(after)) = (
                after_swap.placed_items.get(key),
                after_old_snapshot.placed_items.get(key),
            ) else {
                return false;
            };
            before.item.idx == expected_index
                && after.item.idx == expected_index
                && before.d_transf == after.d_transf
                && Arc::ptr_eq(&before.item, &after.item)
                && Arc::ptr_eq(&before.shape, &after.shape)
        });
    let swap_hazards_unchanged = after_swap
        .cde_snapshot
        .dynamic_hazards
        .iter()
        .zip(&after_old_snapshot.cde_snapshot.dynamic_hazards)
        .all(|(before, after)| {
            before.entity == after.entity
                && before.dynamic == after.dynamic
                && Arc::ptr_eq(&before.shape, &after.shape)
        });
    let old_snapshot_state_unchanged = old_snapshot_rejected
        && after_swap.placed_items.len() == after_old_snapshot.placed_items.len()
        && after_swap.cde_snapshot.dynamic_hazards.len()
            == after_old_snapshot.cde_snapshot.dynamic_hazards.len()
        && swap_placements_unchanged
        && swap_hazards_unchanged
        && Arc::ptr_eq(
            &after_swap.container.base_cde,
            &after_old_snapshot.container.base_cde,
        );
    println!("swap.old_snapshot_rejected={old_snapshot_rejected}");
    println!("swap.old_snapshot_state_unchanged={old_snapshot_state_unchanged}");

    let first_item = square_item(5, 1.0);
    let second_item = square_item(6, 1.0);
    let mut duplicate_item = (*first_item).clone();
    duplicate_item.external_id = 1505;
    let duplicate_item = Arc::new(duplicate_item);
    let mut items_layout = Layout::new(make_container(30.0, 10.0));
    let first_item_key =
        items_layout.place_item(&first_item, DTransformation::new(0.0, (1.0, 1.0)));
    items_layout.place_item(&second_item, DTransformation::new(0.0, (8.0, 1.0)));
    items_layout.place_item(&duplicate_item, DTransformation::new(0.0, (15.0, 1.0)));
    let items_before_removal = items_layout
        .items()
        .map(|item| format!("{}:{}", item.idx, item.external_id))
        .collect::<Vec<_>>()
        .join(",");
    items_layout.remove_item(first_item_key);
    let items_after_removal = items_layout
        .items()
        .map(|item| format!("{}:{}", item.idx, item.external_id))
        .collect::<Vec<_>>()
        .join(",");
    println!("items.before_removal={items_before_removal}");
    println!("items.after_removal={items_after_removal}");
}

fn run_zero_min_item_separation_oracle() {
    let spp = ExtSPInstance {
        name: "zero-spp".to_owned(),
        min_item_separation: 0.0,
        items: vec![],
        strip_height: 1.0,
    };
    let bpp = ExtBPInstance {
        name: "zero-bpp".to_owned(),
        min_item_separation: 0.0,
        items: vec![],
        bins: vec![],
    };
    let mspp = ExtMSPInstance {
        name: "zero-mspp".to_owned(),
        min_item_separation: 0.0,
        items: vec![],
        strips: ExtMSPStrip {
            height: 1.0,
            max_width: 1.0,
        },
    };

    for (name, serialized) in [
        ("spp", serde_json::to_value(spp).unwrap()),
        ("bpp", serde_json::to_value(bpp).unwrap()),
        ("mspp", serde_json::to_value(mspp).unwrap()),
    ] {
        println!(
            "wire.{name}.has_min_item_separation={}",
            serialized.get("min_item_separation").is_some()
        );
        println!(
            "wire.{name}.min_item_separation={}",
            serialized["min_item_separation"]
        );
    }
}

fn run_solution_import_b8_oracle() {
    use std::panic::{catch_unwind, AssertUnwindSafe};

    let empty_bpp_instance = BPInstance::new(vec![], vec![]);
    let empty_bpp_external = ExtBPInstance {
        name: String::new(),
        min_item_separation: 0.0,
        items: vec![],
        bins: vec![],
    };
    let empty_mspp_strip = Strip::new(
        1.0,
        1.0,
        b11_cde_config(),
        ShapeModifyConfig::default(),
        1.0,
    )
    .unwrap();
    let empty_mspp_instance = MSPInstance::new(vec![], empty_mspp_strip);
    let empty_mspp_external = ExtMSPInstance {
        name: String::new(),
        min_item_separation: 0.0,
        items: vec![],
        strips: ExtMSPStrip {
            height: 1.0,
            max_width: 1.0,
        },
    };

    let previous_panic_hook = std::panic::take_hook();
    std::panic::set_hook(Box::new(|_| {}));
    let bpp_panics = catch_unwind(AssertUnwindSafe(|| {
        jagua_rs::probs::bpp::io::import_solution(&empty_bpp_instance, &empty_bpp_external)
    }))
    .is_err();
    let mspp_panics = catch_unwind(AssertUnwindSafe(|| {
        jagua_rs::probs::mspp::io::import_solution(&empty_mspp_instance, &empty_mspp_external)
    }))
    .is_err();
    std::panic::set_hook(previous_panic_hook);

    println!("import.bpp_solution_unimplemented={bpp_panics}");
    println!("import.mspp_solution_unimplemented={mspp_panics}");

    let spp_instance = SPInstance::new(
        vec![(square_item(0, 1.0), 1)],
        SPStrip::new(10.0, b11_cde_config(), ShapeModifyConfig::default(), 10.0).unwrap(),
    );
    for (name, strip_width) in [
        ("zero", 0.0),
        ("negative", -1.0),
        ("negative_infinity", f32::NEG_INFINITY),
        ("nan", f32::NAN),
    ] {
        let solution = ExtSPSolution {
            strip_width,
            layout: ExtLayout {
                container_id: 0,
                placed_items: vec![],
                density: 0.0,
            },
            density: 0.0,
            run_time_sec: 0,
        };
        match import_spp_solution(&spp_instance, &solution) {
            Err(error) => println!("import.spp_strip_width.{name}=err:{error}"),
            Ok(_) => panic!("non-positive SPP solution strip width was accepted"),
        }
    }
    let invalid_width_before_unknown_item = ExtSPSolution {
        strip_width: 0.0,
        layout: ExtLayout {
            container_id: 0,
            placed_items: vec![ExtPlacedItem {
                item_id: u64::MAX,
                transformation: ExtTransformation {
                    rotation: 0.0,
                    translation: (0.0, 0.0),
                },
            }],
            density: 0.0,
        },
        density: 0.0,
        run_time_sec: 0,
    };
    match import_spp_solution(&spp_instance, &invalid_width_before_unknown_item) {
        Err(error) => println!("import.spp_invalid_width_before_unknown_item=err:{error}"),
        Ok(_) => panic!("solution with an invalid width and unknown item was accepted"),
    }
    let infinite_width_solution = ExtSPSolution {
        strip_width: f32::INFINITY,
        layout: ExtLayout {
            container_id: 0,
            placed_items: vec![],
            density: 0.0,
        },
        density: 0.0,
        run_time_sec: 0,
    };
    let previous_panic_hook = std::panic::take_hook();
    std::panic::set_hook(Box::new(|_| {}));
    let infinite_width_panicked = catch_unwind(AssertUnwindSafe(|| {
        import_spp_solution(&spp_instance, &infinite_width_solution)
    }))
    .is_err();
    std::panic::set_hook(previous_panic_hook);
    println!("import.spp_strip_width.positive_infinity_panicked={infinite_width_panicked}");

    let previous_panic_hook = std::panic::take_hook();
    std::panic::set_hook(Box::new(|_| {}));
    for (name, input_name) in [
        ("zero_unknown_item", "spp_width_zero_unknown_item"),
        ("f32_overflow", "spp_width_f32_overflow"),
    ] {
        let json = solution_import_b8_input(input_name);
        match serde_json::from_str::<ExtSPSolution>(json) {
            Err(error) => println!("json.import.spp_width.{name}=parse_err:{error}"),
            Ok(solution) => {
                if name == "f32_overflow" {
                    println!(
                        "json.import.spp_width.{name}.deserialized_inf={}",
                        solution.strip_width.is_infinite()
                    );
                }
                match catch_unwind(AssertUnwindSafe(|| {
                    import_spp_solution(&spp_instance, &solution)
                })) {
                    Ok(Ok(_)) => println!("json.import.spp_width.{name}=ok"),
                    Ok(Err(error)) => println!("json.import.spp_width.{name}=err:{error}"),
                    Err(_) => println!("json.import.spp_width.{name}=panic"),
                }
            }
        }
    }
    std::panic::set_hook(previous_panic_hook);

    for (name, input_name) in [
        ("unknown_top_level", "spp_solution_unknown_top_level"),
        (
            "duplicate_unknown_top_level",
            "spp_solution_duplicate_unknown_top_level",
        ),
        (
            "duplicate_known_strip_width",
            "spp_solution_duplicate_known_strip_width",
        ),
        ("layout_unknown_field", "spp_layout_unknown_field"),
        (
            "layout_duplicate_unknown_field",
            "spp_layout_duplicate_unknown_field",
        ),
        (
            "layout_duplicate_known_placed_items",
            "spp_layout_duplicate_known_placed_items",
        ),
        ("placed_item_unknown_field", "spp_placed_item_unknown_field"),
        (
            "placed_item_duplicate_unknown_field",
            "spp_placed_item_duplicate_unknown_field",
        ),
        (
            "placed_item_duplicate_known_item_id",
            "spp_placed_item_duplicate_known_item_id",
        ),
        (
            "placed_item_duplicate_known_transformation",
            "spp_placed_item_duplicate_known_transformation",
        ),
        (
            "transformation_unknown_field",
            "spp_transformation_unknown_field",
        ),
        (
            "transformation_duplicate_unknown_field",
            "spp_transformation_duplicate_unknown_field",
        ),
        (
            "transformation_duplicate_known_rotation",
            "spp_transformation_duplicate_known_rotation",
        ),
        (
            "transformation_duplicate_known_translation",
            "spp_transformation_duplicate_known_translation",
        ),
        (
            "solution_duplicate_known_layout",
            "spp_solution_duplicate_known_layout",
        ),
        (
            "solution_duplicate_known_density",
            "spp_solution_duplicate_known_density",
        ),
        (
            "solution_duplicate_known_run_time_sec",
            "spp_solution_duplicate_known_run_time_sec",
        ),
        (
            "layout_duplicate_known_container_id",
            "spp_layout_duplicate_known_container_id",
        ),
        (
            "layout_duplicate_known_density",
            "spp_layout_duplicate_known_density",
        ),
    ] {
        let accepted = serde_json::from_str::<ExtSPSolution>(
            solution_import_b8_input(input_name),
        )
        .is_ok();
        println!("json.spp_solution.{name}_accepted={accepted}");
    }
}

fn solution_import_b8_input(name: &str) -> &'static str {
    const INPUTS: &str =
        include_str!("../../../Tests/Fixtures/RustReference/solution_import_b8_inputs.txt");
    INPUTS
        .lines()
        .find_map(|line| {
            let (key, value) = line.split_once('=')?;
            (key == name).then_some(value)
        })
        .unwrap_or_else(|| panic!("missing B8 solution import JSON input: {name}"))
}

fn json_metadata_b8_input(name: &str) -> &'static str {
    const INPUTS: &str =
        include_str!("../../../Tests/Fixtures/RustReference/json_metadata_b8_inputs.txt");
    INPUTS
        .lines()
        .find_map(|line| {
            let (key, value) = line.split_once('=')?;
            (key == name).then_some(value)
        })
        .unwrap_or_else(|| panic!("missing JSON metadata case: {name}"))
}

fn json_metadata_b8_accepts(name: &str) -> bool {
    let json = json_metadata_b8_input(name);
    serde_json::from_str::<ExtSPInstance>(json).is_ok()
}

fn json_metadata_b8_container_accepts(name: &str) -> bool {
    let json = json_metadata_b8_input(name);
    serde_json::from_str::<ExtContainer>(json).is_ok()
}

fn json_metadata_b8_bpp_accepts(name: &str) -> bool {
    let json = json_metadata_b8_input(name);
    serde_json::from_str::<ExtBPInstance>(json).is_ok()
}

fn json_metadata_b8_mspp_accepts(name: &str) -> bool {
    let json = json_metadata_b8_input(name);
    serde_json::from_str::<ExtMSPInstance>(json).is_ok()
}

fn json_metadata_b8_import_error_class(error: &str) -> &'static str {
    if error.starts_with("Simple polygon must have at least 3 points") {
        "invalid_geometry"
    } else if error.starts_with("Offset resulted in an empty polygon") {
        "offset_failure"
    } else if error == "No support for multipolygons yet" {
        "unsupported_shape"
    } else if error == "bin IDs must be unique" {
        "duplicate_bin_ids"
    } else if error == "instance must have positive bin stock" {
        "no_positive_bin_stock"
    } else {
        "other_error"
    }
}

fn json_metadata_b8_bpp_import_outcome(name: &str, importer: &Importer) -> &'static str {
    let Ok(external) = serde_json::from_str::<ExtBPInstance>(json_metadata_b8_input(name)) else {
        return "parse_error";
    };
    match import_bpp_instance(importer, &external) {
        Ok(_) => "success",
        Err(error) => json_metadata_b8_import_error_class(&error.to_string()),
    }
}

fn json_metadata_b8_mspp_import_outcome(name: &str, importer: &Importer) -> &'static str {
    let Ok(external) = serde_json::from_str::<ExtMSPInstance>(json_metadata_b8_input(name)) else {
        return "parse_error";
    };
    match import_mspp_instance(importer, &external) {
        Ok(_) => "success",
        Err(error) => json_metadata_b8_import_error_class(&error.to_string()),
    }
}

fn json_metadata_b8_append_shape_state(values: &mut Vec<f32>, shape: &SPolygon) {
    let centroid = shape.centroid();
    values.extend([
        shape.n_vertices() as f32,
        shape.area,
        centroid.0,
        centroid.1,
    ]);
    for vertex in &shape.vertices {
        values.extend([vertex.0, vertex.1]);
    }
}

fn json_metadata_b8_imported_item_shape_state(item: &Item) -> String {
    let mut values = Vec::new();
    json_metadata_b8_append_shape_state(&mut values, &item.shape_orig.shape);
    json_metadata_b8_append_shape_state(&mut values, &item.shape_cd);
    values
        .iter()
        .map(|value| format!("{value:.9}"))
        .collect::<Vec<_>>()
        .join(",")
}

fn json_metadata_b8_bpp_import_shape_state(name: &str, importer: &Importer) -> Option<String> {
    let external = serde_json::from_str::<ExtBPInstance>(json_metadata_b8_input(name)).ok()?;
    let instance = import_bpp_instance(importer, &external).ok()?;
    Some(json_metadata_b8_imported_item_shape_state(instance.item(0)))
}

fn json_metadata_b8_mspp_import_shape_state(name: &str, importer: &Importer) -> Option<String> {
    let external = serde_json::from_str::<ExtMSPInstance>(json_metadata_b8_input(name)).ok()?;
    let instance = import_mspp_instance(importer, &external).ok()?;
    Some(json_metadata_b8_imported_item_shape_state(instance.item(0)))
}

fn json_metadata_b8_imported_container_shape_state(container: &Container) -> String {
    let mut values = Vec::new();
    json_metadata_b8_append_shape_state(&mut values, &container.outer_orig.shape);
    json_metadata_b8_append_shape_state(&mut values, &container.outer_cd);
    values
        .iter()
        .map(|value| format!("{value:.9}"))
        .collect::<Vec<_>>()
        .join(",")
}

fn json_metadata_b8_imported_container_quality_shape_state(container: &Container) -> String {
    let mut values = Vec::new();
    json_metadata_b8_append_shape_state(&mut values, &container.outer_orig.shape);
    json_metadata_b8_append_shape_state(&mut values, &container.outer_cd);

    let original_quality_count = container.quality_zones.iter().flatten().count();
    values.push(original_quality_count as f32);
    for (quality, zone) in container.quality_zones.iter().enumerate() {
        if let Some(zone) = zone {
            values.extend([quality as f32, zone.shapes_orig.len() as f32]);
            for shape in &zone.shapes_orig {
                json_metadata_b8_append_shape_state(&mut values, &shape.shape);
            }
        }
    }

    let collision_quality_count = container.quality_zones.iter().flatten().count();
    values.push(collision_quality_count as f32);
    for (quality, zone) in container.quality_zones.iter().enumerate() {
        if let Some(zone) = zone {
            values.extend([quality as f32, zone.shapes_cd.len() as f32]);
            for shape in &zone.shapes_cd {
                json_metadata_b8_append_shape_state(&mut values, shape);
            }
        }
    }

    values
        .iter()
        .map(|value| format!("{value:.9}"))
        .collect::<Vec<_>>()
        .join(",")
}

fn json_metadata_b8_bpp_import_container_shape_state(
    name: &str,
    importer: &Importer,
) -> Option<String> {
    let external = serde_json::from_str::<ExtBPInstance>(json_metadata_b8_input(name)).ok()?;
    let instance = import_bpp_instance(importer, &external).ok()?;
    Some(json_metadata_b8_imported_container_shape_state(
        &instance.bins.first()?.container,
    ))
}

fn json_metadata_b8_bpp_import_container_quality_shape_state(
    name: &str,
    importer: &Importer,
) -> Option<String> {
    let external = serde_json::from_str::<ExtBPInstance>(json_metadata_b8_input(name)).ok()?;
    let instance = import_bpp_instance(importer, &external).ok()?;
    Some(json_metadata_b8_imported_container_quality_shape_state(
        &instance.bins.first()?.container,
    ))
}

fn json_metadata_b8_bpp_import_bin_collection_state(
    name: &str,
    importer: &Importer,
) -> Option<String> {
    let external = serde_json::from_str::<ExtBPInstance>(json_metadata_b8_input(name)).ok()?;
    let instance = import_bpp_instance(importer, &external).ok()?;
    let mut values = vec![instance.bins.len() as f32];
    for bin in &instance.bins {
        values.extend([
            bin.idx as f32,
            bin.external_id as f32,
            bin.stock as f32,
            bin.cost as f32,
        ]);
        json_metadata_b8_append_shape_state(&mut values, &bin.container.outer_orig.shape);
        json_metadata_b8_append_shape_state(&mut values, &bin.container.outer_cd);
    }
    Some(
        values
            .iter()
            .map(|value| format!("{value:.9}"))
            .collect::<Vec<_>>()
            .join(","),
    )
}

fn json_metadata_b8_bpp_import_bin_integer_state(
    name: &str,
    importer: &Importer,
) -> Option<String> {
    let external = serde_json::from_str::<ExtBPInstance>(json_metadata_b8_input(name)).ok()?;
    let instance = import_bpp_instance(importer, &external).ok()?;
    Some(
        instance
            .bins
            .iter()
            .map(|bin| format!("{}/{}/{}/{}", bin.idx, bin.external_id, bin.stock, bin.cost))
            .collect::<Vec<_>>()
            .join(";"),
    )
}

fn json_metadata_b8_imported_container_hazard_probe_state(container: &Container) -> String {
    let probes = [
        ("explicit_q0", rectangle(12.75, 4.75, 13.25, 5.25)),
        ("hole_a", rectangle(4.75, 4.75, 5.25, 5.25)),
        ("hole_b", rectangle(8.75, 4.75, 9.25, 5.25)),
        ("q2_a", rectangle(30.75, 4.75, 31.25, 5.25)),
        ("q2_b", rectangle(25.25, 4.75, 25.75, 5.25)),
        ("q2_c", rectangle(19.75, 5.25, 20.25, 5.75)),
        ("q5", rectangle(5.25, 13.25, 5.75, 13.75)),
    ];
    let mut states = Vec::with_capacity(probes.len());
    for (label, probe) in probes {
        let mut collector = BasicHazardCollector::new();
        container
            .base_cde
            .collect_poly_collisions(&probe, &mut collector);
        let mut entities = collector
            .iter()
            .map(|(_, entity)| match *entity {
                HazardEntity::Hole { idx } => format!("hole:{idx}"),
                HazardEntity::InferiorQualityZone { quality, idx } => {
                    format!("quality:{quality}:{idx}")
                }
                HazardEntity::Exterior => "exterior".to_owned(),
                HazardEntity::PlacedItem { .. } => "placed_item".to_owned(),
            })
            .collect::<Vec<_>>();
        entities.sort();
        assert_eq!(entities.len(), 1, "unexpected hazards at {label}");
        states.push(format!("{label}={}", entities.join(",")));
    }
    states.join(";")
}

fn json_metadata_b8_bpp_import_container_hazard_probe_state(
    name: &str,
    importer: &Importer,
) -> Option<String> {
    let external = serde_json::from_str::<ExtBPInstance>(json_metadata_b8_input(name)).ok()?;
    let instance = import_bpp_instance(importer, &external).ok()?;
    Some(json_metadata_b8_imported_container_hazard_probe_state(
        &instance.bins.first()?.container,
    ))
}

fn json_metadata_b8_mspp_strip_container_shape_state(
    name: &str,
    importer: &Importer,
) -> Option<String> {
    let external = serde_json::from_str::<ExtMSPInstance>(json_metadata_b8_input(name)).ok()?;
    let instance = import_mspp_instance(importer, &external).ok()?;
    let container = Container::try_from(instance.base_strip).ok()?;
    Some(json_metadata_b8_imported_container_shape_state(&container))
}

fn json_metadata_b8_mspp_strip_container_outcome(name: &str, importer: &Importer) -> String {
    let Ok(external) = serde_json::from_str::<ExtMSPInstance>(json_metadata_b8_input(name)) else {
        return "instance=parse_error;container=not_reached".to_owned();
    };
    let instance = match import_mspp_instance(importer, &external) {
        Ok(instance) => instance,
        Err(error) => {
            return format!(
                "instance={};container=not_reached",
                json_metadata_b8_import_error_class(&error.to_string())
            );
        }
    };
    match Container::try_from(instance.base_strip) {
        Ok(_) => "instance=success;container=success".to_owned(),
        Err(error) => format!(
            "instance=success;container={}",
            json_metadata_b8_import_error_class(&error.to_string())
        ),
    }
}

fn json_metadata_b8_bpp_solution_accepts(name: &str) -> bool {
    let json = json_metadata_b8_input(name);
    serde_json::from_str::<ExtBPSolution>(json).is_ok()
}

fn json_metadata_b8_mspp_solution_accepts(name: &str) -> bool {
    let json = json_metadata_b8_input(name);
    serde_json::from_str::<ExtMSPSolution>(json).is_ok()
}

fn run_json_metadata_b8_oracle() {
    println!(
        "json.unknown_top_level_accepted={}",
        json_metadata_b8_accepts("unknown_top_level")
    );
    println!(
        "json.duplicate_unknown_top_level_accepted={}",
        json_metadata_b8_accepts("duplicate_unknown_top_level")
    );
    println!(
        "json.unknown_item_metadata_accepted={}",
        json_metadata_b8_accepts("unknown_item_metadata")
    );
    println!(
        "json.duplicate_unknown_item_metadata_accepted={}",
        json_metadata_b8_accepts("duplicate_unknown_item_metadata")
    );
    println!(
        "json.duplicate_known_item_id_accepted={}",
        json_metadata_b8_accepts("duplicate_known_item_id")
    );
    println!(
        "json.unknown_rotation_field_accepted={}",
        json_metadata_b8_accepts("unknown_rotation_field")
    );
    for (input_name, output_name) in [
        (
            "duplicate_known_instance_name",
            "json.duplicate_known_instance_name_accepted",
        ),
        (
            "duplicate_known_min_item_separation",
            "json.duplicate_known_min_item_separation_accepted",
        ),
        (
            "duplicate_known_instance_items",
            "json.duplicate_known_instance_items_accepted",
        ),
        (
            "duplicate_known_strip_height",
            "json.duplicate_known_strip_height_accepted",
        ),
        (
            "duplicate_known_item_demand",
            "json.duplicate_known_item_demand_accepted",
        ),
        (
            "duplicate_known_item_orientation",
            "json.duplicate_known_item_orientation_accepted",
        ),
        (
            "duplicate_known_item_shape",
            "json.duplicate_known_item_shape_accepted",
        ),
        (
            "duplicate_known_item_min_quality",
            "json.duplicate_known_item_min_quality_accepted",
        ),
        (
            "duplicate_known_orientation_rotation",
            "json.duplicate_known_orientation_rotation_accepted",
        ),
        (
            "duplicate_known_rotation_mode",
            "json.duplicate_known_rotation_mode_accepted",
        ),
        (
            "duplicate_known_rotation_angles",
            "json.duplicate_known_rotation_angles_accepted",
        ),
        (
            "duplicate_known_rotation_step",
            "json.duplicate_known_rotation_step_accepted",
        ),
        (
            "duplicate_known_shape_type",
            "json.duplicate_known_shape_type_accepted",
        ),
        (
            "duplicate_known_shape_data",
            "json.duplicate_known_shape_data_accepted",
        ),
        (
            "duplicate_known_rectangle_x_min",
            "json.duplicate_known_rectangle_x_min_accepted",
        ),
        (
            "duplicate_known_rectangle_y_min",
            "json.duplicate_known_rectangle_y_min_accepted",
        ),
        (
            "duplicate_known_rectangle_width",
            "json.duplicate_known_rectangle_width_accepted",
        ),
        (
            "duplicate_known_rectangle_height",
            "json.duplicate_known_rectangle_height_accepted",
        ),
        (
            "duplicate_known_polygon_outer",
            "json.duplicate_known_polygon_outer_accepted",
        ),
        (
            "duplicate_known_polygon_inner",
            "json.duplicate_known_polygon_inner_accepted",
        ),
    ] {
        println!(
            "{output_name}={}",
            json_metadata_b8_accepts(input_name)
        );
    }
    for (input_name, output_name) in [
        (
            "container_unknown_field",
            "json.container_unknown_field_accepted",
        ),
        (
            "container_duplicate_unknown_field",
            "json.container_duplicate_unknown_field_accepted",
        ),
        (
            "container_duplicate_known_id",
            "json.container_duplicate_known_id_accepted",
        ),
        (
            "container_duplicate_known_shape",
            "json.container_duplicate_known_shape_accepted",
        ),
        (
            "container_duplicate_known_zones",
            "json.container_duplicate_known_zones_accepted",
        ),
        (
            "quality_zone_unknown_field",
            "json.quality_zone_unknown_field_accepted",
        ),
        (
            "quality_zone_duplicate_unknown_field",
            "json.quality_zone_duplicate_unknown_field_accepted",
        ),
        (
            "quality_zone_duplicate_known_quality",
            "json.quality_zone_duplicate_known_quality_accepted",
        ),
        (
            "quality_zone_duplicate_known_shape",
            "json.quality_zone_duplicate_known_shape_accepted",
        ),
    ] {
        println!(
            "{output_name}={}",
            json_metadata_b8_container_accepts(input_name)
        );
    }
    for (input_name, output_name) in [
        ("bpp_bin_unknown_field", "json.bpp_bin_unknown_field_accepted"),
        (
            "bpp_bin_duplicate_unknown_field",
            "json.bpp_bin_duplicate_unknown_field_accepted",
        ),
        (
            "bpp_bin_duplicate_known_stock",
            "json.bpp_bin_duplicate_known_stock_accepted",
        ),
        (
            "bpp_bin_duplicate_known_cost",
            "json.bpp_bin_duplicate_known_cost_accepted",
        ),
        (
            "bpp_bin_duplicate_known_id",
            "json.bpp_bin_duplicate_known_id_accepted",
        ),
        (
            "bpp_bin_duplicate_known_shape",
            "json.bpp_bin_duplicate_known_shape_accepted",
        ),
        (
            "bpp_bin_duplicate_known_zones",
            "json.bpp_bin_duplicate_known_zones_accepted",
        ),
        (
            "bpp_instance_duplicate_known_bins",
            "json.bpp_instance_duplicate_known_bins_accepted",
        ),
    ] {
        println!(
            "{output_name}={}",
            json_metadata_b8_bpp_accepts(input_name)
        );
    }
    for (input_name, output_name) in [
        (
            "bpp_instance_unknown_field",
            "json.bpp_instance_unknown_field_accepted",
        ),
        (
            "bpp_instance_duplicate_unknown_field",
            "json.bpp_instance_duplicate_unknown_field_accepted",
        ),
        (
            "bpp_instance_duplicate_known_name",
            "json.bpp_instance_duplicate_known_name_accepted",
        ),
        (
            "bpp_instance_duplicate_known_min_item_separation",
            "json.bpp_instance_duplicate_known_min_item_separation_accepted",
        ),
        (
            "bpp_instance_duplicate_known_items",
            "json.bpp_instance_duplicate_known_items_accepted",
        ),
        (
            "bpp_item_unknown_field",
            "json.bpp_item_unknown_field_accepted",
        ),
        (
            "bpp_item_duplicate_unknown_field",
            "json.bpp_item_duplicate_unknown_field_accepted",
        ),
        (
            "bpp_item_duplicate_known_id",
            "json.bpp_item_duplicate_known_id_accepted",
        ),
        (
            "bpp_item_duplicate_known_demand",
            "json.bpp_item_duplicate_known_demand_accepted",
        ),
    ] {
        println!("{output_name}={}", json_metadata_b8_bpp_accepts(input_name));
    }
    for (input_name, output_name) in [
        (
            "mspp_strip_unknown_field",
            "json.mspp_strip_unknown_field_accepted",
        ),
        (
            "mspp_strip_duplicate_unknown_field",
            "json.mspp_strip_duplicate_unknown_field_accepted",
        ),
        (
            "mspp_strip_duplicate_known_height",
            "json.mspp_strip_duplicate_known_height_accepted",
        ),
        (
            "mspp_strip_duplicate_known_max_width",
            "json.mspp_strip_duplicate_known_max_width_accepted",
        ),
        (
            "mspp_instance_duplicate_known_strips",
            "json.mspp_instance_duplicate_known_strips_accepted",
        ),
    ] {
        println!(
            "{output_name}={}",
            json_metadata_b8_mspp_accepts(input_name)
        );
    }
    for (input_name, output_name) in [
        (
            "mspp_instance_unknown_field",
            "json.mspp_instance_unknown_field_accepted",
        ),
        (
            "mspp_instance_duplicate_unknown_field",
            "json.mspp_instance_duplicate_unknown_field_accepted",
        ),
        (
            "mspp_instance_duplicate_known_name",
            "json.mspp_instance_duplicate_known_name_accepted",
        ),
        (
            "mspp_instance_duplicate_known_min_item_separation",
            "json.mspp_instance_duplicate_known_min_item_separation_accepted",
        ),
        (
            "mspp_instance_duplicate_known_items",
            "json.mspp_instance_duplicate_known_items_accepted",
        ),
        (
            "mspp_item_unknown_field",
            "json.mspp_item_unknown_field_accepted",
        ),
        (
            "mspp_item_duplicate_unknown_field",
            "json.mspp_item_duplicate_unknown_field_accepted",
        ),
        (
            "mspp_item_duplicate_known_id",
            "json.mspp_item_duplicate_known_id_accepted",
        ),
        (
            "mspp_item_duplicate_known_demand",
            "json.mspp_item_duplicate_known_demand_accepted",
        ),
    ] {
        println!(
            "{output_name}={}",
            json_metadata_b8_mspp_accepts(input_name)
        );
    }

    for (input_name, output_name) in [
        (
            "bpp_solution_unknown_field",
            "json.bpp_solution_unknown_field_accepted",
        ),
        (
            "bpp_solution_duplicate_unknown_field",
            "json.bpp_solution_duplicate_unknown_field_accepted",
        ),
        (
            "bpp_solution_duplicate_known_cost",
            "json.bpp_solution_duplicate_known_cost_accepted",
        ),
        (
            "bpp_solution_duplicate_known_layouts",
            "json.bpp_solution_duplicate_known_layouts_accepted",
        ),
        (
            "bpp_solution_duplicate_known_density",
            "json.bpp_solution_duplicate_known_density_accepted",
        ),
        (
            "bpp_solution_duplicate_known_run_time_sec",
            "json.bpp_solution_duplicate_known_run_time_sec_accepted",
        ),
    ] {
        println!(
            "{output_name}={}",
            json_metadata_b8_bpp_solution_accepts(input_name)
        );
    }
    for (input_name, output_name) in [
        (
            "mspp_solution_unknown_field",
            "json.mspp_solution_unknown_field_accepted",
        ),
        (
            "mspp_solution_duplicate_unknown_field",
            "json.mspp_solution_duplicate_unknown_field_accepted",
        ),
        (
            "mspp_solution_duplicate_known_layouts",
            "json.mspp_solution_duplicate_known_layouts_accepted",
        ),
        (
            "mspp_solution_duplicate_known_containers",
            "json.mspp_solution_duplicate_known_containers_accepted",
        ),
        (
            "mspp_solution_duplicate_known_density",
            "json.mspp_solution_duplicate_known_density_accepted",
        ),
        (
            "mspp_solution_duplicate_known_run_time_sec",
            "json.mspp_solution_duplicate_known_run_time_sec_accepted",
        ),
    ] {
        println!(
            "{output_name}={}",
            json_metadata_b8_mspp_solution_accepts(input_name)
        );
    }

    for (input_name, output_name) in [
        (
            "item_min_quality_missing",
            "json.item_missing_min_quality_emits_null",
        ),
        (
            "item_min_quality_null",
            "json.item_null_min_quality_emits_null",
        ),
    ] {
        let item: ExtItem = serde_json::from_str(json_metadata_b8_input(input_name))
            .expect("valid ExtItem fixture");
        let serialized = serde_json::to_value(item).expect("ExtItem serializes");
        println!(
            "{output_name}={}",
            serialized
                .get("min_quality")
                .is_some_and(serde_json::Value::is_null)
        );
    }

    let item: ExtItem = serde_json::from_str(json_metadata_b8_input("item_min_quality_some"))
        .expect("valid ExtItem fixture");
    let serialized = serde_json::to_value(item).expect("ExtItem serializes");
    println!(
        "json.item_present_min_quality_emits_value={}",
        serialized.get("min_quality") == Some(&serde_json::Value::from(2))
    );

    let bpp_instance: ExtBPInstance =
        serde_json::from_str(json_metadata_b8_input("bpp_instance_serialization"))
            .expect("valid BPP instance serialization fixture");
    println!(
        "json.serialize.bpp_instance={}",
        serde_json::to_string(&bpp_instance).expect("BPP instance serializes")
    );
    let bpp_u64_boundary: ExtBPInstance =
        serde_json::from_str(json_metadata_b8_input("bpp_multi_bin_u64_boundary_order"))
            .expect("valid BPP u64-boundary serialization fixture");
    let bpp_u64_serialized =
        serde_json::to_string(&bpp_u64_boundary).expect("BPP u64-boundary instance serializes");
    println!("json.serialize.bpp_u64_boundary_instance={bpp_u64_serialized}");
    let bpp_u64_roundtrip: ExtBPInstance = serde_json::from_str(&bpp_u64_serialized)
        .expect("BPP u64-boundary serialization roundtrips");
    let bin_integer_state = bpp_u64_roundtrip
        .bins
        .iter()
        .map(|bin| format!("{}/{}/{}", bin.base.id, bin.stock, bin.cost))
        .collect::<Vec<_>>()
        .join(";");
    println!("json.serialize.bpp_u64_boundary_bin_integer_state={bin_integer_state}");
    let mspp_instance: ExtMSPInstance =
        serde_json::from_str(json_metadata_b8_input("mspp_instance_serialization"))
            .expect("valid MSP instance serialization fixture");
    println!(
        "json.serialize.mspp_instance={}",
        serde_json::to_string(&mspp_instance).expect("MSP instance serializes")
    );
    let bpp_solution: ExtBPSolution =
        serde_json::from_str(json_metadata_b8_input("bpp_solution_serialization"))
            .expect("valid BPP solution serialization fixture");
    println!(
        "json.serialize.bpp_solution={}",
        serde_json::to_string(&bpp_solution).expect("BPP solution serializes")
    );
    let mspp_solution: ExtMSPSolution =
        serde_json::from_str(json_metadata_b8_input("mspp_solution_serialization"))
            .expect("valid MSP solution serialization fixture");
    println!(
        "json.serialize.mspp_solution={}",
        serde_json::to_string(&mspp_solution).expect("MSP solution serializes")
    );
    for (input_name, output_name) in [
        (
            "bpp_item_duplicate_known_orientation",
            "json.bpp_item_duplicate_known_orientation_accepted",
        ),
        (
            "bpp_item_duplicate_known_shape",
            "json.bpp_item_duplicate_known_shape_accepted",
        ),
        (
            "bpp_item_duplicate_known_min_quality",
            "json.bpp_item_duplicate_known_min_quality_accepted",
        ),
    ] {
        println!("{output_name}={}", json_metadata_b8_bpp_accepts(input_name));
    }
    for (input_name, output_name) in [
        (
            "mspp_item_duplicate_known_orientation",
            "json.mspp_item_duplicate_known_orientation_accepted",
        ),
        (
            "mspp_item_duplicate_known_shape",
            "json.mspp_item_duplicate_known_shape_accepted",
        ),
        (
            "mspp_item_duplicate_known_min_quality",
            "json.mspp_item_duplicate_known_min_quality_accepted",
        ),
    ] {
        println!(
            "{output_name}={}",
            json_metadata_b8_mspp_accepts(input_name)
        );
    }
    for (input_name, output_name) in [
        (
            "bpp_instance_missing_min_item_separation",
            "json.bpp_instance_missing_min_item_separation_accepted",
        ),
        (
            "bpp_instance_null_min_item_separation",
            "json.bpp_instance_null_min_item_separation_accepted",
        ),
        (
            "bpp_instance_f32_overflow_min_item_separation",
            "json.bpp_instance_f32_overflow_min_item_separation_accepted",
        ),
        (
            "bpp_item_u64_overflow_demand",
            "json.bpp_item_u64_overflow_demand_accepted",
        ),
        (
            "bpp_bin_u64_overflow_stock",
            "json.bpp_bin_u64_overflow_stock_accepted",
        ),
        (
            "bpp_bin_u64_overflow_cost",
            "json.bpp_bin_u64_overflow_cost_accepted",
        ),
    ] {
        println!("{output_name}={}", json_metadata_b8_bpp_accepts(input_name));
    }
    for (input_name, output_name) in [
        (
            "mspp_instance_missing_min_item_separation",
            "json.mspp_instance_missing_min_item_separation_accepted",
        ),
        (
            "mspp_instance_null_min_item_separation",
            "json.mspp_instance_null_min_item_separation_accepted",
        ),
        (
            "mspp_instance_f32_overflow_min_item_separation",
            "json.mspp_instance_f32_overflow_min_item_separation_accepted",
        ),
        (
            "mspp_strip_f32_overflow_height",
            "json.mspp_strip_f32_overflow_height_accepted",
        ),
        (
            "mspp_strip_f32_overflow_max_width",
            "json.mspp_strip_f32_overflow_max_width_accepted",
        ),
        (
            "mspp_item_u64_overflow_demand",
            "json.mspp_item_u64_overflow_demand_accepted",
        ),
    ] {
        println!(
            "{output_name}={}",
            json_metadata_b8_mspp_accepts(input_name)
        );
    }
    for (input_name, output_name) in [
        ("bpp_bin_missing_id", "json.bpp_bin_missing_id_accepted"),
        ("bpp_bin_null_id", "json.bpp_bin_null_id_accepted"),
        ("bpp_bin_missing_shape", "json.bpp_bin_missing_shape_accepted"),
        ("bpp_bin_null_shape", "json.bpp_bin_null_shape_accepted"),
        (
            "bpp_bin_missing_stock",
            "json.bpp_bin_missing_stock_accepted",
        ),
        ("bpp_bin_null_stock", "json.bpp_bin_null_stock_accepted"),
        ("bpp_bin_missing_cost", "json.bpp_bin_missing_cost_accepted"),
        ("bpp_bin_null_cost", "json.bpp_bin_null_cost_accepted"),
        (
            "bpp_bin_missing_zones",
            "json.bpp_bin_missing_zones_accepted",
        ),
        ("bpp_bin_null_zones", "json.bpp_bin_null_zones_accepted"),
    ] {
        println!("{output_name}={}", json_metadata_b8_bpp_accepts(input_name));
    }
    for (input_name, output_name) in [
        (
            "mspp_strip_missing_height",
            "json.mspp_strip_missing_height_accepted",
        ),
        (
            "mspp_strip_null_height",
            "json.mspp_strip_null_height_accepted",
        ),
        (
            "mspp_strip_missing_max_width",
            "json.mspp_strip_missing_max_width_accepted",
        ),
        (
            "mspp_strip_null_max_width",
            "json.mspp_strip_null_max_width_accepted",
        ),
    ] {
        println!(
            "{output_name}={}",
            json_metadata_b8_mspp_accepts(input_name)
        );
    }
    for (input_name, output_name) in [
        ("bpp_bin_string_id", "json.bpp_bin_string_id_accepted"),
        ("bpp_bin_string_shape", "json.bpp_bin_string_shape_accepted"),
        (
            "bpp_bin_fractional_stock",
            "json.bpp_bin_fractional_stock_accepted",
        ),
        (
            "bpp_bin_negative_cost",
            "json.bpp_bin_negative_cost_accepted",
        ),
        (
            "bpp_bin_object_zones",
            "json.bpp_bin_object_zones_accepted",
        ),
        (
            "bpp_instance_null_bins",
            "json.bpp_instance_null_bins_accepted",
        ),
        (
            "bpp_instance_object_bins",
            "json.bpp_instance_object_bins_accepted",
        ),
    ] {
        println!("{output_name}={}", json_metadata_b8_bpp_accepts(input_name));
    }
    for (input_name, output_name) in [
        (
            "mspp_strip_string_height",
            "json.mspp_strip_string_height_accepted",
        ),
        (
            "mspp_strip_boolean_max_width",
            "json.mspp_strip_boolean_max_width_accepted",
        ),
        (
            "mspp_instance_null_strips",
            "json.mspp_instance_null_strips_accepted",
        ),
        (
            "mspp_instance_array_strips",
            "json.mspp_instance_array_strips_accepted",
        ),
    ] {
        println!(
            "{output_name}={}",
            json_metadata_b8_mspp_accepts(input_name)
        );
    }
    for (input_name, output_name) in [
        (
            "bpp_instance_number_name",
            "json.bpp_instance_number_name_accepted",
        ),
        (
            "bpp_instance_string_min_item_separation",
            "json.bpp_instance_string_min_item_separation_accepted",
        ),
        (
            "bpp_instance_null_items",
            "json.bpp_instance_null_items_accepted",
        ),
        (
            "bpp_instance_object_items",
            "json.bpp_instance_object_items_accepted",
        ),
        ("bpp_item_string_id", "json.bpp_item_string_id_accepted"),
        (
            "bpp_item_fractional_demand",
            "json.bpp_item_fractional_demand_accepted",
        ),
        (
            "bpp_item_string_orientation",
            "json.bpp_item_string_orientation_accepted",
        ),
        (
            "bpp_item_string_shape",
            "json.bpp_item_string_shape_accepted",
        ),
        (
            "bpp_item_string_min_quality",
            "json.bpp_item_string_min_quality_accepted",
        ),
    ] {
        println!("{output_name}={}", json_metadata_b8_bpp_accepts(input_name));
    }
    for (input_name, output_name) in [
        (
            "mspp_instance_number_name",
            "json.mspp_instance_number_name_accepted",
        ),
        (
            "mspp_instance_string_min_item_separation",
            "json.mspp_instance_string_min_item_separation_accepted",
        ),
        (
            "mspp_instance_null_items",
            "json.mspp_instance_null_items_accepted",
        ),
        (
            "mspp_instance_object_items",
            "json.mspp_instance_object_items_accepted",
        ),
        ("mspp_item_string_id", "json.mspp_item_string_id_accepted"),
        (
            "mspp_item_fractional_demand",
            "json.mspp_item_fractional_demand_accepted",
        ),
        (
            "mspp_item_string_orientation",
            "json.mspp_item_string_orientation_accepted",
        ),
        (
            "mspp_item_string_shape",
            "json.mspp_item_string_shape_accepted",
        ),
        (
            "mspp_item_string_min_quality",
            "json.mspp_item_string_min_quality_accepted",
        ),
    ] {
        println!(
            "{output_name}={}",
            json_metadata_b8_mspp_accepts(input_name)
        );
    }

    for (input_name, output_name) in [
        (
            "bpp_item_missing_orientation",
            "json.bpp_item_missing_orientation_accepted",
        ),
        (
            "bpp_item_null_orientation",
            "json.bpp_item_null_orientation_accepted",
        ),
        (
            "bpp_item_empty_orientation",
            "json.bpp_item_empty_orientation_accepted",
        ),
        (
            "bpp_item_null_rotation",
            "json.bpp_item_null_rotation_accepted",
        ),
        (
            "bpp_item_empty_rotation",
            "json.bpp_item_empty_rotation_accepted",
        ),
        (
            "bpp_item_null_rotation_mode",
            "json.bpp_item_null_rotation_mode_accepted",
        ),
        (
            "bpp_item_unknown_rotation_mode",
            "json.bpp_item_unknown_rotation_mode_accepted",
        ),
        (
            "bpp_item_continuous_extra_step",
            "json.bpp_item_continuous_extra_step_accepted",
        ),
        (
            "bpp_item_discrete_missing_angles",
            "json.bpp_item_discrete_missing_angles_accepted",
        ),
        (
            "bpp_item_discrete_null_angles",
            "json.bpp_item_discrete_null_angles_accepted",
        ),
        (
            "bpp_item_stepped_missing_step",
            "json.bpp_item_stepped_missing_step_accepted",
        ),
        (
            "bpp_item_stepped_null_step",
            "json.bpp_item_stepped_null_step_accepted",
        ),
    ] {
        println!("{output_name}={}", json_metadata_b8_bpp_accepts(input_name));
    }
    for (input_name, output_name) in [
        (
            "mspp_item_missing_orientation",
            "json.mspp_item_missing_orientation_accepted",
        ),
        (
            "mspp_item_null_orientation",
            "json.mspp_item_null_orientation_accepted",
        ),
        (
            "mspp_item_empty_orientation",
            "json.mspp_item_empty_orientation_accepted",
        ),
        (
            "mspp_item_null_rotation",
            "json.mspp_item_null_rotation_accepted",
        ),
        (
            "mspp_item_empty_rotation",
            "json.mspp_item_empty_rotation_accepted",
        ),
        (
            "mspp_item_null_rotation_mode",
            "json.mspp_item_null_rotation_mode_accepted",
        ),
        (
            "mspp_item_unknown_rotation_mode",
            "json.mspp_item_unknown_rotation_mode_accepted",
        ),
        (
            "mspp_item_continuous_extra_step",
            "json.mspp_item_continuous_extra_step_accepted",
        ),
        (
            "mspp_item_discrete_missing_angles",
            "json.mspp_item_discrete_missing_angles_accepted",
        ),
        (
            "mspp_item_discrete_null_angles",
            "json.mspp_item_discrete_null_angles_accepted",
        ),
        (
            "mspp_item_stepped_missing_step",
            "json.mspp_item_stepped_missing_step_accepted",
        ),
        (
            "mspp_item_stepped_null_step",
            "json.mspp_item_stepped_null_step_accepted",
        ),
    ] {
        println!(
            "{output_name}={}",
            json_metadata_b8_mspp_accepts(input_name)
        );
    }

    for (input_name, output_name) in [
        (
            "bpp_item_missing_shape",
            "json.bpp_item_missing_shape_accepted",
        ),
        ("bpp_item_null_shape", "json.bpp_item_null_shape_accepted"),
        (
            "bpp_item_missing_shape_type",
            "json.bpp_item_missing_shape_type_accepted",
        ),
        (
            "bpp_item_null_shape_type",
            "json.bpp_item_null_shape_type_accepted",
        ),
        (
            "bpp_item_number_shape_type",
            "json.bpp_item_number_shape_type_accepted",
        ),
        (
            "bpp_item_unknown_shape_type",
            "json.bpp_item_unknown_shape_type_accepted",
        ),
        (
            "bpp_item_missing_shape_data",
            "json.bpp_item_missing_shape_data_accepted",
        ),
        (
            "bpp_item_null_shape_data",
            "json.bpp_item_null_shape_data_accepted",
        ),
        (
            "bpp_item_array_shape_data",
            "json.bpp_item_array_shape_data_accepted",
        ),
        (
            "bpp_item_rectangle_missing_x_min",
            "json.bpp_item_rectangle_missing_x_min_accepted",
        ),
        (
            "bpp_item_rectangle_null_x_min",
            "json.bpp_item_rectangle_null_x_min_accepted",
        ),
        (
            "bpp_item_rectangle_missing_y_min",
            "json.bpp_item_rectangle_missing_y_min_accepted",
        ),
        (
            "bpp_item_rectangle_null_y_min",
            "json.bpp_item_rectangle_null_y_min_accepted",
        ),
        (
            "bpp_item_rectangle_missing_width",
            "json.bpp_item_rectangle_missing_width_accepted",
        ),
        (
            "bpp_item_rectangle_null_width",
            "json.bpp_item_rectangle_null_width_accepted",
        ),
        (
            "bpp_item_rectangle_missing_height",
            "json.bpp_item_rectangle_missing_height_accepted",
        ),
        (
            "bpp_item_rectangle_null_height",
            "json.bpp_item_rectangle_null_height_accepted",
        ),
    ] {
        println!("{output_name}={}", json_metadata_b8_bpp_accepts(input_name));
    }
    for (input_name, output_name) in [
        (
            "mspp_item_missing_shape",
            "json.mspp_item_missing_shape_accepted",
        ),
        ("mspp_item_null_shape", "json.mspp_item_null_shape_accepted"),
        (
            "mspp_item_missing_shape_type",
            "json.mspp_item_missing_shape_type_accepted",
        ),
        (
            "mspp_item_null_shape_type",
            "json.mspp_item_null_shape_type_accepted",
        ),
        (
            "mspp_item_number_shape_type",
            "json.mspp_item_number_shape_type_accepted",
        ),
        (
            "mspp_item_unknown_shape_type",
            "json.mspp_item_unknown_shape_type_accepted",
        ),
        (
            "mspp_item_missing_shape_data",
            "json.mspp_item_missing_shape_data_accepted",
        ),
        (
            "mspp_item_null_shape_data",
            "json.mspp_item_null_shape_data_accepted",
        ),
        (
            "mspp_item_array_shape_data",
            "json.mspp_item_array_shape_data_accepted",
        ),
        (
            "mspp_item_rectangle_missing_x_min",
            "json.mspp_item_rectangle_missing_x_min_accepted",
        ),
        (
            "mspp_item_rectangle_null_x_min",
            "json.mspp_item_rectangle_null_x_min_accepted",
        ),
        (
            "mspp_item_rectangle_missing_y_min",
            "json.mspp_item_rectangle_missing_y_min_accepted",
        ),
        (
            "mspp_item_rectangle_null_y_min",
            "json.mspp_item_rectangle_null_y_min_accepted",
        ),
        (
            "mspp_item_rectangle_missing_width",
            "json.mspp_item_rectangle_missing_width_accepted",
        ),
        (
            "mspp_item_rectangle_null_width",
            "json.mspp_item_rectangle_null_width_accepted",
        ),
        (
            "mspp_item_rectangle_missing_height",
            "json.mspp_item_rectangle_missing_height_accepted",
        ),
        (
            "mspp_item_rectangle_null_height",
            "json.mspp_item_rectangle_null_height_accepted",
        ),
    ] {
        println!(
            "{output_name}={}",
            json_metadata_b8_mspp_accepts(input_name)
        );
    }

    for (input_name, output_name) in [
        (
            "bpp_item_simple_polygon_null_data",
            "json.bpp_item_simple_polygon_null_data_accepted",
        ),
        (
            "bpp_item_simple_polygon_object_data",
            "json.bpp_item_simple_polygon_object_data_accepted",
        ),
        (
            "bpp_item_simple_polygon_null_point",
            "json.bpp_item_simple_polygon_null_point_accepted",
        ),
        (
            "bpp_item_simple_polygon_number_point",
            "json.bpp_item_simple_polygon_number_point_accepted",
        ),
        (
            "bpp_item_simple_polygon_one_coordinate",
            "json.bpp_item_simple_polygon_one_coordinate_accepted",
        ),
        (
            "bpp_item_simple_polygon_three_coordinates",
            "json.bpp_item_simple_polygon_three_coordinates_accepted",
        ),
        (
            "bpp_item_simple_polygon_string_coordinate",
            "json.bpp_item_simple_polygon_string_coordinate_accepted",
        ),
        (
            "bpp_item_simple_polygon_null_coordinate",
            "json.bpp_item_simple_polygon_null_coordinate_accepted",
        ),
        (
            "bpp_item_simple_polygon_empty_points",
            "json.bpp_item_simple_polygon_empty_points_accepted",
        ),
        (
            "bpp_item_polygon_array_data",
            "json.bpp_item_polygon_array_data_accepted",
        ),
        (
            "bpp_item_polygon_missing_outer",
            "json.bpp_item_polygon_missing_outer_accepted",
        ),
        (
            "bpp_item_polygon_null_outer",
            "json.bpp_item_polygon_null_outer_accepted",
        ),
        (
            "bpp_item_polygon_object_outer",
            "json.bpp_item_polygon_object_outer_accepted",
        ),
        (
            "bpp_item_polygon_null_inner",
            "json.bpp_item_polygon_null_inner_accepted",
        ),
        (
            "bpp_item_polygon_object_inner",
            "json.bpp_item_polygon_object_inner_accepted",
        ),
        (
            "bpp_item_polygon_missing_inner",
            "json.bpp_item_polygon_missing_inner_accepted",
        ),
        (
            "bpp_item_multipolygon_null_data",
            "json.bpp_item_multipolygon_null_data_accepted",
        ),
        (
            "bpp_item_multipolygon_object_data",
            "json.bpp_item_multipolygon_object_data_accepted",
        ),
        (
            "bpp_item_multipolygon_null_element",
            "json.bpp_item_multipolygon_null_element_accepted",
        ),
        (
            "bpp_item_multipolygon_missing_outer",
            "json.bpp_item_multipolygon_missing_outer_accepted",
        ),
        (
            "bpp_item_multipolygon_null_outer",
            "json.bpp_item_multipolygon_null_outer_accepted",
        ),
        (
            "bpp_item_multipolygon_null_inner",
            "json.bpp_item_multipolygon_null_inner_accepted",
        ),
        (
            "bpp_item_multipolygon_empty_data",
            "json.bpp_item_multipolygon_empty_data_accepted",
        ),
    ] {
        println!("{output_name}={}", json_metadata_b8_bpp_accepts(input_name));
    }
    for (input_name, output_name) in [
        (
            "mspp_item_simple_polygon_null_data",
            "json.mspp_item_simple_polygon_null_data_accepted",
        ),
        (
            "mspp_item_simple_polygon_object_data",
            "json.mspp_item_simple_polygon_object_data_accepted",
        ),
        (
            "mspp_item_simple_polygon_null_point",
            "json.mspp_item_simple_polygon_null_point_accepted",
        ),
        (
            "mspp_item_simple_polygon_number_point",
            "json.mspp_item_simple_polygon_number_point_accepted",
        ),
        (
            "mspp_item_simple_polygon_one_coordinate",
            "json.mspp_item_simple_polygon_one_coordinate_accepted",
        ),
        (
            "mspp_item_simple_polygon_three_coordinates",
            "json.mspp_item_simple_polygon_three_coordinates_accepted",
        ),
        (
            "mspp_item_simple_polygon_string_coordinate",
            "json.mspp_item_simple_polygon_string_coordinate_accepted",
        ),
        (
            "mspp_item_simple_polygon_null_coordinate",
            "json.mspp_item_simple_polygon_null_coordinate_accepted",
        ),
        (
            "mspp_item_simple_polygon_empty_points",
            "json.mspp_item_simple_polygon_empty_points_accepted",
        ),
        (
            "mspp_item_polygon_array_data",
            "json.mspp_item_polygon_array_data_accepted",
        ),
        (
            "mspp_item_polygon_missing_outer",
            "json.mspp_item_polygon_missing_outer_accepted",
        ),
        (
            "mspp_item_polygon_null_outer",
            "json.mspp_item_polygon_null_outer_accepted",
        ),
        (
            "mspp_item_polygon_object_outer",
            "json.mspp_item_polygon_object_outer_accepted",
        ),
        (
            "mspp_item_polygon_null_inner",
            "json.mspp_item_polygon_null_inner_accepted",
        ),
        (
            "mspp_item_polygon_object_inner",
            "json.mspp_item_polygon_object_inner_accepted",
        ),
        (
            "mspp_item_polygon_missing_inner",
            "json.mspp_item_polygon_missing_inner_accepted",
        ),
        (
            "mspp_item_multipolygon_null_data",
            "json.mspp_item_multipolygon_null_data_accepted",
        ),
        (
            "mspp_item_multipolygon_object_data",
            "json.mspp_item_multipolygon_object_data_accepted",
        ),
        (
            "mspp_item_multipolygon_null_element",
            "json.mspp_item_multipolygon_null_element_accepted",
        ),
        (
            "mspp_item_multipolygon_missing_outer",
            "json.mspp_item_multipolygon_missing_outer_accepted",
        ),
        (
            "mspp_item_multipolygon_null_outer",
            "json.mspp_item_multipolygon_null_outer_accepted",
        ),
        (
            "mspp_item_multipolygon_null_inner",
            "json.mspp_item_multipolygon_null_inner_accepted",
        ),
        (
            "mspp_item_multipolygon_empty_data",
            "json.mspp_item_multipolygon_empty_data_accepted",
        ),
    ] {
        println!(
            "{output_name}={}",
            json_metadata_b8_mspp_accepts(input_name)
        );
    }

    for (input_name, output_name) in [
        (
            "bpp_item_valid_simple_polygon",
            "json.bpp_item_valid_simple_polygon_accepted",
        ),
        (
            "bpp_item_valid_polygon_no_holes",
            "json.bpp_item_valid_polygon_no_holes_accepted",
        ),
        (
            "bpp_item_valid_oblique_simple_polygon",
            "json.bpp_item_valid_oblique_simple_polygon_accepted",
        ),
        (
            "bpp_item_valid_concave_polygon_no_holes",
            "json.bpp_item_valid_concave_polygon_no_holes_accepted",
        ),
        (
            "bpp_item_positive_separation_simple_polygon",
            "json.bpp_item_positive_separation_simple_polygon_accepted",
        ),
        (
            "bpp_item_positive_separation_polygon_no_holes",
            "json.bpp_item_positive_separation_polygon_no_holes_accepted",
        ),
        (
            "bpp_bin_minsep_collapse_exact",
            "json.bpp_bin_minsep_collapse_exact_accepted",
        ),
        (
            "bpp_bin_minsep_near_collapse_above",
            "json.bpp_bin_minsep_near_collapse_above_accepted",
        ),
        (
            "bpp_container_minsep_hole_quality_zone",
            "json.bpp_container_minsep_hole_quality_zone_accepted",
        ),
        (
            "bpp_container_minsep_invalid_hole",
            "json.bpp_container_minsep_invalid_hole_accepted",
        ),
        (
            "bpp_container_minsep_invalid_quality_zone",
            "json.bpp_container_minsep_invalid_quality_zone_accepted",
        ),
        (
            "bpp_container_minsep_multi_zone_order",
            "json.bpp_container_minsep_multi_zone_order_accepted",
        ),
        (
            "bpp_multi_bin_sorted_zero_stock",
            "json.bpp_multi_bin_sorted_zero_stock_accepted",
        ),
        (
            "bpp_multi_bin_duplicate_zero_stock_id",
            "json.bpp_multi_bin_duplicate_zero_stock_id_accepted",
        ),
        (
            "bpp_multi_bin_all_zero_stock",
            "json.bpp_multi_bin_all_zero_stock_accepted",
        ),
        (
            "bpp_multi_bin_u64_boundary_order",
            "json.bpp_multi_bin_u64_boundary_order_accepted",
        ),
    ] {
        println!("{output_name}={}", json_metadata_b8_bpp_accepts(input_name));
    }
    for (input_name, output_name) in [
        (
            "mspp_item_valid_simple_polygon",
            "json.mspp_item_valid_simple_polygon_accepted",
        ),
        (
            "mspp_item_valid_polygon_no_holes",
            "json.mspp_item_valid_polygon_no_holes_accepted",
        ),
        (
            "mspp_item_valid_oblique_simple_polygon",
            "json.mspp_item_valid_oblique_simple_polygon_accepted",
        ),
        (
            "mspp_item_valid_concave_polygon_no_holes",
            "json.mspp_item_valid_concave_polygon_no_holes_accepted",
        ),
        (
            "mspp_item_positive_separation_simple_polygon",
            "json.mspp_item_positive_separation_simple_polygon_accepted",
        ),
        (
            "mspp_item_positive_separation_polygon_no_holes",
            "json.mspp_item_positive_separation_polygon_no_holes_accepted",
        ),
        (
            "mspp_strip_minsep_collapse_exact",
            "json.mspp_strip_minsep_collapse_exact_accepted",
        ),
        (
            "mspp_strip_minsep_near_collapse_above",
            "json.mspp_strip_minsep_near_collapse_above_accepted",
        ),
    ] {
        println!(
            "{output_name}={}",
            json_metadata_b8_mspp_accepts(input_name)
        );
    }

    let importer = Importer::new(
        CDEConfig {
            quadtree_depth: 4,
            cd_threshold: 32,
            item_surrogate_config: SPSurrogateConfig::none(),
        },
        None,
        None,
    );
    for (input_name, output_name) in [
        (
            "bpp_item_simple_polygon_empty_points",
            "json.bpp_item_simple_polygon_empty_points_import_outcome",
        ),
        (
            "bpp_item_polygon_missing_inner",
            "json.bpp_item_polygon_missing_inner_import_outcome",
        ),
        (
            "bpp_item_multipolygon_empty_data",
            "json.bpp_item_multipolygon_empty_data_import_outcome",
        ),
    ] {
        println!(
            "{output_name}={}",
            json_metadata_b8_bpp_import_outcome(input_name, &importer)
        );
    }
    for (input_name, output_name) in [
        (
            "mspp_item_simple_polygon_empty_points",
            "json.mspp_item_simple_polygon_empty_points_import_outcome",
        ),
        (
            "mspp_item_polygon_missing_inner",
            "json.mspp_item_polygon_missing_inner_import_outcome",
        ),
        (
            "mspp_item_multipolygon_empty_data",
            "json.mspp_item_multipolygon_empty_data_import_outcome",
        ),
    ] {
        println!(
            "{output_name}={}",
            json_metadata_b8_mspp_import_outcome(input_name, &importer)
        );
    }
    for (input_name, output_name) in [
        (
            "bpp_item_valid_simple_polygon",
            "json.bpp_item_valid_simple_polygon_import_outcome",
        ),
        (
            "bpp_item_valid_polygon_no_holes",
            "json.bpp_item_valid_polygon_no_holes_import_outcome",
        ),
        (
            "bpp_item_valid_oblique_simple_polygon",
            "json.bpp_item_valid_oblique_simple_polygon_import_outcome",
        ),
        (
            "bpp_item_valid_concave_polygon_no_holes",
            "json.bpp_item_valid_concave_polygon_no_holes_import_outcome",
        ),
        (
            "bpp_item_positive_separation_simple_polygon",
            "json.bpp_item_positive_separation_simple_polygon_import_outcome",
        ),
        (
            "bpp_item_positive_separation_polygon_no_holes",
            "json.bpp_item_positive_separation_polygon_no_holes_import_outcome",
        ),
        (
            "bpp_bin_minsep_collapse_exact",
            "json.bpp_bin_minsep_collapse_exact_import_outcome",
        ),
        (
            "bpp_bin_minsep_near_collapse_above",
            "json.bpp_bin_minsep_near_collapse_above_import_outcome",
        ),
        (
            "bpp_container_minsep_hole_quality_zone",
            "json.bpp_container_minsep_hole_quality_zone_import_outcome",
        ),
        (
            "bpp_container_minsep_invalid_hole",
            "json.bpp_container_minsep_invalid_hole_import_outcome",
        ),
        (
            "bpp_container_minsep_invalid_quality_zone",
            "json.bpp_container_minsep_invalid_quality_zone_import_outcome",
        ),
        (
            "bpp_container_minsep_multi_zone_order",
            "json.bpp_container_minsep_multi_zone_order_import_outcome",
        ),
        (
            "bpp_multi_bin_sorted_zero_stock",
            "json.bpp_multi_bin_sorted_zero_stock_import_outcome",
        ),
        (
            "bpp_multi_bin_duplicate_zero_stock_id",
            "json.bpp_multi_bin_duplicate_zero_stock_id_import_outcome",
        ),
        (
            "bpp_multi_bin_all_zero_stock",
            "json.bpp_multi_bin_all_zero_stock_import_outcome",
        ),
        (
            "bpp_multi_bin_u64_boundary_order",
            "json.bpp_multi_bin_u64_boundary_order_import_outcome",
        ),
    ] {
        println!(
            "{output_name}={}",
            json_metadata_b8_bpp_import_outcome(input_name, &importer)
        );
    }
    for (input_name, output_name) in [
        (
            "mspp_item_valid_simple_polygon",
            "json.mspp_item_valid_simple_polygon_import_outcome",
        ),
        (
            "mspp_item_valid_polygon_no_holes",
            "json.mspp_item_valid_polygon_no_holes_import_outcome",
        ),
        (
            "mspp_item_valid_oblique_simple_polygon",
            "json.mspp_item_valid_oblique_simple_polygon_import_outcome",
        ),
        (
            "mspp_item_valid_concave_polygon_no_holes",
            "json.mspp_item_valid_concave_polygon_no_holes_import_outcome",
        ),
        (
            "mspp_item_positive_separation_simple_polygon",
            "json.mspp_item_positive_separation_simple_polygon_import_outcome",
        ),
        (
            "mspp_item_positive_separation_polygon_no_holes",
            "json.mspp_item_positive_separation_polygon_no_holes_import_outcome",
        ),
        (
            "mspp_strip_minsep_collapse_exact",
            "json.mspp_strip_minsep_collapse_exact_import_outcome",
        ),
        (
            "mspp_strip_minsep_near_collapse_above",
            "json.mspp_strip_minsep_near_collapse_above_import_outcome",
        ),
    ] {
        println!(
            "{output_name}={}",
            json_metadata_b8_mspp_import_outcome(input_name, &importer)
        );
    }
    for (input_name, output_name) in [
        (
            "mspp_strip_minsep_collapse_exact",
            "json.mspp_strip_minsep_collapse_exact_container_outcome",
        ),
        (
            "mspp_strip_minsep_near_collapse_above",
            "json.mspp_strip_minsep_near_collapse_above_container_outcome",
        ),
    ] {
        println!(
            "{output_name}={}",
            json_metadata_b8_mspp_strip_container_outcome(input_name, &importer)
        );
    }
    for (input_name, output_name) in [
        (
            "bpp_item_positive_separation_simple_polygon",
            "json.bpp_item_positive_separation_simple_polygon_container_shape_state",
        ),
        (
            "bpp_item_positive_separation_polygon_no_holes",
            "json.bpp_item_positive_separation_polygon_no_holes_container_shape_state",
        ),
        (
            "bpp_bin_minsep_near_collapse_above",
            "json.bpp_bin_minsep_near_collapse_above_container_shape_state",
        ),
    ] {
        let state = json_metadata_b8_bpp_import_container_shape_state(input_name, &importer)
            .unwrap_or_else(|| panic!("BPP container import failed: {input_name}"));
        println!("{output_name}={state}");
    }
    for (input_name, output_name) in [(
        "bpp_container_minsep_hole_quality_zone",
        "json.bpp_container_minsep_hole_quality_zone_container_quality_shape_state",
    )] {
        let state =
            json_metadata_b8_bpp_import_container_quality_shape_state(input_name, &importer)
                .unwrap_or_else(|| panic!("BPP quality-zone import failed: {input_name}"));
        println!("{output_name}={state}");
    }
    for (input_name, output_name) in [(
        "bpp_container_minsep_multi_zone_order",
        "json.bpp_container_minsep_multi_zone_order_container_quality_shape_state",
    )] {
        let state =
            json_metadata_b8_bpp_import_container_quality_shape_state(input_name, &importer)
                .unwrap_or_else(|| panic!("BPP multi-zone import failed: {input_name}"));
        println!("{output_name}={state}");
    }
    for (input_name, output_name) in [(
        "bpp_container_minsep_multi_zone_order",
        "json.bpp_container_minsep_multi_zone_order_hazard_mapping",
    )] {
        let state = json_metadata_b8_bpp_import_container_hazard_probe_state(input_name, &importer)
            .unwrap_or_else(|| panic!("BPP hazard probe failed: {input_name}"));
        println!("{output_name}={state}");
    }
    let bin_collection_state = json_metadata_b8_bpp_import_bin_collection_state(
        "bpp_multi_bin_sorted_zero_stock",
        &importer,
    )
    .expect("BPP multi-bin import failed");
    println!("json.bpp_multi_bin_sorted_zero_stock_bin_collection_state={bin_collection_state}");
    let bin_integer_state = json_metadata_b8_bpp_import_bin_integer_state(
        "bpp_multi_bin_u64_boundary_order",
        &importer,
    )
    .expect("BPP u64-boundary bin import failed");
    println!("json.bpp_multi_bin_u64_boundary_order_integer_state={bin_integer_state}");
    for (input_name, output_name) in [
        (
            "mspp_item_positive_separation_simple_polygon",
            "json.mspp_item_positive_separation_simple_polygon_container_shape_state",
        ),
        (
            "mspp_item_positive_separation_polygon_no_holes",
            "json.mspp_item_positive_separation_polygon_no_holes_container_shape_state",
        ),
        (
            "mspp_strip_minsep_near_collapse_above",
            "json.mspp_strip_minsep_near_collapse_above_container_shape_state",
        ),
    ] {
        let state = json_metadata_b8_mspp_strip_container_shape_state(input_name, &importer)
            .unwrap_or_else(|| panic!("MSPP strip container creation failed: {input_name}"));
        println!("{output_name}={state}");
    }
    for (input_name, output_name) in [
        (
            "bpp_item_valid_simple_polygon",
            "json.bpp_item_valid_simple_polygon_shape_state",
        ),
        (
            "bpp_item_valid_polygon_no_holes",
            "json.bpp_item_valid_polygon_no_holes_shape_state",
        ),
        (
            "bpp_item_valid_oblique_simple_polygon",
            "json.bpp_item_valid_oblique_simple_polygon_shape_state",
        ),
        (
            "bpp_item_valid_concave_polygon_no_holes",
            "json.bpp_item_valid_concave_polygon_no_holes_shape_state",
        ),
        (
            "bpp_item_positive_separation_simple_polygon",
            "json.bpp_item_positive_separation_simple_polygon_shape_state",
        ),
        (
            "bpp_item_positive_separation_polygon_no_holes",
            "json.bpp_item_positive_separation_polygon_no_holes_shape_state",
        ),
    ] {
        let state = json_metadata_b8_bpp_import_shape_state(input_name, &importer)
            .unwrap_or_else(|| panic!("BPP valid polygon import failed: {input_name}"));
        println!("{output_name}={state}");
    }
    for (input_name, output_name) in [
        (
            "mspp_item_valid_simple_polygon",
            "json.mspp_item_valid_simple_polygon_shape_state",
        ),
        (
            "mspp_item_valid_polygon_no_holes",
            "json.mspp_item_valid_polygon_no_holes_shape_state",
        ),
        (
            "mspp_item_valid_oblique_simple_polygon",
            "json.mspp_item_valid_oblique_simple_polygon_shape_state",
        ),
        (
            "mspp_item_valid_concave_polygon_no_holes",
            "json.mspp_item_valid_concave_polygon_no_holes_shape_state",
        ),
        (
            "mspp_item_positive_separation_simple_polygon",
            "json.mspp_item_positive_separation_simple_polygon_shape_state",
        ),
        (
            "mspp_item_positive_separation_polygon_no_holes",
            "json.mspp_item_positive_separation_polygon_no_holes_shape_state",
        ),
    ] {
        let state = json_metadata_b8_mspp_import_shape_state(input_name, &importer)
            .unwrap_or_else(|| panic!("MSPP valid polygon import failed: {input_name}"));
        println!("{output_name}={state}");
    }
}

fn run_import_boundary_oracle() {
    use std::panic::{AssertUnwindSafe, catch_unwind};

    let cases = [
        (
            "nonconsecutive_duplicate",
            vec![(0.0, 0.0), (4.0, 0.0), (4.0, 2.0), (0.0, 0.0), (0.0, 2.0)],
        ),
        (
            "signed_zero_closure",
            vec![(0.0, 0.0), (2.0, 0.0), (2.0, 2.0), (0.0, 2.0), (-0.0, 0.0)],
        ),
        (
            "nan_coordinate",
            vec![(0.0, 0.0), (2.0, 0.0), (2.0, 2.0), (f32::NAN, 2.0)],
        ),
        (
            "infinite_coordinate",
            vec![(0.0, 0.0), (2.0, 0.0), (2.0, 2.0), (f32::INFINITY, 2.0)],
        ),
    ];

    for (name, points) in cases {
        match catch_unwind(AssertUnwindSafe(|| {
            import_simple_polygon(&ExtSPolygon(points))
        })) {
            Ok(Ok(shape)) => println!("import.boundary.{name}=ok:{}", shape.n_vertices()),
            Ok(Err(error)) => println!("import.boundary.{name}=err:{error}"),
            Err(_) => println!("import.boundary.{name}=panic"),
        }
    }

    let endpoint_edge = Edge::try_new(Point(0.0, 0.0), Point(2.0, 0.0)).unwrap();
    let touching_edge = Edge::try_new(Point(2.0, 0.0), Point(2.0, 1.0)).unwrap();
    println!(
        "import.boundary.endpoint_touch_collides={}",
        endpoint_edge.collides_at(&touching_edge).is_some()
    );

    let cde_config = CDEConfig {
        quadtree_depth: 1,
        cd_threshold: 8,
        item_surrogate_config: SPSurrogateConfig::none(),
    };
    let instance = SPInstance::new(
        vec![(square_item(0, 1.0), 1)],
        SPStrip::new(10.0, cde_config, ShapeModifyConfig::default(), 10.0).unwrap(),
    );
    for (name, transformation) in [
        (
            "nan_rotation",
            ExtTransformation {
                rotation: f32::NAN,
                translation: (2.0, 2.0),
            },
        ),
        (
            "infinite_rotation",
            ExtTransformation {
                rotation: f32::INFINITY,
                translation: (2.0, 2.0),
            },
        ),
        (
            "infinite_translation",
            ExtTransformation {
                rotation: 0.0,
                translation: (f32::INFINITY, 2.0),
            },
        ),
    ] {
        let solution = ExtSPSolution {
            strip_width: 10.0,
            layout: ExtLayout {
                container_id: 0,
                placed_items: vec![ExtPlacedItem {
                    item_id: 1000,
                    transformation,
                }],
                density: 0.0,
            },
            density: 0.0,
            run_time_sec: 0,
        };
        match catch_unwind(AssertUnwindSafe(|| {
            import_spp_solution(&instance, &solution)
        })) {
            Ok(Ok(_)) => println!("import.boundary.solution.{name}=ok"),
            Ok(Err(error)) => println!("import.boundary.solution.{name}=err:{error}"),
            Err(_) => println!("import.boundary.solution.{name}=panic"),
        }
    }

    let json_solution: ExtSPSolution = serde_json::from_str(
        r#"{
            "strip_width": 10.0,
            "layout": {
                "container_id": 0,
                "placed_items": [{
                    "item_id": 1000,
                    "transformation": {
                        "rotation": 0.0,
                        "translation": [3.5e38, 2.0]
                    }
                }],
                "density": 0.0
            },
            "density": 0.0,
            "run_time_sec": 0
        }"#,
    )
    .expect("finite JSON value above f32 range should deserialize as f32 infinity");
    assert!(
        json_solution.layout.placed_items[0]
            .transformation
            .translation
            .0
            .is_infinite()
    );
    match catch_unwind(AssertUnwindSafe(|| {
        import_spp_solution(&instance, &json_solution)
    })) {
        Err(_) => println!("import.boundary.solution.json_infinite_translation=panic"),
        Ok(Ok(_)) => panic!("JSON infinite translation unexpectedly imported"),
        Ok(Err(error)) => panic!("JSON infinite translation returned Result error: {error}"),
    }

    let json_rotation_solution: ExtSPSolution = serde_json::from_str(
        r#"{
            "strip_width": 10.0,
            "layout": {
                "container_id": 0,
                "placed_items": [{
                    "item_id": 1000,
                    "transformation": {
                        "rotation": 3.5e38,
                        "translation": [2.0, 2.0]
                    }
                }],
                "density": 0.0
            },
            "density": 0.0,
            "run_time_sec": 0
        }"#,
    )
    .expect("finite JSON value above f32 range should deserialize as f32 infinity");
    assert!(
        json_rotation_solution.layout.placed_items[0]
            .transformation
            .rotation
            .is_infinite()
    );
    match catch_unwind(AssertUnwindSafe(|| {
        import_spp_solution(&instance, &json_rotation_solution)
    })) {
        Err(_) => println!("import.boundary.solution.json_infinite_rotation=panic"),
        Ok(Ok(_)) => panic!("JSON infinite rotation unexpectedly imported"),
        Ok(Err(error)) => panic!("JSON infinite rotation returned Result error: {error}"),
    }

    for (name, json, rotation_infinite, translation_infinite) in [
        (
            "negative_infinite_translation",
            r#"{"strip_width":10.0,"layout":{"container_id":0,"placed_items":[{"item_id":1000,"transformation":{"rotation":0.0,"translation":[-3.5e38,2.0]}}],"density":0.0},"density":0.0,"run_time_sec":0}"#,
            false,
            true,
        ),
        (
            "negative_infinite_rotation",
            r#"{"strip_width":10.0,"layout":{"container_id":0,"placed_items":[{"item_id":1000,"transformation":{"rotation":-3.5e38,"translation":[2.0,2.0]}}],"density":0.0},"density":0.0,"run_time_sec":0}"#,
            true,
            false,
        ),
    ] {
        let solution: ExtSPSolution = serde_json::from_str(json)
            .expect("negative f32 overflow should deserialize as -infinity");
        let transformation = &solution.layout.placed_items[0].transformation;
        assert_eq!(transformation.rotation.is_infinite(), rotation_infinite);
        assert_eq!(
            transformation.translation.0.is_infinite(),
            translation_infinite
        );
        assert!(
            if rotation_infinite {
                transformation.rotation.is_sign_negative()
            } else {
                transformation.translation.0.is_sign_negative()
            },
            "negative overflow should preserve its sign"
        );
        match catch_unwind(AssertUnwindSafe(|| {
            import_spp_solution(&instance, &solution)
        })) {
            Err(_) => println!("import.boundary.solution.json_{name}=panic"),
            Ok(Ok(_)) => panic!("JSON {name} unexpectedly imported"),
            Ok(Err(error)) => panic!("JSON {name} returned Result error: {error}"),
        }
    }

    let json_max_finite_solution: ExtSPSolution = serde_json::from_str(
        r#"{"strip_width":10.0,"layout":{"container_id":0,"placed_items":[{"item_id":1000,"transformation":{"rotation":3.4028234663852886e38,"translation":[3.4028234663852886e38,2.0]}}],"density":0.0},"density":0.0,"run_time_sec":0}"#,
    )
    .expect("f32::MAX JSON values should deserialize as finite f32 values");
    let max_finite_transformation = &json_max_finite_solution.layout.placed_items[0].transformation;
    assert_eq!(max_finite_transformation.rotation, f32::MAX);
    assert_eq!(max_finite_transformation.translation.0, f32::MAX);
    println!("import.boundary.solution.json_max_finite_transform_fields=finite");

    let json_rounding_predecessor_solution: ExtSPSolution = serde_json::from_str(
        r#"{"strip_width":10.0,"layout":{"container_id":0,"placed_items":[{"item_id":1000,"transformation":{"rotation":340282356779733623858607532500980858880,"translation":[-340282356779733623858607532500980858880,2.0]}}],"density":0.0},"density":0.0,"run_time_sec":0}"#,
    )
    .expect("the f64 immediately below the f32 overflow midpoint should round finite");
    let predecessor_transformation =
        &json_rounding_predecessor_solution.layout.placed_items[0].transformation;
    assert_eq!(predecessor_transformation.rotation, f32::MAX);
    assert_eq!(predecessor_transformation.translation.0, -f32::MAX);
    println!(
        "import.boundary.solution.json_f32_overflow_predecessor={:08x},{:08x}",
        predecessor_transformation.rotation.to_bits(),
        predecessor_transformation.translation.0.to_bits()
    );

    let fixture_instance_external: ExtSPInstance = serde_json::from_str(
        r#"{"name":"one-spp","items":[{"id":201,"demand":1,"orientation":{"rotation":{"mode":"discrete","angles":[0]}},"shape":{"type":"rectangle","data":{"x_min":0,"y_min":0,"width":2,"height":2}}}],"strip_height":4}"#,
    )
    .expect("matching C++ importer fixture should deserialize");
    let fixture_importer = Importer::new(cde_config, None, None);
    let fixture_instance = import_spp_instance(&fixture_importer, &fixture_instance_external)
        .expect("matching C++ importer fixture should import");
    for (name, json) in [
        (
            "rounded_f32_max_rotation",
            r#"{"strip_width":4.0,"layout":{"container_id":0,"placed_items":[{"item_id":201,"transformation":{"rotation":340282356779733623858607532500980858880,"translation":[2.0,2.0]}}],"density":0.0},"density":0.0,"run_time_sec":0}"#,
        ),
        (
            "rounded_f32_max_translation",
            r#"{"strip_width":4.0,"layout":{"container_id":0,"placed_items":[{"item_id":201,"transformation":{"rotation":0.0,"translation":[340282356779733623858607532500980858880,2.0]}}],"density":0.0},"density":0.0,"run_time_sec":0}"#,
        ),
    ] {
        let solution: ExtSPSolution = serde_json::from_str(json)
            .expect("f64 predecessor should deserialize as finite f32::MAX");
        let result = catch_unwind(AssertUnwindSafe(|| {
            import_spp_solution(&fixture_instance, &solution)
        }));
        match result {
            Err(_) => {
                println!("import.boundary.solution.{name}=panic");
                assert_eq!(name, "rounded_f32_max_translation");
            }
            Ok(Err(error)) => panic!(
                "{name} unexpectedly returned a Result error after finite narrowing: {error}"
            ),
            Ok(Ok(imported)) => {
                assert_eq!(name, "rounded_f32_max_rotation");
                let placed = imported
                    .layout_snapshot
                    .placed_items
                    .values()
                    .next()
                    .expect("successful import should contain the placed item");
                let (tx, ty) = placed.d_transf.translation();
                println!("import.boundary.solution.{name}=ok");
                println!(
                    "import.boundary.solution.{name}.transform_bits={:08x},{:08x},{:08x}",
                    placed.d_transf.rotation().to_bits(),
                    tx.to_bits(),
                    ty.to_bits()
                );
                assert_eq!(placed.d_transf.rotation().to_bits(), 0xbf45_abe3);
                assert_eq!(tx.to_bits(), 0x405a_8071);
                assert_eq!(ty.to_bits(), 0x4001_32dd);
                assert_eq!(placed.shape.area, 4.0);
                assert_eq!(placed.shape.bbox.x_min.to_bits(), 0x3fff_ffff);
                assert_eq!(placed.shape.bbox.y_min.to_bits(), 0x3f1a_c9ae);
                assert_eq!(placed.shape.bbox.x_max.to_bits(), 0x409a_8071);
                assert_eq!(placed.shape.bbox.y_max.to_bits(), 0x405b_b34e);
                let expected_vertices = [
                    (0x3fff_ffff, 0x4000_0000),
                    (0x405b_b34e, 0x3f1a_c9ae),
                    (0x409a_8071, 0x4002_65ba),
                    (0x4059_4d94, 0x405b_b34e),
                ];
                assert_eq!(placed.shape.vertices.len(), expected_vertices.len());
                for (vertex, (expected_x, expected_y)) in
                    placed.shape.vertices.iter().zip(expected_vertices)
                {
                    assert_eq!(vertex.0.to_bits(), expected_x);
                    assert_eq!(vertex.1.to_bits(), expected_y);
                }
                print_polygon(
                    &format!("import.boundary.solution.{name}.shape"),
                    &placed.shape,
                );
                for (index, vertex) in placed.shape.vertices.iter().enumerate() {
                    println!(
                        "import.boundary.solution.{name}.vertex{index}={:08x},{:08x}",
                        vertex.0.to_bits(),
                        vertex.1.to_bits()
                    );
                }
            }
        }
    }

    let finite_placed_solution: ExtSPSolution = serde_json::from_str(
        r#"{"strip_width":4.0,"layout":{"container_id":0,"placed_items":[{"item_id":201,"transformation":{"rotation":37.25,"translation":[4.25,-1.75]}}],"density":0.0},"density":0.0,"run_time_sec":0}"#,
    )
    .expect("finite PlacedItem geometry fixture should deserialize");
    let finite_placed_import = import_spp_solution(&fixture_instance, &finite_placed_solution)
        .expect("finite PlacedItem geometry fixture should import");
    let finite_placed = finite_placed_import
        .layout_snapshot
        .placed_items
        .values()
        .next()
        .expect("finite PlacedItem fixture should contain one placement");
    let shape = &finite_placed.shape;
    let centroid = shape.centroid();
    assert_eq!(
        [
            finite_placed.d_transf.rotation().to_bits(),
            finite_placed.d_transf.translation().0.to_bits(),
            finite_placed.d_transf.translation().1.to_bits(),
        ],
        [0x3f26_6f42, 0x408e_1a48, 0xbeb2_8958]
    );
    let expected_vertices = [
        (0x4088_0000, 0xbfe0_0000),
        (0x40ba_f1b2, 0xbf0a_16e9),
        (0x4094_3490, 0x3f86_bb54),
        (0x4042_85bb, 0xbe21_c9bc),
    ];
    assert_eq!(shape.vertices.len(), expected_vertices.len());
    for (vertex, (expected_x, expected_y)) in shape.vertices.iter().zip(expected_vertices) {
        assert_eq!(vertex.0.to_bits(), expected_x);
        assert_eq!(vertex.1.to_bits(), expected_y);
    }
    assert_eq!(
        [
            shape.bbox.x_min.to_bits(),
            shape.bbox.y_min.to_bits(),
            shape.bbox.x_max.to_bits(),
            shape.bbox.y_max.to_bits(),
            shape.area.to_bits(),
            shape.diameter.to_bits(),
            centroid.0.to_bits(),
            centroid.1.to_bits(),
            finite_placed.item.area().to_bits(),
            shape.poi.center.0.to_bits(),
            shape.poi.center.1.to_bits(),
            shape.poi.radius.to_bits(),
        ],
        [
            0x4042_85bb,
            0xbfe0_0000,
            0x40ba_f1b2,
            0x3f86_bb54,
            0x4080_0000,
            0x4035_04f3,
            0x408e_1a47,
            0xbeb2_8953,
            0x4080_0000,
            0x408e_1a48,
            0xbeb2_8958,
            0x3f80_0000,
        ]
    );
    let surrogate = shape
        .surrogate
        .as_ref()
        .expect("the none-configured surrogate is represented by an empty surrogate");
    assert_eq!(surrogate.poles.len(), 2);
    assert!(surrogate.piers.is_empty());
    assert_eq!(surrogate.convex_hull_area.to_bits(), 0x4080_0000);
    assert_eq!(surrogate.convex_hull_indices, [0, 1, 2, 3]);
    assert_eq!(
        surrogate
            .poles
            .iter()
            .map(|pole| (
                pole.center.0.to_bits(),
                pole.center.1.to_bits(),
                pole.radius.to_bits()
            ))
            .collect::<Vec<_>>(),
        [
            (0x408e_1a48, 0xbeb2_8958, 0x3f80_0000),
            (0x4093_280b, 0x3f4f_ce76, 0x3e2f_40c8),
        ]
    );
    println!(
        "import.placed_item.finite.transform={:08x},{:08x},{:08x}",
        finite_placed.d_transf.rotation().to_bits(),
        finite_placed.d_transf.translation().0.to_bits(),
        finite_placed.d_transf.translation().1.to_bits()
    );
    println!(
        "import.placed_item.finite.vertex_count={}",
        shape.n_vertices()
    );
    for (index, vertex) in shape.vertices.iter().enumerate() {
        println!(
            "import.placed_item.finite.vertex{index}={:08x},{:08x}",
            vertex.0.to_bits(),
            vertex.1.to_bits()
        );
    }
    println!(
        "import.placed_item.finite.bbox={:08x},{:08x},{:08x},{:08x}",
        shape.bbox.x_min.to_bits(),
        shape.bbox.y_min.to_bits(),
        shape.bbox.x_max.to_bits(),
        shape.bbox.y_max.to_bits()
    );
    println!(
        "import.placed_item.finite.metrics={:08x},{:08x},{:08x},{:08x},{:08x}",
        shape.area.to_bits(),
        shape.diameter.to_bits(),
        centroid.0.to_bits(),
        centroid.1.to_bits(),
        finite_placed.item.area().to_bits()
    );
    println!(
        "import.placed_item.finite.poi={:08x},{:08x},{:08x}",
        shape.poi.center.0.to_bits(),
        shape.poi.center.1.to_bits(),
        shape.poi.radius.to_bits()
    );
    println!(
        "import.placed_item.finite.surrogate={}",
        shape.surrogate.is_some()
    );
    if let Some(surrogate) = &shape.surrogate {
        println!(
            "import.placed_item.finite.surrogate_summary={},{},{:08x}",
            surrogate.poles.len(),
            surrogate.piers.len(),
            surrogate.convex_hull_area.to_bits()
        );
        println!(
            "import.placed_item.finite.surrogate_hull_indices={}",
            surrogate
                .convex_hull_indices
                .iter()
                .map(usize::to_string)
                .collect::<Vec<_>>()
                .join(",")
        );
        for (index, pole) in surrogate.poles.iter().enumerate() {
            println!(
                "import.placed_item.finite.surrogate_pole{index}={:08x},{:08x},{:08x}",
                pole.center.0.to_bits(),
                pole.center.1.to_bits(),
                pole.radius.to_bits()
            );
        }
        for (index, pier) in surrogate.piers.iter().enumerate() {
            println!(
                "import.placed_item.finite.surrogate_pier{index}={:08x},{:08x},{:08x},{:08x}",
                pier.start.0.to_bits(),
                pier.start.1.to_bits(),
                pier.end.0.to_bits(),
                pier.end.1.to_bits()
            );
        }
    }

    let json_rounding_midpoint_solution: ExtSPSolution = serde_json::from_str(
        r#"{"strip_width":10.0,"layout":{"container_id":0,"placed_items":[{"item_id":1000,"transformation":{"rotation":340282356779733661637539395458142568448,"translation":[-340282356779733661637539395458142568448,2.0]}}],"density":0.0},"density":0.0,"run_time_sec":0}"#,
    )
    .expect("the exact overflow midpoint should round to signed infinity");
    let midpoint_transformation =
        &json_rounding_midpoint_solution.layout.placed_items[0].transformation;
    assert_eq!(midpoint_transformation.rotation, f32::INFINITY);
    assert_eq!(midpoint_transformation.translation.0, f32::NEG_INFINITY);
    println!(
        "import.boundary.solution.json_f32_overflow_midpoint={:08x},{:08x}",
        midpoint_transformation.rotation.to_bits(),
        midpoint_transformation.translation.0.to_bits()
    );
}

fn run_offset_random_matrix_oracle() {
    struct XorShift64Star {
        state: u64,
    }
    impl XorShift64Star {
        fn next(&mut self) -> u64 {
            self.state ^= self.state >> 12;
            self.state ^= self.state << 25;
            self.state ^= self.state >> 27;
            self.state.wrapping_mul(2_685_821_657_736_338_717)
        }
    }
    // D8-A acceptance infrastructure: deterministic paired random matrix.
    // Rectilinear monotone-mountain polygons generated with integer-only
    // arithmetic (plus exact f32 scale/translate), so the generator is
    // bit-identical in the C++ mirror without any libm dependence. Heights of
    // zero can trip polygon validation; those paired error outcomes are part
    // of the matrix. RNG consumption order per index: profile, then
    // {width,height} per profile point, then scale, tx, ty, distance.
    const N: usize = 200;
    const SCALES: [f32; 4] = [1.0, 10.0, 100.0, 1000.0];
    const DISTANCES: [f32; 4] = [0.05, 0.25, 1.0, 2.5];
    let mut rng = XorShift64Star {
        state: 0x4F464652414E444D,
    };
    for index in 0..N {
        let profile = 3 + (rng.next() % 12) as usize;
        let mut widths = [0u64; 16];
        let mut heights = [0u64; 16];
        for i in 0..profile {
            widths[i] = 1 + rng.next() % 7;
            heights[i] = 1 + rng.next() % 9;
        }
        let scale = SCALES[(rng.next() % 4) as usize];
        let tx = (rng.next() % 100) as f32;
        let ty = (rng.next() % 100) as f32;
        let distance = DISTANCES[(rng.next() % 4) as usize];

        let mut xs = [0.0f32; 16];
        for i in 0..profile {
            let previous = if i == 0 { 0.0 } else { xs[i - 1] };
            xs[i] = previous + widths[i] as f32;
        }
        let total = xs[profile - 1] + 1.0;
        let mut points = Vec::with_capacity(profile + 2);
        let transform =
            |x: f32, y: f32, scale: f32, tx: f32, ty: f32| -> Point {
                Point(x * scale + tx, y * scale + ty)
            };
        points.push(transform(0.0, 0.0, scale, tx, ty));
        for i in 0..profile {
            points.push(transform(xs[i], heights[i] as f32, scale, tx, ty));
        }
        points.push(transform(total, 0.0, scale, tx, ty));

        match SPolygon::new(points) {
            Err(error) => println!("offset.random.{index}.polygon=error:{error}"),
            Ok(polygon) => {
                print_offset_shape_values(
                    &format!("offset.random.{index}.inflate"),
                    &polygon,
                    ShapeModifyMode::Inflate,
                    distance,
                );
                print_offset_shape_values(
                    &format!("offset.random.{index}.deflate"),
                    &polygon,
                    ShapeModifyMode::Deflate,
                    distance,
                );
            }
        }
    }
}

fn run_offset_boundary_oracle() {
    use std::panic::{AssertUnwindSafe, catch_unwind};

    let square = polygon(&[(0.0, 0.0), (2.0, 0.0), (2.0, 2.0), (0.0, 2.0)]);
    for (name, distance) in [
        ("nan", f32::NAN),
        ("positive_infinity", f32::INFINITY),
        ("negative_infinity", f32::NEG_INFINITY),
    ] {
        let output_key = if name == "nan" {
            if cfg!(debug_assertions) {
                "offset.boundary.nan.debug"
            } else {
                "offset.boundary.nan.release"
            }
        } else {
            match name {
                "positive_infinity" => "offset.boundary.positive_infinity",
                "negative_infinity" => "offset.boundary.negative_infinity",
                _ => unreachable!("unexpected offset boundary case"),
            }
        };
        let result = catch_unwind(AssertUnwindSafe(|| {
            offset_shape(&square, ShapeModifyMode::Inflate, distance)
        }));
        match result {
            Ok(Ok(shape)) => print_polygon(output_key, &shape),
            Ok(Err(error)) => println!("{output_key}=error:{error}"),
            Err(_) => println!("{output_key}=panic"),
        }
    }

    match offset_shape(&square, ShapeModifyMode::Deflate, 2.0) {
        Ok(shape) => print_polygon("offset.boundary.collapse", &shape),
        Err(error) => println!("offset.boundary.collapse=error:{error}"),
    }

    let tiny_deflate_width = f32::from_bits(0x3500_0000);
    let tiny_deflate_origin = f32::from_bits(0x3580_0000);
    let tiny_deflate_shape = rectangle(
        tiny_deflate_origin,
        tiny_deflate_origin,
        tiny_deflate_origin + tiny_deflate_width * 2.0,
        tiny_deflate_origin + tiny_deflate_width,
    );
    print_offset_shape_values(
        "offset.boundary.tiny_deflate_fallback",
        &tiny_deflate_shape,
        ShapeModifyMode::Deflate,
        f32::from_bits(0x347f_ffff),
    );
    for (name, bits) in [
        ("fallback_pred1", 0x347f_fffe),
        ("fallback_pred2", 0x347f_fffd),
        ("fallback_pred3", 0x347f_fffc),
        ("fallback_pred4", 0x347f_fffb),
        ("half", 0x3480_0000),
        ("above_half", 0x3480_0001),
    ] {
        let output_key = format!("offset.boundary.tiny_deflate_{name}");
        print_offset_shape_values(
            &output_key,
            &tiny_deflate_shape,
            ShapeModifyMode::Deflate,
            f32::from_bits(bits),
        );
    }
    for exponent in [-24, -30, -38, -40, -42] {
        let width = f32::from_bits(((exponent + 127) as u32) << 23);
        let origin = width * 2.0;
        let scaled_shape = rectangle(origin, origin, origin + width * 2.0, origin + width);
        let distance = f32::from_bits((width * 0.5).to_bits() - 1);
        let key = format!("offset.boundary.tiny_scale_exp{}", -exponent);
        print_offset_shape_values(&key, &scaled_shape, ShapeModifyMode::Deflate, distance);
        for (suffix, control_distance) in [
            ("half", width * 0.5),
            ("above_half", f32::from_bits((width * 0.5).to_bits() + 1)),
        ] {
            let control_key = format!("{key}_{suffix}");
            print_offset_shape_values(
                &control_key,
                &scaled_shape,
                ShapeModifyMode::Deflate,
                control_distance,
            );
        }
    }
    for (exponent, distance_bits) in [(7, 0x427f_fffb), (20, 0x48ff_fffb)] {
        let width = f32::from_bits(((exponent + 127) as u32) << 23);
        let origin = f32::from_bits(0x3580_0000);
        let large_deflate_shape = rectangle(origin, origin, origin + width * 2.0, origin + width);
        let output_key = format!("offset.boundary.large_deflate_fallback_exp{exponent}");
        print_offset_shape_values(
            &output_key,
            &large_deflate_shape,
            ShapeModifyMode::Deflate,
            f32::from_bits(distance_bits),
        );
    }
    for (exponent, distance_bits) in [(7, 0x427f_fffb), (20, 0x48ff_fffb)] {
        let width = f32::from_bits(((exponent + 127) as u32) << 23);
        for (origin_exponent, origin_bits) in [(0, 0x3f80_0000), (20, 0x4980_0000)] {
            let origin = f32::from_bits(origin_bits);
            let translated_shape = rectangle(origin, origin, origin + width * 2.0, origin + width);
            for predecessor in 1..=5 {
                let output_key = if predecessor == 5 {
                    format!(
                        "offset.boundary.large_deflate_translated_exp{exponent}_origin{origin_exponent}"
                    )
                } else {
                    format!(
                        "offset.boundary.large_deflate_translated_exp{exponent}_origin{origin_exponent}_pred{predecessor}"
                    )
                };
                print_offset_shape_values(
                    &output_key,
                    &translated_shape,
                    ShapeModifyMode::Deflate,
                    f32::from_bits(distance_bits + (5 - predecessor)),
                );
            }
        }
    }

    print_offset_shape_values(
        "offset.finite.square_deflate",
        &square,
        ShapeModifyMode::Deflate,
        0.25,
    );
    print_offset_shape_values(
        "offset.finite.square_inflate",
        &square,
        ShapeModifyMode::Inflate,
        0.25,
    );
    let collinear_vertex_square =
        polygon(&[(0.0, 0.0), (2.0, 0.0), (2.0, 2.0), (0.0, 2.0), (0.0, 1.0)]);
    print_offset_shape_values(
        "offset.finite.collinear_vertex_square_inflate",
        &collinear_vertex_square,
        ShapeModifyMode::Inflate,
        0.25,
    );
    let narrow_notch = polygon(&[
        (0.0, 0.0),
        (10.0, 0.0),
        (10.0, 10.0),
        (6.0, 10.0),
        (6.0, 9.9),
        (4.0, 9.9),
        (4.0, 10.0),
        (0.0, 10.0),
    ]);
    print_offset_shape_values(
        "offset.finite.narrow_notch_inflate_quarter",
        &narrow_notch,
        ShapeModifyMode::Inflate,
        0.25,
    );
    let oblique_pentagon = polygon(&[(0.0, 0.0), (8.0, 0.0), (10.0, 5.0), (5.0, 9.0), (-1.0, 5.0)]);
    print_offset_shape_values(
        "offset.finite.oblique_pentagon_inflate",
        &oblique_pentagon,
        ShapeModifyMode::Inflate,
        0.5,
    );

    let concave_l = polygon(&[
        (0.0, 0.0),
        (6.0, 0.0),
        (6.0, 2.0),
        (2.0, 2.0),
        (2.0, 6.0),
        (0.0, 6.0),
    ]);
    print_offset_shape_values(
        "offset.finite.concave_l_inflate",
        &concave_l,
        ShapeModifyMode::Inflate,
        0.5,
    );
    print_offset_shape_values(
        "offset.finite.concave_l_deflate",
        &concave_l,
        ShapeModifyMode::Deflate,
        0.5,
    );
    print_offset_shape_values(
        "offset.finite.concave_l_deflate_1_0",
        &concave_l,
        ShapeModifyMode::Deflate,
        1.0,
    );
    for (name, bits) in [
        (
            "offset.boundary.concave_l_deflate_radius_below",
            0x3f95_f619,
        ),
        (
            "offset.boundary.concave_l_deflate_radius_above",
            0x3f95_f61a,
        ),
    ] {
        print_offset_shape_values(
            name,
            &concave_l,
            ShapeModifyMode::Deflate,
            f32::from_bits(bits),
        );
    }
    for (scale_name, scale, threshold_bits) in [
        ("0_5", 0.5_f32, 0x3f15_f61a),
        ("2", 2.0_f32, 0x4015_f61a),
        ("10", 10.0_f32, 0x413b_73a0),
    ] {
        let scaled_concave_l = polygon(&[
            (0.0, 0.0),
            (6.0 * scale, 0.0),
            (6.0 * scale, 2.0 * scale),
            (2.0 * scale, 2.0 * scale),
            (2.0 * scale, 6.0 * scale),
            (0.0, 6.0 * scale),
        ]);
        for (side, bits) in [("below", threshold_bits - 1), ("nearest", threshold_bits)] {
            let name = format!("offset.boundary.concave_l_scale_{scale_name}_{side}");
            print_offset_shape_values(
                &name,
                &scaled_concave_l,
                ShapeModifyMode::Deflate,
                f32::from_bits(bits),
            );
        }
    }

    let split_candidate = polygon(&[(0.0, 0.0), (4.0, 0.0), (4.0, 4.0), (2.0, 1.0), (0.0, 4.0)]);
    print_offset_shape_values(
        "offset.finite.split_first_deflate",
        &split_candidate,
        ShapeModifyMode::Deflate,
        0.45,
    );

    let narrow_bridge = polygon(&[
        (0.0, 0.0),
        (5.0, 0.0),
        (5.0, 2.0),
        (7.0, 2.0),
        (7.0, 0.0),
        (12.0, 0.0),
        (12.0, 5.0),
        (7.0, 5.0),
        (7.0, 3.0),
        (5.0, 3.0),
        (5.0, 5.0),
        (0.0, 5.0),
    ]);
    print_pinned_offset_components(
        "offset.finite.narrow_bridge_deflate",
        &[
            (0.0, 0.0),
            (5.0, 0.0),
            (5.0, 2.0),
            (7.0, 2.0),
            (7.0, 0.0),
            (12.0, 0.0),
            (12.0, 5.0),
            (7.0, 5.0),
            (7.0, 3.0),
            (5.0, 3.0),
            (5.0, 5.0),
            (0.0, 5.0),
        ],
        0.6,
    );
    print_offset_shape_values(
        "offset.finite.narrow_bridge_first_deflate",
        &narrow_bridge,
        ShapeModifyMode::Deflate,
        0.6,
    );
    print_pinned_offset_components(
        "offset.finite.narrow_bridge_touching_deflate",
        &[
            (0.0, 0.0),
            (5.0, 0.0),
            (5.0, 2.0),
            (7.0, 2.0),
            (7.0, 0.0),
            (12.0, 0.0),
            (12.0, 5.0),
            (7.0, 5.0),
            (7.0, 3.0),
            (5.0, 3.0),
            (5.0, 5.0),
            (0.0, 5.0),
        ],
        1.0,
    );

    let comb = [
        (0.0, 0.0),
        (22.0, 0.0),
        (22.0, 12.0),
        (16.0, 12.0),
        (16.0, 4.0),
        (14.0, 4.0),
        (14.0, 12.0),
        (8.0, 12.0),
        (8.0, 4.0),
        (6.0, 4.0),
        (6.0, 12.0),
        (0.0, 12.0),
    ];
    let comb_collinear_top = [
        (0.0, 0.0),
        (22.0, 0.0),
        (22.0, 12.0),
        (19.0, 12.0),
        (16.0, 12.0),
        (16.0, 4.0),
        (14.0, 4.0),
        (14.0, 12.0),
        (8.0, 12.0),
        (8.0, 4.0),
        (6.0, 4.0),
        (6.0, 12.0),
        (0.0, 12.0),
    ];
    let dense_tooth_gear = dense_tooth_gear_points(25);
    assert_eq!(dense_tooth_gear.len(), 304);
    let dense_tooth_gear_30 = dense_tooth_gear_points(30);
    assert_eq!(dense_tooth_gear_30.len(), 364);
    let dense_tooth_gear_37 = dense_tooth_gear_points(37);
    assert_eq!(dense_tooth_gear_37.len(), 448);
    let triangle_notch = polygon(&[
        (0.0, 0.0),
        (20.0, 0.0),
        (20.0, 20.0),
        (12.0, 20.0),
        (10.0, 14.0),
        (8.0, 20.0),
        (0.0, 20.0),
    ]);
    print_offset_shape_values(
        "offset.finite.triangle_notch_inflate_round_fix",
        &triangle_notch,
        ShapeModifyMode::Inflate,
        3.0,
    );
    print_pinned_offset_components("offset.finite.comb_deflate", &comb, 2.1);
    print_pinned_offset_components("offset.finite.comb_exact_collapse_deflate", &comb, 2.0);
    print_pinned_offset_components(
        "offset.finite.comb_collinear_top_exact_collapse_deflate",
        &comb_collinear_top,
        2.0,
    );
    print_pinned_offset_components(
        "offset.finite.dense_tooth_gear_deflate",
        &dense_tooth_gear,
        3.1,
    );
    print_pinned_offset_components(
        "offset.finite.dense_tooth_gear_30_deflate",
        &dense_tooth_gear_30,
        3.1,
    );
    print_pinned_offset_components(
        "offset.finite.dense_tooth_gear_37_deflate",
        &dense_tooth_gear_37,
        3.1,
    );
    print_pinned_offset_components("offset.finite.comb_inflate_close_gaps", &comb, -2.25);
    let scaled_translated_comb = comb
        .iter()
        .map(|(x, y)| (x * 1024.0 - 13427.0, y * 1024.0 - 2150.375))
        .collect::<Vec<_>>();
    print_pinned_offset_components(
        "offset.finite.comb_deflate_scaled_translated",
        &scaled_translated_comb,
        2150.4,
    );
    let scaled_translated_comb_shape = polygon(&scaled_translated_comb);
    print_offset_shape_values(
        "offset.finite.comb_deflate_scaled_translated_first",
        &scaled_translated_comb_shape,
        ShapeModifyMode::Deflate,
        2150.4,
    );
    let t_shape = [
        (0.0, 0.0),
        (12.0, 0.0),
        (12.0, 4.0),
        (8.0, 4.0),
        (8.0, 12.0),
        (4.0, 12.0),
        (4.0, 4.0),
        (0.0, 4.0),
    ];
    print_pinned_offset_components("offset.finite.t_exact_collapse_deflate", &t_shape, 2.0);
    let h_shape = [
        (0.0, 0.0),
        (4.0, 0.0),
        (4.0, 4.0),
        (8.0, 4.0),
        (8.0, 0.0),
        (12.0, 0.0),
        (12.0, 12.0),
        (8.0, 12.0),
        (8.0, 8.0),
        (4.0, 8.0),
        (4.0, 12.0),
        (0.0, 12.0),
    ];
    print_pinned_offset_components("offset.finite.h_exact_collapse_deflate", &h_shape, 2.0);
    let u_shape = [
        (0.0, 0.0),
        (12.0, 0.0),
        (12.0, 10.0),
        (8.0, 10.0),
        (8.0, 4.0),
        (4.0, 4.0),
        (4.0, 10.0),
        (0.0, 10.0),
    ];
    let u_shape_polygon = polygon(&u_shape);
    print_offset_shape_values(
        "offset.finite.u_notch_closes_inflate",
        &u_shape_polygon,
        ShapeModifyMode::Inflate,
        2.25,
    );
    let oblique_u_shape = [
        (0.0, 0.0),
        (9.6, 7.2),
        (3.6, 15.2),
        (0.4, 12.8),
        (4.0, 8.0),
        (0.8, 5.6),
        (-2.8, 10.4),
        (-6.0, 8.0),
    ];
    let oblique_u_shape_polygon = polygon(&oblique_u_shape);
    print_offset_shape_values(
        "offset.finite.oblique_u_notch_closes_inflate",
        &oblique_u_shape_polygon,
        ShapeModifyMode::Inflate,
        2.25,
    );
    print_pinned_offset_components("offset.finite.u_exact_collapse_deflate", &u_shape, 2.0);
    let c_shape = [
        (0.0, 0.0),
        (12.0, 0.0),
        (12.0, 4.0),
        (4.0, 4.0),
        (4.0, 8.0),
        (12.0, 8.0),
        (12.0, 12.0),
        (0.0, 12.0),
    ];
    print_pinned_offset_components("offset.finite.c_exact_collapse_deflate", &c_shape, 2.0);
    let plus_shape = [
        (4.0, 0.0),
        (8.0, 0.0),
        (8.0, 4.0),
        (12.0, 4.0),
        (12.0, 8.0),
        (8.0, 8.0),
        (8.0, 12.0),
        (4.0, 12.0),
        (4.0, 8.0),
        (0.0, 8.0),
        (0.0, 4.0),
        (4.0, 4.0),
    ];
    print_pinned_offset_components(
        "offset.finite.plus_exact_collapse_deflate",
        &plus_shape,
        2.0,
    );
    print_offset_shape_values(
        "offset.finite.comb_first_deflate",
        &polygon(&comb),
        ShapeModifyMode::Deflate,
        2.1,
    );
    print_offset_shape_values(
        "offset.finite.square_subgrid_deflate",
        &square,
        ShapeModifyMode::Deflate,
        0.000000001,
    );

    let large_square = polygon(&[
        (1_000_000.0, 1_000_000.0),
        (2_000_000.0, 1_000_000.0),
        (2_000_000.0, 2_000_000.0),
        (1_000_000.0, 2_000_000.0),
    ]);
    print_offset_shape_values(
        "offset.finite.sub_ulp_inflate",
        &large_square,
        ShapeModifyMode::Inflate,
        0.01,
    );
    print_offset_shape_values(
        "offset.finite.sub_ulp_deflate",
        &large_square,
        ShapeModifyMode::Deflate,
        0.01,
    );
    print_offset_shape_values(
        "offset.finite.subgrid_inflate",
        &large_square,
        ShapeModifyMode::Inflate,
        0.00001,
    );
}

fn print_offset_shape_values(name: &str, shape: &SPolygon, mode: ShapeModifyMode, distance: f32) {
    match offset_shape(shape, mode, distance) {
        Ok(offset) => {
            let mut values = vec![
                offset.n_vertices().to_string(),
                format!("{:.9}", offset.area),
            ];
            for point in &offset.vertices {
                values.push(format!("{:.9}", point.0));
                values.push(format!("{:.9}", point.1));
            }
            println!("{name}={}", values.join(","));
        }
        Err(error) => println!("{name}=error:{error}"),
    }
}

fn print_b2_shape_modification(
    name: &str,
    points: &[(f32, f32)],
    mode: ShapeModifyMode,
    cutoff: (f32, f32),
) {
    let original = polygon(points);
    let modified = close_narrow_concavities(&original, mode, cutoff);
    let mut values = vec![
        modified.n_vertices().to_string(),
        format!("{:.9}", modified.area),
    ];
    for point in &modified.vertices {
        values.push(format!("{:.9}", point.0));
        values.push(format!("{:.9}", point.1));
    }
    println!("{name}={}", values.join(","));
}

fn print_b2_simplify(name: &str, points: &[(f32, f32)], mode: ShapeModifyMode, tolerance: f32) {
    let original = polygon(points);
    let simplified = simplify_shape(&original, mode, tolerance);
    let mut values = vec![
        simplified.n_vertices().to_string(),
        format!("{:.9}", simplified.area),
    ];
    for point in &simplified.vertices {
        values.push(format!("{:.9}", point.0));
        values.push(format!("{:.9}", point.1));
    }
    println!("{name}={}", values.join(","));
}

fn run_b2_shape_modification_oracle() {
    let narrow_notch = [
        (0.0, 0.0),
        (10.0, 0.0),
        (10.0, 10.0),
        (6.0, 10.0),
        (6.0, 9.9),
        (4.0, 9.9),
        (4.0, 10.0),
        (0.0, 10.0),
    ];
    print_b2_shape_modification(
        "b2.narrow_notch.inflate_half_open_area",
        &narrow_notch,
        ShapeModifyMode::Inflate,
        (0.2, 0.0015),
    );

    let wrapped_notch = [
        (0.1, 6.0),
        (0.1, 4.0),
        (0.0, 4.0),
        (0.0, 0.0),
        (10.0, 0.0),
        (10.0, 10.0),
        (0.0, 10.0),
        (0.0, 6.0),
    ];
    print_b2_shape_modification(
        "b2.wrapped_notch.inflate",
        &wrapped_notch,
        ShapeModifyMode::Inflate,
        (0.2, 0.01),
    );

    let convex_bump = [
        (0.0, 0.0),
        (10.0, 0.0),
        (10.0, 10.0),
        (6.0, 10.0),
        (5.0, 10.1),
        (4.0, 10.0),
        (0.0, 10.0),
    ];
    print_b2_shape_modification(
        "b2.convex_bump.deflate",
        &convex_bump,
        ShapeModifyMode::Deflate,
        (0.2, 0.01),
    );

    print_b2_simplify(
        "b2.simplify.narrow_notch.inflate",
        &narrow_notch,
        ShapeModifyMode::Inflate,
        0.02,
    );
    print_b2_simplify(
        "b2.simplify.narrow_notch.deflate",
        &narrow_notch,
        ShapeModifyMode::Deflate,
        0.02,
    );

    let convex_bevel = [
        (0.0, 0.0),
        (4.0, 0.0),
        (4.1, 2.0),
        (4.0, 4.0),
        (2.0, 4.1),
        (0.0, 4.0),
        (-0.1, 2.0),
    ];
    print_b2_simplify(
        "b2.simplify.convex_bevel.inflate",
        &convex_bevel,
        ShapeModifyMode::Inflate,
        0.05,
    );
    print_b2_simplify(
        "b2.simplify.convex_bevel.deflate",
        &convex_bevel,
        ShapeModifyMode::Deflate,
        0.05,
    );

    let redundant_collinear = [(0.0, 0.0), (1.0, 0.0), (2.0, 0.0), (2.0, 2.0), (0.0, 2.0)];
    print_b2_simplify(
        "b2.simplify.collinear.zero_tolerance",
        &redundant_collinear,
        ShapeModifyMode::Inflate,
        0.0,
    );
    print_b2_simplify(
        "b2.simplify.collinear.negative_min_subnormal",
        &redundant_collinear,
        ShapeModifyMode::Inflate,
        f32::from_bits(0x8000_0001),
    );
}

fn run_b1_geometry_oracle() {
    let angles = [-7.0_f32, -1.0, 7.0, 1_000_000.0];
    println!(
        "geometry.normalize_rotation={:.9},{:.9},{:.9},{:.9}",
        jagua_rs::geometry::normalize_rotation(angles[0]),
        jagua_rs::geometry::normalize_rotation(angles[1]),
        jagua_rs::geometry::normalize_rotation(angles[2]),
        jagua_rs::geometry::normalize_rotation(angles[3])
    );
    let normalized_nan = jagua_rs::geometry::normalize_rotation(f32::NAN);
    let normalized_infinity = jagua_rs::geometry::normalize_rotation(f32::INFINITY);
    println!(
        "geometry.normalize_rotation_nonfinite=nan:{},infinity:{},negative_infinity:{}",
        normalized_nan.is_nan(),
        normalized_infinity.is_nan(),
        jagua_rs::geometry::normalize_rotation(f32::NEG_INFINITY).is_nan()
    );
    println!(
        "geometry.normalize_rotation_negative_zero={}",
        jagua_rs::geometry::normalize_rotation(-0.0_f32).is_sign_negative()
    );

    let transform = DTransformation::new(0.37, (1.25, -0.75));
    let decomposed = DTransformation::from(transform.compose());
    let (decomposed_x, decomposed_y) = decomposed.translation();
    println!(
        "geometry.dtransformation={:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9}",
        DTransformation::empty().rotation(),
        DTransformation::empty().translation().0,
        DTransformation::empty().translation().1,
        transform.rotation(),
        transform.translation().0,
        transform.translation().1,
        decomposed.rotation(),
        decomposed_x,
        decomposed_y
    );
    let applied = Point(2.0, -3.0).transform_clone(&transform.compose());
    println!(
        "geometry.transformation_apply={:.9},{:.9}",
        applied.0, applied.1
    );
    let rotate_then_translate = jagua_rs::geometry::Transformation::empty()
        .rotate_translate(0.5, (3.0, -2.0))
        .decompose();
    let translate_then_rotate = jagua_rs::geometry::Transformation::empty()
        .translate_rotate((3.0, -2.0), 0.5)
        .decompose();
    println!(
        "geometry.transformation_order={:.9},{:.9},{:.9};{:.9},{:.9},{:.9}",
        rotate_then_translate.rotation(),
        rotate_then_translate.translation().0,
        rotate_then_translate.translation().1,
        translate_then_rotate.rotation(),
        translate_then_rotate.translation().0,
        translate_then_rotate.translation().1
    );
    let transform_base = jagua_rs::geometry::Transformation::from_translation((1.25, -2.5));
    let transform_other =
        jagua_rs::geometry::Transformation::from_rotation(-0.3).translate((0.25, 1.5));
    let decomposed_operation = DTransformation::new(-0.4, (0.5, 2.25));
    let transformation_paths = vec![
        jagua_rs::geometry::Transformation::empty(),
        jagua_rs::geometry::Transformation::from_translation((3.0, -4.0)),
        jagua_rs::geometry::Transformation::from_rotation(0.375),
        transform_base.clone().rotate(0.2),
        transform_base.clone().translate((3.0, -1.0)),
        transform_base.clone().rotate_translate(0.5, (2.0, -1.0)),
        transform_base.clone().translate_rotate((2.0, -1.0), 0.5),
        transform_base.clone().transform(&transform_other),
        transform_base
            .clone()
            .transform_from_decomposed(&decomposed_operation),
        transform_base.clone().inverse(),
    ];
    let transformation_probes = [Point(0.0, 0.0), Point(1.0, 0.0), Point(0.0, 1.0)];
    let mut matrix_probe_values = Vec::new();
    let mut decomposed_values = Vec::new();
    for path in &transformation_paths {
        for probe in &transformation_probes {
            let transformed = probe.transform_clone(path);
            matrix_probe_values.push(format!("{:.9},{:.9}", transformed.0, transformed.1));
        }
        let decomposed = path.decompose();
        let (translation_x, translation_y) = decomposed.translation();
        decomposed_values.push(format!(
            "{:.9},{:.9},{:.9}",
            decomposed.rotation(),
            translation_x,
            translation_y
        ));
    }
    println!(
        "geometry.transformation_matrix_probes={}",
        matrix_probe_values.join(";")
    );
    println!(
        "geometry.transformation_decompositions={}",
        decomposed_values.join(";")
    );
    let empty_transform_cases = [
        jagua_rs::geometry::Transformation::empty(),
        jagua_rs::geometry::Transformation::from_translation((0.0, 0.0)),
        jagua_rs::geometry::Transformation::from_rotation(0.375),
        jagua_rs::geometry::Transformation::from_rotation(std::f32::consts::TAU),
        jagua_rs::geometry::Transformation::from_translation((-0.0, 0.0)),
    ];
    println!(
        "geometry.transformation_is_empty={}",
        empty_transform_cases
            .iter()
            .map(|transform| transform.is_empty().to_string())
            .collect::<Vec<_>>()
            .join(",")
    );

    let circle = Circle::try_new(Point(2.0, 3.0), 5.0).unwrap();
    let circle_bbox = circle.bbox();
    println!(
        "geometry.circle_basics={:.9},{:.9},{:.9},{:.9},{:.9},{:.9}",
        circle.area(),
        circle_bbox.x_min,
        circle_bbox.y_min,
        circle_bbox.x_max,
        circle_bbox.y_max,
        circle.diameter()
    );
    println!(
        "geometry.circle_contains={}",
        circle.collides_with(&Point(2.0, 3.0))
    );
    println!(
        "geometry.circle_construction=zero_radius:{},negative_radius:{},nonfinite_center:{},nonfinite_radius:{}",
        Circle::try_new(Point(0.0, 0.0), 0.0).is_ok(),
        Circle::try_new(Point(0.0, 0.0), -1.0).is_ok(),
        Circle::try_new(Point(f32::INFINITY, 0.0), 1.0).is_ok(),
        Circle::try_new(Point(0.0, 0.0), f32::INFINITY).is_ok()
    );

    let bounding_single = Circle::try_new(Point(3.0, -4.0), 0.0).unwrap();
    let single_bounding_circle = Circle::bounding_circle([&bounding_single]);
    let outer_first_circles = [
        Circle::try_new(Point(0.0, 0.0), 10.0).unwrap(),
        Circle::try_new(Point(1.0, 0.0), 2.0).unwrap(),
    ];
    let outer_first = Circle::bounding_circle(outer_first_circles.iter());
    let inner_first_circles = [
        Circle::try_new(Point(0.0, 0.0), 1.0).unwrap(),
        Circle::try_new(Point(1.0, 0.0), 10.0).unwrap(),
    ];
    let inner_first = Circle::bounding_circle(inner_first_circles.iter());
    let concentric_nested_circles = [
        Circle::try_new(Point(0.0, 0.0), 1.0).unwrap(),
        Circle::try_new(Point(0.0, 0.0), 2.0).unwrap(),
    ];
    let concentric_nested = Circle::bounding_circle(concentric_nested_circles.iter());
    println!(
        "geometry.circle_bounding_circle_empty_contract={}",
        std::panic::catch_unwind(|| Circle::bounding_circle(std::iter::empty::<&Circle>()))
            .is_err()
    );
    println!(
        "geometry.circle_bounding_circle_finite_values={:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9}",
        single_bounding_circle.center.0,
        single_bounding_circle.center.1,
        single_bounding_circle.radius,
        outer_first.center.0,
        outer_first.center.1,
        outer_first.radius,
        inner_first.center.0,
        inner_first.center.1,
        inner_first.radius
    );
    println!(
        "geometry.circle_bounding_circle_finite_contract={},{},{},{},{}",
        single_bounding_circle == bounding_single,
        outer_first.center == Point(0.0, 0.0) && outer_first.radius == 10.0,
        inner_first.center == Point(5.0, 0.0) && inner_first.radius == 6.0,
        inner_first
            .center
            .distance_to(&inner_first_circles[1].center)
            + inner_first_circles[1].radius
            > inner_first.radius,
        concentric_nested.center.0.is_nan()
            && concentric_nested.center.1.is_nan()
            && concentric_nested.radius.is_nan()
    );

    let edge = Edge::try_new(Point(0.0, 0.0), Point(3.0, 4.0)).unwrap();
    let closest = edge.closest_point_on_edge(&Point(1.0, 2.0));
    println!(
        "geometry.edge_closest={:.9},{:.9},{:.9},{:.9}",
        closest.0,
        closest.1,
        edge.distance_to(&Point(1.0, 2.0)),
        edge.sq_distance_to(&Point(1.0, 2.0))
    );
    println!(
        "geometry.edge_construction=valid:{},zero_length:{},f32_collapsed:{}",
        Edge::try_new(Point(0.0, 0.0), Point(1.0, 0.0)).is_ok(),
        Edge::try_new(Point(1.0, 1.0), Point(1.0, 1.0)).is_ok(),
        Edge::try_new(Point(1.0, 0.0), Point(1.0 + f32::EPSILON * 0.5, 0.0)).is_ok()
    );
    let zero_length_edge = Edge {
        start: Point(1.0, 2.0),
        end: Point(1.0, 2.0),
    };
    let zero_length_closest = zero_length_edge.closest_point_on_edge(&Point(2.0, 3.0));
    let zero_length_bbox = zero_length_edge.bbox();
    println!(
        "geometry.edge_zero_length_operations={:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9}",
        zero_length_closest.0,
        zero_length_closest.1,
        zero_length_edge.length(),
        zero_length_edge.centroid().0,
        zero_length_edge.centroid().1,
        zero_length_bbox.x_min,
        zero_length_bbox.y_min,
        zero_length_bbox.x_max,
        zero_length_bbox.y_max,
        zero_length_edge.sq_distance_to(&Point(2.0, 3.0))
    );
    let zero_length_front = zero_length_edge.extend_at_front(2.0);
    let zero_length_back = zero_length_edge.extend_at_back(2.0);
    let zero_length_scaled = zero_length_edge.scale(3.0);
    let zero_length_reversed = zero_length_edge.reverse();
    let zero_length_crossing = Edge {
        start: Point(0.0, 2.0),
        end: Point(2.0, 2.0),
    };
    println!(
        "geometry.edge_zero_length_finite_contract={},{},{},{},{},{},{},{},{},{}",
        zero_length_front.start.0.is_nan(),
        zero_length_front.start.1.is_nan(),
        zero_length_front.end == zero_length_edge.end,
        zero_length_back.start == zero_length_edge.start,
        zero_length_back.end.0.is_nan(),
        zero_length_back.end.1.is_nan(),
        zero_length_scaled.start == zero_length_edge.start
            && zero_length_scaled.end == zero_length_edge.end,
        zero_length_reversed.start == zero_length_edge.end
            && zero_length_reversed.end == zero_length_edge.start,
        zero_length_edge.collides_with(&zero_length_crossing),
        zero_length_edge
            .collides_at(&zero_length_crossing)
            .is_some()
    );
    println!(
        "geometry.edge_zero_length_scale_reverse={:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9}",
        zero_length_scaled.start.0,
        zero_length_scaled.start.1,
        zero_length_scaled.end.0,
        zero_length_scaled.end.1,
        zero_length_reversed.start.0,
        zero_length_reversed.start.1,
        zero_length_reversed.end.0,
        zero_length_reversed.end.1
    );

    let smallest_normal = f32::MIN_POSITIVE;
    let underflow_length_edge =
        Edge::try_new(Point(0.0, 0.0), Point(smallest_normal, 0.0)).unwrap();
    let underflow_front = underflow_length_edge.extend_at_front(1.0);
    let underflow_back = underflow_length_edge.extend_at_back(1.0);
    println!(
        "geometry.edge_underflow_length_contract={},{},{},{},{},{}",
        true,
        underflow_length_edge.length() == 0.0,
        underflow_front.start.0.is_infinite() && underflow_front.start.0.is_sign_negative(),
        underflow_front.start.1.is_nan(),
        underflow_back.end.0.is_infinite() && underflow_back.end.0.is_sign_positive(),
        underflow_back.end.1.is_nan()
    );

    let nan_edge = Edge::try_new(Point(f32::NAN, 0.0), Point(1.0, 1.0)).unwrap();
    let infinite_edge = Edge::try_new(Point(f32::INFINITY, 0.0), Point(1.0, 1.0)).unwrap();
    let nan_edge_bbox = nan_edge.bbox();
    let infinite_edge_bbox = infinite_edge.bbox();
    println!(
        "geometry.edge_nonfinite_bbox_contract={},{},{},{},{},{},{},{}",
        nan_edge.start.0.is_nan(),
        infinite_edge.start.0.is_infinite(),
        !nan_edge_bbox.x_min.is_nan(),
        !nan_edge_bbox.y_min.is_nan(),
        !nan_edge_bbox.x_max.is_nan(),
        !nan_edge_bbox.y_max.is_nan(),
        nan_edge_bbox.x_min == nan_edge_bbox.x_max,
        infinite_edge_bbox.x_max.is_infinite()
    );
    println!(
        "geometry.edge_nan_bbox_values={:.9},{:.9},{:.9},{:.9}",
        nan_edge_bbox.x_min, nan_edge_bbox.y_min, nan_edge_bbox.x_max, nan_edge_bbox.y_max
    );
    let proper_edge_a = Edge::try_new(Point(0.0, 0.0), Point(10.0, 10.0)).unwrap();
    let proper_edge_b = Edge::try_new(Point(0.0, 10.0), Point(10.0, 0.0)).unwrap();
    let endpoint_edge_a = Edge::try_new(Point(0.0, 0.0), Point(2.0, 0.0)).unwrap();
    let endpoint_edge_b = Edge::try_new(Point(2.0, 0.0), Point(2.0, 1.0)).unwrap();
    let collinear_edge = Edge::try_new(Point(1.0, 0.0), Point(3.0, 0.0)).unwrap();
    println!(
        "geometry.edge_intersects=proper:{},endpoint:{},collinear:{}",
        proper_edge_a.collides_with(&proper_edge_b),
        endpoint_edge_a.collides_with(&endpoint_edge_b),
        endpoint_edge_a.collides_with(&collinear_edge)
    );
    let edge_contact_cases = vec![
        (
            Edge::try_new(Point(0.0, 0.0), Point(4.0, 0.0)).unwrap(),
            Edge::try_new(Point(2.0, -1.0), Point(2.0, 1.0)).unwrap(),
        ),
        (
            Edge::try_new(Point(0.0, 0.0), Point(2.0, 0.0)).unwrap(),
            Edge::try_new(Point(2.0, 0.0), Point(2.0, 1.0)).unwrap(),
        ),
        (
            Edge::try_new(Point(0.0, 0.0), Point(4.0, 0.0)).unwrap(),
            Edge::try_new(Point(2.0, 0.0), Point(2.0, 2.0)).unwrap(),
        ),
        (
            Edge::try_new(Point(0.0, 0.0), Point(2.0, 0.0)).unwrap(),
            Edge::try_new(Point(1.0, 0.0), Point(3.0, 0.0)).unwrap(),
        ),
        (
            Edge::try_new(Point(0.0, 0.0), Point(2.0, 0.0)).unwrap(),
            Edge::try_new(Point(2.0, 0.0), Point(4.0, 0.0)).unwrap(),
        ),
        (
            Edge::try_new(Point(0.0, 0.0), Point(4.0, 0.0)).unwrap(),
            Edge::try_new(Point(0.0, 1.0), Point(4.0, 1.0)).unwrap(),
        ),
        (
            Edge::try_new(Point(0.0, 0.0), Point(2.0, 2.0)).unwrap(),
            Edge::try_new(Point(0.0, 1.0), Point(1.0, 1.5)).unwrap(),
        ),
    ];
    let mut edge_contact_results = Vec::new();
    let mut edge_contact_points = Vec::new();
    for (edge_a, edge_b) in edge_contact_cases {
        if let Some(Point(x, y)) = edge_a.collides_at(&edge_b) {
            edge_contact_results.push("true");
            edge_contact_points.push(format!("{x:.9},{y:.9}"));
        } else {
            edge_contact_results.push("false");
        }
    }
    println!(
        "geometry.edge_collides_at={}",
        edge_contact_results.join(",")
    );
    println!(
        "geometry.edge_collides_at_points={}",
        edge_contact_points.join(";")
    );

    let rect = Rect::try_new(0.0, 0.0, 4.0, 2.0).unwrap();
    let quadrants = rect.quadrants();
    println!(
        "geometry.rect_basics={:.9},{:.9};{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9};{:.9},{:.9}",
        rect.centroid().0,
        rect.centroid().1,
        quadrants[0].x_min,
        quadrants[0].y_min,
        quadrants[0].x_max,
        quadrants[0].y_max,
        quadrants[1].x_min,
        quadrants[1].y_min,
        quadrants[1].x_max,
        quadrants[1].y_max,
        quadrants[2].x_min,
        quadrants[2].y_min,
        quadrants[2].x_max,
        quadrants[2].y_max,
        quadrants[3].x_min,
        quadrants[3].y_min,
        quadrants[3].x_max,
        quadrants[3].y_max,
        rect.width(),
        rect.height()
    );
    let enclosed_rect = Rect::try_new(1.0, 0.5, 3.0, 1.5).unwrap();
    let overlap_rect = Rect::try_new(3.0, 1.0, 5.0, 4.0).unwrap();
    let disjoint_rect = Rect::try_new(5.0, 0.0, 6.0, 1.0).unwrap();
    println!(
        "geometry.rect_relations=surrounding:{},enclosed:{},intersecting:{},disjoint:{}",
        matches!(
            rect.relation_to(enclosed_rect),
            jagua_rs::geometry::geo_enums::GeoRelation::Surrounding
        ),
        matches!(
            enclosed_rect.relation_to(rect),
            jagua_rs::geometry::geo_enums::GeoRelation::Enclosed
        ),
        matches!(
            rect.relation_to(overlap_rect),
            jagua_rs::geometry::geo_enums::GeoRelation::Intersecting
        ),
        matches!(
            rect.relation_to(disjoint_rect),
            jagua_rs::geometry::geo_enums::GeoRelation::Disjoint
        )
    );
    let threshold_rect = Rect::try_new(1.0, 1.0, 3.0, 3.0).unwrap();
    let mut rect_relation_ulp_results = Vec::new();
    for ulp_distance in [1_u32, 2, 3, 4, 8, 16] {
        let x_min = f32::from_bits(1.0_f32.to_bits() - ulp_distance);
        let near_boundary_rect = Rect::try_new(x_min, 1.5, 3.0, 2.5).unwrap();
        let strict_relation = threshold_rect.relation_to(near_boundary_rect);
        let almost_relation = threshold_rect.almost_relation_to(near_boundary_rect);
        rect_relation_ulp_results.push(format!(
            "{},{},{}",
            matches!(
                strict_relation,
                jagua_rs::geometry::geo_enums::GeoRelation::Intersecting
            ),
            matches!(
                almost_relation,
                jagua_rs::geometry::geo_enums::GeoRelation::Surrounding
            ),
            matches!(
                almost_relation,
                jagua_rs::geometry::geo_enums::GeoRelation::Intersecting
            )
        ));
    }
    println!(
        "geometry.rect_relation_ulp_boundary={}",
        rect_relation_ulp_results.join(";")
    );
    let through_rect = Edge::try_new(Point(-1.0, 1.0), Point(5.0, 1.0)).unwrap();
    let outside_rect = Edge::try_new(Point(-1.0, 3.0), Point(5.0, 3.0)).unwrap();
    println!(
        "geometry.rect_edge_intersects=through:{},outside:{}",
        rect.collides_with(&through_rect),
        rect.collides_with(&outside_rect)
    );
    println!(
        "geometry.rect_construction=valid:{},zero_width:{},zero_height:{},nan_max:{},infinite_max:{}",
        Rect::try_new(0.0, 0.0, 1.0, 1.0).is_ok(),
        Rect::try_new(1.0, 0.0, 1.0, 1.0).is_ok(),
        Rect::try_new(0.0, 1.0, 1.0, 1.0).is_ok(),
        Rect::try_new(0.0, 0.0, f32::NAN, 1.0).is_ok(),
        Rect::try_new(0.0, 0.0, f32::INFINITY, 1.0).is_ok()
    );
    let boundary_rect = Rect::try_new(0.0, 0.0, 2.0, 1.0).unwrap();
    let edge_touch_rect = Rect::try_new(2.0, 0.0, 3.0, 1.0).unwrap();
    let corner_touch_rect = Rect::try_new(2.0, 1.0, 3.0, 2.0).unwrap();
    let overlap_boundary_rect = Rect::try_new(1.0, 0.5, 3.0, 2.0).unwrap();
    let disjoint_boundary_rect = Rect::try_new(3.0, 0.0, 4.0, 1.0).unwrap();
    println!(
        "geometry.rect_boundary_intersections={},{},{},{}",
        Rect::intersection(boundary_rect, overlap_boundary_rect).is_some(),
        Rect::intersection(boundary_rect, edge_touch_rect).is_some(),
        Rect::intersection(boundary_rect, corner_touch_rect).is_some(),
        Rect::intersection(boundary_rect, disjoint_boundary_rect).is_some()
    );
    println!(
        "geometry.rect_boundary_resizes={},{},{},{}",
        boundary_rect.resize_by(-1.0, 0.0).is_none(),
        boundary_rect.resize_by(0.0, -0.5).is_none(),
        boundary_rect.resize_by(-1.0, -0.5).is_none(),
        boundary_rect.resize_by(0.0, 0.0) == Some(boundary_rect)
    );
    println!(
        "geometry.rect_zero_scale_rejected={}",
        std::panic::catch_unwind(|| boundary_rect.scale(0.0)).is_err()
    );
    let zero_width_rect = Rect {
        x_min: 1.0,
        y_min: 0.0,
        x_max: 1.0,
        y_max: 2.0,
    };
    let zero_height_rect = Rect {
        x_min: 0.0,
        y_min: 1.0,
        x_max: 2.0,
        y_max: 1.0,
    };
    println!(
        "geometry.rect_degenerate_metrics={:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9}",
        zero_width_rect.width(),
        zero_width_rect.height(),
        zero_width_rect.area(),
        zero_width_rect.diameter(),
        zero_width_rect.centroid().0,
        zero_width_rect.centroid().1,
        zero_height_rect.width(),
        zero_height_rect.height(),
        zero_height_rect.area(),
        zero_height_rect.diameter(),
        zero_height_rect.centroid().0,
        zero_height_rect.centroid().1
    );
    let mut degenerate_rect_shapes = Vec::new();
    for degenerate_rect in [zero_width_rect, zero_height_rect] {
        for corner in degenerate_rect.corners() {
            degenerate_rect_shapes.push(format!("{:.9},{:.9}", corner.0, corner.1));
        }
        for edge in degenerate_rect.edges() {
            degenerate_rect_shapes.push(format!(
                "{:.9},{:.9},{:.9},{:.9}",
                edge.start.0, edge.start.1, edge.end.0, edge.end.1
            ));
        }
    }
    println!(
        "geometry.rect_degenerate_boundary_shapes={}",
        degenerate_rect_shapes.join(";")
    );
    let mut degenerate_rect_sides = Vec::new();
    for degenerate_rect in [zero_width_rect, zero_height_rect] {
        for side in degenerate_rect.sides() {
            degenerate_rect_sides.push(format!(
                "{:.9},{:.9},{:.9},{:.9}",
                side.start.0, side.start.1, side.end.0, side.end.1
            ));
        }
    }
    println!(
        "geometry.rect_degenerate_sides={}",
        degenerate_rect_sides.join(";")
    );
    let area_rect = Rect::try_new(0.0, 0.0, 2.0, 2.0).unwrap();
    println!(
        "geometry.rect_degenerate_intersections={},{},{},{}",
        Rect::intersection(zero_width_rect, area_rect).is_some(),
        Rect::intersection(zero_width_rect, zero_width_rect).is_some(),
        Rect::intersection(zero_height_rect, area_rect).is_some(),
        Rect::intersection(zero_height_rect, zero_height_rect).is_some()
    );
    let lower_left_area_rect = Rect::try_new(-2.0, -1.0, 0.0, 1.0).unwrap();
    let upper_right_area_rect = Rect::try_new(3.0, 2.0, 4.0, 3.0).unwrap();
    let degenerate_bounding_rects = [
        Rect::bounding_rect(zero_width_rect, lower_left_area_rect),
        Rect::bounding_rect(zero_height_rect, upper_right_area_rect),
        Rect::bounding_rect(zero_width_rect, zero_height_rect),
    ];
    println!(
        "geometry.rect_degenerate_bounding_rects={:.9},{:.9},{:.9},{:.9};{:.9},{:.9},{:.9},{:.9};{:.9},{:.9},{:.9},{:.9}",
        degenerate_bounding_rects[0].x_min,
        degenerate_bounding_rects[0].y_min,
        degenerate_bounding_rects[0].x_max,
        degenerate_bounding_rects[0].y_max,
        degenerate_bounding_rects[1].x_min,
        degenerate_bounding_rects[1].y_min,
        degenerate_bounding_rects[1].x_max,
        degenerate_bounding_rects[1].y_max,
        degenerate_bounding_rects[2].x_min,
        degenerate_bounding_rects[2].y_min,
        degenerate_bounding_rects[2].x_max,
        degenerate_bounding_rects[2].y_max
    );
    let recover_width = zero_width_rect.resize_by(1.0, 0.0).unwrap();
    let recover_height = zero_height_rect.resize_by(0.0, 1.0).unwrap();
    println!(
        "geometry.rect_degenerate_resize_recovery={:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9}",
        recover_width.x_min,
        recover_width.y_min,
        recover_width.x_max,
        recover_width.y_max,
        recover_height.x_min,
        recover_height.y_min,
        recover_height.x_max,
        recover_height.y_max
    );
    println!(
        "geometry.rect_degenerate_scale_contract={},{},{},{},{}",
        std::panic::catch_unwind(|| rect.scale(-1.0)).is_err(),
        std::panic::catch_unwind(|| zero_width_rect.scale(1.0)).is_err(),
        std::panic::catch_unwind(|| zero_width_rect.scale(2.0)).is_err(),
        std::panic::catch_unwind(|| zero_height_rect.scale(1.0)).is_err(),
        std::panic::catch_unwind(|| zero_height_rect.scale(2.0)).is_err()
    );
    let one_ulp_rect = Rect::try_new(1.0, 0.0, f32::from_bits(0x3f80_0001), 1.0).unwrap();
    let zero_width_rect = Rect {
        x_min: 1.0,
        y_min: 0.0,
        x_max: 1.0,
        y_max: 1.0,
    };
    let one_ulp_height_rect = Rect::try_new(0.0, 1.0, 1.0, f32::from_bits(0x3f80_0001)).unwrap();
    println!(
        "geometry.rect_quadrant_panic_contract={},{},{},{}",
        std::panic::catch_unwind(|| one_ulp_rect.quadrants()).is_err(),
        std::panic::catch_unwind(|| zero_width_rect.quadrants()).is_err(),
        std::panic::catch_unwind(|| one_ulp_height_rect.quadrants()).is_err(),
        std::panic::catch_unwind(|| zero_height_rect.quadrants()).is_err()
    );

    let rectangle = polygon(&[(0.0, 0.0), (4.0, 0.0), (4.0, 2.0), (0.0, 2.0)]);
    let edges = rectangle
        .edge_iter()
        .map(|edge| {
            format!(
                "{:.9},{:.9}>{:.9},{:.9}",
                edge.start.0, edge.start.1, edge.end.0, edge.end.1
            )
        })
        .collect::<Vec<_>>()
        .join(";");
    println!(
        "geometry.simple_polygon_basics=vertices:{},area:{:.9},diameter:{:.9},bbox:{:.9},{:.9},{:.9},{:.9},centroid:{:.9},{:.9},poi:{:.9},{:.9},{:.9},edges:{}",
        rectangle.n_vertices(),
        rectangle.area,
        rectangle.diameter,
        rectangle.bbox.x_min,
        rectangle.bbox.y_min,
        rectangle.bbox.x_max,
        rectangle.bbox.y_max,
        rectangle.centroid().0,
        rectangle.centroid().1,
        rectangle.poi.center.0,
        rectangle.poi.center.1,
        rectangle.poi.radius,
        edges
    );
    println!(
        "geometry.simple_polygon_contains=inside:{},boundary:{},outside:{}",
        rectangle.collides_with(&Point(1.0, 1.0)),
        rectangle.collides_with(&Point(0.0, 1.0)),
        rectangle.collides_with(&Point(-1.0, 1.0))
    );
    let clockwise = polygon(&[(0.0, 0.0), (0.0, 2.0), (4.0, 2.0), (4.0, 0.0)]);
    println!(
        "geometry.simple_polygon_clockwise_vertices={}",
        clockwise
            .vertices
            .iter()
            .map(|point| format!("{:.0},{:.0}", point.0, point.1))
            .collect::<Vec<_>>()
            .join(";")
    );
    let constructor_result = |points: &[(f32, f32)]| {
        SPolygon::new(points.iter().copied().map(Point::from).collect()).is_ok()
    };
    println!(
        "geometry.simple_polygon_construction=valid:{},too_few:{},duplicate:{},collinear:{},bowtie:{},tiny:{}",
        constructor_result(&[(0.0, 0.0), (4.0, 0.0), (4.0, 2.0), (0.0, 2.0)]),
        constructor_result(&[(0.0, 0.0), (1.0, 0.0)]),
        constructor_result(&[(0.0, 0.0), (2.0, 0.0), (2.0, 2.0), (2.0, 2.0), (0.0, 2.0)]),
        constructor_result(&[(0.0, 0.0), (1.0, 0.0), (2.0, 0.0)]),
        constructor_result(&[(0.0, 0.0), (2.0, 2.0), (0.0, 2.0), (2.0, 0.0)]),
        constructor_result(&[(0.0, 0.0), (0.0, 1.0e-5), (1.0e-5, 1.0e-5), (1.0e-5, 0.0)])
    );

    let polygon_transform = DTransformation::new(0.37, (10.0, -2.0)).compose();
    let transformed = rectangle.transform_clone(&polygon_transform);
    let transformed_vertices = transformed
        .vertices
        .iter()
        .map(|point| format!("{:.9},{:.9}", point.0, point.1))
        .collect::<Vec<_>>()
        .join(";");
    println!(
        "geometry.simple_polygon_transform=vertices:{};bbox:{:.9},{:.9},{:.9},{:.9};centroid:{:.9},{:.9};area:{:.9};diameter:{:.9}",
        transformed_vertices,
        transformed.bbox.x_min,
        transformed.bbox.y_min,
        transformed.bbox.x_max,
        transformed.bbox.y_max,
        transformed.centroid().0,
        transformed.centroid().1,
        transformed.area,
        transformed.diameter
    );

    let accessor_point = Point(1.25, -2.5);
    let accessor_edge = Edge::try_new(Point(0.0, 0.0), Point(3.0, 4.0)).unwrap();
    let accessor_bbox = accessor_edge.bbox();
    let accessor_rect = Rect::try_new(0.0, 0.0, 4.0, 2.0).unwrap();
    let accessor_sides = accessor_rect.sides();
    let accessor_sides_text = accessor_sides
        .iter()
        .map(|side| {
            format!(
                "{:.9},{:.9},{:.9},{:.9}",
                side.start.0, side.start.1, side.end.0, side.end.1
            )
        })
        .collect::<Vec<_>>()
        .join(";");
    println!(
        "geometry.independent_accessors=point:{:.9},{:.9};bbox:{:.9},{:.9},{:.9},{:.9};sides:{}",
        accessor_point.x(),
        accessor_point.y(),
        accessor_bbox.x_min,
        accessor_bbox.y_min,
        accessor_bbox.x_max,
        accessor_bbox.y_max,
        accessor_sides_text
    );

    let transform_from = jagua_rs::geometry::Transformation::from_translation((3.0, -2.0));
    let transform_from_point = Point(2.0, -3.0);
    let mut point_from_target = Point(99.0, 98.0);
    point_from_target.transform_from(&transform_from_point, &transform_from);
    let transform_from_circle = Circle::try_new(Point(1.0, 2.0), 1.5).unwrap();
    let mut circle_from_target = Circle::try_new(Point(99.0, 98.0), 1.5).unwrap();
    circle_from_target.transform_from(&transform_from_circle, &transform_from);
    let transform_from_edge = Edge::try_new(Point(-1.0, 0.0), Point(2.0, 1.0)).unwrap();
    let mut edge_from_target = Edge::try_new(Point(10.0, 10.0), Point(11.0, 12.0)).unwrap();
    edge_from_target.transform_from(&transform_from_edge, &transform_from);
    let transform_from_source_polygon = polygon(&[(0.0, 0.0), (4.0, 0.0), (4.0, 2.0), (0.0, 2.0)]);
    let mut polygon_from_target =
        polygon(&[(10.0, 10.0), (14.0, 10.0), (14.0, 12.0), (10.0, 12.0)]);
    polygon_from_target.transform_from(&transform_from_source_polygon, &transform_from);
    let polygon_from_vertices = polygon_from_target
        .vertices
        .iter()
        .map(|point| format!("{:.9},{:.9}", point.0, point.1))
        .collect::<Vec<_>>()
        .join(";");
    println!(
        "geometry.transform_from=point:{:.9},{:.9};circle:{:.9},{:.9},{:.9};edge:{:.9},{:.9},{:.9},{:.9};polygon_vertices:{};bbox:{:.9},{:.9},{:.9},{:.9};centroid:{:.9},{:.9};area:{:.9};diameter:{:.9};poi:{:.9},{:.9},{:.9}",
        point_from_target.0,
        point_from_target.1,
        circle_from_target.center.0,
        circle_from_target.center.1,
        circle_from_target.radius,
        edge_from_target.start.0,
        edge_from_target.start.1,
        edge_from_target.end.0,
        edge_from_target.end.1,
        polygon_from_vertices,
        polygon_from_target.bbox.x_min,
        polygon_from_target.bbox.y_min,
        polygon_from_target.bbox.x_max,
        polygon_from_target.bbox.y_max,
        polygon_from_target.centroid().0,
        polygon_from_target.centroid().1,
        polygon_from_target.area,
        polygon_from_target.diameter,
        polygon_from_target.poi.center.0,
        polygon_from_target.poi.center.1,
        polygon_from_target.poi.radius
    );

    let surrogate_config = SPSurrogateConfig {
        n_pole_limits: [(1, 0.0), (0, 0.0), (0, 0.0)],
        ff_pole_area_ratio: 1.0,
        n_ff_piers: 1,
    };
    let mut surrogate_reference = polygon(&[(0.0, 0.0), (4.0, 0.0), (4.0, 2.0), (0.0, 2.0)]);
    surrogate_reference
        .generate_surrogate(surrogate_config)
        .unwrap();
    let mut surrogate_target = polygon(&[(0.0, 0.0), (4.0, 0.0), (4.0, 2.0), (0.0, 2.0)]);
    surrogate_target
        .generate_surrogate(surrogate_config)
        .unwrap();
    let surrogate_transform = DTransformation::new(0.37, (3.0, -2.0)).compose();
    surrogate_target.transform_from(&surrogate_reference, &surrogate_transform);
    let transformed_surrogate = surrogate_target.surrogate();
    let mut transformed_surrogate_values = vec![surrogate_target.n_vertices() as f32];
    for vertex in &surrogate_target.vertices {
        transformed_surrogate_values.extend([vertex.0, vertex.1]);
    }
    transformed_surrogate_values.extend([
        surrogate_target.bbox.x_min,
        surrogate_target.bbox.y_min,
        surrogate_target.bbox.x_max,
        surrogate_target.bbox.y_max,
        surrogate_target.centroid().0,
        surrogate_target.centroid().1,
        surrogate_target.area,
        surrogate_target.diameter,
        surrogate_target.poi.center.0,
        surrogate_target.poi.center.1,
        surrogate_target.poi.radius,
        transformed_surrogate.poles.len() as f32,
        transformed_surrogate.ff_poles().len() as f32,
        transformed_surrogate.piers.len() as f32,
        transformed_surrogate.convex_hull_area,
        transformed_surrogate.convex_hull_indices.len() as f32,
    ]);
    for index in &transformed_surrogate.convex_hull_indices {
        transformed_surrogate_values.push(*index as f32);
    }
    for pole in &transformed_surrogate.poles {
        transformed_surrogate_values.extend([pole.center.0, pole.center.1, pole.radius]);
    }
    for pier in &transformed_surrogate.piers {
        transformed_surrogate_values.extend([pier.start.0, pier.start.1, pier.end.0, pier.end.1]);
    }
    let surrogate_fixture_key = if cfg!(debug_assertions) {
        "geometry.transform_from_surrogate_debug"
    } else {
        "geometry.transform_from_surrogate_release"
    };
    println!(
        "{}={}",
        surrogate_fixture_key,
        transformed_surrogate_values
            .iter()
            .map(|value| format!("{value:.9}"))
            .collect::<Vec<_>>()
            .join(",")
    );
    let mut no_surrogate_target = polygon(&[(0.0, 0.0), (4.0, 0.0), (4.0, 2.0), (0.0, 2.0)]);
    no_surrogate_target.transform_from(&surrogate_reference, &surrogate_transform);
    let mut surrogate_target_without_reference =
        polygon(&[(0.0, 0.0), (4.0, 0.0), (4.0, 2.0), (0.0, 2.0)]);
    surrogate_target_without_reference
        .generate_surrogate(surrogate_config)
        .unwrap();
    let no_surrogate_reference = polygon(&[(0.0, 0.0), (4.0, 0.0), (4.0, 2.0), (0.0, 2.0)]);
    let missing_reference_surrogate_panics =
        std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
            surrogate_target_without_reference
                .transform_from(&no_surrogate_reference, &surrogate_transform);
        }))
        .is_err();
    println!(
        "geometry.transform_from_surrogate_state={},{}",
        no_surrogate_target.surrogate.is_none(),
        missing_reference_surrogate_panics
    );

    let dispatch_circle = Circle::try_new(Point(0.0, 0.0), 1.0).unwrap();
    let dispatch_rect = Rect::try_new(0.0, 0.0, 2.0, 2.0).unwrap();
    let dispatch_touching_rect = Rect::try_new(2.0, 0.0, 4.0, 2.0).unwrap();
    let dispatch_separated_rect = Rect::try_new(2.01, 0.0, 4.0, 2.0).unwrap();
    let dispatch_epsilon = f32::EPSILON;
    let dispatch_near_rect = Rect::try_new(-1.0, 0.0, -dispatch_epsilon * 0.25, 2.0).unwrap();
    let dispatch_far_rect = Rect::try_new(-1.0, 0.0, -dispatch_epsilon * 2.0, 2.0).unwrap();
    let dispatch_polygon = polygon(&[(0.0, 0.0), (4.0, 0.0), (4.0, 2.0), (0.0, 2.0)]);
    let dispatch_crossing_edge = Edge::try_new(Point(-1.0, 1.0), Point(3.0, 1.0)).unwrap();
    let circle_inside_point = Point(0.25, 0.0);
    let circle_outside_point = Point(3.0, 0.0);
    let rect_inside_point = Point(1.0, 1.0);
    let rect_outside_point = Point(3.0, 1.0);
    let near_rect_point = Point(-dispatch_epsilon * 0.25, 1.0);
    let far_rect_point = Point(-dispatch_epsilon * 2.0, 1.0);
    println!(
        "geometry.trait_dispatch=point_circle_inside:{},point_circle_outside:{},point_rect_inside:{},point_rect_outside:{},point_polygon_inside:{},point_polygon_outside:{},rect_point_inside:{},rect_point_outside:{},rect_rect_touching:{},rect_rect_separated:{},almost_rect_near:{},almost_rect_far:{},almost_point_near:{},almost_point_far:{},edge_rect:{},rect_edge:{}",
        circle_inside_point.collides_with(&dispatch_circle),
        circle_outside_point.collides_with(&dispatch_circle),
        rect_inside_point.collides_with(&dispatch_rect),
        rect_outside_point.collides_with(&dispatch_rect),
        rect_inside_point.collides_with(&dispatch_polygon),
        Point(5.0, 1.0).collides_with(&dispatch_polygon),
        dispatch_rect.collides_with(&rect_inside_point),
        dispatch_rect.collides_with(&rect_outside_point),
        dispatch_rect.collides_with(&dispatch_touching_rect),
        dispatch_rect.collides_with(&dispatch_separated_rect),
        dispatch_rect.almost_collides_with(&dispatch_near_rect),
        dispatch_rect.almost_collides_with(&dispatch_far_rect),
        dispatch_rect.almost_collides_with(&near_rect_point),
        dispatch_rect.almost_collides_with(&far_rect_point),
        dispatch_crossing_edge.collides_with(&dispatch_rect),
        dispatch_rect.collides_with(&dispatch_crossing_edge)
    );

    let helper_points = vec![
        Point(0.0, 0.0),
        Point(4.0, 0.0),
        Point(4.0, 2.0),
        Point(0.0, 2.0),
    ];
    let helper_diameter = SPolygon::calculate_diameter(helper_points.clone());
    let helper_bbox = SPolygon::generate_bounding_box(&helper_points);
    let helper_area = SPolygon::calculate_area(&helper_points);
    let helper_poi = SPolygon::calculate_poi(&helper_points, helper_diameter).unwrap();
    println!(
        "geometry.simple_polygon_helpers=diameter:{:.9};bbox:{:.9},{:.9},{:.9},{:.9};area:{:.9};poi:{:.9},{:.9},{:.9}",
        helper_diameter,
        helper_bbox.x_min,
        helper_bbox.y_min,
        helper_bbox.x_max,
        helper_bbox.y_max,
        helper_area,
        helper_poi.center.0,
        helper_poi.center.1,
        helper_poi.radius
    );

    let empty_helper_points: Vec<Point> = vec![];
    let singleton_helper_points = vec![Point(1.0, 2.0)];
    let two_helper_points = vec![Point(0.0, 0.0), Point(2.0, 2.0)];
    let collinear_helper_points = vec![Point(0.0, 0.0), Point(1.0, 1.0), Point(2.0, 2.0)];
    let two_helper_diameter = SPolygon::calculate_diameter(two_helper_points.clone());
    let collinear_helper_diameter = SPolygon::calculate_diameter(collinear_helper_points.clone());
    let empty_diameter_panics =
        std::panic::catch_unwind(|| SPolygon::calculate_diameter(empty_helper_points.clone()))
            .is_err();
    let empty_bbox_panics =
        std::panic::catch_unwind(|| SPolygon::generate_bounding_box(&empty_helper_points)).is_err();
    let empty_poi_panics =
        std::panic::catch_unwind(|| SPolygon::calculate_poi(&empty_helper_points, 0.0)).is_err();
    let singleton_diameter_panics =
        std::panic::catch_unwind(|| SPolygon::calculate_diameter(singleton_helper_points.clone()))
            .is_err();
    let singleton_bbox_panics =
        std::panic::catch_unwind(|| SPolygon::generate_bounding_box(&singleton_helper_points))
            .is_err();
    let singleton_poi_panics =
        std::panic::catch_unwind(|| SPolygon::calculate_poi(&singleton_helper_points, 0.0))
            .is_err();
    let two_point_poi = std::panic::catch_unwind(|| {
        SPolygon::calculate_poi(&two_helper_points, two_helper_diameter)
    });
    let (two_point_poi_panics, two_point_poi_is_error) = match two_point_poi {
        Ok(result) => (false, result.is_err()),
        Err(_) => (true, false),
    };
    let collinear_poi = std::panic::catch_unwind(|| {
        SPolygon::calculate_poi(&collinear_helper_points, collinear_helper_diameter)
    });
    let (collinear_poi_panics, collinear_poi_is_error) = match collinear_poi {
        Ok(result) => (false, result.is_err()),
        Err(_) => (true, false),
    };
    println!(
        "geometry.simple_polygon_helper_failures={},{},{},{},{},{},{},{},{},{}",
        empty_diameter_panics,
        empty_bbox_panics,
        empty_poi_panics,
        singleton_diameter_panics,
        singleton_bbox_panics,
        singleton_poi_panics,
        two_point_poi_is_error,
        two_point_poi_panics,
        collinear_poi_is_error,
        collinear_poi_panics
    );
    println!(
        "geometry.simple_polygon_helper_rejections={},{},{},{}",
        SPolygon::new(empty_helper_points.clone()).is_err(),
        SPolygon::new(singleton_helper_points.clone()).is_err(),
        SPolygon::new(two_helper_points.clone()).is_err(),
        SPolygon::new(collinear_helper_points.clone()).is_err()
    );
    let duplicate_pair_points = vec![Point(1.0, 2.0), Point(1.0, 2.0)];
    let duplicate_vertex_points = vec![
        Point(0.0, 0.0),
        Point(2.0, 0.0),
        Point(2.0, 0.0),
        Point(0.0, 2.0),
    ];
    let horizontal_collinear_points = vec![Point(0.0, 0.0), Point(1.0, 0.0), Point(2.0, 0.0)];
    let duplicate_pair_diameter = SPolygon::calculate_diameter(duplicate_pair_points.clone());
    let duplicate_vertex_diameter = SPolygon::calculate_diameter(duplicate_vertex_points.clone());
    let duplicate_vertex_bbox = SPolygon::generate_bounding_box(&duplicate_vertex_points);
    let duplicate_vertex_area = SPolygon::calculate_area(&duplicate_vertex_points);
    let horizontal_bbox_panics =
        std::panic::catch_unwind(|| SPolygon::generate_bounding_box(&horizontal_collinear_points))
            .is_err();
    println!(
        "geometry.simple_polygon_degenerate_metrics={:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9}",
        SPolygon::calculate_area(&empty_helper_points),
        SPolygon::calculate_area(&singleton_helper_points),
        duplicate_pair_diameter,
        SPolygon::calculate_area(&duplicate_pair_points),
        duplicate_vertex_diameter,
        duplicate_vertex_bbox.x_min,
        duplicate_vertex_bbox.y_min,
        duplicate_vertex_bbox.x_max,
        duplicate_vertex_bbox.y_max,
        duplicate_vertex_area,
        collinear_helper_diameter,
        SPolygon::calculate_area(&collinear_helper_points)
    );
    println!(
        "geometry.simple_polygon_degenerate_helper_contract={},{},{},{},{},{},{}",
        duplicate_pair_diameter == 0.0,
        SPolygon::calculate_area(&duplicate_pair_points) == 0.0,
        std::panic::catch_unwind(|| { SPolygon::generate_bounding_box(&duplicate_pair_points) })
            .is_err(),
        horizontal_bbox_panics,
        duplicate_vertex_area == 2.0,
        duplicate_vertex_bbox.x_min == 0.0
            && duplicate_vertex_bbox.y_min == 0.0
            && duplicate_vertex_bbox.x_max == 2.0
            && duplicate_vertex_bbox.y_max == 2.0,
        SPolygon::calculate_area(&collinear_helper_points) == 0.0
    );
    println!(
        "geometry.simple_polygon_degenerate_construction={},{}",
        SPolygon::new(duplicate_vertex_points.clone()).is_err(),
        SPolygon::new(horizontal_collinear_points.clone()).is_err()
    );

    let degenerate_hull_inputs = vec![
        vec![],
        vec![Point(1.0, 2.0)],
        vec![Point(1.0, 2.0), Point(1.0, 2.0)],
        duplicate_vertex_points,
        collinear_helper_points.clone(),
        horizontal_collinear_points,
    ];
    let mut degenerate_hull_counts = Vec::new();
    let mut degenerate_hull_points = Vec::new();
    for points in degenerate_hull_inputs {
        let hull = convex_hull_from_points(points);
        degenerate_hull_counts.push(hull.len().to_string());
        degenerate_hull_points.extend(
            hull.into_iter()
                .map(|point| format!("{:.9},{:.9}", point.0, point.1)),
        );
    }
    println!(
        "geometry.convex_hull_finite_degenerates_counts={}",
        degenerate_hull_counts.join(",")
    );
    println!(
        "geometry.convex_hull_finite_degenerates_points={}",
        degenerate_hull_points.join(",")
    );

    let original = OriginalShape {
        shape: rectangle,
        pre_transform: DTransformation::new(0.5, (10.0, 20.0)),
        modify_mode: ShapeModifyMode::Inflate,
        modify_config: ShapeModifyConfig::default(),
    };
    let internal = original.convert_to_internal().unwrap();
    println!(
        "geometry.original_shape=raw:{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9};internal:{},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9},{:.9}",
        original.area(),
        original.diameter(),
        original.bbox().x_min,
        original.bbox().y_min,
        original.bbox().x_max,
        original.bbox().y_max,
        original.centroid().0,
        original.centroid().1,
        internal.n_vertices(),
        internal.vertices[0].0,
        internal.vertices[0].1,
        internal.bbox.x_min,
        internal.bbox.y_min,
        internal.bbox.x_max,
        internal.bbox.y_max,
        internal.centroid().0,
        internal.centroid().1,
        internal.area,
        internal.diameter
    );
}

fn run_problem_types_b7_parity_oracle() {
    let importer = Importer::new(
        CDEConfig {
            quadtree_depth: 4,
            cd_threshold: 32,
            item_surrogate_config: SPSurrogateConfig::none(),
        },
        None,
        None,
    );

    let spp_external: ExtSPInstance = serde_json::from_str(include_str!(
        "../../../Tests/Fixtures/RustReference/spp_instance.json"
    ))
    .unwrap();
    let spp_instance = import_spp_instance(&importer, &spp_external).unwrap();
    println!(
        "spp.instance={},{:.9},{:.9},{:.9}",
        spp_instance.total_item_qty(),
        spp_instance.item_area(),
        spp_instance.base_strip.fixed_height,
        spp_instance.base_strip.width
    );
    let mut spp_problem = SPProblem::new(spp_instance).unwrap();
    spp_problem.change_strip_width(5.0).unwrap();
    println!("spp.changed={:.9}", spp_problem.strip_width());
    let spp_placed = spp_problem.place_item(SPPlacement {
        item_idx: 0,
        d_transf: DTransformation::new(0.0, (1.0, 1.0)),
    });
    println!(
        "spp.placed={},{},{:.9},{:.9}",
        spp_problem.item_demand_qtys[0],
        spp_problem.n_placed_items(),
        spp_problem.density(),
        spp_problem.strip_width()
    );
    let spp_solution = spp_problem.save();
    println!(
        "spp.solution={},{:.9},{:.9}",
        spp_solution.layout_snapshot.placed_items.len(),
        spp_solution.density(),
        spp_solution.strip_width()
    );
    spp_problem.remove_item(spp_placed);
    println!(
        "spp.removed={},{},{:.9}",
        spp_problem.item_demand_qtys[0],
        spp_problem.n_placed_items(),
        spp_problem.density()
    );
    spp_problem.restore(&spp_solution);
    println!(
        "spp.restored={},{},{:.9},{:.9}",
        spp_problem.item_demand_qtys[0],
        spp_problem.n_placed_items(),
        spp_problem.density(),
        spp_problem.strip_width()
    );

    // Paired case: SPP import with min_item_separation > 0. Locks the initial
    // strip width formula `separation + total_item_area / (fixed_height -
    // separation)` (spp/io/import.rs) including the raw width bits, plus the
    // "separation leaves no usable strip height" error exit.
    let spp_sep_external: ExtSPInstance = {
        let mut value = serde_json::from_str::<serde_json::Value>(include_str!(
            "../../../Tests/Fixtures/RustReference/spp_instance.json"
        ))
        .unwrap();
        value["min_item_separation"] = serde_json::Value::from(0.5_f64);
        serde_json::from_value(value).unwrap()
    };
    match import_spp_instance(&importer, &spp_sep_external) {
        Ok(instance) => {
            println!(
                "spp_sep.instance={},{:.9},{:.9},{:.9}",
                instance.total_item_qty(),
                instance.item_area(),
                instance.base_strip.fixed_height,
                instance.base_strip.width
            );
            println!(
                "spp_sep.width_bits={:08x}",
                instance.base_strip.width.to_bits()
            );
        }
        Err(e) => println!("spp_sep.instance=error:{}", e),
    }
    let spp_sep_too_much: ExtSPInstance = {
        let mut value = serde_json::from_str::<serde_json::Value>(include_str!(
            "../../../Tests/Fixtures/RustReference/spp_instance.json"
        ))
        .unwrap();
        value["min_item_separation"] = serde_json::Value::from(4.0_f64);
        serde_json::from_value(value).unwrap()
    };
    match import_spp_instance(&importer, &spp_sep_too_much) {
        Ok(_) => println!("spp_sep.too_much_error=unexpected-ok"),
        Err(e) => println!("spp_sep.too_much_error={}", e),
    }

    let bpp_external: ExtBPInstance = serde_json::from_str(include_str!(
        "../../../Tests/Fixtures/RustReference/bpp_instance.json"
    ))
    .unwrap();
    let bpp_instance = import_bpp_instance(&importer, &bpp_external).unwrap();
    println!(
        "bpp.instance={},{:.9},{},{},{}",
        bpp_instance.total_item_qty(),
        bpp_instance.item_area(),
        bpp_instance.bins.len(),
        bpp_instance.bins[0].stock,
        bpp_instance.bins[0].cost
    );
    let mut bpp_problem = BPProblem::new(bpp_instance.clone());
    let (bpp_first_key, _) = bpp_problem.place_item(BPPlacement {
        layout_id: BPLayoutType::Closed { bin_id: 0 },
        item_idx: 0,
        d_transf: DTransformation::new(0.0, (1.0, 1.0)),
    });
    println!(
        "bpp.closed={},{},{},{},{},{:.9}",
        bpp_problem.item_demand_qtys[0],
        bpp_problem.bin_stock_qtys[0],
        bpp_problem.layouts.len(),
        bpp_problem.n_placed_items(),
        bpp_problem.bin_cost(),
        bpp_problem.density()
    );
    let (_, bpp_second_item_key) = bpp_problem.place_item(BPPlacement {
        layout_id: BPLayoutType::Open(bpp_first_key),
        item_idx: 0,
        d_transf: DTransformation::new(0.0, (3.5, 1.0)),
    });
    println!(
        "bpp.open={},{},{},{},{},{:.9}",
        bpp_problem.item_demand_qtys[0],
        bpp_problem.bin_stock_qtys[0],
        bpp_problem.layouts.len(),
        bpp_problem.n_placed_items(),
        bpp_problem.bin_cost(),
        bpp_problem.density()
    );
    let bpp_solution = bpp_problem.save();
    println!(
        "bpp.solution={},{},{},{:.9}",
        bpp_solution.layout_snapshots.len(),
        bpp_solution
            .layout_snapshots
            .values()
            .map(|snapshot| snapshot.placed_items.len())
            .sum::<usize>(),
        bpp_solution.cost(&bpp_instance),
        bpp_solution.density()
    );
    bpp_problem.remove_item(bpp_first_key, bpp_second_item_key);
    println!(
        "bpp.removed={},{},{},{},{},{:.9}",
        bpp_problem.item_demand_qtys[0],
        bpp_problem.bin_stock_qtys[0],
        bpp_problem.layouts.len(),
        bpp_problem.n_placed_items(),
        bpp_problem.bin_cost(),
        bpp_problem.density()
    );
    let bpp_rekeyed = bpp_problem.restore(&bpp_solution);
    println!("bpp.restore_rekey={bpp_rekeyed}");
    println!(
        "bpp.restored={},{},{},{},{},{:.9}",
        bpp_problem.item_demand_qtys[0],
        bpp_problem.bin_stock_qtys[0],
        bpp_problem.layouts.len(),
        bpp_problem.n_placed_items(),
        bpp_problem.bin_cost(),
        bpp_problem.density()
    );

    let mspp_external: ExtMSPInstance = serde_json::from_str(include_str!(
        "../../../Tests/Fixtures/RustReference/mspp_instance.json"
    ))
    .unwrap();
    let mspp_instance = import_mspp_instance(&importer, &mspp_external).unwrap();
    println!(
        "mspp.instance={},{:.9},{:.9},{:.9},{:.9}",
        mspp_instance.total_item_qty(),
        mspp_instance.item_area(),
        mspp_instance.base_strip.max_width,
        mspp_instance.base_strip.fixed_height,
        mspp_instance.base_strip.width
    );
    let mut mspp_problem = MSPProblem::new(mspp_instance);
    let mspp_key = mspp_problem
        .add_layout_from_strip(mspp_problem.instance.base_strip)
        .unwrap();
    println!(
        "mspp.layout={},{:.9},{:.9}",
        mspp_problem.layouts.len(),
        mspp_problem.strips[mspp_key].width,
        mspp_problem.total_strip_width()
    );
    mspp_problem.place_item(MSPPlacement {
        lk: mspp_key,
        item_idx: 0,
        d_transf: DTransformation::new(0.0, (1.0, 1.0)),
    });
    println!(
        "mspp.placed={},{},{:.9},{:.9}",
        mspp_problem.item_demand_qtys[0],
        mspp_problem.n_placed_items(),
        mspp_problem.density(),
        mspp_problem.total_strip_width()
    );
    mspp_problem.change_strip_width(mspp_key, 8.0).unwrap();
    println!(
        "mspp.changed={:.9},{:.9},{:.9}",
        mspp_problem.strips[mspp_key].width,
        mspp_problem.total_strip_width(),
        mspp_problem.density()
    );
    mspp_problem.fit_strip(mspp_key).unwrap();
    let mspp_solution = mspp_problem.save();
    println!(
        "mspp.solution={},{:.9},{:.9}",
        mspp_solution.layout_snapshots.len(),
        mspp_solution.total_strip_width(),
        mspp_solution.density()
    );
    let mspp_placed_key = mspp_problem.layouts[mspp_key]
        .placed_items
        .keys()
        .next()
        .unwrap();
    mspp_problem.remove_item(mspp_key, mspp_placed_key);
    println!(
        "mspp.removed={},{},{:.9},{:.9}",
        mspp_problem.item_demand_qtys[0],
        mspp_problem.n_placed_items(),
        mspp_problem.total_strip_width(),
        mspp_problem.density()
    );
    let mspp_rekeyed = mspp_problem.restore(&mspp_solution);
    println!("mspp.restore_rekey={mspp_rekeyed}");
    println!(
        "mspp.restored={},{},{:.9},{:.9},{:.9}",
        mspp_problem.item_demand_qtys[0],
        mspp_problem.n_placed_items(),
        mspp_problem.strips[mspp_key].width,
        mspp_problem.total_strip_width(),
        mspp_problem.density()
    );
}

fn svg_collision_layout(
    positions: &[(f32, f32)],
    quality_zones: Vec<InferiorQualityZone>,
    min_quality: Option<usize>,
) -> Layout {
    let container = Container::new(
        OriginalShape {
            shape: polygon(&[(0.0, 0.0), (100.0, 0.0), (100.0, 100.0), (0.0, 100.0)]),
            pre_transform: DTransformation::empty(),
            modify_mode: ShapeModifyMode::Deflate,
            modify_config: ShapeModifyConfig::default(),
        },
        quality_zones,
        CDEConfig {
            quadtree_depth: 1,
            cd_threshold: 8,
            item_surrogate_config: SPSurrogateConfig::none(),
        },
    )
    .unwrap();
    let item = Arc::new(
        Item::new(
            0,
            9001,
            OriginalShape {
                shape: polygon(&[(-5.0, -10.0), (5.0, -10.0), (5.0, 10.0), (-5.0, 10.0)]),
                pre_transform: DTransformation::empty(),
                modify_mode: ShapeModifyMode::Inflate,
                modify_config: ShapeModifyConfig::default(),
            },
            RotationRange::None,
            min_quality,
            SPSurrogateConfig::none(),
        )
        .unwrap(),
    );
    let mut layout = Layout::new(container);
    for (x, y) in positions {
        layout.place_item(&item, DTransformation::new(0.0, (*x, *y)));
    }
    layout
}

fn svg_collision_zone(quality: usize, rectangles: &[(f32, f32, f32, f32)]) -> InferiorQualityZone {
    let shapes = rectangles
        .iter()
        .map(|(x_min, y_min, x_max, y_max)| OriginalShape {
            shape: rectangle(*x_min, *y_min, *x_max, *y_max),
            pre_transform: DTransformation::empty(),
            modify_mode: ShapeModifyMode::Inflate,
            modify_config: ShapeModifyConfig::default(),
        })
        .collect();
    InferiorQualityZone::new(quality, shapes).unwrap()
}

fn svg_path_data_for_group(svg: &str, group_id: &str) -> Vec<String> {
    svg_path_elements_for_group(svg, group_id)
        .iter()
        .map(|element| svg_attribute(element, "d"))
        .collect()
}

fn svg_path_elements_for_group(svg: &str, group_id: &str) -> Vec<String> {
    let group_marker = format!("<g id=\"{group_id}\"");
    let group_start = svg
        .find(&group_marker)
        .unwrap_or_else(|| panic!("missing SVG group {group_id}"));
    let group_remainder = &svg[group_start..];
    let group_end = group_remainder
        .find("</g>")
        .unwrap_or_else(|| panic!("unterminated SVG group {group_id}"));
    let group = &group_remainder[..group_end];
    let mut elements = Vec::new();
    let mut cursor = 0;
    while let Some(relative_start) = group[cursor..].find("<path") {
        let path_start = cursor + relative_start;
        let tag_end = group[path_start..]
            .find('>')
            .map(|offset| path_start + offset)
            .unwrap_or_else(|| panic!("unterminated path in SVG group {group_id}"));
        let element_end = if group[path_start..=tag_end].ends_with("/>") {
            tag_end + 1
        } else {
            group[tag_end + 1..]
                .find("</path>")
                .map(|offset| tag_end + 1 + offset + "</path>".len())
                .unwrap_or_else(|| panic!("unterminated path in SVG group {group_id}"))
        };
        elements.push(group[path_start..element_end].to_owned());
        cursor = element_end;
    }
    elements
}

fn svg_circle_elements_for_group(svg: &str, group_id: &str) -> Vec<String> {
    let group_marker = format!("<g id=\"{group_id}\"");
    let group_start = svg
        .find(&group_marker)
        .unwrap_or_else(|| panic!("missing SVG group {group_id}"));
    let group_remainder = &svg[group_start..];
    let group_end = group_remainder
        .find("</g>")
        .unwrap_or_else(|| panic!("unterminated SVG group {group_id}"));
    let group = &group_remainder[..group_end];
    let mut elements = Vec::new();
    let mut cursor = 0;
    while let Some(relative_start) = group[cursor..].find("<circle") {
        let circle_start = cursor + relative_start;
        let tag_end = group[circle_start..]
            .find('>')
            .map(|offset| circle_start + offset)
            .unwrap_or_else(|| panic!("unterminated circle in SVG group {group_id}"));
        elements.push(group[circle_start..=tag_end].to_owned());
        cursor = tag_end + 1;
    }
    elements
}

fn svg_attribute(element: &str, name: &str) -> String {
    let marker = format!(" {name}=\"");
    let value_start = element
        .find(&marker)
        .map(|offset| offset + marker.len())
        .unwrap_or_else(|| panic!("missing SVG attribute {name}"));
    let value_end = element[value_start..]
        .find('"')
        .map(|offset| value_start + offset)
        .unwrap_or_else(|| panic!("unterminated SVG attribute {name}"));
    element[value_start..value_end].to_owned()
}

fn svg_opening_tag_with_attribute(
    svg: &str,
    tag_name: &str,
    attribute: &str,
    expected_value: &str,
) -> String {
    let marker = format!("<{tag_name}");
    let mut cursor = 0;
    while let Some(relative_start) = svg[cursor..].find(&marker) {
        let tag_start = cursor + relative_start;
        let tag_end = svg[tag_start..]
            .find('>')
            .map(|offset| tag_start + offset)
            .unwrap_or_else(|| panic!("unterminated SVG {tag_name} tag"));
        let tag = &svg[tag_start..=tag_end];
        if svg_attribute(tag, attribute) == expected_value {
            return tag.to_owned();
        }
        cursor = tag_end + 1;
    }
    panic!("missing SVG {tag_name} with {attribute}={expected_value}");
}

fn svg_imported_bpp_placed_state(svg: &str) -> String {
    let original_item_paths = svg_path_elements_for_group(svg, "item_1");
    let collision_item_paths = svg_path_elements_for_group(svg, "cd_shape_1");
    let collision_lines = svg_path_elements_for_group(svg, "collision_lines");
    assert_eq!(original_item_paths.len(), 1);
    assert_eq!(collision_item_paths.len(), 1);
    assert_eq!(collision_lines.len(), 1);

    let original_item = &original_item_paths[0];
    let collision_item = &collision_item_paths[0];
    let item_use = svg_opening_tag_with_attribute(svg, "use", "href", "#item_1");
    let cd_use = svg_opening_tag_with_attribute(svg, "use", "href", "#cd_shape_1");
    let mut state = format!(
        "item={},fill={},fill-opacity={},fill-rule={},stroke={},stroke-width={},href={},transform={};cd={},fill={},stroke={},stroke-width={},stroke-opacity={},stroke-dasharray={},stroke-linecap={},stroke-linejoin={},href={},transform={};collisions={}",
        svg_attribute(original_item, "d"),
        svg_attribute(original_item, "fill"),
        svg_attribute(original_item, "fill-opacity"),
        svg_attribute(original_item, "fill-rule"),
        svg_attribute(original_item, "stroke"),
        svg_attribute(original_item, "stroke-width"),
        svg_attribute(&item_use, "href"),
        svg_attribute(&item_use, "transform"),
        svg_attribute(collision_item, "d"),
        svg_attribute(collision_item, "fill"),
        svg_attribute(collision_item, "stroke"),
        svg_attribute(collision_item, "stroke-width"),
        svg_attribute(collision_item, "stroke-opacity"),
        svg_attribute(collision_item, "stroke-dasharray"),
        svg_attribute(collision_item, "stroke-linecap"),
        svg_attribute(collision_item, "stroke-linejoin"),
        svg_attribute(&cd_use, "href"),
        svg_attribute(&cd_use, "transform"),
        collision_lines.len(),
    );
    for line in collision_lines {
        state.push('|');
        state.push_str(&format!(
            "{},stroke={},stroke-opacity={},stroke-width={},stroke-dasharray={},stroke-linecap={},stroke-linejoin={}",
            svg_attribute(&line, "d"),
            svg_attribute(&line, "stroke"),
            svg_attribute(&line, "stroke-opacity"),
            svg_attribute(&line, "stroke-width"),
            svg_attribute(&line, "stroke-dasharray"),
            svg_attribute(&line, "stroke-linecap"),
            svg_attribute(&line, "stroke-linejoin"),
        ));
    }
    state
}

fn svg_imported_bpp_quality_filter_state(layout: &Layout, svg: &str) -> String {
    let collision_group = "<g id=\"collision_lines\"";
    let collision_lines = if svg.contains(collision_group) {
        svg_path_elements_for_group(svg, "collision_lines")
    } else {
        Vec::new()
    };
    let mut state = format!(
        "feasible={};collisions={}",
        layout.is_feasible(),
        collision_lines.len()
    );
    for line in collision_lines {
        state.push('|');
        state.push_str(&format!(
            "{},stroke={},stroke-opacity={},stroke-width={},stroke-dasharray={},stroke-linecap={},stroke-linejoin={}",
            svg_attribute(&line, "d"),
            svg_attribute(&line, "stroke"),
            svg_attribute(&line, "stroke-opacity"),
            svg_attribute(&line, "stroke-width"),
            svg_attribute(&line, "stroke-dasharray"),
            svg_attribute(&line, "stroke-linecap"),
            svg_attribute(&line, "stroke-linejoin"),
        ));
    }
    state
}

fn canonical_svg_collision_line_path(path_data: &str) -> String {
    let path_data = path_data
        .strip_prefix('M')
        .expect("collision path must start with M");
    let (first_endpoint, second_endpoint) = path_data
        .split_once(" L")
        .expect("collision path must contain a line command");
    if second_endpoint < first_endpoint {
        format!("M{second_endpoint} L{first_endpoint}")
    } else {
        format!("M{first_endpoint} L{second_endpoint}")
    }
}

fn svg_imported_bpp_multi_hazard_state(layout: &Layout, svg: &str) -> String {
    let collision_group = "<g id=\"collision_lines\"";
    let collision_lines = if svg.contains(collision_group) {
        svg_path_elements_for_group(svg, "collision_lines")
    } else {
        Vec::new()
    };
    let mut line_states = collision_lines
        .iter()
        .map(|line| {
            format!(
                "{},stroke={},stroke-opacity={},stroke-width={},stroke-dasharray={},stroke-linecap={},stroke-linejoin={}",
                canonical_svg_collision_line_path(&svg_attribute(line, "d")),
                svg_attribute(line, "stroke"),
                svg_attribute(line, "stroke-opacity"),
                svg_attribute(line, "stroke-width"),
                svg_attribute(line, "stroke-dasharray"),
                svg_attribute(line, "stroke-linecap"),
                svg_attribute(line, "stroke-linejoin"),
            )
        })
        .collect::<Vec<_>>();
    line_states.sort();

    let mut state = format!(
        "feasible={};collisions={}",
        layout.is_feasible(),
        line_states.len()
    );
    for line_state in line_states {
        state.push('|');
        state.push_str(&line_state);
    }
    state
}

fn svg_imported_bpp_exterior_state(layout: &Layout, svg: &str) -> String {
    let circles = svg_circle_elements_for_group(svg, "collision_lines");
    let mut state = format!(
        "feasible={};exterior_circles={}",
        layout.is_feasible(),
        circles.len()
    );
    for circle in circles {
        state.push_str(&format!(
            "|cx={},cy={},r={},fill={}",
            svg_attribute(&circle, "cx"),
            svg_attribute(&circle, "cy"),
            svg_attribute(&circle, "r"),
            svg_attribute(&circle, "fill"),
        ));
    }
    state
}

fn svg_quality_title(element: &str) -> String {
    let marker = "<title>quality zone, q: ";
    let value_start = element
        .find(marker)
        .map(|offset| offset + marker.len())
        .unwrap_or_else(|| panic!("missing quality-zone title"));
    let value_end = element[value_start..]
        .find("</title>")
        .map(|offset| value_start + offset)
        .unwrap_or_else(|| panic!("unterminated quality-zone title"));
    element[value_start..value_end].to_owned()
}

fn svg_path_style_state(svg: &str, group_id: &str, attributes: &[&str]) -> Vec<String> {
    svg_path_elements_for_group(svg, group_id)
        .iter()
        .map(|element| {
            let fields = attributes
                .iter()
                .map(|name| format!("{name}={}", svg_attribute(element, name)))
                .collect::<Vec<_>>()
                .join(",");
            if group_id == "quality_zones" {
                format!("q={},{}", svg_quality_title(element), fields)
            } else {
                fields
            }
        })
        .collect()
}

fn svg_imported_bpp_container_path_style_state(svg: &str) -> String {
    let container =
        svg_path_style_state(svg, "container", &["d", "fill", "stroke", "stroke-width"]);
    assert_eq!(container.len(), 1);
    format!("container:{}:{}", container.len(), container.join("|"))
}

fn svg_imported_bpp_quality_zero_hole_path_style_state(svg: &str) -> String {
    let quality_zones = svg_path_style_state(
        svg,
        "quality_zones",
        &[
            "d",
            "fill",
            "fill-opacity",
            "stroke",
            "stroke-width",
            "stroke-opacity",
            "stroke-dasharray",
            "stroke-linecap",
            "stroke-linejoin",
        ],
    );
    let hole_paths = quality_zones
        .into_iter()
        .filter(|state| state.starts_with("q=0,"))
        .collect::<Vec<_>>();
    assert!(!hole_paths.is_empty());
    format!("quality0:{}:{}", hole_paths.len(), hole_paths.join("|"))
}

fn svg_imported_bpp_quality_two_five_path_style_state(svg: &str) -> String {
    let quality_zones = svg_path_style_state(
        svg,
        "quality_zones",
        &[
            "d",
            "fill",
            "fill-opacity",
            "stroke",
            "stroke-width",
            "stroke-opacity",
            "stroke-dasharray",
            "stroke-linecap",
            "stroke-linejoin",
        ],
    );
    let selected_paths = quality_zones
        .into_iter()
        .filter(|state| state.starts_with("q=2,") || state.starts_with("q=5,"))
        .collect::<Vec<_>>();
    assert!(!selected_paths.is_empty());
    let quality_two_count = selected_paths
        .iter()
        .filter(|state| state.starts_with("q=2,"))
        .count();
    let quality_five_count = selected_paths
        .iter()
        .filter(|state| state.starts_with("q=5,"))
        .count();
    format!(
        "quality2={quality_two_count};quality5={quality_five_count};paths:{}",
        selected_paths.join("|")
    )
}

fn svg_imported_bpp_cd_style_state(svg: &str) -> String {
    let container = svg_path_style_state(svg, "container", &["fill", "stroke", "stroke-width"]);
    let quality_zones = svg_path_style_state(
        svg,
        "quality_zones",
        &[
            "fill",
            "fill-opacity",
            "stroke",
            "stroke-width",
            "stroke-opacity",
            "stroke-dasharray",
            "stroke-linecap",
            "stroke-linejoin",
        ],
    );
    format!(
        "container:{}:{};quality_zones:{}:{}",
        container.len(),
        container.join("|"),
        quality_zones.len(),
        quality_zones.join("|")
    )
}

fn run_svg_imported_bpp_parity_oracle() {
    let importer = Importer::new(
        CDEConfig {
            quadtree_depth: 4,
            cd_threshold: 32,
            item_surrogate_config: SPSurrogateConfig::none(),
        },
        None,
        None,
    );
    let external: ExtBPInstance = serde_json::from_str(json_metadata_b8_input(
        "bpp_container_minsep_multi_zone_order",
    ))
    .unwrap();
    let instance = import_bpp_instance(&importer, &external).unwrap();
    assert_eq!(instance.bins.len(), 1);
    let layout = Layout::new(instance.bins[0].container.clone());
    let svg = layout_to_svg(&layout, SvgDrawOptions::default(), "oracle").to_string();
    let svg_root =
        svg_opening_tag_with_attribute(&svg, "svg", "xmlns", "http://www.w3.org/2000/svg");
    let container_path_elements = svg_path_elements_for_group(&svg, "container");
    assert_eq!(container_path_elements.len(), 1);
    let canvas_state = format!(
        "viewBox={};container_stroke_width={}",
        svg_attribute(&svg_root, "viewBox"),
        svg_attribute(&container_path_elements[0], "stroke-width")
    );
    let mut scaled_canvas_options = SvgDrawOptions::default();
    scaled_canvas_options.theme.stroke_width_multiplier = 3.25;
    let scaled_canvas_svg = layout_to_svg(&layout, scaled_canvas_options, "oracle").to_string();
    let scaled_canvas_root = svg_opening_tag_with_attribute(
        &scaled_canvas_svg,
        "svg",
        "xmlns",
        "http://www.w3.org/2000/svg",
    );
    let scaled_canvas_container_paths =
        svg_path_elements_for_group(&scaled_canvas_svg, "container");
    assert_eq!(scaled_canvas_container_paths.len(), 1);
    assert_eq!(
        svg_attribute(&scaled_canvas_root, "viewBox"),
        svg_attribute(&svg_root, "viewBox")
    );
    let scaled_canvas_state = format!(
        "viewBox={};stroke_multiplier=3.25;container_stroke_width={}",
        svg_attribute(&scaled_canvas_root, "viewBox"),
        svg_attribute(&scaled_canvas_container_paths[0], "stroke-width")
    );
    let portrait_external: ExtBPInstance =
        serde_json::from_str(json_metadata_b8_input("bpp_svg_portrait_canvas")).unwrap();
    let portrait_instance = import_bpp_instance(&importer, &portrait_external).unwrap();
    assert_eq!(portrait_instance.bins.len(), 1);
    let portrait_layout = Layout::new(portrait_instance.bins[0].container.clone());
    let portrait_svg =
        layout_to_svg(&portrait_layout, SvgDrawOptions::default(), "oracle").to_string();
    let portrait_root =
        svg_opening_tag_with_attribute(&portrait_svg, "svg", "xmlns", "http://www.w3.org/2000/svg");
    let portrait_container_paths = svg_path_elements_for_group(&portrait_svg, "container");
    assert_eq!(portrait_container_paths.len(), 1);
    let portrait_canvas_state = format!(
        "viewBox={};container_stroke_width={}",
        svg_attribute(&portrait_root, "viewBox"),
        svg_attribute(&portrait_container_paths[0], "stroke-width")
    );
    let mut portrait_scaled_options = SvgDrawOptions::default();
    portrait_scaled_options.theme.stroke_width_multiplier = 3.25;
    let portrait_scaled_svg =
        layout_to_svg(&portrait_layout, portrait_scaled_options, "oracle").to_string();
    let portrait_scaled_root = svg_opening_tag_with_attribute(
        &portrait_scaled_svg,
        "svg",
        "xmlns",
        "http://www.w3.org/2000/svg",
    );
    let portrait_scaled_container_paths =
        svg_path_elements_for_group(&portrait_scaled_svg, "container");
    assert_eq!(portrait_scaled_container_paths.len(), 1);
    assert_eq!(
        svg_attribute(&portrait_scaled_root, "viewBox"),
        svg_attribute(&portrait_root, "viewBox")
    );
    let portrait_scaled_canvas_state = format!(
        "viewBox={};stroke_multiplier=3.25;container_stroke_width={}",
        svg_attribute(&portrait_scaled_root, "viewBox"),
        svg_attribute(&portrait_scaled_container_paths[0], "stroke-width")
    );
    let wide_external: ExtBPInstance =
        serde_json::from_str(json_metadata_b8_input("bpp_svg_wide_canvas")).unwrap();
    let wide_instance = import_bpp_instance(&importer, &wide_external).unwrap();
    assert_eq!(wide_instance.bins.len(), 1);
    let wide_layout = Layout::new(wide_instance.bins[0].container.clone());
    let wide_svg = layout_to_svg(&wide_layout, SvgDrawOptions::default(), "oracle").to_string();
    let wide_root =
        svg_opening_tag_with_attribute(&wide_svg, "svg", "xmlns", "http://www.w3.org/2000/svg");
    let wide_container_paths = svg_path_elements_for_group(&wide_svg, "container");
    assert_eq!(wide_container_paths.len(), 1);
    let wide_canvas_state = format!(
        "viewBox={};container_stroke_width={}",
        svg_attribute(&wide_root, "viewBox"),
        svg_attribute(&wide_container_paths[0], "stroke-width")
    );
    let oblique_external: ExtBPInstance =
        serde_json::from_str(json_metadata_b8_input("bpp_svg_oblique_canvas")).unwrap();
    let oblique_instance = import_bpp_instance(&importer, &oblique_external).unwrap();
    assert_eq!(oblique_instance.bins.len(), 1);
    let oblique_layout = Layout::new(oblique_instance.bins[0].container.clone());
    let oblique_svg =
        layout_to_svg(&oblique_layout, SvgDrawOptions::default(), "oracle").to_string();
    let oblique_root =
        svg_opening_tag_with_attribute(&oblique_svg, "svg", "xmlns", "http://www.w3.org/2000/svg");
    let oblique_container_paths = svg_path_elements_for_group(&oblique_svg, "container");
    assert_eq!(oblique_container_paths.len(), 1);
    let oblique_canvas_state = format!(
        "viewBox={};container_stroke_width={}",
        svg_attribute(&oblique_root, "viewBox"),
        svg_attribute(&oblique_container_paths[0], "stroke-width")
    );
    let oblique_path_state = format!(
        "d={};fill={};stroke={};stroke-width={}",
        svg_attribute(&oblique_container_paths[0], "d"),
        svg_attribute(&oblique_container_paths[0], "fill"),
        svg_attribute(&oblique_container_paths[0], "stroke"),
        svg_attribute(&oblique_container_paths[0], "stroke-width")
    );
    let concave_external: ExtBPInstance =
        serde_json::from_str(json_metadata_b8_input("bpp_svg_concave_canvas")).unwrap();
    let concave_instance = import_bpp_instance(&importer, &concave_external).unwrap();
    assert_eq!(concave_instance.bins.len(), 1);
    let concave_layout = Layout::new(concave_instance.bins[0].container.clone());
    let concave_svg =
        layout_to_svg(&concave_layout, SvgDrawOptions::default(), "oracle").to_string();
    let concave_container_paths = svg_path_elements_for_group(&concave_svg, "container");
    assert_eq!(concave_container_paths.len(), 1);
    let concave_path_state = format!(
        "d={};fill={};stroke={};stroke-width={}",
        svg_attribute(&concave_container_paths[0], "d"),
        svg_attribute(&concave_container_paths[0], "fill"),
        svg_attribute(&concave_container_paths[0], "stroke"),
        svg_attribute(&concave_container_paths[0], "stroke-width")
    );
    let hole_external: ExtBPInstance = serde_json::from_str(json_metadata_b8_input(
        "bpp_container_minsep_hole_quality_zone",
    ))
    .unwrap();
    let hole_instance = import_bpp_instance(&importer, &hole_external).unwrap();
    assert_eq!(hole_instance.bins.len(), 1);
    let hole_layout = Layout::new(hole_instance.bins[0].container.clone());
    let hole_svg = layout_to_svg(&hole_layout, SvgDrawOptions::default(), "oracle").to_string();
    let hole_quality_zero_state = svg_imported_bpp_quality_zero_hole_path_style_state(&hole_svg);
    let hole_quality_two_five_state = svg_imported_bpp_quality_two_five_path_style_state(&hole_svg);
    let hole_container_paths = svg_path_elements_for_group(&hole_svg, "container");
    assert!(!hole_container_paths.is_empty());
    let hole_path_state = hole_container_paths
        .iter()
        .map(|path| {
            format!(
                "d={};fill={};stroke={};stroke-width={}",
                svg_attribute(path, "d"),
                svg_attribute(path, "fill"),
                svg_attribute(path, "stroke"),
                svg_attribute(path, "stroke-width")
            )
        })
        .collect::<Vec<_>>()
        .join("|");
    let container_paths = svg_path_data_for_group(&svg, "container");
    let quality_zone_paths = svg_path_data_for_group(&svg, "quality_zones");
    let state = format!(
        "container:{}:{};quality_zones:{}:{}",
        container_paths.len(),
        container_paths.join("|"),
        quality_zone_paths.len(),
        quality_zone_paths.join("|")
    );
    let mut hole_cd_options = SvgDrawOptions::default();
    hole_cd_options.draw_cd_shapes = true;
    let hole_cd_svg = layout_to_svg(&hole_layout, hole_cd_options, "oracle").to_string();
    let hole_cd_path_state = svg_imported_bpp_container_path_style_state(&hole_cd_svg);
    let hole_cd_quality_zero_state =
        svg_imported_bpp_quality_zero_hole_path_style_state(&hole_cd_svg);
    let hole_cd_quality_two_five_state =
        svg_imported_bpp_quality_two_five_path_style_state(&hole_cd_svg);
    let cd_svg = layout_to_svg(
        &layout,
        SvgDrawOptions {
            draw_cd_shapes: true,
            ..SvgDrawOptions::default()
        },
        "oracle",
    )
    .to_string();
    let cd_container_paths = svg_path_data_for_group(&cd_svg, "container");
    let cd_quality_zone_paths = svg_path_data_for_group(&cd_svg, "quality_zones");
    let cd_state = format!(
        "container:{}:{};quality_zones:{}:{}",
        cd_container_paths.len(),
        cd_container_paths.join("|"),
        cd_quality_zone_paths.len(),
        cd_quality_zone_paths.join("|")
    );
    let cd_style_state = svg_imported_bpp_cd_style_state(&cd_svg);
    let mut placed_layout = Layout::new(instance.bins[0].container.clone());
    placed_layout.place_item(&instance.items[0].0, DTransformation::new(0.0, (5.5, 5.0)));
    assert_eq!(placed_layout.placed_items.len(), 1);
    let placed_svg = layout_to_svg(&placed_layout, SvgDrawOptions::default(), "oracle").to_string();
    let placed_cd_svg = layout_to_svg(
        &placed_layout,
        SvgDrawOptions {
            draw_cd_shapes: true,
            ..SvgDrawOptions::default()
        },
        "oracle",
    )
    .to_string();
    let placed_state = format!(
        "original{{{}}};cd{{{}}}",
        svg_imported_bpp_placed_state(&placed_svg),
        svg_imported_bpp_placed_state(&placed_cd_svg),
    );
    let profile = if cfg!(debug_assertions) {
        "debug"
    } else {
        "release"
    };
    println!("{profile}.svg.bpp_container_minsep_multi_zone_order_canvas_state={canvas_state}");
    println!(
        "{profile}.svg.bpp_container_minsep_multi_zone_order_scaled_canvas_state={scaled_canvas_state}"
    );
    println!("{profile}.svg.bpp_svg_portrait_canvas_state={portrait_canvas_state}");
    println!("{profile}.svg.bpp_svg_portrait_canvas_scaled_state={portrait_scaled_canvas_state}");
    println!("{profile}.svg.bpp_svg_wide_canvas_state={wide_canvas_state}");
    println!("{profile}.svg.bpp_svg_oblique_canvas_state={oblique_canvas_state}");
    println!("{profile}.svg.bpp_svg_oblique_canvas_path_state={oblique_path_state}");
    println!("{profile}.svg.bpp_svg_concave_canvas_path_state={concave_path_state}");
    println!("{profile}.svg.bpp_svg_hole_canvas_path_state={hole_path_state}");
    println!("{profile}.svg.bpp_svg_hole_canvas_quality_zero_state={hole_quality_zero_state}");
    println!("{profile}.svg.bpp_svg_hole_canvas_cd_path_state={hole_cd_path_state}");
    println!(
        "{profile}.svg.bpp_svg_hole_canvas_cd_quality_zero_state={hole_cd_quality_zero_state}"
    );
    println!("{profile}.svg.bpp_svg_hole_canvas_quality_2_5_state={hole_quality_two_five_state}");
    println!(
        "{profile}.svg.bpp_svg_hole_canvas_cd_quality_2_5_state={hole_cd_quality_two_five_state}"
    );
    println!("{profile}.svg.bpp_container_minsep_multi_zone_order_path_state={state}");
    println!("svg.bpp_container_minsep_multi_zone_order_cd_path_state={cd_state}");
    println!("svg.bpp_container_minsep_multi_zone_order_cd_style_state={cd_style_state}");
    println!("svg.bpp_container_minsep_multi_zone_order_placed_state={placed_state}");

    let min_quality_external: ExtBPInstance =
        serde_json::from_str(json_metadata_b8_input("bpp_svg_quality_minimum_3")).unwrap();
    let min_quality_instance = import_bpp_instance(&importer, &min_quality_external).unwrap();
    assert_eq!(min_quality_instance.bins.len(), 1);
    let min_quality_item = &min_quality_instance.items[0].0;
    assert_eq!(min_quality_item.min_quality, Some(3));

    let mut q2_min3_layout = Layout::new(min_quality_instance.bins[0].container.clone());
    q2_min3_layout.place_item(min_quality_item, DTransformation::new(0.0, (25.0, 5.25)));
    let q2_min3_svg =
        layout_to_svg(&q2_min3_layout, SvgDrawOptions::default(), "oracle").to_string();

    let mut q5_unfiltered_layout = Layout::new(instance.bins[0].container.clone());
    q5_unfiltered_layout.place_item(
        &instance.items[0].0,
        DTransformation::new(0.0, (5.0, 13.25)),
    );
    let q5_unfiltered_svg =
        layout_to_svg(&q5_unfiltered_layout, SvgDrawOptions::default(), "oracle").to_string();

    let mut q5_min3_layout = Layout::new(min_quality_instance.bins[0].container.clone());
    q5_min3_layout.place_item(min_quality_item, DTransformation::new(0.0, (5.0, 13.25)));
    let q5_min3_svg =
        layout_to_svg(&q5_min3_layout, SvgDrawOptions::default(), "oracle").to_string();

    let quality_filter_state = format!(
        "q2_min3{{{}}};q5_unfiltered{{{}}};q5_min3{{{}}}",
        svg_imported_bpp_quality_filter_state(&q2_min3_layout, &q2_min3_svg),
        svg_imported_bpp_quality_filter_state(&q5_unfiltered_layout, &q5_unfiltered_svg),
        svg_imported_bpp_quality_filter_state(&q5_min3_layout, &q5_min3_svg),
    );
    println!("{profile}.svg.bpp_svg_quality_zone_filter_state={quality_filter_state}");

    let mut exterior_layout = Layout::new(min_quality_instance.bins[0].container.clone());
    exterior_layout.place_item(min_quality_item, DTransformation::new(0.0, (0.5, 15.0)));
    assert!(!exterior_layout.is_feasible());
    let exterior_svg =
        layout_to_svg(&exterior_layout, SvgDrawOptions::default(), "oracle").to_string();
    let exterior_state = svg_imported_bpp_exterior_state(&exterior_layout, &exterior_svg);
    println!("{profile}.svg.bpp_svg_exterior_collision_state={exterior_state}");

    let multi_hazard_external: ExtBPInstance =
        serde_json::from_str(json_metadata_b8_input("bpp_svg_multi_hazard_min_quality_3")).unwrap();
    let multi_hazard_instance = import_bpp_instance(&importer, &multi_hazard_external).unwrap();
    assert_eq!(multi_hazard_instance.bins.len(), 1);
    let multi_hazard_item = &multi_hazard_instance.items[0].0;
    assert_eq!(multi_hazard_item.min_quality, Some(3));
    let mut multi_hazard_layout = Layout::new(multi_hazard_instance.bins[0].container.clone());
    multi_hazard_layout.place_item(multi_hazard_item, DTransformation::new(0.0, (12.5, 9.75)));
    assert!(!multi_hazard_layout.is_feasible());
    let multi_hazard_svg =
        layout_to_svg(&multi_hazard_layout, SvgDrawOptions::default(), "oracle").to_string();
    let multi_hazard_container_paths = svg_path_elements_for_group(&multi_hazard_svg, "container");
    assert!(!multi_hazard_container_paths.is_empty());
    let multi_hazard_container_path_state = multi_hazard_container_paths
        .iter()
        .map(|path| {
            format!(
                "d={};fill={};stroke={};stroke-width={}",
                svg_attribute(path, "d"),
                svg_attribute(path, "fill"),
                svg_attribute(path, "stroke"),
                svg_attribute(path, "stroke-width")
            )
        })
        .collect::<Vec<_>>()
        .join("|");
    let multi_hazard_state =
        svg_imported_bpp_multi_hazard_state(&multi_hazard_layout, &multi_hazard_svg);
    let multi_hazard_quality_zero_state =
        svg_imported_bpp_quality_zero_hole_path_style_state(&multi_hazard_svg);
    let multi_hazard_quality_two_five_state =
        svg_imported_bpp_quality_two_five_path_style_state(&multi_hazard_svg);
    assert!(multi_hazard_state.starts_with("feasible=false;collisions=4"));
    assert!(multi_hazard_state.contains("|M12.5,9.75 L13,5,"));
    assert!(multi_hazard_state.contains("|M12.5,9.75 L20,5.5,"));
    assert!(multi_hazard_state.contains("|M12.5,9.75 L5,5,"));
    assert!(multi_hazard_state.contains("|M12.5,9.75 L9,5,"));
    assert!(!multi_hazard_state.contains("5.5,13.5"));
    println!("{profile}.svg.bpp_svg_multi_hazard_collision_state={multi_hazard_state}");
    println!(
        "{profile}.svg.bpp_svg_multi_hole_container_path_state={multi_hazard_container_path_state}"
    );
    println!(
        "{profile}.svg.bpp_svg_multi_hole_container_quality_zero_state={multi_hazard_quality_zero_state}"
    );
    println!(
        "{profile}.svg.bpp_svg_multi_hole_container_quality_2_5_state={multi_hazard_quality_two_five_state}"
    );

    let multi_hazard_cd_svg = layout_to_svg(
        &multi_hazard_layout,
        SvgDrawOptions {
            draw_cd_shapes: true,
            ..SvgDrawOptions::default()
        },
        "oracle",
    )
    .to_string();
    let multi_hazard_cd_state =
        svg_imported_bpp_multi_hazard_state(&multi_hazard_layout, &multi_hazard_cd_svg);
    let multi_hazard_cd_container_path_state =
        svg_imported_bpp_container_path_style_state(&multi_hazard_cd_svg);
    let multi_hazard_cd_quality_zero_state =
        svg_imported_bpp_quality_zero_hole_path_style_state(&multi_hazard_cd_svg);
    let multi_hazard_cd_quality_two_five_state =
        svg_imported_bpp_quality_two_five_path_style_state(&multi_hazard_cd_svg);
    assert!(multi_hazard_cd_state.starts_with("feasible=false;collisions=4"));
    assert_eq!(multi_hazard_cd_state, multi_hazard_state);
    println!("{profile}.svg.bpp_svg_multi_hazard_cd_collision_state={multi_hazard_cd_state}");
    println!(
        "{profile}.svg.bpp_svg_multi_hole_container_cd_path_state={multi_hazard_cd_container_path_state}"
    );
    println!(
        "{profile}.svg.bpp_svg_multi_hole_container_cd_quality_zero_state={multi_hazard_cd_quality_zero_state}"
    );
    println!(
        "{profile}.svg.bpp_svg_multi_hole_container_cd_quality_2_5_state={multi_hazard_cd_quality_two_five_state}"
    );

    let multi_hazard_disabled_svg = layout_to_svg(
        &multi_hazard_layout,
        SvgDrawOptions {
            highlight_collisions: false,
            ..SvgDrawOptions::default()
        },
        "oracle",
    )
    .to_string();
    let multi_hazard_disabled_state =
        svg_imported_bpp_multi_hazard_state(&multi_hazard_layout, &multi_hazard_disabled_svg);
    assert!(!multi_hazard_layout.is_feasible());
    assert!(!multi_hazard_disabled_svg.contains("id=\"collision_lines\""));
    assert_eq!(multi_hazard_disabled_state, "feasible=false;collisions=0");
    println!(
        "{profile}.svg.bpp_svg_multi_hazard_disabled_collision_state={multi_hazard_disabled_state}"
    );

    let multi_hazard_original_item_paths = svg_path_elements_for_group(&multi_hazard_svg, "item_1");
    let multi_hazard_cd_item_paths = svg_path_elements_for_group(&multi_hazard_cd_svg, "item_1");
    assert_eq!(multi_hazard_original_item_paths.len(), 1);
    assert_eq!(multi_hazard_cd_item_paths.len(), 1);
    let multi_hazard_original_item_path = svg_attribute(&multi_hazard_original_item_paths[0], "d");
    let multi_hazard_cd_item_path = svg_attribute(&multi_hazard_cd_item_paths[0], "d");
    assert_ne!(multi_hazard_original_item_path, multi_hazard_cd_item_path);

    let multi_hazard_cd_disabled_svg = layout_to_svg(
        &multi_hazard_layout,
        SvgDrawOptions {
            draw_cd_shapes: true,
            highlight_collisions: false,
            ..SvgDrawOptions::default()
        },
        "oracle",
    )
    .to_string();
    let multi_hazard_cd_disabled_collisions =
        svg_imported_bpp_multi_hazard_state(&multi_hazard_layout, &multi_hazard_cd_disabled_svg);
    assert!(!multi_hazard_layout.is_feasible());
    assert!(!multi_hazard_cd_disabled_svg.contains("id=\"collision_lines\""));
    let multi_hazard_cd_disabled_item_paths =
        svg_path_elements_for_group(&multi_hazard_cd_disabled_svg, "item_1");
    assert_eq!(multi_hazard_cd_disabled_item_paths.len(), 1);
    assert_eq!(
        svg_attribute(&multi_hazard_cd_disabled_item_paths[0], "d"),
        multi_hazard_cd_item_path
    );
    let multi_hazard_cd_disabled_state = format!(
        "{};cd_item={}",
        multi_hazard_cd_disabled_collisions,
        svg_attribute(&multi_hazard_cd_disabled_item_paths[0], "d")
    );
    assert!(multi_hazard_cd_disabled_state.starts_with("feasible=false;collisions=0;cd_item="));
    println!(
        "{profile}.svg.bpp_svg_multi_hazard_cd_disabled_collision_state={multi_hazard_cd_disabled_state}"
    );
}

fn run_svg_collision_parity_oracle(output_dir: &str) {
    let layout = svg_collision_layout(&[(45.0, 50.0), (53.0, 50.0)], vec![], None);
    let multi_collision_layout =
        svg_collision_layout(&[(45.0, 50.0), (49.0, 50.0), (53.0, 50.0)], vec![], None);
    let exterior_layout = svg_collision_layout(&[(1.0, 50.0)], vec![], None);
    let hole_zone = svg_collision_zone(0, &[(2.0, 2.0, 4.0, 4.0)]);
    let hole_layout = svg_collision_layout(&[(5.0, 5.0)], vec![hole_zone], None);
    let inferior_zone = svg_collision_zone(1, &[(60.0, 60.0, 80.0, 80.0)]);
    let inferior_quality_layout =
        svg_collision_layout(&[(72.0, 70.0)], vec![inferior_zone], Some(2));
    let multiple_quality_zones = vec![
        svg_collision_zone(1, &[(45.0, 45.0, 47.0, 47.0), (48.0, 45.0, 50.0, 47.0)]),
        svg_collision_zone(2, &[(53.0, 52.0, 55.0, 54.0)]),
        svg_collision_zone(3, &[(45.0, 52.0, 47.0, 54.0)]),
    ];
    let multiple_quality_layout =
        svg_collision_layout(&[(50.0, 50.0)], multiple_quality_zones, Some(3));
    let disabled_options = SvgDrawOptions {
        highlight_collisions: false,
        ..SvgDrawOptions::default()
    };
    let boundary_quality_zones = vec![
        svg_collision_zone(0, &[(44.0, 48.0, 48.0, 52.0)]),
        svg_collision_zone(1, &[(47.0, 48.0, 51.0, 52.0)]),
        svg_collision_zone(2, &[(50.0, 48.0, 54.0, 52.0)]),
        svg_collision_zone(3, &[(48.0, 49.0, 52.0, 53.0)]),
    ];
    let quality_boundary_svgs: Vec<_> = (0..=4)
        .map(|min_quality| {
            let layout = svg_collision_layout(
                &[(50.0, 50.0)],
                boundary_quality_zones.clone(),
                Some(min_quality),
            );
            (
                min_quality,
                layout_to_svg(&layout, SvgDrawOptions::default(), "oracle").to_string(),
                layout_to_svg(&layout, disabled_options, "oracle").to_string(),
            )
        })
        .collect();
    let maximum_quality_zones = vec![
        InferiorQualityZone::new(7, vec![]).unwrap(),
        svg_collision_zone(8, &[(44.0, 44.0, 48.0, 48.0), (47.0, 44.0, 51.0, 48.0)]),
        svg_collision_zone(9, &[(50.0, 48.0, 54.0, 52.0)]),
    ];
    let maximum_quality_layout =
        svg_collision_layout(&[(50.0, 50.0)], maximum_quality_zones, Some(9));
    let concave_quality_zone = InferiorQualityZone::new(
        8,
        vec![OriginalShape {
            shape: polygon(&[
                (40.0, 40.0),
                (60.0, 40.0),
                (60.0, 59.0),
                (59.0, 59.0),
                (59.0, 60.0),
                (40.0, 60.0),
            ]),
            pre_transform: DTransformation::empty(),
            modify_mode: ShapeModifyMode::Inflate,
            modify_config: ShapeModifyConfig::default(),
        }],
    )
    .unwrap();
    let concave_quality_layout =
        svg_collision_layout(&[(58.0, 50.0)], vec![concave_quality_zone], Some(9));
    let slanted_concave_quality_zone = InferiorQualityZone::new(
        8,
        vec![OriginalShape {
            shape: polygon(&[
                (40.0, 40.0),
                (60.0, 40.0),
                (60.0, 58.0),
                (58.0, 58.0),
                (59.0, 60.0),
                (40.0, 60.0),
            ]),
            pre_transform: DTransformation::empty(),
            modify_mode: ShapeModifyMode::Inflate,
            modify_config: ShapeModifyConfig::default(),
        }],
    )
    .unwrap();
    let slanted_concave_quality_layout =
        svg_collision_layout(&[(58.0, 50.0)], vec![slanted_concave_quality_zone], Some(9));
    let indexed_quality_zone = InferiorQualityZone::new(
        8,
        vec![
            OriginalShape {
                shape: rectangle(44.0, 44.0, 48.0, 48.0),
                pre_transform: DTransformation::empty(),
                modify_mode: ShapeModifyMode::Inflate,
                modify_config: ShapeModifyConfig::default(),
            },
            OriginalShape {
                shape: polygon(&[
                    (50.0, 40.0),
                    (70.0, 40.0),
                    (70.0, 58.0),
                    (68.0, 58.0),
                    (69.0, 60.0),
                    (50.0, 60.0),
                ]),
                pre_transform: DTransformation::empty(),
                modify_mode: ShapeModifyMode::Inflate,
                modify_config: ShapeModifyConfig::default(),
            },
        ],
    )
    .unwrap();
    let indexed_quality_layout =
        svg_collision_layout(&[(50.0, 50.0)], vec![indexed_quality_zone], Some(9));
    let offset_indexed_quality_zone = InferiorQualityZone::new(
        8,
        vec![
            OriginalShape {
                shape: rectangle(5.0, 5.0, 10.0, 10.0),
                pre_transform: DTransformation::empty(),
                modify_mode: ShapeModifyMode::Inflate,
                modify_config: ShapeModifyConfig::default(),
            },
            OriginalShape {
                shape: rectangle(44.0, 44.0, 48.0, 48.0),
                pre_transform: DTransformation::empty(),
                modify_mode: ShapeModifyMode::Inflate,
                modify_config: ShapeModifyConfig::default(),
            },
            OriginalShape {
                shape: polygon(&[
                    (50.0, 40.0),
                    (70.0, 40.0),
                    (70.0, 58.0),
                    (68.0, 58.0),
                    (69.0, 60.0),
                    (50.0, 60.0),
                ]),
                pre_transform: DTransformation::empty(),
                modify_mode: ShapeModifyMode::Inflate,
                modify_config: ShapeModifyConfig::default(),
            },
        ],
    )
    .unwrap();
    let offset_indexed_quality_layout =
        svg_collision_layout(&[(50.0, 50.0)], vec![offset_indexed_quality_zone], Some(9));
    let middle_indexed_quality_zone = InferiorQualityZone::new(
        8,
        vec![
            OriginalShape {
                shape: rectangle(44.0, 44.0, 48.0, 48.0),
                pre_transform: DTransformation::empty(),
                modify_mode: ShapeModifyMode::Inflate,
                modify_config: ShapeModifyConfig::default(),
            },
            OriginalShape {
                shape: rectangle(5.0, 5.0, 10.0, 10.0),
                pre_transform: DTransformation::empty(),
                modify_mode: ShapeModifyMode::Inflate,
                modify_config: ShapeModifyConfig::default(),
            },
            OriginalShape {
                shape: polygon(&[
                    (50.0, 40.0),
                    (70.0, 40.0),
                    (70.0, 58.0),
                    (68.0, 58.0),
                    (69.0, 60.0),
                    (50.0, 60.0),
                ]),
                pre_transform: DTransformation::empty(),
                modify_mode: ShapeModifyMode::Inflate,
                modify_config: ShapeModifyConfig::default(),
            },
        ],
    )
    .unwrap();
    let middle_indexed_quality_layout =
        svg_collision_layout(&[(50.0, 50.0)], vec![middle_indexed_quality_zone], Some(9));
    let sparse_indexed_quality_zone = InferiorQualityZone::new(
        8,
        vec![
            OriginalShape {
                shape: rectangle(5.0, 5.0, 10.0, 10.0),
                pre_transform: DTransformation::empty(),
                modify_mode: ShapeModifyMode::Inflate,
                modify_config: ShapeModifyConfig::default(),
            },
            OriginalShape {
                shape: rectangle(44.0, 44.0, 48.0, 48.0),
                pre_transform: DTransformation::empty(),
                modify_mode: ShapeModifyMode::Inflate,
                modify_config: ShapeModifyConfig::default(),
            },
            OriginalShape {
                shape: rectangle(80.0, 5.0, 85.0, 10.0),
                pre_transform: DTransformation::empty(),
                modify_mode: ShapeModifyMode::Inflate,
                modify_config: ShapeModifyConfig::default(),
            },
            OriginalShape {
                shape: polygon(&[
                    (50.0, 40.0),
                    (70.0, 40.0),
                    (70.0, 58.0),
                    (68.0, 58.0),
                    (69.0, 60.0),
                    (50.0, 60.0),
                ]),
                pre_transform: DTransformation::empty(),
                modify_mode: ShapeModifyMode::Inflate,
                modify_config: ShapeModifyConfig::default(),
            },
            OriginalShape {
                shape: rectangle(5.0, 80.0, 10.0, 85.0),
                pre_transform: DTransformation::empty(),
                modify_mode: ShapeModifyMode::Inflate,
                modify_config: ShapeModifyConfig::default(),
            },
        ],
    )
    .unwrap();
    let sparse_indexed_quality_layout =
        svg_collision_layout(&[(50.0, 50.0)], vec![sparse_indexed_quality_zone], Some(9));
    let enabled_svg = layout_to_svg(&layout, SvgDrawOptions::default(), "oracle").to_string();
    let disabled_svg = layout_to_svg(&layout, disabled_options, "oracle").to_string();
    let multi_collision_enabled_svg =
        layout_to_svg(&multi_collision_layout, SvgDrawOptions::default(), "oracle").to_string();
    let multi_collision_disabled_svg =
        layout_to_svg(&multi_collision_layout, disabled_options, "oracle").to_string();
    let exterior_enabled_svg =
        layout_to_svg(&exterior_layout, SvgDrawOptions::default(), "oracle").to_string();
    let exterior_disabled_svg =
        layout_to_svg(&exterior_layout, disabled_options, "oracle").to_string();
    let hole_enabled_svg =
        layout_to_svg(&hole_layout, SvgDrawOptions::default(), "oracle").to_string();
    let hole_disabled_svg = layout_to_svg(&hole_layout, disabled_options, "oracle").to_string();
    let inferior_quality_enabled_svg = layout_to_svg(
        &inferior_quality_layout,
        SvgDrawOptions::default(),
        "oracle",
    )
    .to_string();
    let inferior_quality_disabled_svg =
        layout_to_svg(&inferior_quality_layout, disabled_options, "oracle").to_string();
    let multiple_quality_enabled_svg = layout_to_svg(
        &multiple_quality_layout,
        SvgDrawOptions::default(),
        "oracle",
    )
    .to_string();
    let multiple_quality_disabled_svg =
        layout_to_svg(&multiple_quality_layout, disabled_options, "oracle").to_string();
    let output_dir = std::path::Path::new(output_dir);
    std::fs::create_dir_all(output_dir).unwrap();
    std::fs::write(
        output_dir.join("RustCollisionHighlightTest.svg"),
        enabled_svg,
    )
    .unwrap();
    std::fs::write(
        output_dir.join("RustCollisionHighlightDisabledTest.svg"),
        disabled_svg,
    )
    .unwrap();
    std::fs::write(
        output_dir.join("RustMultiCollisionHighlightTest.svg"),
        multi_collision_enabled_svg,
    )
    .unwrap();
    std::fs::write(
        output_dir.join("RustMultiCollisionHighlightDisabledTest.svg"),
        multi_collision_disabled_svg,
    )
    .unwrap();
    std::fs::write(
        output_dir.join("RustExteriorHighlightTest.svg"),
        exterior_enabled_svg,
    )
    .unwrap();
    std::fs::write(
        output_dir.join("RustExteriorHighlightDisabledTest.svg"),
        exterior_disabled_svg,
    )
    .unwrap();
    std::fs::write(
        output_dir.join("RustHoleHighlightTest.svg"),
        hole_enabled_svg,
    )
    .unwrap();
    std::fs::write(
        output_dir.join("RustHoleHighlightDisabledTest.svg"),
        hole_disabled_svg,
    )
    .unwrap();
    std::fs::write(
        output_dir.join("RustInferiorQualityZoneHighlightTest.svg"),
        inferior_quality_enabled_svg,
    )
    .unwrap();
    std::fs::write(
        output_dir.join("RustInferiorQualityZoneHighlightDisabledTest.svg"),
        inferior_quality_disabled_svg,
    )
    .unwrap();
    std::fs::write(
        output_dir.join("RustMultipleQualityZonesHighlightTest.svg"),
        multiple_quality_enabled_svg,
    )
    .unwrap();
    std::fs::write(
        output_dir.join("RustMultipleQualityZonesHighlightDisabledTest.svg"),
        multiple_quality_disabled_svg,
    )
    .unwrap();
    for (min_quality, enabled_svg, disabled_svg) in quality_boundary_svgs {
        std::fs::write(
            output_dir.join(format!("RustQualityBoundaryMin{min_quality}Enabled.svg")),
            enabled_svg,
        )
        .unwrap();
        std::fs::write(
            output_dir.join(format!("RustQualityBoundaryMin{min_quality}Disabled.svg")),
            disabled_svg,
        )
        .unwrap();
    }
    std::fs::write(
        output_dir.join("RustQualityBoundaryMax9Enabled.svg"),
        layout_to_svg(&maximum_quality_layout, SvgDrawOptions::default(), "oracle").to_string(),
    )
    .unwrap();
    std::fs::write(
        output_dir.join("RustQualityBoundaryMax9Disabled.svg"),
        layout_to_svg(&maximum_quality_layout, disabled_options, "oracle").to_string(),
    )
    .unwrap();
    std::fs::write(
        output_dir.join("RustConcaveQualityZoneCollisionEnabled.svg"),
        layout_to_svg(&concave_quality_layout, SvgDrawOptions::default(), "oracle").to_string(),
    )
    .unwrap();
    std::fs::write(
        output_dir.join("RustConcaveQualityZoneCollisionDisabled.svg"),
        layout_to_svg(&concave_quality_layout, disabled_options, "oracle").to_string(),
    )
    .unwrap();
    std::fs::write(
        output_dir.join("RustSlantedConcaveQualityZoneCollisionEnabled.svg"),
        layout_to_svg(
            &slanted_concave_quality_layout,
            SvgDrawOptions::default(),
            "oracle",
        )
        .to_string(),
    )
    .unwrap();
    std::fs::write(
        output_dir.join("RustSlantedConcaveQualityZoneCollisionDisabled.svg"),
        layout_to_svg(&slanted_concave_quality_layout, disabled_options, "oracle").to_string(),
    )
    .unwrap();
    std::fs::write(
        output_dir.join("RustIndexedQualityZoneShapesEnabled.svg"),
        layout_to_svg(&indexed_quality_layout, SvgDrawOptions::default(), "oracle").to_string(),
    )
    .unwrap();
    std::fs::write(
        output_dir.join("RustIndexedQualityZoneShapesDisabled.svg"),
        layout_to_svg(&indexed_quality_layout, disabled_options, "oracle").to_string(),
    )
    .unwrap();
    std::fs::write(
        output_dir.join("RustOffsetIndexedQualityZoneShapesEnabled.svg"),
        layout_to_svg(
            &offset_indexed_quality_layout,
            SvgDrawOptions::default(),
            "oracle",
        )
        .to_string(),
    )
    .unwrap();
    std::fs::write(
        output_dir.join("RustOffsetIndexedQualityZoneShapesDisabled.svg"),
        layout_to_svg(&offset_indexed_quality_layout, disabled_options, "oracle").to_string(),
    )
    .unwrap();
    std::fs::write(
        output_dir.join("RustMiddleIndexedQualityZoneShapesEnabled.svg"),
        layout_to_svg(
            &middle_indexed_quality_layout,
            SvgDrawOptions::default(),
            "oracle",
        )
        .to_string(),
    )
    .unwrap();
    std::fs::write(
        output_dir.join("RustMiddleIndexedQualityZoneShapesDisabled.svg"),
        layout_to_svg(&middle_indexed_quality_layout, disabled_options, "oracle").to_string(),
    )
    .unwrap();
    std::fs::write(
        output_dir.join("RustSparseIndexedQualityZoneShapesEnabled.svg"),
        layout_to_svg(
            &sparse_indexed_quality_layout,
            SvgDrawOptions::default(),
            "oracle",
        )
        .to_string(),
    )
    .unwrap();
    std::fs::write(
        output_dir.join("RustSparseIndexedQualityZoneShapesDisabled.svg"),
        layout_to_svg(&sparse_indexed_quality_layout, disabled_options, "oracle").to_string(),
    )
    .unwrap();
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args
        .iter()
        .any(|arg| arg == "--svg-imported-bpp-parity-only")
    {
        run_svg_imported_bpp_parity_oracle();
        return;
    }
    if let Some(index) = args
        .iter()
        .position(|arg| arg == "--svg-collision-parity-only")
    {
        let output_dir = args
            .get(index + 1)
            .expect("--svg-collision-parity-only requires an output directory");
        run_svg_collision_parity_oracle(output_dir);
        return;
    }
    if std::env::args().any(|arg| arg == "--boundary-b11-parity-only") {
        run_boundary_b11_oracle();
        return;
    }
    if std::env::args().any(|arg| arg == "--matrix-b11-parity-only") {
        run_matrix_b11_oracle();
        return;
    }
    if std::env::args().any(|arg| arg == "--trajectory-b11-parity-only") {
        run_trajectory_b11_oracle();
        return;
    }
    if std::env::args().any(|arg| arg == "--problem-types-b7-parity-only") {
        run_problem_types_b7_parity_oracle();
        return;
    }
    if std::env::args().any(|arg| arg == "--layout-lifecycle-parity-only") {
        run_layout_lifecycle_parity_oracle();
        return;
    }
    if std::env::args().any(|arg| arg == "--fail-fast-parity-only") {
        run_b3_surrogate_parity_oracle();
        return;
    }
    if std::env::args().any(|arg| arg == "--restore-boundary-parity-only") {
        run_restore_boundary_parity_oracle();
        return;
    }
    if std::env::args().any(|arg| arg == "--model-boundary-parity-only") {
        run_model_boundary_parity_oracle();
        return;
    }
    if std::env::args().any(|arg| arg == "--offset-boundary-parity-only") {
        run_offset_boundary_oracle();
        return;
    }
    if std::env::args().any(|arg| arg == "--shape-modification-b2-parity-only") {
        run_b2_shape_modification_oracle();
        return;
    }
    if std::env::args().any(|arg| arg == "--import-boundary-parity-only") {
        run_import_boundary_oracle();
        return;
    }
    if std::env::args().any(|arg| arg == "--solution-import-b8-parity-only") {
        run_solution_import_b8_oracle();
        return;
    }
    if std::env::args().any(|arg| arg == "--json-metadata-b8-parity-only") {
        run_json_metadata_b8_oracle();
        return;
    }
    if std::env::args().any(|arg| arg == "--zero-separation-parity-only") {
        run_zero_min_item_separation_oracle();
        return;
    }
    if std::env::args().any(|arg| arg == "--collision-parity-only") {
        run_collision_parity_oracle();
        return;
    }
    if std::env::args().any(|arg| arg == "--offset-random-matrix") {
        run_offset_random_matrix_oracle();
        return;
    }
    if std::env::args().any(|arg| arg == "--collision-entire-b4a-parity-only") {
        run_b4a_entire_branch_oracle();
        return;
    }
    if std::env::args().any(|arg| arg == "--geometry-primitives-parity-only") {
        run_b1_geometry_oracle();
        return;
    }

    run_b1_geometry_oracle();

    let tied_x_hull = convex_hull_from_points(vec![
        Point(0.0, 1.0),
        Point(1.0, 0.0),
        Point(0.0, 0.0),
        Point(1.0, 1.0),
    ]);
    println!(
        "geometry.convex_hull_tied_x={}",
        tied_x_hull
            .iter()
            .map(|point| format!("{:.0},{:.0}", point.0, point.1))
            .collect::<Vec<_>>()
            .join(";")
    );
    let singleton_hull = convex_hull_from_points(vec![Point(1.0, 2.0)]);
    println!(
        "geometry.convex_hull_singleton_count={}",
        singleton_hull.len()
    );

    let mut transformed_polygon = polygon(&[(0.0, 0.0), (10.0, 0.0), (10.0, 3.0), (0.0, 3.0)]);
    let original_area_bits = transformed_polygon.area.to_bits();
    let original_diameter_bits = transformed_polygon.diameter.to_bits();
    let large_translation = DTransformation::new(0.37, (1_000_000.0, -2_000_000.0)).compose();
    transformed_polygon.transform(&large_translation);
    println!(
        "geometry.polygon_transform_metrics=area:{original_area_bits:08x}->{:08x};diameter:{original_diameter_bits:08x}->{:08x}",
        transformed_polygon.area.to_bits(),
        transformed_polygon.diameter.to_bits()
    );
    let transformed_centroid = transformed_polygon.centroid();
    println!(
        "geometry.polygon_transform_centroid={:08x},{:08x}",
        transformed_centroid.0.to_bits(),
        transformed_centroid.1.to_bits()
    );

    let signed_zero_polygon = polygon(&[(0.0, 0.0), (2.0, 0.0), (2.0, 1.0), (-0.0, 1.0)]);
    println!(
        "geometry.polygon_signed_zero_bbox={:08x},{:08x}",
        signed_zero_polygon.bbox.x_min.to_bits(),
        signed_zero_polygon.bbox.x_max.to_bits()
    );

    let mut translated_collapse = polygon(&[(0.0, 0.0), (1.0, 0.0), (4.0, 1.0), (0.0, 1.0)]);
    let collapse_area_bits = translated_collapse.area.to_bits();
    translated_collapse.transform(&DTransformation::new(0.0, (16_777_216.0, 0.0)).compose());
    let collapse_centroid = translated_collapse.centroid();
    println!(
        "geometry.polygon_translated_collapse=vertices:{},bbox:{:08x},{:08x};area:{:08x}->{:08x};diameter:{:08x};centroid:{:08x},{:08x}",
        translated_collapse.n_vertices(),
        translated_collapse.bbox.x_min.to_bits(),
        translated_collapse.bbox.x_max.to_bits(),
        collapse_area_bits,
        translated_collapse.area.to_bits(),
        translated_collapse.diameter.to_bits(),
        collapse_centroid.0.to_bits(),
        collapse_centroid.1.to_bits()
    );

    let mut degenerate_transform = polygon(&[(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)]);
    let degenerate_result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        degenerate_transform.transform(&DTransformation::new(0.0, (16_777_216.0, 0.0)).compose());
    }));
    println!(
        "geometry.polygon_degenerate_transform={}",
        if degenerate_result.is_err() {
            "panic"
        } else {
            "ok"
        }
    );

    let near_zero_vertex_square = polygon(&[(0.0, 0.0), (2.0, 0.0), (2.0, 2.0), (0.0, 2.0)]);
    println!(
        "geometry.polygon_fpa_near_zero_contains={}",
        near_zero_vertex_square.collides_with(&Point(1.0, f32::EPSILON * 0.25))
    );
    let tiny_side = f32::EPSILON * 0.5;
    let mut tiny_square = polygon(&[
        (0.0, 0.0),
        (tiny_side, 0.0),
        (tiny_side, tiny_side),
        (0.0, tiny_side),
    ]);
    let tiny_center = tiny_side * 0.5;
    println!(
        "geometry.tiny_square_contains_center={}",
        tiny_square.collides_with(&Point(tiny_center, tiny_center))
    );
    let tiny_surrogate_config = SPSurrogateConfig {
        n_pole_limits: [(2, 0.0); 3],
        ff_pole_area_ratio: 0.0,
        n_ff_piers: 1,
    };
    println!(
        "geometry.tiny_square_surrogate={}",
        tiny_square
            .generate_surrogate(tiny_surrogate_config)
            .is_ok()
    );
    if let Some(surrogate) = &tiny_square.surrogate {
        let first_pier = surrogate.ff_piers().first();
        println!(
            "geometry.tiny_square_pier={}",
            first_pier.map_or_else(
                || "none".to_string(),
                |pier| format!(
                    "{:.18},{:.18},{:.18},{:.18}",
                    pier.start.0, pier.start.1, pier.end.0, pier.end.1
                )
            )
        );
    }

    let mut unused_large_pole_limit_shape =
        polygon(&[(0.0, 0.0), (2.0, 0.0), (2.0, 2.0), (0.0, 2.0)]);
    let unused_large_pole_limit_config = SPSurrogateConfig {
        n_pole_limits: [(1, 0.0), (2000, 0.0), (0, 2.0)],
        ff_pole_area_ratio: 0.0,
        n_ff_piers: 0,
    };
    let unused_large_pole_limit_result =
        unused_large_pole_limit_shape.generate_surrogate(unused_large_pole_limit_config);
    println!(
        "geometry.unused_large_pole_limit={}",
        unused_large_pole_limit_result.is_ok()
    );
    if let Some(surrogate) = &unused_large_pole_limit_shape.surrogate {
        println!(
            "geometry.unused_large_pole_limit_poles={}",
            surrogate.poles.len()
        );
    }

    let mut underflowing_coverage_shape =
        polygon(&[(0.0, 0.0), (0.5, 0.0), (0.5, 0.5), (0.0, 0.5)]);
    let underflowing_coverage_config = SPSurrogateConfig {
        n_pole_limits: [(0, 0.0); 3],
        ff_pole_area_ratio: f32::from_bits(1),
        n_ff_piers: 0,
    };
    let underflowing_coverage_result =
        underflowing_coverage_shape.generate_surrogate(underflowing_coverage_config);
    println!(
        "geometry.underflowing_coverage_target={}",
        underflowing_coverage_result.is_ok()
    );
    if let Some(surrogate) = &underflowing_coverage_shape.surrogate {
        println!(
            "geometry.underflowing_coverage_poles={}",
            surrogate.ff_poles().len()
        );
    }

    let mut pier_lattice_shape = polygon(&[(0.13, 0.27), (4.21, 0.39), (3.72, 3.19), (1.46, 2.87)]);
    let pier_lattice_config = SPSurrogateConfig {
        n_pole_limits: [(1, 0.0); 3],
        ff_pole_area_ratio: 0.0,
        n_ff_piers: 1,
    };
    let pier_lattice_result = pier_lattice_shape.generate_surrogate(pier_lattice_config);
    if let Some(pier) = pier_lattice_shape
        .surrogate
        .as_ref()
        .and_then(|surrogate| surrogate.ff_piers().first())
    {
        println!(
            "geometry.pier_lattice_case=ok:{},{},{},{}",
            pier.start.0, pier.start.1, pier.end.0, pier.end.1
        );
    } else {
        println!(
            "geometry.pier_lattice_case=error:{}",
            pier_lattice_result.is_err()
        );
    }

    let transform_a = DTransformation::new(100_000.123456789, (1.23456789, -7.65432109));
    let transform_b = DTransformation::new(-99_999.87654321, (0.333333333, 0.777777777));
    let followed_by = transform_a.compose().transform(&transform_b.compose());
    let followed_by_decomposed = DTransformation::from(&followed_by);
    let (followed_x, followed_y) = followed_by_decomposed.translation();
    println!(
        "geometry.transform_followed_by={:.9},{:.9},{:.9}",
        followed_by_decomposed.rotation(),
        followed_x,
        followed_y
    );
    let inverse = DTransformation::from(&transform_a.compose().inverse());
    let (inverse_x, inverse_y) = inverse.translation();
    println!(
        "geometry.transform_inverse={:.9},{:.9},{:.9}",
        inverse.rotation(),
        inverse_x,
        inverse_y
    );

    let wide_circle = Circle::try_new(Point(0.0, 0.0), 12_345.678).unwrap();
    println!("geometry.circle_area_f32={:.9}", wide_circle.area());
    println!(
        "circle.f32_input_boundaries=center_overflow_rejected={},radius_overflow_rejected={},negative_zero_radius_accepted={}",
        Circle::try_new(Point(f32::INFINITY, 0.0), 1.0).is_err(),
        Circle::try_new(Point(0.0, 0.0), f32::INFINITY).is_err(),
        Circle::try_new(Point(0.0, 0.0), -0.0).is_ok()
    );
    let boundary_circle = Circle::try_new(Point(16_777_216.0, 0.0), 1.0).unwrap();
    let circle_bbox = boundary_circle.bbox();
    println!(
        "geometry.circle_bbox_f32={:.0},{:.0},{:.0},{:.0}",
        circle_bbox.x_min, circle_bbox.y_min, circle_bbox.x_max, circle_bbox.y_max
    );
    println!(
        "geometry.circle_contains_f32={}",
        boundary_circle.collides_with(&Point(16_777_217.0, 0.0))
    );
    let extreme_bound = Rect::try_new(f32::MAX * 0.5, f32::MAX * 0.5, f32::MAX, f32::MAX).unwrap();
    let extreme_centroid = extreme_bound.centroid();
    println!(
        "geometry.rect_extreme_centroid={:08x},{:08x}",
        extreme_centroid.0.to_bits(),
        extreme_centroid.1.to_bits()
    );
    let extreme_quadrants = extreme_bound.quadrants();
    println!(
        "geometry.rect_extreme_q1={:08x},{:08x},{:08x},{:08x}",
        extreme_quadrants[0].x_min.to_bits(),
        extreme_quadrants[0].y_min.to_bits(),
        extreme_quadrants[0].x_max.to_bits(),
        extreme_quadrants[0].y_max.to_bits()
    );

    println!(
        "edge.nan_start_accepted={},infinite_start_accepted={}",
        Edge::try_new(Point(f32::NAN, 0.0), Point(1.0, 1.0)).is_ok(),
        Edge::try_new(Point(f32::INFINITY, 0.0), Point(1.0, 1.0)).is_ok()
    );
    let nan_bbox_edge = Edge::try_new(Point(f32::NAN, 0.0), Point(1.0, 1.0)).unwrap();
    let nan_bbox = nan_bbox_edge.bbox();
    println!(
        "edge.nan_start_bbox_bits={:08x},{:08x},{:08x},{:08x}",
        nan_bbox.x_min.to_bits(),
        nan_bbox.y_min.to_bits(),
        nan_bbox.x_max.to_bits(),
        nan_bbox.y_max.to_bits()
    );
    println!(
        "edge.f32_collapsed_endpoints_rejected={}",
        Edge::try_new(Point(1.0, 0.0), Point(1.0 + f32::EPSILON * 0.5, 0.0)).is_err()
    );

    println!(
        "rect.infinite_max_accepted={},nan_max_accepted={}",
        Rect::try_new(0.0, 0.0, f32::INFINITY, 1.0).is_ok(),
        Rect::try_new(0.0, 0.0, f32::NAN, 1.0).is_ok()
    );
    println!(
        "rect.f32_degenerate_bounds_rejected={},same_infinite_bounds_rejected={}",
        Rect::try_new(1.0, 0.0, 1.0 + f32::EPSILON * 0.5, 1.0).is_err(),
        Rect::try_new(f32::INFINITY, 0.0, f32::INFINITY, 1.0).is_err()
    );
    let hazard_bounds = Rect::try_new(0.0, 0.0, 2.0, 2.0).unwrap();
    let near_zero_bounds = Rect::try_new(-f32::EPSILON * 0.25, 0.5, 1.0, 1.5).unwrap();
    println!(
        "rect.almost_relation_fpa_near_zero_surrounds={}",
        matches!(
            hazard_bounds.almost_relation_to(near_zero_bounds),
            jagua_rs::geometry::geo_enums::GeoRelation::Surrounding
        )
    );
    let near_bounds_point = Point(-f32::EPSILON * 0.25, 1.0);
    let outside_bounds_point = Point(-f32::EPSILON * 2.0, 1.0);
    println!(
        "rect.almost_contains_points=near:{},outside:{}",
        hazard_bounds.almost_collides_with(&near_bounds_point),
        hazard_bounds.almost_collides_with(&outside_bounds_point)
    );
    let crossing_rect_edge = Edge::try_new(Point(-1.0, 1.0), Point(3.0, 1.0)).unwrap();
    let outside_rect_edge = Edge::try_new(Point(-1.0, 3.0), Point(3.0, 3.0)).unwrap();
    let endpoint_inside_rect_edge = Edge::try_new(Point(1.0, 1.0), Point(3.0, 3.0)).unwrap();
    let boundary_rect_edge = Edge::try_new(Point(-1.0, 0.0), Point(3.0, 0.0)).unwrap();
    println!(
        "rect.edge_collision=through:{},outside:{},endpoint_inside:{},boundary:{}",
        hazard_bounds.collides_with(&crossing_rect_edge),
        hazard_bounds.collides_with(&outside_rect_edge),
        hazard_bounds.collides_with(&endpoint_inside_rect_edge),
        hazard_bounds.collides_with(&boundary_rect_edge)
    );
    let nan_start_rect_edge = Edge::try_new(Point(f32::NAN, 3.0), Point(-1.0, 1.0)).unwrap();
    println!(
        "rect.edge_collision_nan_start_outside={}",
        hazard_bounds.collides_with(&nan_start_rect_edge)
    );
    let unit_circle = Circle::try_new(Point(0.0, 0.0), 1.0).unwrap();
    let tangent_circle = Circle::try_new(Point(2.0, 0.0), 1.0).unwrap();
    let separated_circle = Circle::try_new(Point(2.01, 0.0), 1.0).unwrap();
    let tangent_circle_edge = Edge::try_new(Point(-2.0, 1.0), Point(2.0, 1.0)).unwrap();
    let separated_circle_edge = Edge::try_new(Point(-2.0, 1.01), Point(2.0, 1.01)).unwrap();
    let tangent_circle_rect = Rect::try_new(1.0, 0.0, 2.0, 1.0).unwrap();
    let separated_circle_rect = Rect::try_new(1.01, 0.0, 2.0, 1.0).unwrap();
    println!(
        "circle.collision=circles:{},circles_separated:{},edge:{},edge_separated:{},rect:{},rect_separated:{}",
        unit_circle.collides_with(&tangent_circle),
        unit_circle.collides_with(&separated_circle),
        unit_circle.collides_with(&tangent_circle_edge),
        unit_circle.collides_with(&separated_circle_edge),
        unit_circle.collides_with(&tangent_circle_rect),
        unit_circle.collides_with(&separated_circle_rect)
    );
    let inside_circle_point = Point(0.25, 0.0);
    let outside_circle_point = Point(3.0, 0.0);
    let (inside_point_position, inside_point_separation) =
        unit_circle.separation_distance(&inside_circle_point);
    let (outside_point_position, outside_point_separation) =
        unit_circle.separation_distance(&outside_circle_point);
    let separated_circle = Circle::try_new(Point(3.0, 0.0), 1.0).unwrap();
    let (circle_position, circle_separation) = unit_circle.separation_distance(&separated_circle);
    let overlapping_edge = Edge::try_new(Point(-2.0, 0.25), Point(2.0, 0.25)).unwrap();
    let (edge_position, edge_separation) = unit_circle.separation_distance(&overlapping_edge);
    println!(
        "circle.distance=diameter:{:.9},point_inside:{:.9},point_outside:{:.9},point_outside_sq:{:.9},circle_outside:{:.9},circle_outside_sq:{:.9},edge_inside:{:.9},edge_inside_sq:{:.9}",
        unit_circle.diameter(),
        unit_circle.distance_to(&inside_circle_point),
        unit_circle.distance_to(&outside_circle_point),
        unit_circle.sq_distance_to(&outside_circle_point),
        unit_circle.distance_to(&separated_circle),
        unit_circle.sq_distance_to(&separated_circle),
        unit_circle.distance_to(&overlapping_edge),
        unit_circle.sq_distance_to(&overlapping_edge)
    );
    println!(
        "circle.separation=point_inside:{:?}:{:.9},point_outside:{:?}:{:.9},circle_outside:{:?}:{:.9},edge_inside:{:?}:{:.9}",
        inside_point_position,
        inside_point_separation,
        outside_point_position,
        outside_point_separation,
        circle_position,
        circle_separation,
        edge_position,
        edge_separation
    );
    let (squared_inside_point_position, squared_inside_point_separation) =
        unit_circle.sq_separation_distance(&inside_circle_point);
    let (squared_outside_point_position, squared_outside_point_separation) =
        unit_circle.sq_separation_distance(&outside_circle_point);
    let (squared_circle_position, squared_circle_separation) =
        unit_circle.sq_separation_distance(&separated_circle);
    let (squared_edge_position, squared_edge_separation) =
        unit_circle.sq_separation_distance(&overlapping_edge);
    println!(
        "circle.squared_separation=point_inside:{:?}:{:.9},point_outside:{:?}:{:.9},circle_outside:{:?}:{:.9},edge_inside:{:?}:{:.9}",
        squared_inside_point_position,
        squared_inside_point_separation,
        squared_outside_point_position,
        squared_outside_point_separation,
        squared_circle_position,
        squared_circle_separation,
        squared_edge_position,
        squared_edge_separation
    );
    let bounding_circle = Circle::bounding_circle([
        &unit_circle,
        &Circle::try_new(Point(4.0, 0.0), 2.0).unwrap(),
    ]);
    println!(
        "circle.bounding_circle={:.9},{:.9},{:.9},diameter:{:.9}",
        bounding_circle.center.0,
        bounding_circle.center.1,
        bounding_circle.radius,
        bounding_circle.diameter()
    );
    let mut transformed_circle = Circle::try_new(Point(1.0, 0.0), 1.0).unwrap();
    let circle_transform =
        jagua_rs::geometry::Transformation::from_rotation(std::f32::consts::FRAC_PI_2)
            .translate((10.0, 20.0));
    transformed_circle.transform(&circle_transform);
    println!(
        "circle.transform={:.9},{:.9},radius:{:.9}",
        transformed_circle.center.0, transformed_circle.center.1, transformed_circle.radius
    );
    let rounded_circle = Circle::try_new(Point(16_777_217.0, 0.0), 1.00000006).unwrap();
    println!(
        "circle.f32_storage={:08x},{:08x},{:08x}",
        rounded_circle.center.0.to_bits(),
        rounded_circle.center.1.to_bits(),
        rounded_circle.radius.to_bits()
    );
    let rounded_rect = Rect::try_new(1.00000006, -1.00000006, 2.00000012, 3.00000024).unwrap();
    let rounded_edge = Edge::try_new(
        Point(1.00000006, -1.00000006),
        Point(2.00000012, 3.00000024),
    )
    .unwrap();
    println!(
        "primitive.f32_storage=rect:{:08x},{:08x},{:08x},{:08x},edge:{:08x},{:08x},{:08x},{:08x}",
        rounded_rect.x_min.to_bits(),
        rounded_rect.y_min.to_bits(),
        rounded_rect.x_max.to_bits(),
        rounded_rect.y_max.to_bits(),
        rounded_edge.start.0.to_bits(),
        rounded_edge.start.1.to_bits(),
        rounded_edge.end.0.to_bits(),
        rounded_edge.end.1.to_bits()
    );
    let smallest_subnormal = f32::from_bits(1);
    let subnormal_edge = Edge::try_new(
        Point(smallest_subnormal, 0.0),
        Point(smallest_subnormal, 1.0),
    )
    .unwrap();
    let first_subnormal_circle = Circle::try_new(Point(smallest_subnormal, 0.0), 0.0).unwrap();
    let second_subnormal_circle = Circle::try_new(Point(smallest_subnormal, 1.0), 0.0).unwrap();
    let subnormal_bounding_circle =
        Circle::bounding_circle([&first_subnormal_circle, &second_subnormal_circle]);
    println!(
        "primitive.subnormal_midpoint=edge:{:08x},circle:{:08x}",
        subnormal_edge.centroid().0.to_bits(),
        subnormal_bounding_circle.center.0.to_bits()
    );
    let edge_ops = Edge::try_new(Point(0.0, 0.0), Point(3.0, 4.0)).unwrap();
    let front_extended = edge_ops.extend_at_front(2.0);
    let back_extended = edge_ops.extend_at_back(2.0);
    let scaled_edge = edge_ops.scale(2.0);
    let reversed_edge = edge_ops.reverse();
    println!(
        "edge.operations=minmax:{:.1},{:.1},{:.1},{:.1};length:{:.1};centroid:{:.1},{:.1};front:{:.1},{:.1};back:{:.1},{:.1};scaled:{:.1},{:.1},{:.1},{:.1};reversed:{:.1},{:.1},{:.1},{:.1}",
        edge_ops.x_min(),
        edge_ops.y_min(),
        edge_ops.x_max(),
        edge_ops.y_max(),
        edge_ops.length(),
        edge_ops.centroid().0,
        edge_ops.centroid().1,
        front_extended.start.0,
        front_extended.start.1,
        back_extended.end.0,
        back_extended.end.1,
        scaled_edge.start.0,
        scaled_edge.start.1,
        scaled_edge.end.0,
        scaled_edge.end.1,
        reversed_edge.start.0,
        reversed_edge.start.1,
        reversed_edge.end.0,
        reversed_edge.end.1
    );
    let crossing_edge_a = Edge::try_new(Point(0.0, 0.0), Point(10.0, 10.0)).unwrap();
    let crossing_edge_b = Edge::try_new(Point(0.0, 10.0), Point(10.0, 0.0)).unwrap();
    let endpoint_edge = Edge::try_new(Point(0.0, 0.0), Point(2.0, 0.0)).unwrap();
    let touching_edge = Edge::try_new(Point(2.0, 0.0), Point(2.0, 1.0)).unwrap();
    let collinear_edge = Edge::try_new(Point(1.0, 0.0), Point(3.0, 0.0)).unwrap();
    println!(
        "edge.intersection=proper:{:.1},{:.1};endpoint:{:.1},{:.1};collinear:{}",
        crossing_edge_a.collides_at(&crossing_edge_b).unwrap().0,
        crossing_edge_a.collides_at(&crossing_edge_b).unwrap().1,
        endpoint_edge.collides_at(&touching_edge).unwrap().0,
        endpoint_edge.collides_at(&touching_edge).unwrap().1,
        collinear_edge.collides_at(&endpoint_edge).is_some()
    );
    let mut transformed_edge = edge_ops;
    transformed_edge.transform(&circle_transform);
    println!(
        "edge.transform={:.1},{:.1},{:.1},{:.1}",
        transformed_edge.start.0,
        transformed_edge.start.1,
        transformed_edge.end.0,
        transformed_edge.end.1
    );
    let utility_rect = Rect::try_new(0.0, 0.0, 4.0, 2.0).unwrap();
    let diagonal_rect = Rect::from_diagonal_corners(Point(4.0, 5.0), Point(-2.0, -1.0)).unwrap();
    let square_rect = utility_rect.inflate_to_square();
    let resized_rect = utility_rect.resize_by(-0.5, -0.5).unwrap();
    let scaled_rect = utility_rect.scale(1.5);
    let overlap_rect = Rect::try_new(3.0, 1.0, 5.0, 4.0).unwrap();
    let intersection_rect = Rect::intersection(utility_rect, overlap_rect).unwrap();
    let bounding_rect = Rect::bounding_rect(utility_rect, overlap_rect);
    let inside_rect_point = Point(1.0, 1.0);
    let outside_rect_point = Point(-3.0, 4.0);
    let (inside_rect_position, inside_rect_separation) =
        utility_rect.separation_distance(&inside_rect_point);
    let (outside_rect_position, outside_rect_separation) =
        utility_rect.separation_distance(&outside_rect_point);
    let (inside_rect_sq_position, inside_rect_sq_separation) =
        utility_rect.sq_separation_distance(&inside_rect_point);
    let (outside_rect_sq_position, outside_rect_sq_separation) =
        utility_rect.sq_separation_distance(&outside_rect_point);
    println!(
        "rect.operations=diagonal:{:.1},{:.1},{:.1},{:.1};square:{:.1},{:.1},{:.1},{:.1};resize:{:.1},{:.1},{:.1},{:.1};scale:{:.1},{:.1},{:.1},{:.1};measure:{:.1},{:.1},{:.1},{:.9}",
        diagonal_rect.x_min,
        diagonal_rect.y_min,
        diagonal_rect.x_max,
        diagonal_rect.y_max,
        square_rect.x_min,
        square_rect.y_min,
        square_rect.x_max,
        square_rect.y_max,
        resized_rect.x_min,
        resized_rect.y_min,
        resized_rect.x_max,
        resized_rect.y_max,
        scaled_rect.x_min,
        scaled_rect.y_min,
        scaled_rect.x_max,
        scaled_rect.y_max,
        utility_rect.width(),
        utility_rect.height(),
        utility_rect.area(),
        utility_rect.diameter()
    );
    println!(
        "rect.combination=intersection:{:.1},{:.1},{:.1},{:.1};bounding:{:.1},{:.1},{:.1},{:.1};corners:{:.1},{:.1};{:.1},{:.1};{:.1},{:.1};{:.1},{:.1}",
        intersection_rect.x_min,
        intersection_rect.y_min,
        intersection_rect.x_max,
        intersection_rect.y_max,
        bounding_rect.x_min,
        bounding_rect.y_min,
        bounding_rect.x_max,
        bounding_rect.y_max,
        utility_rect.corners()[0].0,
        utility_rect.corners()[0].1,
        utility_rect.corners()[1].0,
        utility_rect.corners()[1].1,
        utility_rect.corners()[2].0,
        utility_rect.corners()[2].1,
        utility_rect.corners()[3].0,
        utility_rect.corners()[3].1
    );
    let utility_edges = utility_rect.edges();
    println!(
        "rect.edges={:.1},{:.1}>{:.1},{:.1};{:.1},{:.1}>{:.1},{:.1};{:.1},{:.1}>{:.1},{:.1};{:.1},{:.1}>{:.1},{:.1}",
        utility_edges[0].start.0,
        utility_edges[0].start.1,
        utility_edges[0].end.0,
        utility_edges[0].end.1,
        utility_edges[1].start.0,
        utility_edges[1].start.1,
        utility_edges[1].end.0,
        utility_edges[1].end.1,
        utility_edges[2].start.0,
        utility_edges[2].start.1,
        utility_edges[2].end.0,
        utility_edges[2].end.1,
        utility_edges[3].start.0,
        utility_edges[3].start.1,
        utility_edges[3].end.0,
        utility_edges[3].end.1
    );
    println!(
        "rect.distance=inside:{:.1},outside:{:.9},outside_sq:{:.1};separation:inside:{:?}:{:.1},outside:{:?}:{:.9};squared:inside:{:?}:{:.1},outside:{:?}:{:.1};invalid_resize:{}",
        utility_rect.distance_to(&inside_rect_point),
        utility_rect.distance_to(&outside_rect_point),
        utility_rect.sq_distance_to(&outside_rect_point),
        inside_rect_position,
        inside_rect_separation,
        outside_rect_position,
        outside_rect_separation,
        inside_rect_sq_position,
        inside_rect_sq_separation,
        outside_rect_sq_position,
        outside_rect_sq_separation,
        utility_rect.resize_by(-2.0, 0.0).is_none()
    );
    let point_a = Point(1.25, -2.5);
    let point_b = Point(4.5, 2.5);
    let point_rounding_a = Point(16_777_216.0, 0.0);
    let point_rounding_b = Point(16_777_217.0, 0.0);
    println!(
        "point.distance=normal:{:.9},normal_sq:{:.9},f32_rounded:{:.1}",
        point_a.distance_to(&point_b),
        point_a.sq_distance_to(&point_b),
        point_rounding_a.distance_to(&point_rounding_b)
    );
    let distance_polygon = polygon(&[(0.0, 0.0), (4.0, 0.0), (4.0, 2.0), (0.0, 2.0)]);
    let polygon_inside_point = Point(1.0, 1.0);
    let polygon_outside_point = Point(-3.0, 4.0);
    let (polygon_inside_position, polygon_inside_separation) =
        distance_polygon.separation_distance(&polygon_inside_point);
    let (polygon_outside_position, polygon_outside_separation) =
        distance_polygon.separation_distance(&polygon_outside_point);
    let (polygon_inside_sq_position, polygon_inside_sq_separation) =
        distance_polygon.sq_separation_distance(&polygon_inside_point);
    let (polygon_outside_sq_position, polygon_outside_sq_separation) =
        distance_polygon.sq_separation_distance(&polygon_outside_point);
    println!(
        "polygon.distance=inside:{:.1},inside_sq:{:.1},outside:{:.9},outside_sq:{:.1};separation:inside:{:?}:{:.1},outside:{:?}:{:.9};squared:inside:{:?}:{:.1},outside:{:?}:{:.1}",
        distance_polygon.distance_to(&polygon_inside_point),
        distance_polygon.sq_distance_to(&polygon_inside_point),
        distance_polygon.distance_to(&polygon_outside_point),
        distance_polygon.sq_distance_to(&polygon_outside_point),
        polygon_inside_position,
        polygon_inside_separation,
        polygon_outside_position,
        polygon_outside_separation,
        polygon_inside_sq_position,
        polygon_inside_sq_separation,
        polygon_outside_sq_position,
        polygon_outside_sq_separation
    );
    let enclosed_bounds = Rect::try_new(-1.0, -1.0, 3.0, 3.0).unwrap();
    let intersecting_bounds = Rect::try_new(1.5, 1.5, 3.0, 3.0).unwrap();
    let disjoint_bounds = Rect::try_new(3.0, 0.0, 4.0, 1.0).unwrap();
    println!(
        "rect.almost_relation_cases=surrounding:{},enclosed:{},intersecting:{},disjoint:{}",
        matches!(
            hazard_bounds.almost_relation_to(near_zero_bounds),
            jagua_rs::geometry::geo_enums::GeoRelation::Surrounding
        ),
        matches!(
            hazard_bounds.almost_relation_to(enclosed_bounds),
            jagua_rs::geometry::geo_enums::GeoRelation::Enclosed
        ),
        matches!(
            hazard_bounds.almost_relation_to(intersecting_bounds),
            jagua_rs::geometry::geo_enums::GeoRelation::Intersecting
        ),
        matches!(
            hazard_bounds.almost_relation_to(disjoint_bounds),
            jagua_rs::geometry::geo_enums::GeoRelation::Disjoint
        )
    );

    let zero_radius_circle = Circle::try_new(Point(0.0, 0.0), 0.0).unwrap();
    println!(
        "circle.zero_radius_valid={},area={:.0}",
        zero_radius_circle.radius >= 0.0,
        zero_radius_circle.area()
    );

    let fit_width = 3.25f32 * 1.00001f32 + 0.1f32;
    println!("strip.fit_width={fit_width:.12}");

    let square_bounds = Rect::try_new(-16_777_216.0, 0.0, 1.0, 2.0)
        .unwrap()
        .inflate_to_square();
    println!(
        "rect.square_bounds={:.0},{:.0},{:.0},{:.0}",
        square_bounds.x_min, square_bounds.y_min, square_bounds.x_max, square_bounds.y_max
    );

    let density_item_area = [16_777_216.0f32, 1.0, 1.0].iter().copied().sum::<f32>();
    println!("density.f32_items_area={density_item_area:.0}");
    println!(
        "density.f32_ratio={:.1}",
        density_item_area / 33_554_432.0f32
    );

    let empty_bpp = BPProblem::new(BPInstance::new(vec![], vec![]));
    println!(
        "density.empty_bpp_problem_nan={}",
        empty_bpp.density().is_nan()
    );

    let float_id_item_json = r#"{"id":1.0,"orientation":{"rotation":{"mode":"continuous"}},"shape":{"type":"simple_polygon","data":[[0,0],[1,0],[0,1]]}}"#;
    let negative_zero_id_item_json = r#"{"id":-0,"orientation":{"rotation":{"mode":"continuous"}},"shape":{"type":"simple_polygon","data":[[0,0],[1,0],[0,1]]}}"#;
    let float_quality_item_json = r#"{"id":1,"orientation":{"rotation":{"mode":"continuous"}},"shape":{"type":"simple_polygon","data":[[0,0],[1,0],[0,1]]},"min_quality":1.0}"#;
    println!(
        "json.float_id_rejected={}",
        serde_json::from_str::<ExtItem>(float_id_item_json).is_err()
    );
    println!(
        "json.negative_zero_id_accepted={}",
        serde_json::from_str::<ExtItem>(negative_zero_id_item_json).is_ok()
    );
    println!(
        "json.float_min_quality_rejected={}",
        serde_json::from_str::<ExtItem>(float_quality_item_json).is_err()
    );
    let duplicate_unknown_item_json = r#"{"id":1,"dxf":"first.dxf","dxf":"second.dxf","orientation":{"rotation":{"mode":"continuous"}},"shape":{"type":"simple_polygon","data":[[0,0],[1,0],[0,1]]}}"#;
    let duplicate_id_item_json = r#"{"id":1,"id":2,"orientation":{"rotation":{"mode":"continuous"}},"shape":{"type":"simple_polygon","data":[[0,0],[1,0],[0,1]]}}"#;
    let duplicate_rotation_mode_item_json = r#"{"id":1,"orientation":{"rotation":{"mode":"continuous","mode":"continuous"}},"shape":{"type":"simple_polygon","data":[[0,0],[1,0],[0,1]]}}"#;
    println!(
        "json.duplicate_unknown_metadata_accepted={}",
        serde_json::from_str::<ExtItem>(duplicate_unknown_item_json).is_ok()
    );
    println!(
        "json.duplicate_id_rejected={}",
        serde_json::from_str::<ExtItem>(duplicate_id_item_json).is_err()
    );
    println!(
        "json.duplicate_rotation_mode_rejected={}",
        serde_json::from_str::<ExtItem>(duplicate_rotation_mode_item_json).is_err()
    );
    let f32_angle_item_json = r#"{"id":1,"orientation":{"rotation":{"mode":"discrete","angles":[123.456789]}},"shape":{"type":"simple_polygon","data":[[0,0],[1,0],[0,1]]}}"#;
    let parsed_f32_angle_item = serde_json::from_str::<ExtItem>(f32_angle_item_json).unwrap();
    if let ExtRotation::Discrete { angles } = parsed_f32_angle_item.orientation.rotation {
        println!("json.angle_f32_bits={:08x}", angles[0].to_bits());
    }

    let cde_config = CDEConfig {
        quadtree_depth: 1,
        cd_threshold: 8,
        item_surrogate_config: SPSurrogateConfig::none(),
    };
    let empty_quality_zone = InferiorQualityZone::new(1, vec![]).unwrap();
    let empty_quality_zone_container = Container::new(
        OriginalShape {
            shape: polygon(&[(0.0, 0.0), (4.0, 0.0), (4.0, 4.0), (0.0, 4.0)]),
            pre_transform: DTransformation::empty(),
            modify_mode: ShapeModifyMode::Deflate,
            modify_config: ShapeModifyConfig::default(),
        },
        vec![empty_quality_zone],
        cde_config,
    );
    println!(
        "container.empty_quality_zone_accepted={}",
        empty_quality_zone_container.is_ok()
    );
    let min_quality_zone = InferiorQualityZone::new(
        1,
        vec![OriginalShape {
            shape: polygon(&[(0.0, 0.0), (4.0, 0.0), (4.0, 4.0), (0.0, 4.0)]),
            pre_transform: DTransformation::empty(),
            modify_mode: ShapeModifyMode::Inflate,
            modify_config: ShapeModifyConfig::default(),
        }],
    )
    .unwrap();
    let min_quality_container = Container::new(
        OriginalShape {
            shape: polygon(&[(0.0, 0.0), (10.0, 0.0), (10.0, 10.0), (0.0, 10.0)]),
            pre_transform: DTransformation::empty(),
            modify_mode: ShapeModifyMode::Deflate,
            modify_config: ShapeModifyConfig::default(),
        },
        vec![min_quality_zone],
        cde_config,
    )
    .unwrap();
    let min_quality_item = Arc::new(
        Item::new(
            0,
            6300,
            OriginalShape {
                shape: polygon(&[(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)]),
                pre_transform: DTransformation::empty(),
                modify_mode: ShapeModifyMode::Inflate,
                modify_config: ShapeModifyConfig::default(),
            },
            RotationRange::None,
            Some(2),
            SPSurrogateConfig::none(),
        )
        .unwrap(),
    );
    let mut min_quality_layout = Layout::new(min_quality_container);
    min_quality_layout.place_item(&min_quality_item, DTransformation::new(0.0, (0.5, 0.5)));
    println!(
        "layout.inferior_quality_min_feasible={}",
        min_quality_layout.is_feasible()
    );
    let empty_discrete_rotation_item = Item::new(
        0,
        6200,
        OriginalShape {
            shape: polygon(&[(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)]),
            pre_transform: DTransformation::empty(),
            modify_mode: ShapeModifyMode::Inflate,
            modify_config: ShapeModifyConfig::default(),
        },
        RotationRange::Discrete(vec![]),
        None,
        SPSurrogateConfig::none(),
    );
    println!(
        "item.empty_discrete_rotation_accepted={}",
        empty_discrete_rotation_item.is_ok()
    );
    let rounded_rotation_item = Item::new(
        1,
        6201,
        OriginalShape {
            shape: polygon(&[(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)]),
            pre_transform: DTransformation::empty(),
            modify_mode: ShapeModifyMode::Inflate,
            modify_config: ShapeModifyConfig::default(),
        },
        RotationRange::Discrete(vec![1.00000006_f32]),
        None,
        SPSurrogateConfig::none(),
    )
    .unwrap();
    let rotation_bits = match &rounded_rotation_item.allowed_rotation {
        RotationRange::Discrete(angles) => angles[0].to_bits(),
        _ => unreachable!(),
    };
    println!("item.rotation_f32_bits={rotation_bits:08x}");
    let rotation_importer = Importer::new(cde_config, None, None);
    let polygon_quality_zone = ExtContainer {
        id: 6300,
        shape: ExtShape::Rectangle {
            x_min: 0.0,
            y_min: 0.0,
            width: 10.0,
            height: 10.0,
        },
        zones: vec![ExtQualityZone {
            quality: 1,
            shape: ExtShape::Polygon(ExtPolygon {
                outer: ExtSPolygon(vec![(2.0, 2.0), (4.0, 2.0), (4.0, 4.0), (2.0, 4.0)]),
                inner: vec![],
            }),
        }],
    };
    let empty_bpp_instance = BPInstance::new(vec![], vec![]);
    let empty_bpp_external = jagua_rs::probs::bpp::io::ext_repr::ExtBPInstance {
        name: String::new(),
        min_item_separation: 0.0,
        items: vec![],
        bins: vec![],
    };
    let empty_mspp_strip =
        Strip::new(1.0, 1.0, cde_config, ShapeModifyConfig::default(), 1.0).unwrap();
    let empty_mspp_instance = MSPInstance::new(vec![], empty_mspp_strip);
    let distinct_width_mspp_strip =
        Strip::new(10.0, 5.0, cde_config, ShapeModifyConfig::default(), 4.0).unwrap();
    let distinct_width_mspp_instance = MSPInstance::new(vec![], distinct_width_mspp_strip);
    println!(
        "mspp.base_strip_width={:.0},max={:.0}",
        distinct_width_mspp_instance.base_strip.width,
        distinct_width_mspp_instance.base_strip.max_width
    );
    let empty_mspp_external = jagua_rs::probs::mspp::io::ext_repr::ExtMSPInstance {
        name: String::new(),
        min_item_separation: 0.0,
        items: vec![],
        strips: jagua_rs::probs::mspp::io::ext_repr::ExtMSPStrip {
            height: 1.0,
            max_width: 1.0,
        },
    };
    let previous_panic_hook = std::panic::take_hook();
    std::panic::set_hook(Box::new(|_| {}));
    let polygon_quality_zone_panics =
        std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
            rotation_importer.import_container(&polygon_quality_zone)
        }))
        .is_err();
    let bpp_solution_import_panics = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        jagua_rs::probs::bpp::io::import_solution(&empty_bpp_instance, &empty_bpp_external)
    }))
    .is_err();
    let mspp_solution_import_panics =
        std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
            jagua_rs::probs::mspp::io::import_solution(&empty_mspp_instance, &empty_mspp_external)
        }))
        .is_err();
    std::panic::set_hook(previous_panic_hook);
    println!("import.polygon_quality_zone_unimplemented={polygon_quality_zone_panics}");
    println!("import.bpp_solution_unimplemented={bpp_solution_import_panics}");
    println!("import.mspp_solution_unimplemented={mspp_solution_import_panics}");

    let svg_container = Container::new(
        OriginalShape {
            shape: polygon(&[(0.0, 0.0), (100.0, 0.0), (100.0, 100.0), (0.0, 100.0)]),
            pre_transform: DTransformation::empty(),
            modify_mode: ShapeModifyMode::Deflate,
            modify_config: ShapeModifyConfig::default(),
        },
        vec![],
        CDEConfig {
            quadtree_depth: 1,
            cd_threshold: 8,
            item_surrogate_config: SPSurrogateConfig::none(),
        },
    )
    .unwrap();
    let svg_collision_item = Arc::new(
        Item::new(
            0,
            9001,
            OriginalShape {
                shape: polygon(&[(-5.0, -10.0), (5.0, -10.0), (5.0, 10.0), (-5.0, 10.0)]),
                pre_transform: DTransformation::empty(),
                modify_mode: ShapeModifyMode::Inflate,
                modify_config: ShapeModifyConfig::default(),
            },
            RotationRange::None,
            None,
            SPSurrogateConfig::none(),
        )
        .unwrap(),
    );
    let mut svg_collision_layout = Layout::new(svg_container);
    svg_collision_layout.place_item(&svg_collision_item, DTransformation::new(0.0, (45.0, 50.0)));
    svg_collision_layout.place_item(&svg_collision_item, DTransformation::new(0.0, (53.0, 50.0)));
    let collision_svg =
        layout_to_svg(&svg_collision_layout, SvgDrawOptions::default(), "oracle").to_string();
    println!(
        "svg.collision_lines_present={}",
        collision_svg.contains("id=\"collision_lines\"")
    );
    println!(
        "svg.collision_highlight_present={}",
        collision_svg.contains("collision_highlight_color") || collision_svg.contains("#00FF00")
    );
    println!(
        "svg.collision_line_endpoints_match={}",
        collision_svg.contains("M45,50 L53,50") || collision_svg.contains("M53,50 L45,50")
    );
    if let Some(collision_line) = collision_svg.lines().find(|line| line.contains("#00FF00")) {
        println!("svg.collision_line={collision_line}");
    }

    let ordered_container = rotation_importer
        .import_container(&ExtContainer {
            id: 6205,
            shape: ExtShape::Polygon(ExtPolygon {
                outer: ExtSPolygon(vec![(0.0, 0.0), (10.0, 0.0), (10.0, 10.0), (0.0, 10.0)]),
                inner: vec![ExtSPolygon(vec![
                    (4.0, 4.0),
                    (6.0, 4.0),
                    (6.0, 6.0),
                    (4.0, 6.0),
                ])],
            }),
            zones: vec![ExtQualityZone {
                quality: 0,
                shape: ExtShape::SimplePolygon(ExtSPolygon(vec![
                    (7.0, 7.0),
                    (9.0, 7.0),
                    (9.0, 9.0),
                    (7.0, 9.0),
                ])),
            }],
        })
        .unwrap();
    let zone_probe = polygon(&[(7.2, 7.2), (7.8, 7.2), (7.8, 7.8), (7.2, 7.8)]);
    let mut zone_collector = BasicHazardCollector::new();
    ordered_container
        .base_cde
        .collect_poly_collisions(&zone_probe, &mut zone_collector);
    let imported_zone_uses_first_hole_index = zone_collector
        .iter()
        .any(|(_, entity)| matches!(entity, HazardEntity::Hole { idx: 0 }));
    println!("import.q0_zone_hazard_index_zero={imported_zone_uses_first_hole_index}");
    let precise_angle = 123.456789_f32;
    let precise_angle_item = rotation_importer
        .import_item(
            &ExtItem {
                id: 6204,
                orientation: ExtOrientation {
                    rotation: ExtRotation::Discrete {
                        angles: vec![precise_angle],
                    },
                },
                shape: ExtShape::Rectangle {
                    x_min: 0.0,
                    y_min: 0.0,
                    width: 1.0,
                    height: 1.0,
                },
                min_quality: None,
            },
            4,
        )
        .unwrap();
    let precise_rotation_bits = match &precise_angle_item.allowed_rotation {
        RotationRange::Discrete(angles) => angles[0].to_bits(),
        _ => unreachable!(),
    };
    println!("import.rotation_noninteger_f32_bits={precise_rotation_bits:08x}");
    let rounded_360 = 360.00000001_f32;
    let discrete_boundary_item = rotation_importer
        .import_item(
            &ExtItem {
                id: 6202,
                orientation: ExtOrientation {
                    rotation: ExtRotation::Discrete {
                        angles: vec![rounded_360],
                    },
                },
                shape: ExtShape::Rectangle {
                    x_min: 0.0,
                    y_min: 0.0,
                    width: 1.0,
                    height: 1.0,
                },
                min_quality: None,
            },
            2,
        )
        .unwrap();
    let stepped_boundary_item = rotation_importer
        .import_item(
            &ExtItem {
                id: 6203,
                orientation: ExtOrientation {
                    rotation: ExtRotation::Stepped { step: rounded_360 },
                },
                shape: ExtShape::Rectangle {
                    x_min: 0.0,
                    y_min: 0.0,
                    width: 1.0,
                    height: 1.0,
                },
                min_quality: None,
            },
            3,
        )
        .unwrap();
    println!(
        "import.rotation_f32_rounded_360=discrete_none:{},stepped_none:{}",
        matches!(discrete_boundary_item.allowed_rotation, RotationRange::None),
        matches!(stepped_boundary_item.allowed_rotation, RotationRange::None)
    );
    let wide_rotation_external = jagua_rs::probs::spp::io::ext_repr::ExtSPInstance {
        name: "wide-rotation".to_string(),
        min_item_separation: 0.0,
        items: vec![jagua_rs::probs::spp::io::ext_repr::ExtItem {
            base: ExtItem {
                id: 6206,
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
            demand: 1,
        }],
        strip_height: 10.0,
    };
    let wide_rotation_instance =
        jagua_rs::probs::spp::io::import_instance(&rotation_importer, &wide_rotation_external)
            .unwrap();
    let wide_rotation_solution = jagua_rs::probs::spp::io::import_solution(
        &wide_rotation_instance,
        &jagua_rs::probs::spp::io::ext_repr::ExtSPSolution {
            strip_width: 10.0,
            layout: ExtLayout {
                container_id: 0,
                placed_items: vec![ExtPlacedItem {
                    item_id: 6206,
                    transformation: ExtTransformation {
                        rotation: 869_592.625,
                        translation: (5.0, 5.0),
                    },
                }],
                density: 0.0,
            },
            density: 0.0,
            run_time_sec: 0,
        },
    )
    .unwrap();
    let wide_rotation_bits = wide_rotation_solution
        .layout_snapshot
        .placed_items
        .values()
        .next()
        .unwrap()
        .d_transf
        .rotation()
        .to_bits();
    println!("import.external_solution_rotation_bits={wide_rotation_bits:08x}");
    let wide_rotation_placement = wide_rotation_solution
        .layout_snapshot
        .placed_items
        .values()
        .next()
        .unwrap();
    let wide_rotation_external_transform = int_to_ext_transformation(
        &wide_rotation_placement.d_transf,
        &wide_rotation_placement.item.shape_orig.pre_transform,
    );
    println!(
        "export.external_solution_rotation_bits={:08x}",
        wide_rotation_external_transform
            .rotation()
            .to_degrees()
            .to_bits()
    );
    let roundtrip_probe_solution = jagua_rs::probs::spp::io::import_solution(
        &wide_rotation_instance,
        &jagua_rs::probs::spp::io::ext_repr::ExtSPSolution {
            strip_width: 10.0,
            layout: ExtLayout {
                container_id: 0,
                placed_items: vec![ExtPlacedItem {
                    item_id: 6206,
                    transformation: ExtTransformation {
                        rotation: f32::from_bits(0xc1fa_7be4),
                        translation: (5.0, 5.0),
                    },
                }],
                density: 0.0,
            },
            density: 0.0,
            run_time_sec: 0,
        },
    )
    .unwrap();
    let roundtrip_probe_placement = roundtrip_probe_solution
        .layout_snapshot
        .placed_items
        .values()
        .next()
        .unwrap();
    println!(
        "import.roundtrip_probe_internal_rotation_bits={:08x}",
        roundtrip_probe_placement.d_transf.rotation().to_bits()
    );
    let internal_translation = roundtrip_probe_placement.d_transf.translation();
    println!(
        "import.roundtrip_probe_internal_translation_bits={:08x},{:08x}",
        internal_translation.0.to_bits(),
        internal_translation.1.to_bits()
    );
    let roundtrip_probe_external_transform = int_to_ext_transformation(
        &roundtrip_probe_placement.d_transf,
        &roundtrip_probe_placement.item.shape_orig.pre_transform,
    );
    let external_translation = roundtrip_probe_external_transform.translation();
    println!(
        "export.roundtrip_probe_translation_bits={:08x},{:08x}",
        external_translation.0.to_bits(),
        external_translation.1.to_bits()
    );
    println!(
        "export.roundtrip_probe_rotation_bits={:08x}",
        roundtrip_probe_external_transform
            .rotation()
            .to_degrees()
            .to_bits()
    );
    let collapsed_rectangle = rotation_importer.import_item(
        &ExtItem {
            id: 6205,
            orientation: ExtOrientation {
                rotation: ExtRotation::Continuous {},
            },
            shape: ExtShape::Rectangle {
                x_min: 16_777_216.5_f32,
                y_min: 0.0,
                width: 0.6_f32,
                height: 2.0,
            },
            min_quality: None,
        },
        5,
    );
    println!(
        "import.rect_f32_endpoints_collapsed_rejected={}",
        collapsed_rectangle.is_err()
    );
    let rounded_bounds_max = 1.00000006_f32;
    let rounded_bounds = Rect::try_new(0.0, 0.0, rounded_bounds_max, 1.0).unwrap();
    println!(
        "collision.f32_bounds_max={:08x}",
        rounded_bounds.x_max.to_bits()
    );
    let collision_root = Rect::try_new(0.0, 0.0, 10.0, 10.0).unwrap();
    let exterior_shape = Arc::new(polygon(&[
        (0.0, 0.0),
        (10.0, 0.0),
        (10.0, 10.0),
        (0.0, 10.0),
    ]));
    let collision_engine = CDEngine::new(
        collision_root,
        vec![Hazard::new(HazardEntity::Exterior, exterior_shape, false)],
        cde_config,
    );
    let exterior_key = collision_engine
        .hazards_map
        .iter()
        .find(|(_, hazard)| matches!(hazard.entity, HazardEntity::Exterior))
        .map(|(key, _)| key)
        .unwrap();
    let ignore_exterior = HazKeyFilter::from_keys([exterior_key]);
    let partly_outside = polygon(&[(-1.0, 1.0), (1.0, 1.0), (1.0, 2.0), (-1.0, 2.0)]);
    println!(
        "collision.out_of_bounds_ignores_exterior_filter={}",
        collision_engine.detect_poly_collision(&partly_outside, &ignore_exterior)
    );
    let depth_zero_collision_config = CDEConfig {
        quadtree_depth: 0,
        cd_threshold: 8,
        item_surrogate_config: SPSurrogateConfig::none(),
    };
    let depth_zero_engine = CDEngine::new(
        Rect::try_new(0.0, 0.0, 10.0, 10.0).unwrap(),
        vec![Hazard::new(
            HazardEntity::Exterior,
            Arc::new(polygon(&[(2.0, 2.0), (8.0, 2.0), (8.0, 8.0), (2.0, 8.0)])),
            false,
        )],
        depth_zero_collision_config,
    );
    let query_containing_exterior = polygon(&[(1.0, 1.0), (9.0, 1.0), (9.0, 9.0), (1.0, 9.0)]);
    println!(
        "collision.query_contains_exterior={}",
        depth_zero_engine.detect_poly_collision(&query_containing_exterior, &NoFilter)
    );
    let depth_four_engine = CDEngine::new(
        Rect::try_new(0.0, 0.0, 10.0, 10.0).unwrap(),
        vec![Hazard::new(
            HazardEntity::Exterior,
            Arc::new(polygon(&[(2.0, 2.0), (8.0, 2.0), (8.0, 8.0), (2.0, 8.0)])),
            false,
        )],
        CDEConfig {
            quadtree_depth: 4,
            cd_threshold: 8,
            item_surrogate_config: SPSurrogateConfig::none(),
        },
    );
    let root_enclosing_query = polygon(&[(0.0, 0.0), (10.0, 0.0), (10.0, 10.0), (0.0, 10.0)]);
    println!(
        "collision.root_contains_exterior={}",
        depth_four_engine.detect_poly_collision(&root_enclosing_query, &NoFilter)
    );
    println!(
        "collision.depth_four_query_contains_exterior={}",
        depth_four_engine.detect_poly_collision(&query_containing_exterior, &NoFilter)
    );
    let empty_spp = SPInstance::new(
        vec![],
        SPStrip::new(4.0, cde_config, ShapeModifyConfig::default(), 1.0).unwrap(),
    );
    println!("spp.empty_instance_items={}", empty_spp.items.len());
    let zero_demand_spp = SPInstance::new(
        vec![(square_item(0, 1.0), 0)],
        SPStrip::new(4.0, cde_config, ShapeModifyConfig::default(), 1.0).unwrap(),
    );
    println!(
        "spp.zero_demand_item_area={:.0}",
        zero_demand_spp.item_area()
    );
    println!(
        "spp.item_idx_zero_demand={:?},unknown={:?}",
        zero_demand_spp.item_idx(1000),
        zero_demand_spp.item_idx(9999)
    );
    println!(
        "spp.positive_infinite_initial_width_accepted={}",
        SPStrip::new(5.0, cde_config, ShapeModifyConfig::default(), f32::INFINITY,).is_ok()
    );
    let mut positive_infinite_spp_strip =
        SPStrip::new(5.0, cde_config, ShapeModifyConfig::default(), 10.0).unwrap();
    positive_infinite_spp_strip.set_width(f32::INFINITY);
    println!("spp.positive_infinite_width_accepted=true");

    let empty_bpp_instance = BPInstance::new(vec![], vec![]);
    println!(
        "bpp.empty_instance_items={},bins={}",
        empty_bpp_instance.items.len(),
        empty_bpp_instance.bins.len()
    );
    let zero_demand_bpp = BPInstance::new(vec![(square_item(0, 1.0), 0)], vec![]);
    println!(
        "bpp.item_idx_zero_demand={:?},unknown={:?}",
        zero_demand_bpp.item_idx(1000),
        zero_demand_bpp.item_idx(9999)
    );
    let duplicate_bin_container = Container::new(
        OriginalShape {
            shape: polygon(&[(0.0, 0.0), (4.0, 0.0), (4.0, 4.0), (0.0, 4.0)]),
            pre_transform: DTransformation::empty(),
            modify_mode: ShapeModifyMode::Deflate,
            modify_config: ShapeModifyConfig::default(),
        },
        vec![],
        cde_config,
    )
    .unwrap();
    let duplicate_external_bins = vec![
        Bin::new(0, 5200, duplicate_bin_container.clone(), 1, 1),
        Bin::new(1, 5200, duplicate_bin_container, 1, 2),
    ];
    let duplicate_bin_instance = BPInstance::new(vec![], duplicate_external_bins);
    println!(
        "bpp.duplicate_external_bin_ids={}",
        duplicate_bin_instance.bins.len()
    );
    let make_bin_container = |width: f32, height: f32| {
        Container::new(
            OriginalShape {
                shape: polygon(&[(0.0, 0.0), (width, 0.0), (width, height), (0.0, height)]),
                pre_transform: DTransformation::empty(),
                modify_mode: ShapeModifyMode::Deflate,
                modify_config: ShapeModifyConfig::default(),
            },
            vec![],
            cde_config,
        )
        .unwrap()
    };
    let item_list_first = square_item(5, 1.0);
    let item_list_second = square_item(6, 1.0);
    let mut duplicate_item_index = (*item_list_first).clone();
    duplicate_item_index.external_id = 1505;
    let duplicate_item_index = Arc::new(duplicate_item_index);
    let mut distinct_items_layout = Layout::new(make_bin_container(30.0, 10.0));
    let item_list_first_key =
        distinct_items_layout.place_item(&item_list_first, DTransformation::new(0.0, (1.0, 1.0)));
    distinct_items_layout.place_item(&item_list_second, DTransformation::new(0.0, (8.0, 1.0)));
    distinct_items_layout.place_item(
        &duplicate_item_index,
        DTransformation::new(0.0, (15.0, 1.0)),
    );
    let distinct_items: Vec<_> = distinct_items_layout
        .items()
        .map(|item| format!("{}:{}", item.idx, item.external_id))
        .collect();
    println!("layout.distinct_items={}", distinct_items.join(","));
    distinct_items_layout.remove_item(item_list_first_key);
    let distinct_items_after_removal: Vec<_> = distinct_items_layout
        .items()
        .map(|item| format!("{}:{}", item.idx, item.external_id))
        .collect();
    println!(
        "layout.distinct_items_after_removal={}",
        distinct_items_after_removal.join(",")
    );

    let slot_item = square_item(0, 1.0);
    let mut slot_map_layout = Layout::new(make_bin_container(20.0, 10.0));
    let first_slot_key =
        slot_map_layout.place_item(&slot_item, DTransformation::new(0.0, (1.0, 1.0)));
    let second_slot_key =
        slot_map_layout.place_item(&slot_item, DTransformation::new(0.0, (5.0, 1.0)));
    slot_map_layout.remove_item(first_slot_key);
    slot_map_layout.remove_item(second_slot_key);
    let reused_slot_key =
        slot_map_layout.place_item(&slot_item, DTransformation::new(0.0, (9.0, 1.0)));
    println!(
        "layout.slot_reuse_order=first:{first_slot_key:?},second:{second_slot_key:?},reused:{reused_slot_key:?}"
    );
    let cde_dynamic_keys: Vec<_> = slot_map_layout
        .cde()
        .hazards_map
        .iter()
        .filter(|(_, hazard)| hazard.dynamic)
        .map(|(key, _)| format!("{key:?}"))
        .collect();
    println!("layout.cde_dynamic_keys={cde_dynamic_keys:?}");
    let slot_map_snapshot = slot_map_layout.save();
    let mut restored_slot_map_layout = Layout::new(slot_map_snapshot.container.clone());
    restored_slot_map_layout
        .restore(&slot_map_snapshot)
        .unwrap();
    let reused_after_restore =
        restored_slot_map_layout.place_item(&slot_item, DTransformation::new(0.0, (13.0, 1.0)));
    println!(
        "layout.restore_free_reuse={reused_after_restore:?},stale_first_active={}",
        restored_slot_map_layout
            .placed_items
            .contains_key(first_slot_key)
    );

    let mut cde_restore_layout = Layout::new(make_bin_container(20.0, 10.0));
    let cde_item_a = square_item(7, 1.0);
    let cde_item_b = square_item(8, 1.0);
    let cde_item_c = square_item(9, 1.0);
    let cde_item_key_a =
        cde_restore_layout.place_item(&cde_item_a, DTransformation::new(0.0, (1.0, 1.0)));
    let cde_item_key_b =
        cde_restore_layout.place_item(&cde_item_b, DTransformation::new(0.0, (5.0, 1.0)));
    let cde_snapshot = cde_restore_layout.save();
    cde_restore_layout.remove_item(cde_item_key_a);
    cde_restore_layout.remove_item(cde_item_key_b);
    cde_restore_layout.place_item(&cde_item_c, DTransformation::new(0.0, (9.0, 1.0)));
    let mut restored_cde = cde_restore_layout.cde().clone();
    restored_cde.restore(&cde_snapshot.cde_snapshot);
    let restored_dynamic_hazard_keys: Vec<_> = restored_cde
        .hazards_map
        .iter()
        .filter(|(_, hazard)| hazard.dynamic)
        .map(|(key, hazard)| format!("{:?}:{key:?}", hazard.entity))
        .collect();
    println!("layout.cde_restore_dynamic_keys={restored_dynamic_hazard_keys:?}");

    let mut swap_after_restore = cde_restore_layout.clone();
    swap_after_restore.restore(&cde_snapshot).unwrap();
    swap_after_restore.swap_container(make_bin_container(21.0, 10.0));
    let swap_after_restore_dynamic_order: Vec<_> = swap_after_restore
        .save()
        .cde_snapshot
        .dynamic_hazards
        .iter()
        .map(|hazard| format!("{:?}", hazard.entity))
        .collect();
    println!("layout.swap_after_restore_dynamic_order={swap_after_restore_dynamic_order:?}");

    let mut cde_with_dynamic_hole = slot_map_layout.cde().clone();
    let mut dynamic_hole_snapshot = cde_with_dynamic_hole.save();
    dynamic_hole_snapshot.dynamic_hazards.push(Hazard::new(
        HazardEntity::Hole { idx: 88 },
        slot_item.shape_cd.clone(),
        true,
    ));
    cde_with_dynamic_hole.restore(&dynamic_hole_snapshot);
    println!(
        "layout.dynamic_non_item_hazard_restored={}",
        cde_with_dynamic_hole
            .save()
            .dynamic_hazards
            .iter()
            .any(|hazard| matches!(hazard.entity, HazardEntity::Hole { idx: 88 }))
    );

    let spp_restore_strip =
        SPStrip::new(10.0, cde_config, ShapeModifyConfig::default(), 20.0).unwrap();
    let mut spp_restore = SPProblem::new(SPInstance::new(
        vec![
            (square_item(0, 1.0), 1),
            (square_item(1, 1.0), 1),
            (square_item(2, 1.0), 1),
        ],
        spp_restore_strip,
    ))
    .unwrap();
    let spp_item_a = spp_restore.place_item(SPPlacement {
        item_idx: 0,
        d_transf: DTransformation::new(0.0, (1.0, 1.0)),
    });
    let spp_item_b = spp_restore.place_item(SPPlacement {
        item_idx: 1,
        d_transf: DTransformation::new(0.0, (5.0, 1.0)),
    });
    let spp_restore_snapshot = spp_restore.save();
    spp_restore.remove_item(spp_item_a);
    spp_restore.remove_item(spp_item_b);
    spp_restore.place_item(SPPlacement {
        item_idx: 2,
        d_transf: DTransformation::new(0.0, (9.0, 1.0)),
    });
    spp_restore.restore(&spp_restore_snapshot);
    let spp_restore_hazard_order: Vec<_> = spp_restore
        .layout
        .cde()
        .hazards_map
        .values()
        .filter(|hazard| hazard.dynamic)
        .map(|hazard| match hazard.entity {
            HazardEntity::PlacedItem { id, .. } => id,
            _ => usize::MAX,
        })
        .collect();
    println!("spp.restore_cde_item_order={spp_restore_hazard_order:?}");

    println!(
        "mspp.positive_infinite_initial_width_accepted={}",
        Strip::new(
            10.0,
            5.0,
            cde_config,
            ShapeModifyConfig::default(),
            f32::INFINITY,
        )
        .is_ok()
    );
    let mut positive_infinite_mspp_strip = Strip::new(
        f32::INFINITY,
        5.0,
        cde_config,
        ShapeModifyConfig::default(),
        10.0,
    )
    .unwrap();
    println!("mspp.positive_infinite_max_width_accepted=true");
    positive_infinite_mspp_strip.set_width(f32::INFINITY);
    println!("mspp.positive_infinite_width_accepted=true");

    let stale_msp_strip =
        Strip::new(10.0, 5.0, cde_config, ShapeModifyConfig::default(), 10.0).unwrap();
    let mut stale_msp = MSPProblem::new(MSPInstance::new(
        vec![(square_item(0, 1.0), 2)],
        stale_msp_strip,
    ));
    let old_msp_key = stale_msp.add_layout_from_strip(stale_msp_strip).unwrap();
    println!("mspp.first_layout_key={old_msp_key:?}");
    stale_msp.place_item(MSPPlacement {
        lk: old_msp_key,
        item_idx: 0,
        d_transf: DTransformation::empty(),
    });
    let old_msp_solution = stale_msp.save();
    stale_msp.remove_layout(old_msp_key);
    let replacement_msp_key = stale_msp.add_layout_from_strip(stale_msp_strip).unwrap();
    stale_msp.place_item(MSPPlacement {
        lk: replacement_msp_key,
        item_idx: 0,
        d_transf: DTransformation::empty(),
    });
    let msp_restore_keys_changed = stale_msp.restore(&old_msp_solution);
    println!(
        "mspp.restore_rekeyed={},old_key_active={}",
        msp_restore_keys_changed,
        stale_msp.layouts.contains_key(old_msp_key)
    );

    let restore_order_base =
        Strip::new(10.0, 5.0, cde_config, ShapeModifyConfig::default(), 10.0).unwrap();
    let mut restore_order_msp = MSPProblem::new(MSPInstance::new(
        vec![(square_item(0, 1.0), 1)],
        restore_order_base,
    ));
    let restore_order_first = restore_order_msp
        .add_layout_from_strip(
            Strip::new(10.0, 5.0, cde_config, ShapeModifyConfig::default(), 4.0).unwrap(),
        )
        .unwrap();
    let restore_order_second = restore_order_msp
        .add_layout_from_strip(
            Strip::new(10.0, 5.0, cde_config, ShapeModifyConfig::default(), 6.0).unwrap(),
        )
        .unwrap();
    let restore_order_solution = restore_order_msp.save();
    restore_order_msp.remove_layout(restore_order_first);
    restore_order_msp.remove_layout(restore_order_second);
    restore_order_msp.restore(&restore_order_solution);
    let restored_strip_widths = restore_order_msp
        .layouts
        .iter()
        .map(|(key, _)| restore_order_msp.strips[key].width)
        .collect::<Vec<_>>();
    println!("mspp.restore_rekeyed_widths={restored_strip_widths:?}");

    let mut stale_bpp = BPProblem::new(BPInstance::new(
        vec![(square_item(0, 1.0), 2)],
        vec![Bin::new(0, 6500, make_bin_container(10.0, 5.0), 2, 1)],
    ));
    let (old_bpp_key, _) = stale_bpp.place_item(BPPlacement {
        layout_id: BPLayoutType::Closed { bin_id: 0 },
        item_idx: 0,
        d_transf: DTransformation::empty(),
    });
    println!("bpp.first_layout_key={old_bpp_key:?}");
    let old_bpp_solution = stale_bpp.save();
    stale_bpp.remove_layout(old_bpp_key);
    stale_bpp.place_item(BPPlacement {
        layout_id: BPLayoutType::Closed { bin_id: 0 },
        item_idx: 0,
        d_transf: DTransformation::empty(),
    });
    let bpp_restore_keys_changed = stale_bpp.restore(&old_bpp_solution);
    println!(
        "bpp.restore_rekeyed={},old_key_active={}",
        bpp_restore_keys_changed,
        stale_bpp.layouts.contains_key(old_bpp_key)
    );

    let mut bpp_restore_order = BPProblem::new(BPInstance::new(
        vec![(square_item(0, 1.0), 2)],
        vec![
            Bin::new(0, 6560, make_bin_container(2.0, 2.0), 1, 10),
            Bin::new(1, 6561, make_bin_container(3.0, 2.0), 1, 25),
        ],
    ));
    let (bpp_restore_first, _) = bpp_restore_order.place_item(BPPlacement {
        layout_id: BPLayoutType::Closed { bin_id: 0 },
        item_idx: 0,
        d_transf: DTransformation::empty(),
    });
    let (bpp_restore_second, _) = bpp_restore_order.place_item(BPPlacement {
        layout_id: BPLayoutType::Closed { bin_id: 1 },
        item_idx: 0,
        d_transf: DTransformation::empty(),
    });
    let bpp_restore_order_solution = bpp_restore_order.save();
    bpp_restore_order.remove_layout(bpp_restore_first);
    bpp_restore_order.remove_layout(bpp_restore_second);
    bpp_restore_order.restore(&bpp_restore_order_solution);
    let restored_bin_order = bpp_restore_order
        .layouts
        .iter()
        .map(|(key, _)| bpp_restore_order.layout_bins[key])
        .collect::<Vec<_>>();
    println!("bpp.restore_rekeyed_bin_order={restored_bin_order:?}");

    let mut bpp_churn = BPProblem::new(BPInstance::new(
        vec![(square_item(0, 1.0), 4)],
        vec![
            Bin::new(0, 5400, make_bin_container(1.0, 1.0), 4, 1),
            Bin::new(1, 5401, make_bin_container(16_777_216.0, 1.0), 1, 1),
        ],
    ));
    let (first_layout, first_item_key) = bpp_churn.place_item(BPPlacement {
        layout_id: BPLayoutType::Closed { bin_id: 0 },
        item_idx: 0,
        d_transf: DTransformation::empty(),
    });
    bpp_churn.place_item(BPPlacement {
        layout_id: BPLayoutType::Closed { bin_id: 0 },
        item_idx: 0,
        d_transf: DTransformation::empty(),
    });
    bpp_churn.place_item(BPPlacement {
        layout_id: BPLayoutType::Closed { bin_id: 1 },
        item_idx: 0,
        d_transf: DTransformation::empty(),
    });
    bpp_churn.remove_item(first_layout, first_item_key);
    bpp_churn.place_item(BPPlacement {
        layout_id: BPLayoutType::Closed { bin_id: 0 },
        item_idx: 0,
        d_transf: DTransformation::empty(),
    });
    println!("bpp.reused_slot_density={:.12}", bpp_churn.density());

    let spp_instance = SPInstance::new(
        vec![
            (square_item(0, 4096.0), 1),
            (square_item(1, 1.0), 1),
            (square_item(2, 1.0), 1),
        ],
        SPStrip::new(2.0, cde_config, ShapeModifyConfig::default(), 1.0).unwrap(),
    );
    println!("spp.item_area={:.0}", spp_instance.item_area());
    println!(
        "spp.auto_base_strip_width={:.0}",
        spp_instance.item_area() / 2.0
    );

    let strip_width_base = Strip::new(
        16_777_216.0,
        1.0,
        cde_config,
        ShapeModifyConfig::default(),
        16_777_216.0,
    )
    .unwrap();
    let mut strip_width_problem = MSPProblem::new(MSPInstance::new(
        vec![(square_item(0, 1.0), 1)],
        strip_width_base,
    ));
    for width in [16_777_216.0, 1.0, 1.0] {
        strip_width_problem
            .add_layout_from_strip(
                Strip::new(
                    16_777_216.0,
                    1.0,
                    cde_config,
                    ShapeModifyConfig::default(),
                    width,
                )
                .unwrap(),
            )
            .unwrap();
    }
    println!(
        "mspp.total_strip_width={:.0}",
        strip_width_problem.total_strip_width()
    );
    let churn_base = Strip::new(
        16_777_216.0,
        1.0,
        cde_config,
        ShapeModifyConfig::default(),
        16_777_216.0,
    )
    .unwrap();
    let mut churn_problem =
        MSPProblem::new(MSPInstance::new(vec![(square_item(0, 1.0), 1)], churn_base));
    let removed_key = churn_problem
        .add_layout_from_strip(
            Strip::new(
                16_777_216.0,
                1.0,
                cde_config,
                ShapeModifyConfig::default(),
                1.0,
            )
            .unwrap(),
        )
        .unwrap();
    churn_problem
        .add_layout_from_strip(
            Strip::new(
                16_777_216.0,
                1.0,
                cde_config,
                ShapeModifyConfig::default(),
                1.0,
            )
            .unwrap(),
        )
        .unwrap();
    churn_problem
        .add_layout_from_strip(
            Strip::new(
                16_777_216.0,
                1.0,
                cde_config,
                ShapeModifyConfig::default(),
                16_777_216.0,
            )
            .unwrap(),
        )
        .unwrap();
    churn_problem.remove_layout(removed_key);
    let reused_churn_key = churn_problem
        .add_layout_from_strip(
            Strip::new(
                16_777_216.0,
                1.0,
                cde_config,
                ShapeModifyConfig::default(),
                1.0,
            )
            .unwrap(),
        )
        .unwrap();
    println!(
        "mspp.reused_slot_total_strip_width={:.0}",
        churn_problem.total_strip_width()
    );
    churn_problem.place_item(MSPPlacement {
        lk: reused_churn_key,
        item_idx: 0,
        d_transf: DTransformation::empty(),
    });
    println!("mspp.reused_slot_density={:.12}", churn_problem.density());
    let slot_ordered_churn_widths = churn_problem
        .layouts
        .iter()
        .map(|(key, _)| churn_problem.strips[key].width)
        .collect::<Vec<_>>();
    println!("mspp.reused_slot_width_order={slot_ordered_churn_widths:?}");
    println!(
        "mspp.initial_width_above_max_accepted={}",
        Strip::new(10.0, 5.0, cde_config, ShapeModifyConfig::default(), 11.0,).is_ok()
    );
    let over_max_base =
        Strip::new(10.0, 5.0, cde_config, ShapeModifyConfig::default(), 10.0).unwrap();
    let mut over_max_problem = MSPProblem::new(MSPInstance::new(
        vec![(square_item(0, 1.0), 1)],
        over_max_base,
    ));
    let over_max_key = over_max_problem
        .add_layout_from_strip(
            Strip::new(10.0, 5.0, cde_config, ShapeModifyConfig::default(), 11.0).unwrap(),
        )
        .unwrap();
    over_max_problem
        .add_layout_from_strip(
            Strip::new(10.0, 5.0, cde_config, ShapeModifyConfig::default(), 9.0).unwrap(),
        )
        .unwrap();
    over_max_problem
        .add_layout_from_strip(
            Strip::new(10.0, 5.0, cde_config, ShapeModifyConfig::default(), 10.0).unwrap(),
        )
        .unwrap();
    println!(
        "mspp.extendable_strips_count={}",
        over_max_problem.extendable_strips().count()
    );
    let over_max_snapshot = over_max_problem.save();
    over_max_problem.restore(&over_max_snapshot);
    println!(
        "mspp.over_max_restore_width={:.0}",
        over_max_problem.strips[over_max_key].width
    );

    let strip = Strip::new(1.0, 1.0, cde_config, ShapeModifyConfig::default(), 1.0).unwrap();
    let empty_mspp = MSPProblem::new(MSPInstance::new(vec![], strip));
    println!(
        "mspp.empty_instance_items={}",
        empty_mspp.instance.items.len()
    );
    println!(
        "density.empty_mspp_problem_nan={}",
        empty_mspp.density().is_nan()
    );
    let zero_demand_mspp = MSPInstance::new(vec![(square_item(0, 1.0), 0)], strip);
    println!("mspp.zero_demand_item_qty={}", zero_demand_mspp.item_qty(0));
    println!(
        "mspp.item_idx_zero_demand={:?},unknown={:?}",
        zero_demand_mspp.item_idx(1000),
        zero_demand_mspp.item_idx(9999)
    );

    let imported = import_simple_polygon(&ExtSPolygon(vec![
        (0.0, 0.0),
        (4.0, 0.0),
        ((4.0_f64 + 1.0e-7) as f32, 0.0),
        (4.0, 2.0),
        (0.0, 2.0),
    ]))
    .unwrap();
    println!("import.f32duplicate.vertices={}", imported.n_vertices());
    println!("import.f32duplicate.area={:.12}", imported.area);

    let square = polygon(&[(0.0, 0.0), (2.0, 0.0), (2.0, 2.0), (0.0, 2.0)]);
    let large_square = polygon(&[
        (1_000_000.0, 1_000_000.0),
        (2_000_000.0, 1_000_000.0),
        (2_000_000.0, 2_000_000.0),
        (1_000_000.0, 2_000_000.0),
    ]);
    match offset_shape(&large_square, ShapeModifyMode::Inflate, 0.01) {
        Ok(shape) => print_polygon("offset.sub_ulp_inflate", &shape),
        Err(error) => println!("offset.sub_ulp_inflate=error:{error}"),
    }
    match offset_shape(&large_square, ShapeModifyMode::Deflate, 0.01) {
        Ok(shape) => print_polygon("offset.sub_ulp_deflate", &shape),
        Err(error) => println!("offset.sub_ulp_deflate=error:{error}"),
    }
    match offset_shape(&square, ShapeModifyMode::Deflate, 2.0) {
        Ok(shape) => print_polygon("offset.deflate_collapse", &shape),
        Err(error) => println!("offset.deflate_collapse=error:{error}"),
    }
    match offset_shape(&square, ShapeModifyMode::Inflate, 0.25) {
        Ok(shape) => print_polygon("offset.square_inflate", &shape),
        Err(error) => println!("offset.square_inflate=error:{error}"),
    }
    let negative_offset = offset_shape(&square, ShapeModifyMode::Inflate, -0.25).unwrap();
    println!(
        "offset.negative_inflate=area:{:.2},bbox:{:.2},{:.2},{:.2},{:.2}",
        negative_offset.area,
        negative_offset.bbox.x_min,
        negative_offset.bbox.y_min,
        negative_offset.bbox.x_max,
        negative_offset.bbox.y_max
    );
    let epsilon_offset_shape = polygon(&[
        (0.0, 0.0),
        (f32::from_bits(0x3b03126f), 0.0),
        (f32::from_bits(0x3b03126f), f32::from_bits(0x3b03126f)),
        (0.0, f32::from_bits(0x3b03126f)),
    ]);
    let epsilon_offset = std::panic::catch_unwind(|| {
        offset_shape(
            &epsilon_offset_shape,
            ShapeModifyMode::Deflate,
            f32::from_bits(0x3a83126e),
        )
    });
    match epsilon_offset {
        Ok(Ok(shape)) => print_polygon("offset.epsilon_before_event", &shape),
        Ok(Err(error)) => println!("offset.epsilon_before_event=error:{error}"),
        Err(_) => println!("offset.epsilon_before_event=panic"),
    }
    print_buffer_components(
        "offset.epsilon_before_event.raw",
        &[
            (0.0, 0.0),
            (f32::from_bits(0x3b03126f), 0.0),
            (f32::from_bits(0x3b03126f), f32::from_bits(0x3b03126f)),
            (0.0, f32::from_bits(0x3b03126f)),
        ],
        -f64::from(f32::from_bits(0x3a83126e)),
    );
    let near_zero_edge_shape = polygon(&[
        (0.0, 0.0),
        (1.0e-8, 0.0),
        (1.0, 0.0),
        (1.0, 1.0),
        (0.0, 1.0),
    ]);
    match offset_shape(&near_zero_edge_shape, ShapeModifyMode::Inflate, 0.0) {
        Ok(shape) => {
            print_polygon("offset.zero_near_edge", &shape);
            for (index, point) in shape.vertices.iter().enumerate() {
                println!(
                    "offset.zero_near_edge.vertex[{index}]={:.12},{:.12}",
                    point.0, point.1
                );
            }
        }
        Err(error) => println!("offset.zero_near_edge=error:{error}"),
    }
    let validation_bounds = polygon(&[(0.0, 0.0), (3.0, 0.0), (3.0, 3.0), (0.0, 3.0)]);
    let edge_tolerance = validation_bounds.diameter * 1e-6;
    let just_outside = polygon(&[(-edge_tolerance, 1.5), (1.0, 1.0), (1.0, 2.0)]);
    println!(
        "shape_modification.edge_threshold_valid={}",
        shape_modification_valid(&just_outside, &validation_bounds, ShapeModifyMode::Inflate)
    );

    let concave = polygon(&[(0.0, 0.0), (4.0, 0.0), (4.0, 4.0), (2.0, 1.0), (0.0, 4.0)]);
    print_polygon("concave", &concave);
    let deflated = offset_shape(&concave, ShapeModifyMode::Deflate, 0.45);
    println!(
        "concave.deflate045={}",
        if deflated.is_ok() { "ok" } else { "error" }
    );
    if let Ok(shape) = deflated {
        print_polygon("concave.deflate045.first", &shape);
    }
    print_buffer_components(
        "concave.deflate045",
        &[(0.0, 0.0), (4.0, 0.0), (4.0, 4.0), (2.0, 1.0), (0.0, 4.0)],
        -0.45,
    );

    print_buffer_components(
        "l.deflate025",
        &[
            (0.0, 0.0),
            (6.0, 0.0),
            (6.0, 2.0),
            (2.0, 2.0),
            (2.0, 6.0),
            (0.0, 6.0),
        ],
        -0.25,
    );

    let comb = polygon(&[
        (0.0, 0.0),
        (22.0, 0.0),
        (22.0, 12.0),
        (16.0, 12.0),
        (16.0, 4.0),
        (14.0, 4.0),
        (14.0, 12.0),
        (8.0, 12.0),
        (8.0, 4.0),
        (6.0, 4.0),
        (6.0, 12.0),
        (0.0, 12.0),
    ]);
    match offset_shape(&comb, ShapeModifyMode::Deflate, 2.1) {
        Ok(shape) => print_polygon("offset.comb_deflate.first", &shape),
        Err(error) => println!("offset.comb_deflate.first=error:{error}"),
    }
    print_buffer_components(
        "offset.comb_deflate",
        &[
            (0.0, 0.0),
            (22.0, 0.0),
            (22.0, 12.0),
            (16.0, 12.0),
            (16.0, 4.0),
            (14.0, 4.0),
            (14.0, 12.0),
            (8.0, 12.0),
            (8.0, 4.0),
            (6.0, 4.0),
            (6.0, 12.0),
            (0.0, 12.0),
        ],
        f64::from(-2.1_f32),
    );

    let notch = polygon(&[
        (0.0, 0.0),
        (10.0, 0.0),
        (10.0, 10.0),
        (6.0, 10.0),
        (6.0, 9.9),
        (4.0, 9.9),
        (4.0, 10.0),
        (0.0, 10.0),
    ]);
    print_polygon("notch", &notch);
    let notch_offset = offset_shape(&notch, ShapeModifyMode::Inflate, 0.25);
    println!(
        "notch.inflate025={}",
        if notch_offset.is_ok() { "ok" } else { "error" }
    );
    if let Ok(shape) = notch_offset {
        print_polygon("notch.inflate025.first", &shape);
    }

    let u_shape = polygon(&[
        (0.0, 0.0),
        (10.0, 0.0),
        (10.0, 10.0),
        (6.0, 10.0),
        (6.0, 4.0),
        (4.0, 4.0),
        (4.0, 10.0),
        (0.0, 10.0),
    ]);
    let single_nonzero_limit_config = SPSurrogateConfig {
        n_pole_limits: [(8, 0.0), (0, 0.0), (0, 0.0)],
        ff_pole_area_ratio: 0.0,
        n_ff_piers: 0,
    };
    let mut single_nonzero_limit_shape = u_shape.clone();
    single_nonzero_limit_shape
        .generate_surrogate(single_nonzero_limit_config)
        .unwrap();
    println!(
        "surrogate.single_nonzero_limit={}",
        single_nonzero_limit_shape.surrogate().poles.len()
    );
    println!(
        "surrogate.config_threshold_f32_bits={:08x}",
        0.75000001_f32.to_bits()
    );
    println!(
        "surrogate.config_fail_fast_ratio_f32_bits={:08x}",
        0.50000001_f32.to_bits()
    );
    let mut infinite_threshold_shape = polygon(&[(0.0, 0.0), (2.0, 0.0), (2.0, 2.0), (0.0, 2.0)]);
    let infinite_threshold_config = SPSurrogateConfig {
        n_pole_limits: [(1, f32::INFINITY), (0, 0.0), (0, 0.0)],
        ff_pole_area_ratio: 0.0,
        n_ff_piers: 0,
    };
    let infinite_threshold_result =
        infinite_threshold_shape.generate_surrogate(infinite_threshold_config);
    let infinite_threshold_poles = infinite_threshold_shape
        .surrogate
        .as_ref()
        .map_or(0, |surrogate| surrogate.poles.len());
    println!(
        "surrogate.infinite_threshold=ok:{},poles:{}",
        infinite_threshold_result.is_ok(),
        infinite_threshold_poles
    );
    let config = SPSurrogateConfig {
        n_pole_limits: [(8, 0.0), (4, 0.75), (2, 0.9)],
        ff_pole_area_ratio: 0.5,
        n_ff_piers: 2,
    };
    let mut surrogate_shape = u_shape.clone();
    surrogate_shape.generate_surrogate(config).unwrap();
    let surrogate = surrogate_shape.surrogate();
    println!("u.surrogate.poles={}", surrogate.poles.len());
    println!("u.surrogate.ff_poles={}", surrogate.ff_poles().len());
    println!("u.surrogate.piers={}", surrogate.piers.len());
    println!("u.surrogate.hull_area={:.12}", surrogate.convex_hull_area);
    println!(
        "u.surrogate.hull_indices={:?}",
        surrogate.convex_hull_indices
    );
    for (index, pole) in surrogate.poles.iter().enumerate().take(4) {
        println!(
            "u.pole[{index}]={:.12},{:.12},{:.12}",
            pole.center.0, pole.center.1, pole.radius
        );
    }
    for (index, pier) in surrogate.piers.iter().enumerate() {
        println!(
            "u.pier[{index}]={:.12},{:.12},{:.12},{:.12},{:.12}",
            pier.start.0,
            pier.start.1,
            pier.end.0,
            pier.end.1,
            pier.length()
        );
    }

    for (name, points) in [
        (
            "l",
            vec![
                (0.0, 0.0),
                (8.0, 0.0),
                (8.0, 3.0),
                (3.0, 3.0),
                (3.0, 8.0),
                (0.0, 8.0),
            ],
        ),
        (
            "c",
            vec![
                (0.0, 0.0),
                (10.0, 0.0),
                (10.0, 2.0),
                (3.0, 2.0),
                (3.0, 8.0),
                (10.0, 8.0),
                (10.0, 10.0),
                (0.0, 10.0),
            ],
        ),
    ] {
        let shape = polygon(&points);
        let mut shape_with_surrogate = shape.clone();
        shape_with_surrogate.generate_surrogate(config).unwrap();
        let surrogate = shape_with_surrogate.surrogate();
        println!("{name}.surrogate.poles={}", surrogate.poles.len());
        println!("{name}.surrogate.ff_poles={}", surrogate.ff_poles().len());
        println!("{name}.surrogate.piers={}", surrogate.piers.len());
        println!(
            "{name}.surrogate.hull_area={:.12}",
            surrogate.convex_hull_area
        );
        println!(
            "{name}.surrogate.hull_indices={:?}",
            surrogate.convex_hull_indices
        );
        for (index, pole) in surrogate.poles.iter().enumerate().take(4) {
            println!(
                "{name}.pole[{index}]={:.12},{:.12},{:.12}",
                pole.center.0, pole.center.1, pole.radius
            );
        }
        for (index, pier) in surrogate.piers.iter().enumerate() {
            println!(
                "{name}.pier[{index}]={:.12},{:.12},{:.12},{:.12},{:.12}",
                pier.start.0,
                pier.start.1,
                pier.end.0,
                pier.end.1,
                pier.length()
            );
        }
    }
}
