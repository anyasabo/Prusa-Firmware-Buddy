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

TEST_CASE("pressure advance: sparse steps after the start of the ending move pass the debug check", "[motion_diff][regression]") {
    // From a fuzz input: the last move is very slow, so the E steps of the smoothing tail come more
    // than 65.5 ms after the step event before them. check_step_time() allowed only 65.5 ms after the
    // last processed move when the ending empty move is the last move in the queue.
    Config config;
    config.steps_per_mm[3] = 550.f;
    config.pressure_advance = 0.0944f;
    config.smooth_time = 0.12f;
    config.start_steps = { 60000, -4124, 93169 };
    const Run r = run_both(config,
        {
            { .dx = 27.256, .de = 0.419261, .v_start = 0, .v_cruise = 180.491f, .v_end = 1, .accel = 4646.76f },
            { .dx = -8.31528, .dy = -29.8713, .de = 1.1381, .v_start = 1, .v_cruise = 1, .v_end = 0, .accel = 50 },
        });
    // Before the fix, a debug_assert() in check_step_time() failed (an exception in this test).
    // The E steps themselves also need the fix of anya/step-direction-at-emit to agree with Klipper.
    CHECK(r.buddy.finished);
}
