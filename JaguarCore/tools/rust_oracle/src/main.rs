use jagua_rs::geometry::fail_fast::SPSurrogateConfig;
use jagua_rs::geometry::primitives::{Point, SPolygon};
use jagua_rs::geometry::shape_modification::{offset_shape, ShapeModifyMode};
use geo_buffer::buffer_polygon_rounded;
use geo_types::{LineString, Polygon};

fn polygon(points: &[(f32, f32)]) -> SPolygon {
    SPolygon::new(points.iter().copied().map(Point::from).collect()).unwrap()
}

fn print_polygon(name: &str, shape: &SPolygon) {
    println!("{name}.vertices={}", shape.n_vertices());
    println!("{name}.area={:.12}", shape.area);
    println!("{name}.bbox={:.12},{:.12},{:.12},{:.12}",
             shape.bbox.x_min, shape.bbox.y_min,
             shape.bbox.x_max, shape.bbox.y_max);
    println!("{name}.centroid={:.12},{:.12}", shape.centroid().0, shape.centroid().1);
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

fn main() {
    let concave = polygon(&[
        (0.0, 0.0), (4.0, 0.0), (4.0, 4.0), (2.0, 1.0), (0.0, 4.0),
    ]);
    print_polygon("concave", &concave);
    let deflated = offset_shape(&concave, ShapeModifyMode::Deflate, 0.45);
    println!("concave.deflate045={}", if deflated.is_ok() { "ok" } else { "error" });
    if let Ok(shape) = deflated {
        print_polygon("concave.deflate045.first", &shape);
    }
    print_buffer_components(
        "concave.deflate045",
        &[
            (0.0, 0.0),
            (4.0, 0.0),
            (4.0, 4.0),
            (2.0, 1.0),
            (0.0, 4.0),
        ],
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

    let notch = polygon(&[
        (0.0, 0.0), (10.0, 0.0), (10.0, 10.0), (6.0, 10.0),
        (6.0, 9.9), (4.0, 9.9), (4.0, 10.0), (0.0, 10.0),
    ]);
    print_polygon("notch", &notch);
    let notch_offset = offset_shape(&notch, ShapeModifyMode::Inflate, 0.25);
    println!("notch.inflate025={}", if notch_offset.is_ok() { "ok" } else { "error" });
    if let Ok(shape) = notch_offset {
        print_polygon("notch.inflate025.first", &shape);
    }

    let u_shape = polygon(&[
        (0.0, 0.0), (10.0, 0.0), (10.0, 10.0), (6.0, 10.0),
        (6.0, 4.0), (4.0, 4.0), (4.0, 10.0), (0.0, 10.0),
    ]);
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
    for (index, pole) in surrogate.poles.iter().enumerate().take(4) {
        println!("u.pole[{index}]={:.12},{:.12},{:.12}",
                 pole.center.0, pole.center.1, pole.radius);
    }
    for (index, pier) in surrogate.piers.iter().enumerate() {
        println!("u.pier[{index}]={:.12},{:.12},{:.12},{:.12},{:.12}",
                 pier.start.0, pier.start.1, pier.end.0, pier.end.1, pier.length());
    }

    for (name, points) in [
        ("l", vec![
            (0.0, 0.0), (8.0, 0.0), (8.0, 3.0),
            (3.0, 3.0), (3.0, 8.0), (0.0, 8.0),
        ]),
        ("c", vec![
            (0.0, 0.0), (10.0, 0.0), (10.0, 2.0),
            (3.0, 2.0), (3.0, 8.0), (10.0, 8.0),
            (10.0, 10.0), (0.0, 10.0),
        ]),
    ] {
        let shape = polygon(&points);
        let mut shape_with_surrogate = shape.clone();
        shape_with_surrogate.generate_surrogate(config).unwrap();
        let surrogate = shape_with_surrogate.surrogate();
        println!("{name}.surrogate.poles={}", surrogate.poles.len());
        println!("{name}.surrogate.ff_poles={}", surrogate.ff_poles().len());
        println!("{name}.surrogate.piers={}", surrogate.piers.len());
        println!("{name}.surrogate.hull_area={:.12}", surrogate.convex_hull_area);
        for (index, pole) in surrogate.poles.iter().enumerate().take(4) {
            println!("{name}.pole[{index}]={:.12},{:.12},{:.12}",
                     pole.center.0, pole.center.1, pole.radius);
        }
        for (index, pier) in surrogate.piers.iter().enumerate() {
            println!("{name}.pier[{index}]={:.12},{:.12},{:.12},{:.12},{:.12}",
                     pier.start.0, pier.start.1, pier.end.0, pier.end.1, pier.length());
        }
    }
}
