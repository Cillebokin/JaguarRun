use std::time::Duration;

#[test]
fn default_seed_is_absent_but_zero_remains_an_explicit_seed() {
    assert_eq!(sparrow::config::DEFAULT_SPARROW_CONFIG.rng_seed, None);

    let explicit_zero = sparrow::config::SparrowConfig {
        rng_seed: Some(0_usize),
        ..sparrow::config::DEFAULT_SPARROW_CONFIG
    };
    assert_eq!(explicit_zero.rng_seed, Some(0));
}

#[test]
fn sub_millisecond_duration_keeps_nanosecond_precision() {
    let duration = Duration::from_nanos(500_123);
    assert_eq!(duration.as_nanos(), 500_123);
    assert_eq!(
        duration,
        Duration::from_micros(500) + Duration::from_nanos(123)
    );
}
