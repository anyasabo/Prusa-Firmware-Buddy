// The input shaper epsilon bound of the harness (IsEpsilonBound): it must cover a micro segment
// whose speed crosses zero, and it must not grow with the length of a cruise.
#include "compare.hpp"

#include <feature/input_shaper/input_shaper_config.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace motion_diff;

namespace {

input_shaper::AxisConfig zv(float frequency) {
    return { .type = input_shaper::Type::zv, .frequency = frequency, .damping_ratio = 0.1f, .vibration_reduction = 20.f };
}

} // namespace

TEST_CASE("is epsilon bound: a zero crossing of the speed counts in full", "[motion_diff][regression]") {
    // B = X - Y almost at rest: its half acceleration is 125 * 0.0007 = 0.0875 mm/s^2, below the epsilon
    // of 0.1. If the speed crosses zero in the micro segment, Buddy sets the speed to 0, and the
    // error is up to |half_accel| * T^2 (input_shaper.cpp, input_shaper_state_update()).
    Config config;
    config.shaper[0] = zv(50.f);
    config.shaper[1] = zv(50.f);
    const double rx = 0.7075, ry = 0.7068;
    const double half_accel = 125.; // along the path, mm/s^2 / 2
    const double T = 1.;
    const std::vector<Segment> segments { { 0., T, 0., half_accel, { 0., 0., 0., 0. }, { rx, ry, 0., 0. }, 0 } };
    const IsEpsilonBound bound(config, segments, 1);
    const double h_b = half_accel * (rx - ry);
    CHECK(bound.at(0.5e6) >= Catch::Approx(h_b * T * T * config.steps_per_mm[1]).epsilon(1e-6));
}

TEST_CASE("is epsilon bound: a long cruise adds no error inside it", "[motion_diff][regression]") {
    // A Z cruise of 2 s: the micro segments inside it have no acceleration, so the zeroing changes
    // nothing. Only micro segments across a segment boundary (at most the pulse span long) count.
    Config config;
    config.shaper[2] = zv(40.f);
    const std::vector<Segment> segments {
        { 0., 2., 5., 0., { 0., 0., 0., 0. }, { 0., 0., 1., 0. }, 0 },
    };
    const IsEpsilonBound bound(config, segments, 2);
    CHECK(bound.at(1e6) < 0.5); // Z steps; the old bound gave 80
}
