use ordered_float::OrderedFloat;

fn selected_worker(losses: &[(usize, f32)]) -> usize {
    losses
        .iter()
        .min_by_key(|worker| OrderedFloat(worker.1))
        .map(|worker| worker.0)
        .unwrap()
}

#[test]
fn mixed_nan_and_infinite_worker_losses_follow_pinned_ordered_float_minimum() {
    let nan = f32::NAN;

    assert_eq!(
        selected_worker(&[(0, nan), (1, 4.0), (2, 2.0), (3, nan)]),
        2
    );
    assert_eq!(selected_worker(&[(0, 4.0), (1, nan)]), 0);
    assert_eq!(selected_worker(&[(0, nan), (1, nan)]), 0);

    assert_eq!(
        selected_worker(&[(0, nan), (1, f32::INFINITY), (2, nan)]),
        1
    );
    assert_eq!(
        selected_worker(&[(0, f32::INFINITY), (1, nan), (2, nan)]),
        0
    );
    assert_eq!(
        selected_worker(&[(0, nan), (1, f32::NEG_INFINITY), (2, f32::INFINITY)]),
        1
    );
    assert_eq!(
        selected_worker(&[(0, f32::INFINITY), (1, f32::INFINITY), (2, nan)]),
        0
    );
}
