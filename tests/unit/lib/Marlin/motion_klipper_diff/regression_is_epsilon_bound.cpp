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

// B = X - Y almost at rest on a diagonal path: the stepper half acceleration is h = 125 * 0.0007
// = 0.0875 mm/s^2, below the epsilon of 0.1. input_shaper_state_update() then sets it to zero and
// keeps the average speed: up to h * T^2 / 4. If the speed then has the other sign, it sets the
// speed to 0: up to h * T^2. The speed of one segment keeps its sign (the path speed is not
// negative), so a sign flip needs a speed near zero: a start from rest (+0 or -0) or a micro
// segment across a boundary.
constexpr double rx = 0.7075, ry = 0.7068, half_accel = 125., T = 1.;

TEST_CASE("is epsilon bound: a slow-acceleration segment from rest counts h * T^2", "[motion_diff][regression]") {
    Config config;
    config.shaper[0] = zv(50.f);
    config.shaper[1] = zv(50.f);
    const std::vector<Segment> segments { { 0., T, 0., half_accel, { 0., 0., 0., 0. }, { rx, ry, 0., 0. }, 0 } };
    const IsEpsilonBound bound(config, segments, 1);
    const double h = half_accel * (rx - ry);
    CHECK(bound.at(0.5e6) >= Catch::Approx(h * T * T * config.steps_per_mm[1]).epsilon(1e-6));
}

TEST_CASE("is epsilon bound: a slow-acceleration segment away from zero speed counts h * T^2 / 4", "[motion_diff][regression]") {
    Config config;
    config.shaper[0] = zv(50.f);
    config.shaper[1] = zv(50.f);
    // B speed 100 * 0.0007 = 0.07 mm/s and more: no sign change
    const std::vector<Segment> segments { { 0., T, 100., half_accel, { 0., 0., 0., 0. }, { rx, ry, 0., 0. }, 0 } };
    const IsEpsilonBound bound(config, segments, 1);
    const double h = half_accel * (rx - ry);
    const double quarter = h * T * T / 4. * config.steps_per_mm[1];
    CHECK(bound.at(0.5e6) >= Catch::Approx(quarter).epsilon(1e-6));
    CHECK(bound.at(0.5e6) < 1.1 * quarter);
}

TEST_CASE("is epsilon bound: the float epsilon of the firmware counts", "[motion_diff][regression]") {
    // The firmware compares a float with 0.1f = 0.10000000149. A double h just above 0.1 is still
    // zeroed there.
    Config config;
    config.shaper[0] = zv(50.f);
    config.shaper[1] = zv(50.f);
    const double h = 0.1000000005;
    const std::vector<Segment> segments { { 0., T, 100., h / (rx - ry), { 0., 0., 0., 0. }, { rx, ry, 0., 0. }, 0 } };
    const IsEpsilonBound bound(config, segments, 1);
    CHECK(bound.at(0.5e6) >= Catch::Approx(h * T * T / 4. * config.steps_per_mm[1]).epsilon(1e-6));
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

TEST_CASE("is epsilon bound: CoreXY float rounding around the epsilon counts", "[motion_diff][regression]") {
    // B = X - Y: the firmware sums the X and Y terms (about 770 mm/s^2 each) in float and takes the
    // difference. Its rounding is about 1e-4 relative to the result. Here the double h is 0.1000012,
    // above 0.1f. A float32 model of the ZV sums gives 0.0999756, so the firmware zeroes it.
    Config config;
    config.shaper[0] = zv(50.f);
    config.shaper[1] = zv(50.f);
    const double rx2 = 0.540163, ry2 = 0.5400926068590253, half = 1420.61, T2 = 0.5;
    const std::vector<Segment> segments { { 0., T2, 100., half, { 0., 0., 0., 0. }, { rx2, ry2, 0., 0. }, 0 } };
    const IsEpsilonBound bound(config, segments, 1);
    const double h = half * (rx2 - ry2);
    REQUIRE(h > 0.1000001);
    CHECK(bound.at(0.25e6) >= Catch::Approx(h * T2 * T2 / 4. * config.steps_per_mm[1]).epsilon(1e-6));
}
