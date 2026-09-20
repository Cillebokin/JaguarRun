use jagua_rs::entities::Layout;
use jagua_rs::geometry::DTransformation;
use jagua_rs::io::ext_repr::{ExtContainer, ExtItem};
use jagua_rs::io::import::Importer;
use lbf::config::LBFConfig;

struct XorShift64Star { state: u64 }
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

#[test]
fn shared_seed_trajectory_matches_cpp_oracle() -> anyhow::Result<()> {
    let importer = Importer::new(LBFConfig::default().cde_config, None, None);
    let item: ExtItem = serde_json::from_value(serde_json::json!({
        "id": 8801, "orientation": {"rotation": {"mode": "discrete", "angles": [0, 90, 180, 270]}},
        "shape": {"type": "rectangle", "data": {"x_min": 0, "y_min": 0, "width": 2, "height": 2}}
    }))?;
    let item = importer.import_item(&item, 0)?;
    let container: ExtContainer = serde_json::from_value(serde_json::json!({
        "id": 8800, "shape": {"type": "rectangle", "data": {"x_min": 0, "y_min": 0, "width": 52, "height": 26}}
    }))?;
    let mut layout = Layout::new(importer.import_container(&container)?);
    let mut rng = XorShift64Star { state: 0x4A47524312345678 };
    let mut active = Vec::new();
    let mut checksum = 1_469_598_103_934_665_603u64;
    let (mut placements, mut removals, mut restores) = (0usize, 0usize, 0usize);
    for step in 0..256usize {
        let decision = rng.next();
        if active.is_empty() || decision % 3 == 0 {
            let serial = placements; let column = serial % 16; let row = (serial / 16) % 8;
            let rotation = (rng.next() % 4) as usize;
            let key = layout.place_item(&item, DTransformation::new(
                rotation as f32 * std::f32::consts::FRAC_PI_2,
                (1.0 + (column * 2) as f32, 1.0 + (row * 2) as f32)));
            active.push((key, serial, column, row, rotation)); placements += 1;
            for value in [0, step as u64, serial as u64, column as u64, row as u64, rotation as u64, active.len() as u64] { mix(&mut checksum, value); }
        } else if decision % 3 == 1 {
            let selected = (rng.next() % active.len() as u64) as usize;
            let removed_serial = active[selected].1; let (key, _, _, _, _) = active.remove(selected);
            layout.remove_item(key); removals += 1;
            for value in [1, step as u64, selected as u64, removed_serial as u64, active.len() as u64] { mix(&mut checksum, value); }
        } else {
            let snapshot = layout.save(); layout.restore(&snapshot).unwrap(); restores += 1;
            for value in [2, step as u64, active.len() as u64] { mix(&mut checksum, value); }
        }
        assert_eq!(layout.placed_items.len(), active.len());
    }
    for value in [255, 256, active.len() as u64] { mix(&mut checksum, value); }
    for (_, serial, column, row, rotation) in &active { for value in [*serial as u64, *column as u64, *row as u64, *rotation as u64] { mix(&mut checksum, value); } }
    assert_eq!((placements, removals, restores, active.len()), (91, 80, 85, 11));
    assert_eq!(checksum, 0x4bfd29cfe028430b);
    assert_eq!(rng.state, 0xe607235f6e91befa);
    Ok(())
}
