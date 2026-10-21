#[path = "../src/compression_minimum_width_trace.rs"]
mod compression_minimum_width_trace;

const REFERENCE: &str = include_str!("../../Fixtures/CompressionMinimumWidthRustReference.tsv");
const INFINITE_MINIMUM_WIDTH_REFERENCE: &str =
    include_str!("../../Fixtures/CompressionMinimumWidthInfinityRustReference.tsv");

#[test]
fn compression_minimum_width_and_threshold_behavior_match_locked_rust_trace() {
    let actual = compression_minimum_width_trace::compression_minimum_width_trace_tsv()
        .expect("run compression minimum-width boundary oracle");
    let expected = REFERENCE.replace("\r\n", "\n");
    assert_eq!(
        actual, expected,
        "Rust compression minimum-width boundary trace changed"
    );
}

#[test]
fn compression_stops_before_progress_when_all_discrete_rotations_are_too_tall() {
    let actual = compression_minimum_width_trace::compression_infinite_minimum_width_trace()
        .expect("run compression infinite minimum-width oracle");
    let expected = INFINITE_MINIMUM_WIDTH_REFERENCE.replace("\r\n", "\n");
    assert_eq!(
        actual, expected,
        "Rust Compression +Inf minimum-width early-stop trace changed"
    );
}

#[test]
fn failure_based_decay_zero_and_one_reach_the_compression_phase() {
    let actual = compression_minimum_width_trace::failure_decay_endpoint_attempt_counts()
        .expect("run failure-decay endpoint oracle");
    assert_eq!(actual, vec![(0.0, 1), (1.0, 1)]);
}

#[test]
fn shrink_range_zero_and_one_endpoints_follow_compression_width_guards() {
    let actual = compression_minimum_width_trace::shrink_range_endpoint_attempt_counts()
        .expect("run compression shrink-range endpoint oracle");
    assert_eq!(actual, vec![(0.0, 0.0, 0), (0.01, 0.0, 1), (1.0, 1.0, 0)]);
}

#[test]
fn direct_time_based_zero_duration_skips_compression_attempts() {
    let actual = compression_minimum_width_trace::zero_duration_time_based_state()
        .expect("run direct zero-duration TimeBased compression oracle");
    assert_eq!(actual, (0x4080_0000, 0x4080_0000, 0));
}

#[test]
fn time_based_threshold_rounding_permits_the_attempt_at_shrink_min() {
    let actual = compression_minimum_width_trace::time_based_threshold_trace()
        .expect("run TimeBased shrink-min threshold oracle");
    assert_eq!(
        actual,
        (vec![0x3e00_0000, 0x3d80_0000], 0x4052_0000, 4, false)
    );
}

#[test]
fn time_based_duration_rounding_matches_rust_seconds_conversion() {
    let one_nanosecond_seconds = std::time::Duration::from_nanos(1).as_secs_f32();
    let two_nanosecond_seconds = std::time::Duration::from_nanos(2).as_secs_f32();
    assert_eq!(one_nanosecond_seconds.to_bits(), 0x3089_705f);
    assert_eq!(two_nanosecond_seconds.to_bits(), 0x3109_705f);
    assert_eq!(
        (one_nanosecond_seconds / two_nanosecond_seconds).to_bits(),
        0x3f00_0000
    );
    assert_eq!(
        (0.125_f32 + 0.5_f32 * (0.0625_f32 - 0.125_f32)).to_bits(),
        0x3dc0_0000
    );
    let three_nanosecond_seconds = std::time::Duration::from_nanos(3).as_secs_f32();
    let one_over_three_ratio = one_nanosecond_seconds / three_nanosecond_seconds;
    assert_eq!(three_nanosecond_seconds.to_bits(), 0x314e_288f);
    assert_eq!(one_over_three_ratio.to_bits(), 0x3eaa_aaaa);
    assert_eq!(
        (0.125_f32 + one_over_three_ratio * (0.0625_f32 - 0.125_f32)).to_bits(),
        0x3dd5_5556
    );
    let four_nanosecond_seconds = std::time::Duration::from_nanos(4).as_secs_f32();
    let one_over_four_ratio = one_nanosecond_seconds / four_nanosecond_seconds;
    assert_eq!(four_nanosecond_seconds.to_bits(), 0x3189_705f);
    assert_eq!(one_over_four_ratio.to_bits(), 0x3e80_0000);
    assert_eq!(
        (0.125_f32 + one_over_four_ratio * (0.0625_f32 - 0.125_f32)).to_bits(),
        0x3de0_0000
    );
    let five_nanosecond_seconds = std::time::Duration::from_nanos(5).as_secs_f32();
    let one_over_five_ratio = one_nanosecond_seconds / five_nanosecond_seconds;
    assert_eq!(five_nanosecond_seconds.to_bits(), 0x31ab_cc77);
    assert_eq!(one_over_five_ratio.to_bits(), 0x3e4c_cccd);
    assert_eq!(
        (0.125_f32 + one_over_five_ratio * (0.0625_f32 - 0.125_f32)).to_bits(),
        0x3de6_6666
    );
    let six_nanosecond_seconds = std::time::Duration::from_nanos(6).as_secs_f32();
    let one_over_six_ratio = one_nanosecond_seconds / six_nanosecond_seconds;
    assert_eq!(six_nanosecond_seconds.to_bits(), 0x31ce_288f);
    assert_eq!(one_over_six_ratio.to_bits(), 0x3e2a_aaaa);
    assert_eq!(
        (0.125_f32 + one_over_six_ratio * (0.0625_f32 - 0.125_f32)).to_bits(),
        0x3dea_aaab
    );
    let seven_nanosecond_seconds = std::time::Duration::from_nanos(7).as_secs_f32();
    let one_over_seven_ratio = one_nanosecond_seconds / seven_nanosecond_seconds;
    assert_eq!(seven_nanosecond_seconds.to_bits(), 0x31f0_84a7);
    assert_eq!(one_over_seven_ratio.to_bits(), 0x3e12_4924);
    assert_eq!(
        (0.125_f32 + one_over_seven_ratio * (0.0625_f32 - 0.125_f32)).to_bits(),
        0x3ded_b6dc
    );
    let eight_nanosecond_seconds = std::time::Duration::from_nanos(8).as_secs_f32();
    let one_over_eight_ratio = one_nanosecond_seconds / eight_nanosecond_seconds;
    assert_eq!(eight_nanosecond_seconds.to_bits(), 0x3209_705f);
    assert_eq!(one_over_eight_ratio.to_bits(), 0x3e00_0000);
    assert_eq!(
        (0.125_f32 + one_over_eight_ratio * (0.0625_f32 - 0.125_f32)).to_bits(),
        0x3df0_0000
    );
    let nine_nanosecond_seconds = std::time::Duration::from_nanos(9).as_secs_f32();
    let one_over_nine_ratio = one_nanosecond_seconds / nine_nanosecond_seconds;
    assert_eq!(nine_nanosecond_seconds.to_bits(), 0x321a_9e6b);
    assert_eq!(one_over_nine_ratio.to_bits(), 0x3de3_8e39);
    assert_eq!(
        (0.125_f32 + one_over_nine_ratio * (0.0625_f32 - 0.125_f32)).to_bits(),
        0x3df1_c71c
    );
    let ten_nanosecond_seconds = std::time::Duration::from_nanos(10).as_secs_f32();
    let one_over_ten_ratio = one_nanosecond_seconds / ten_nanosecond_seconds;
    assert_eq!(ten_nanosecond_seconds.to_bits(), 0x322b_cc77);
    assert_eq!(one_over_ten_ratio.to_bits(), 0x3dcc_cccd);
    assert_eq!(
        (0.125_f32 + one_over_ten_ratio * (0.0625_f32 - 0.125_f32)).to_bits(),
        0x3df3_3333
    );
    let eleven_nanosecond_seconds = std::time::Duration::from_nanos(11).as_secs_f32();
    let one_over_eleven_ratio = one_nanosecond_seconds / eleven_nanosecond_seconds;
    assert_eq!(eleven_nanosecond_seconds.to_bits(), 0x323c_fa83);
    assert_eq!(one_over_eleven_ratio.to_bits(), 0x3dba_2e8b);
    assert_eq!(
        (0.125_f32 + one_over_eleven_ratio * (0.0625_f32 - 0.125_f32)).to_bits(),
        0x3df4_5d17
    );
    let twelve_nanosecond_seconds = std::time::Duration::from_nanos(12).as_secs_f32();
    let one_over_twelve_ratio = one_nanosecond_seconds / twelve_nanosecond_seconds;
    assert_eq!(twelve_nanosecond_seconds.to_bits(), 0x324e_288f);
    assert_eq!(one_over_twelve_ratio.to_bits(), 0x3daa_aaaa);
    assert_eq!(
        (0.125_f32 + one_over_twelve_ratio * (0.0625_f32 - 0.125_f32)).to_bits(),
        0x3df5_5555
    );
    let thirteen_nanosecond_seconds = std::time::Duration::from_nanos(13).as_secs_f32();
    let one_over_thirteen_ratio = one_nanosecond_seconds / thirteen_nanosecond_seconds;
    assert_eq!(thirteen_nanosecond_seconds.to_bits(), 0x325f_569b);
    assert_eq!(one_over_thirteen_ratio.to_bits(), 0x3d9d_89d8);
    assert_eq!(
        (0.125_f32 + one_over_thirteen_ratio * (0.0625_f32 - 0.125_f32)).to_bits(),
        0x3df6_2762
    );
    let fourteen_nanosecond_seconds = std::time::Duration::from_nanos(14).as_secs_f32();
    let one_over_fourteen_ratio = one_nanosecond_seconds / fourteen_nanosecond_seconds;
    assert_eq!(fourteen_nanosecond_seconds.to_bits(), 0x3270_84a7);
    assert_eq!(one_over_fourteen_ratio.to_bits(), 0x3d92_4924);
    assert_eq!(
        (0.125_f32 + one_over_fourteen_ratio * (0.0625_f32 - 0.125_f32)).to_bits(),
        0x3df6_db6e
    );
    let fifteen_nanosecond_seconds = std::time::Duration::from_nanos(15).as_secs_f32();
    let one_over_fifteen_ratio = one_nanosecond_seconds / fifteen_nanosecond_seconds;
    assert_eq!(fifteen_nanosecond_seconds.to_bits(), 0x3280_d959);
    assert_eq!(one_over_fifteen_ratio.to_bits(), 0x3d88_8889);
    assert_eq!(
        (0.125_f32 + one_over_fifteen_ratio * (0.0625_f32 - 0.125_f32)).to_bits(),
        0x3df7_7777
    );

    let time_limit_seconds = std::time::Duration::new(2, 119).as_secs_f32();
    let elapsed_seconds = std::time::Duration::new(2, 120).as_secs_f32();
    assert_eq!(time_limit_seconds.to_bits(), 0x4000_0000);
    assert_eq!(elapsed_seconds.to_bits(), 0x4000_0001);
    let ratio = elapsed_seconds / time_limit_seconds;
    assert_eq!(ratio.to_bits(), 0x3f80_0001);
    let step = 0.125_f32 + ratio * (0.0625_f32 - 0.125_f32);
    assert_eq!(step.to_bits(), 0x3d7f_fffe);
    let four_second_time_limit = std::time::Duration::new(4, 238).as_secs_f32();
    let four_second_elapsed = std::time::Duration::new(4, 239).as_secs_f32();
    assert_eq!(four_second_time_limit.to_bits(), 0x4080_0000);
    assert_eq!(four_second_elapsed.to_bits(), 0x4080_0001);
    let four_second_ratio = four_second_elapsed / four_second_time_limit;
    assert_eq!(four_second_ratio.to_bits(), 0x3f80_0001);
    let four_second_step = 0.125_f32 + four_second_ratio * (0.0625_f32 - 0.125_f32);
    assert_eq!(four_second_step.to_bits(), 0x3d7f_fffe);
    let half_second_time_limit = std::time::Duration::new(0, 500_000_016).as_secs_f32();
    let half_second_elapsed = std::time::Duration::new(0, 500_000_017).as_secs_f32();
    assert_eq!(half_second_time_limit.to_bits(), 0x3f00_0000);
    assert_eq!(half_second_elapsed.to_bits(), 0x3f00_0001);
    let half_second_ratio = half_second_elapsed / half_second_time_limit;
    assert_eq!(half_second_ratio.to_bits(), 0x3f80_0001);
    let half_second_step = 0.125_f32 + half_second_ratio * (0.0625_f32 - 0.125_f32);
    assert_eq!(half_second_step.to_bits(), 0x3d7f_fffe);

    let actual = compression_minimum_width_trace::time_based_duration_traces()
        .expect("run TimeBased duration conversion oracle");
    assert_eq!(
        actual,
        vec![
            (
                "elapsed_seconds_rounding_before_limit".to_owned(),
                60_000_000_000,
                0x3e00_0000,
                0x3d80_0000,
                10_000_000_000,
                69_999_999_999,
                1,
                0x3d80_0000,
                0x4070_0000,
                2,
                false,
            ),
            (
                "time_limit_seconds_rounding".to_owned(),
                59_999_999_999,
                0x3f4c_cccd,
                0x0000_0000,
                10_000_000_000,
                40_000_000_000,
                1,
                0x3ecc_cccd,
                0x4080_0000,
                2,
                false,
            ),
            (
                "elapsed_seconds_rounding_above_limit".to_owned(),
                60_000_000_000,
                0x3e00_0000,
                0x3d80_0000,
                10_000_000_000,
                70_000_002_000,
                0,
                0x0000_0000,
                0x4080_0000,
                2,
                false,
            ),
            (
                "time_limit_seconds_rounding_above_elapsed".to_owned(),
                60_000_002_000,
                0x3f80_0000,
                0x3d80_0000,
                10_000_000_000,
                70_000_000_000,
                1,
                0x3d80_0008,
                0x406f_ffff,
                2,
                false,
            ),
            (
                "time_limit_seconds_rounding_equal_elapsed".to_owned(),
                59_999_999_999,
                0x3e00_0000,
                0x3d80_0000,
                10_000_000_000,
                70_000_000_000,
                1,
                0x3d80_0000,
                0x4070_0000,
                2,
                false,
            ),
            (
                "maximum_time_limit_one_f32_ulp_before_deadline".to_owned(),
                9_223_372_036_854_775_807,
                0x3e00_0000,
                0x3d80_0000,
                0,
                9_223_371_012_854_775_807,
                1,
                0x3d80_0001,
                0x4070_0000,
                2,
                false,
            ),
            (
                "one_nanosecond_deadline_keeps_f32_seconds_nonzero".to_owned(),
                1,
                0x3e00_0000,
                0x3d80_0000,
                10_000_000_000,
                10_000_000_001,
                1,
                0x3d80_0000,
                0x4070_0000,
                2,
                false,
            ),
            (
                "one_nanosecond_at_start_uses_maximum_step".to_owned(),
                1,
                0x3e00_0000,
                0x3d80_0000,
                10_000_000_000,
                10_000_000_000,
                1,
                0x3e00_0000,
                0x4060_0000,
                2,
                false,
            ),
            (
                "two_nanosecond_half_elapsed_uses_midpoint_step".to_owned(),
                2,
                0x3e00_0000,
                0x3d80_0000,
                10_000_000_000,
                10_000_000_001,
                1,
                0x3dc0_0000,
                0x4068_0000,
                2,
                false,
            ),
            (
                "three_nanosecond_limit_one_nanosecond_elapsed_uses_rounded_third_step".to_owned(),
                3,
                0x3e00_0000,
                0x3d80_0000,
                10_000_000_000,
                10_000_000_001,
                1,
                0x3dd5_5556,
                0x4065_5555,
                2,
                false,
            ),
            (
                "four_nanosecond_limit_one_nanosecond_elapsed_uses_quarter_step".to_owned(),
                4,
                0x3e00_0000,
                0x3d80_0000,
                10_000_000_000,
                10_000_000_001,
                1,
                0x3de0_0000,
                0x4064_0000,
                2,
                false,
            ),
            (
                "five_nanosecond_limit_one_nanosecond_elapsed_uses_fifth_step".to_owned(),
                5,
                0x3e00_0000,
                0x3d80_0000,
                10_000_000_000,
                10_000_000_001,
                1,
                0x3de6_6666,
                0x4063_3333,
                2,
                false,
            ),
            (
                "six_nanosecond_limit_one_nanosecond_elapsed_uses_sixth_step".to_owned(),
                6,
                0x3e00_0000,
                0x3d80_0000,
                10_000_000_000,
                10_000_000_001,
                1,
                0x3dea_aaab,
                0x4062_aaab,
                2,
                false,
            ),
            (
                "seven_nanosecond_limit_one_nanosecond_elapsed_uses_seventh_step".to_owned(),
                7,
                0x3e00_0000,
                0x3d80_0000,
                10_000_000_000,
                10_000_000_001,
                1,
                0x3ded_b6dc,
                0x4062_4924,
                2,
                false,
            ),
            (
                "eight_nanosecond_limit_one_nanosecond_elapsed_uses_eighth_step".to_owned(),
                8,
                0x3e00_0000,
                0x3d80_0000,
                10_000_000_000,
                10_000_000_001,
                1,
                0x3df0_0000,
                0x4062_0000,
                2,
                false,
            ),
            (
                "nine_nanosecond_limit_one_nanosecond_elapsed_uses_ninth_step".to_owned(),
                9,
                0x3e00_0000,
                0x3d80_0000,
                10_000_000_000,
                10_000_000_001,
                1,
                0x3df1_c71c,
                0x4061_c71c,
                2,
                false,
            ),
            (
                "ten_nanosecond_limit_one_nanosecond_elapsed_uses_tenth_step".to_owned(),
                10,
                0x3e00_0000,
                0x3d80_0000,
                10_000_000_000,
                10_000_000_001,
                1,
                0x3df3_3333,
                0x4061_999a,
                2,
                false,
            ),
            (
                "eleven_nanosecond_limit_one_nanosecond_elapsed_uses_eleventh_step".to_owned(),
                11,
                0x3e00_0000,
                0x3d80_0000,
                10_000_000_000,
                10_000_000_001,
                1,
                0x3df4_5d17,
                0x4061_745d,
                2,
                false,
            ),
            (
                "twelve_nanosecond_limit_one_nanosecond_elapsed_uses_twelfth_step".to_owned(),
                12,
                0x3e00_0000,
                0x3d80_0000,
                10_000_000_000,
                10_000_000_001,
                1,
                0x3df5_5555,
                0x4061_5555,
                2,
                false,
            ),
            (
                "thirteen_nanosecond_limit_one_nanosecond_elapsed_uses_thirteenth_step".to_owned(),
                13,
                0x3e00_0000,
                0x3d80_0000,
                10_000_000_000,
                10_000_000_001,
                1,
                0x3df6_2762,
                0x4061_3b14,
                2,
                false,
            ),
            (
                "fourteen_nanosecond_limit_one_nanosecond_elapsed_uses_fourteenth_step".to_owned(),
                14,
                0x3e00_0000,
                0x3d80_0000,
                10_000_000_000,
                10_000_000_001,
                1,
                0x3df6_db6e,
                0x4061_2492,
                2,
                false,
            ),
            (
                "fifteen_nanosecond_limit_one_nanosecond_elapsed_uses_fifteenth_step".to_owned(),
                15,
                0x3e00_0000,
                0x3d80_0000,
                10_000_000_000,
                10_000_000_001,
                1,
                0x3df7_7777,
                0x4061_1111,
                2,
                false,
            ),
            (
                "equal_range_above_deadline".to_owned(),
                30_000_000_000,
                0x3d80_0000,
                0x3d80_0000,
                10_000_000_000,
                70_000_000_000,
                1,
                0x3d80_0000,
                0x4070_0000,
                2,
                false,
            ),
            (
                "unit_equal_range_stops_before_zero_width".to_owned(),
                1_000_000_000,
                0x3f80_0000,
                0x3f80_0000,
                10_000_000_000,
                10_000_000_000,
                0,
                0,
                0x4080_0000,
                2,
                false,
            ),
            (
                "zero_equal_range_stops_on_unchanged_width".to_owned(),
                1_000_000_000,
                0,
                0,
                10_000_000_000,
                10_000_000_000,
                0,
                0,
                0x4080_0000,
                2,
                false,
            ),
            (
                "full_range_midpoint_uses_half_step".to_owned(),
                1_000_000_000,
                0x3f80_0000,
                0,
                10_000_000_000,
                10_500_000_000,
                1,
                0x3f00_0000,
                0x4080_0000,
                2,
                false,
            ),
            (
                "unequal_range_twice_deadline_stops".to_owned(),
                30_000_000_000,
                0x3e00_0000,
                0x3d80_0000,
                10_000_000_000,
                70_000_000_000,
                0,
                0,
                0x4080_0000,
                2,
                false,
            ),
            (
                "zero_min_at_deadline_stops_on_unchanged_width".to_owned(),
                30_000_000_000,
                0x3e00_0000,
                0,
                10_000_000_000,
                40_000_000_000,
                0,
                0,
                0x4080_0000,
                2,
                false,
            ),
            (
                "one_second_fraction_rounds_to_min_attempt".to_owned(),
                1_000_000_032,
                0x3e00_0000,
                0x3d80_0000,
                10_000_000_000,
                11_000_000_033,
                1,
                0x3d80_0000,
                0x4070_0000,
                2,
                false,
            ),
            (
                "one_second_nanosecond_rounding_crosses_min".to_owned(),
                1_000_000_059,
                0x3e00_0000,
                0x3d80_0000,
                10_000_000_000,
                11_000_000_060,
                0,
                0,
                0x4080_0000,
                2,
                false,
            ),
            (
                "two_second_nanosecond_rounding_crosses_min".to_owned(),
                2_000_000_119,
                0x3e00_0000,
                0x3d80_0000,
                10_000_000_000,
                12_000_000_120,
                0,
                0,
                0x4080_0000,
                2,
                false,
            ),
            (
                "four_second_nanosecond_rounding_crosses_min".to_owned(),
                4_000_000_238,
                0x3e00_0000,
                0x3d80_0000,
                10_000_000_000,
                14_000_000_239,
                0,
                0,
                0x4080_0000,
                2,
                false,
            ),
            (
                "half_second_nanosecond_rounding_crosses_min".to_owned(),
                500_000_016,
                0x3e00_0000,
                0x3d80_0000,
                10_000_000_000,
                10_500_000_017,
                0,
                0,
                0x4080_0000,
                2,
                false,
            ),
        ]
    );
}
