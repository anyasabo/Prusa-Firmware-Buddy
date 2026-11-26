// Pressure advance: the steps of the last smooth_time / 2 before a halt must not be lost.
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace motion_diff;

TEST_CASE("pressure advance: the smoothing tail at the end of motion makes its steps", "[motion_diff][regression]") {
    Config config;
    config.pressure_advance = 0.05f;
    config.smooth_time = 0.04f;
    // A deretraction (E only, so no pressure advance, but the smoothing applies) and then a halt.
    // 0.8 mm at 380 steps/mm = 304 steps. Smoothing spreads them to 20 ms after the end of the move.
    const Run r = run_both(config, { { .de = 0.8, .v_start = 0, .v_cruise = 35, .v_end = 0, .accel = 1250 } });
    REQUIRE(r.buddy.finished);
    INFO(describe(r.axis[3]));
    CHECK(r.axis[3].klipper_net == 304);
    CHECK(r.axis[3].buddy_net == 304);
    CHECK(check_axis(r.axis[3], r.buddy.final_count_position[3] - r.buddy.start_count_position[3]) == "");
}
