// The input shaper pulse merge bound of the harness (IsPulseMergeBound). On CoreXY, Buddy merges a
// pulse of one logical axis into a pulse of the other axis that is up to 10 us earlier
// (adjust_input_shaper_pulses_to_match_time_pulses()). The moved pulse is then up to 10 us early,
// which Klipper does not do. Fuzz run 3: X ZVD at 84.5 Hz with almost no damping has its middle
// pulse at -7.3 us. The single pulse of the unshaped Y moves there, and at 354 mm/s on Y, A was
// 0.27 step from its boundary.
#include "compare.hpp"
#include "test_support.hpp"

#include <feature/input_shaper/input_shaper.hpp>

#include <feature/input_shaper/input_shaper_config.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace motion_diff;

TEST_CASE("is pulse merge bound: an unshaped Y pulse moved to the X middle pulse", "[motion_diff][regression]") {
    Config config;
    const float f = 84.4976f, d = 0.000785655f;
    config.shaper[0] = input_shaper::AxisConfig { .type = input_shaper::Type::zvd, .frequency = f, .damping_ratio = d, .vibration_reduction = 20.f };
    // A Y cruise at 354 mm/s
    const double v = 354.;
    const std::vector<Segment> segments { { 0., 0.1, v, 0., { 0., 0., 0., 0. }, { 0., 1., 0., 0. }, 0 } };
    const IsPulseMergeBound bound(config, segments, 0);

    // ZVD: amplitudes 1, 2K, K^2 at 0, td/2, td. After centering, the middle pulse is at
    // -td (1 - K) / (2 (1 + K)).
    const double df = std::sqrt(1. - double(d) * d);
    const double td = 1. / (double(f) * df);
    const double K = std::exp(-double(d) * M_PI / df);
    const double shift = td * (1. - K) / (2. * (1. + K));
    REQUIRE(shift < 10e-6);
    CHECK(bound.at(0.05e6) == Catch::Approx(shift * v * config.steps_per_mm[0]).epsilon(1e-3));
    // Far after the motion: no speed, no error
    CHECK(bound.at(1e6) == 0.);
}

TEST_CASE("is pulse merge bound: no merge, no tolerance", "[motion_diff][regression]") {
    Config config; // the middle pulse at 40 Hz is 15 us from 0: no merge
    config.shaper[0] = input_shaper::AxisConfig { .type = input_shaper::Type::zvd, .frequency = 40.f, .damping_ratio = 0.000785655f, .vibration_reduction = 20.f };
    const std::vector<Segment> segments { { 0., 0.1, 354., 0., { 0., 0., 0., 0. }, { 0., 1., 0., 0. }, 0 } };
    const IsPulseMergeBound bound(config, segments, 0);
    CHECK(bound.at(0.05e6) == 0.);
}

TEST_CASE("is pulse merge bound: no tolerance after the moved pulse leaves the motion", "[motion_diff][regression]") {
    // The moved Y pulse is 7.3 us from its kept pulse. Its error ends about 7 us after the end of
    // the Y motion, not a pulse span (12 ms) later.
    Config config;
    config.shaper[0] = input_shaper::AxisConfig { .type = input_shaper::Type::zvd, .frequency = 84.4976f, .damping_ratio = 0.000785655f, .vibration_reduction = 20.f };
    const std::vector<Segment> segments { { 0., 0.1, 354., 0., { 0., 0., 0., 0. }, { 0., 1., 0., 0. }, 0 } };
    const IsPulseMergeBound bound(config, segments, 0);
    CHECK(bound.at(0.05e6) > 0.2);
    CHECK(bound.at(0.1e6 + 100.) == 0.);
    CHECK(bound.at(0.1e6 + 5000.) == 0.);
}

TEST_CASE("is pulse merge bound: the harness grid is the firmware grid", "[motion_diff][regression]") {
    using T = input_shaper::Type;
    const auto cfg = [](T type, float f, float d) { return input_shaper::AxisConfig { .type = type, .frequency = f, .damping_ratio = d, .vibration_reduction = 20.f }; };
    const std::vector<std::pair<std::optional<input_shaper::AxisConfig>, std::optional<input_shaper::AxisConfig>>> cases {
        { cfg(T::zvd, 84.4976f, 0.000785655f), std::nullopt }, // the fuzz run 3 stop
        { cfg(T::mzv, 47.f, 0.1f), cfg(T::ei, 43.f, 0.1f) },
        { cfg(T::zvd, 60.f, 0.f), cfg(T::zvd, 60.f, 0.f) },
        { cfg(T::ei_3hump, 35.f, 0.05f), cfg(T::zv, 80.f, 0.001f) },
        { std::nullopt, cfg(T::ei, 70.f, 0.f) },
    };
    for (const auto &[x, y] : cases) {
        Config config;
        config.shaper[0] = x;
        config.shaper[1] = y;
        (void)run_buddy(config, {}); // input_shaper::init() makes the firmware grid
        const PulseMerge merge = merge_pulses(config);
        const input_shaper_pulses_t &fx = InputShaper::logical_axis_pulses[X_AXIS];
        const input_shaper_pulses_t &fy = InputShaper::logical_axis_pulses[Y_AXIS];
        REQUIRE(fx.num_pulses == int(merge.grid.size()));
        REQUIRE(fy.num_pulses == int(merge.grid.size()));
        for (size_t i = 0; i < merge.grid.size(); ++i) {
            CHECK(fx.pulses[i].t.to_seconds() == Catch::Approx(merge.grid[i].t).margin(1e-7));
            CHECK(fy.pulses[i].t.to_seconds() == Catch::Approx(merge.grid[i].t).margin(1e-7));
            CHECK(fx.pulses[i].a == Catch::Approx(merge.grid[i].a[0]).margin(1e-6));
            CHECK(fy.pulses[i].a == Catch::Approx(merge.grid[i].a[1]).margin(1e-6));
        }
    }
}

TEST_CASE("is pulse merge bound: the fuzz run 3 motion agrees on A and B", "[motion_diff][regression]") {
    // The first two blocks of the fuzz run 3 stop (fuzz-inputs/crash-bea89bc0 in the notes), without
    // pressure advance. Without IsPulseMergeBound, A was 0.268 and B 0.262 step from a boundary.
    Config config;
    config.steps_per_mm = { 101.587f, 101.587f, 800.f, 380.f };
    config.start_steps = { 53911, 12589, 32994 };
    config.shaper[0] = input_shaper::AxisConfig { .type = input_shaper::Type::zvd, .frequency = 84.4976f, .damping_ratio = 0.000785655f, .vibration_reduction = 20.f };
    const std::vector<Block> blocks {
        { .dx = -11.8008, .dy = 22.9868, .v_start = 0, .v_cruise = 398.514f, .v_end = 1.00609f, .accel = 12000 },
        { .dx = 14.1034, .v_start = 1.00609f, .v_cruise = 1.00609f, .v_end = 0, .accel = 8971.66f },
    };
    const Run r = run_both(config, blocks);
    CHECK(check_axis(r.axis[0]) == "");
    CHECK(check_axis(r.axis[1]) == "");
}
