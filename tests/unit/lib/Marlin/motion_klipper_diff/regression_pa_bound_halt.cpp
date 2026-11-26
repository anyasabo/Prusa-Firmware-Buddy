// The pressure advance error bound of the harness must count the jump of the PA speed at a halt.
// Fuzz stop r2 (motion-audit.md): a deceleration with PA ends at a halt. The PA speed
// q = v_e + pa * a_e jumps from pa * a_e to 0 there, and the bound did not include that jump.
#include "compare.hpp"

#include <cmath>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace motion_diff;

TEST_CASE("pa error bound: the PA speed jump at a halt counts", "[motion_diff][regression]") {
    Config config;
    config.steps_per_mm[3] = 550.f;
    config.pressure_advance = 0.175f;
    config.smooth_time = 0.034f;
    const double halt = 0.9;
    const double decel = 0.05; // s, longer than the two windows of st / 2 + h
    const double half_accel = -5953.;
    const double re = 0.0794; // E share of the X+E path
    const double v0 = -2. * half_accel * decel; // reaches 0 at the halt
    const std::vector<Segment> segments {
        { 0., halt - decel, 0., 0., { 0., 0., 0., 0. }, { 0., 0., 0., 0. }, 0 }, // beginning empty move
        { halt - decel, decel, v0, half_accel, { 0., 0., 0., 0. }, { 1., 0., 0., re }, 0 },
        { halt, 1e7, 0., 0., { 0., 0., 0., 0. }, { 0., 0., 0., 0. }, 0 }, // ending empty move
    };
    const PaErrorBound bound(config, segments);

    const double st = config.smooth_time;
    const double h = st / 40.;
    const double a_e = 2. * half_accel * re;
    const double dq = std::abs(config.pressure_advance * a_e); // q drops from pa * a_e to 0
    const double base = h * h * std::abs(a_e) / 8.;
    const double kink = st * dq / 4800.;

    // 18 us before the halt, as in r2
    CHECK(bound.at(halt * 1e6 - 18.) >= Catch::Approx((base + kink) * 550.).epsilon(1e-6));
    // Far from both jumps: only the base term
    CHECK(bound.at((halt - decel / 2.) * 1e6) == Catch::Approx(base * 550.).epsilon(1e-6));
}
