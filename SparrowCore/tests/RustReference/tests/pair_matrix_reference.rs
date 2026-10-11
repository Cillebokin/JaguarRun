mod quantify {
    pub mod tracker {
        #[derive(Debug, Clone, Copy)]
        pub struct CTEntry {
            pub weight: f32,
            pub loss: f32,
        }
    }

    #[path = "../../../../../CodeResource/sparrow/src/quantify/pair_matrix.rs"]
    pub mod pair_matrix;
}

use quantify::pair_matrix::PairMatrix;

#[test]
fn pair_matrix_storage_and_loss_fold_follow_rust_upper_triangle_order() {
    let mut matrix = PairMatrix::new(4);
    let mut ordinal = 0.0_f32;
    for row in 0..4 {
        for col in row..4 {
            ordinal += 1.0;
            matrix[(row, col)].loss = ordinal;
        }
    }
    let physical_order: Vec<f32> = matrix.data.iter().map(|entry| entry.loss).collect();
    assert_eq!(
        physical_order,
        (1..=10).map(|value| value as f32).collect::<Vec<_>>()
    );
    assert!(matrix.data.iter().all(|entry| entry.weight == 1.0));

    let mut accumulation = PairMatrix::new(4);
    accumulation[(0, 2)].loss = 1.0;
    accumulation[(0, 3)].loss = 1.0;
    accumulation[(1, 1)].loss = 16_777_216.0;
    let total = accumulation
        .data
        .iter()
        .fold(0.0_f32, |sum, entry| sum + entry.loss);
    assert_eq!(total.to_bits(), 16_777_218.0_f32.to_bits());
}

#[test]
fn pair_matrix_loss_and_weight_arithmetic_preserve_ieee_values() {
    let mut matrix = PairMatrix::new(2);
    matrix[(0, 0)].loss = f32::INFINITY;
    matrix[(0, 0)].weight = 2.0;
    assert_eq!(matrix[(0, 0)].loss * matrix[(0, 0)].weight, f32::INFINITY);

    matrix[(0, 1)].loss = f32::NAN;
    let total = matrix
        .data
        .iter()
        .fold(0.0_f32, |sum, entry| sum + entry.loss);
    let weighted_total = matrix
        .data
        .iter()
        .fold(0.0_f32, |sum, entry| sum + entry.loss * entry.weight);
    assert!(total.is_nan());
    assert!(weighted_total.is_nan());
}

#[test]
fn pair_matrix_logical_bounds_are_debug_checked_but_release_uses_vec_bounds() {
    let mut matrix = PairMatrix::new(4);
    matrix[(1, 1)].loss = 11.0;

    #[cfg(debug_assertions)]
    {
        let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
            let _ = matrix[(4, 0)].loss;
        }));
        assert!(result.is_err(), "debug builds assert logical pair bounds");
    }

    #[cfg(not(debug_assertions))]
    {
        assert_eq!(matrix[(4, 0)].loss, 11.0);
        let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
            let _ = matrix[(10, 0)].loss;
        }));
        assert!(
            result.is_err(),
            "release still bounds-checks the backing Vec"
        );
    }
}

#[test]
fn pair_matrix_max_size_matches_rust_arithmetic_profile() {
    let result = std::panic::catch_unwind(|| PairMatrix::new(usize::MAX));

    #[cfg(debug_assertions)]
    assert!(
        result.is_err(),
        "debug pair length arithmetic must panic on overflow"
    );

    #[cfg(not(debug_assertions))]
    {
        let matrix = result.expect("release multiplication wraps to an empty matrix");
        assert_eq!(matrix.size, usize::MAX);
        assert!(matrix.data.is_empty());
        let access = std::panic::catch_unwind(|| matrix[(0, 0)]);
        assert!(
            access.is_err(),
            "release still checks the backing Vec length"
        );
    }
}
