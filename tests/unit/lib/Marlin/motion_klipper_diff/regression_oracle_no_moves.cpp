// The Klipper oracle with no blocks: fuzz run 3b gave a scenario without blocks. The trapq then has
// no moves and no sentinels, and kl_session_position() read past its move list (ASan
// heap-buffer-overflow in kin_shaper.c calc_position()).
#include "test_support.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace motion_diff;

TEST_CASE("klipper oracle: no blocks stay at the start position", "[motion_diff][regression]") {
    Config config;
    config.shaper[0] = input_shaper::AxisConfig { .type = input_shaper::Type::zvd, .frequency = 85.8f, .damping_ratio = 0.13f, .vibration_reduction = 20.f };
    config.start_steps = { 49260, 5552, 83274 };
    const Run r = run_both(config, {});
    for (int axis = 0; axis < 3; ++axis) {
        CHECK(r.klipper.position_steps(axis, 0.) == Catch::Approx(config.start_steps[axis]).margin(1e-6));
        CHECK(check_axis(r.axis[axis]) == "");
    }
}
