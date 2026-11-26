// The input shaper pulse merge bound of the harness (IsPulseMergeBound). On CoreXY, Buddy merges a
// pulse of one logical axis into a pulse of the other axis that is up to 10 us earlier
// (adjust_input_shaper_pulses_to_match_time_pulses()). The moved pulse is then up to 10 us early,
// which Klipper does not do. Fuzz run 3: X ZVD at 84.5 Hz with almost no damping has its middle
// pulse at -7.3 us. The single pulse of the unshaped Y moves there, and at 354 mm/s on Y, A was
// 0.27 step from its boundary.
#include "compare.hpp"

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
