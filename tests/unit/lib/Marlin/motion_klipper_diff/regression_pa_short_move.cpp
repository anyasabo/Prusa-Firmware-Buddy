// Pressure advance: a short extruder move just before a halt must make its steps.
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

using namespace motion_diff;

TEST_CASE("pressure advance: a short extruder move before a halt makes its steps", "[motion_diff][regression]") {
    Config config;
    config.pressure_advance = 0.115f;
    // Samples every 3.3 ms: the move (5 ms) has one sample, which has zero weight in the filter at
    // first.
    config.smooth_time = 0.132f;
    const double de = GENERATE(-0.0344, 0.0344); // 13 steps at 380 steps/mm
    const Run r = run_both(config, { { .de = de, .v_start = 0, .v_cruise = 120, .v_end = 0, .accel = 5000 } });
    REQUIRE(r.buddy.finished);
    INFO(describe(r.axis[3]));
    const int64_t expected = de > 0 ? 13 : -13;
    CHECK(r.axis[3].klipper_net == expected);
    CHECK(r.axis[3].buddy_net == expected);
    CHECK(check_axis(r.axis[3], r.buddy.final_count_position[3] - r.buddy.start_count_position[3]) == "");
}
