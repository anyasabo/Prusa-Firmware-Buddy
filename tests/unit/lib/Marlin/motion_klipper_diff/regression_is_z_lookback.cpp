// CoreXY: the Z input shaper must count for the lookback time also when X or Y is shaped.
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace motion_diff;

TEST_CASE("input shaper: the Z shaper counts for the lookback time on CoreXY", "[motion_diff][regression]") {
    Config config;
    config.shaper[0] = input_shaper::AxisConfig { .type = input_shaper::Type::mzv, .frequency = 52.f, .damping_ratio = 0.1f, .vibration_reduction = 20.f };
    // ZVD at 30 Hz reaches about 19 ms back, MZV at 52 Hz about 8 ms. The beginning empty move must
    // be longer than the larger of the two.
    config.shaper[2] = input_shaper::AxisConfig { .type = input_shaper::Type::zvd, .frequency = 30.f, .damping_ratio = 0.1f, .vibration_reduction = 20.f };
    const Run r = run_both(config, { line(20, 10, 0, 200, 4000), { .dz = 0.5, .v_start = 0, .v_cruise = 10, .v_end = 0, .accel = 400 } });
    REQUIRE(r.buddy.finished);
    for (int i = 0; i < 3; ++i) {
        INFO("axis " << i << ": " << describe(r.axis[i]));
        // Before the fix, all steps were late by the difference of the two lookback times.
        CHECK(r.axis[i].max_abs_diff_us() < KLIPPER_MAX_ERROR_US + 1.);
        CHECK(check_axis(r.axis[i], r.buddy.final_count_position[i] - r.buddy.start_count_position[i]) == "");
    }
}
