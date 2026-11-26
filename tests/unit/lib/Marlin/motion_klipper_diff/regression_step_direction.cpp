// The direction of a step is the direction that it was generated with.
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace motion_diff;

TEST_CASE("pressure advance: the last step before a direction change keeps its direction", "[motion_diff][regression]") {
    Config config;
    config.pressure_advance = 0.05f;
    config.smooth_time = 0.04f;
    // The PA position (E + pa * v) rises during the acceleration and falls back during the
    // deceleration: E changes direction twice. The travel after the line keeps the end of the
    // smoothing away from the halt. 0.45 mm at 380 steps/mm = 171 steps.
    const Run r = run_both(config, { line(10, 0, 0.45, 120, 4000), line(10, 10, 0, 200, 4000) });
    REQUIRE(r.buddy.finished);
    INFO(describe(r.axis[3]));
    CHECK(r.axis[3].klipper_net == 171);
    CHECK(r.axis[3].buddy_net == 171);
    CHECK(r.axis[3].unmatched_buddy == 0);
    CHECK(r.axis[3].unmatched_klipper == 0);
    CHECK(check_axis(r.axis[3], r.buddy.final_count_position[3] - r.buddy.start_count_position[3]) == "");
}
