// Tests of the real CoreXY homing refinement (homing_corexy.cpp) as a CORE One L, against the fake
// world of fake_world.cpp. Tests tagged [!shouldfail][known_bug] show a defect that this branch does
// not fix. The comment above each test gives the defect.
#include "fake_world.hpp"
#include "test_hooks.hpp"

#include <module/prusa/homing_corexy.hpp>
#include <config_store/store_instance.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <random>

using fake::world;

namespace {

constexpr float fr = XY_HOMING_MEASURE_FR;

/// A sensitivity calibration as corexy_sens_calibrate() stores it
void store_sensitivity() {
    config_store().corexy_home_tmc_sens.set({ .feedrate = XY_HOMING_MEASURE_FR, .sensitivity = XY_HOMING_MEASURE_SENS_MIN, .current = XY_HOMING_MEASURE_CURRENT, .score = 1.f });
}

/// Machine Y at each homeaxis(X_AXIS) call
std::vector<float> y_at_x_homes() {
    std::vector<float> ys;
    for (const auto &h : world.homes) {
        if (h.axis == X_AXIS) {
            ys.push_back(h.y_mm);
        }
    }
    return ys;
}

} // namespace

// H1: G28 moves Y to HOMING_PREEMPTIVE_MOVE_Y (10 mm) before it homes X, to keep the carriage away
// from the lever of the vent grille (G28.cpp, commit 84fe747bc). The lever is at Y -7
// (automatic_chamber_vents.cpp, Y_LEVER of the CORE One L). corexy_rehome_xy() homes Y and then X
// without this move, so X homes at Y -6 (Y_MIN_POS + the 2 mm backoff). The refinement retries
// of G28 and the calibration call corexy_rehome_xy().
TEST_CASE("corexy_rehome_xy homes X at the Y of G28", "[homing_corexy][H1][!shouldfail][known_bug]") {
    SECTION("direct call") {
        fake::reset(150, 150);
        REQUIRE(corexy_rehome_xy(fr));
    }
    SECTION("sensitivity calibration, which rehomes for each sensitivity") {
        fake::reset(150, 150);
        REQUIRE(corexy_rehome_xy(fr));
        world.homes.clear();
        REQUIRE(corexy_sens_calibrate(fr));
    }
    REQUIRE(world.wall_crashes == 0);
    REQUIRE(world.homes.size() >= 2);
    CHECK(world.homes.front().axis == Y_AXIS); // HOME_Y_BEFORE_X
    for (const float y : y_at_x_homes()) {
        CHECK(std::abs(y - HOMING_PREEMPTIVE_MOVE_Y) < 1e-3f);
    }
}

// O2: measure_phase_cycles() compares the pairs only when idx >= measure_probe_n, so a measurement
// with no error does measure_probe_n + 1 bump pairs. It averages only measure_probe_n of them: the
// first pair is overwritten. Commit 787c7c3ba introduced the loop and says "currently still using
// n=2 samples, as previously done". The loop before it compared the first two pairs.
TEST_CASE("measure_phase_cycles does measure_probe_n bump pairs", "[homing_corexy][O2][!shouldfail][known_bug]") {
    fake::reset(150, 150);
    store_sensitivity();
    REQUIRE(hooks::rehome_and_phase(true));
    world.moves.clear();

    hooks::Cycles c_dist;
    REQUIRE(hooks::measure_phase_cycles(c_dist));
    CHECK(world.single_motor_bumps() == size_t(2 * hooks::measure_probe_n()));
}

TEST_CASE("measure_phase_cycles retries an early trigger", "[homing_corexy]") {
    fake::reset(150, 150);
    store_sensitivity();
    REQUIRE(hooks::rehome_and_phase(true));

    world.moves.clear();
    hooks::Cycles ref;
    REQUIRE(hooks::measure_phase_cycles(ref));
    const size_t ref_bumps = world.single_motor_bumps();

    // An early trigger of 600 steps (6 mm of the motor) on the first bump: the measurement repeats
    // that bump, and the result does not change
    world.moves.clear();
    world.bump_noise = { -600 };
    hooks::Cycles c_dist;
    REQUIRE(hooks::measure_phase_cycles(c_dist));
    CHECK(world.single_motor_bumps() == ref_bumps + 1);
    CHECK(c_dist.a == ref.a);
    CHECK(c_dist.b == ref.b);

    // More early triggers than XY_HOMING_ORIGIN_BUMP_RETRIES: the measurement fails
    world.bump_noise.assign(XY_HOMING_ORIGIN_BUMP_RETRIES + 1, -600);
    CHECK_FALSE(hooks::measure_phase_cycles(c_dist));
    CHECK(world.wall_crashes == 0);
}

// A late trigger past the planned travel ("planned travel too short") fails at once: a retry
// cannot reach the endstop.
TEST_CASE("measure_phase_cycles fails on a late trigger", "[homing_corexy]") {
    fake::reset(150, 150);
    store_sensitivity();
    REQUIRE(hooks::rehome_and_phase(true));
    world.moves.clear();
    world.bump_noise = { 600 };
    hooks::Cycles c_dist;
    CHECK_FALSE(hooks::measure_phase_cycles(c_dist));
    CHECK(world.single_motor_bumps() == 1);
}

// H3: corexy_sens_is_calibrated() has no caller (since 7694a03f2). G28 checks only the grid origin
// (corexy_home_is_calibrated()). With an origin and no sensitivity calibration, the refinement
// measures without a sensitivity and BSODs ("axis measurement without calibration").
TEST_CASE("refinement without the sensitivity calibration", "[homing_corexy][H3]") {
    fake::reset(150, 150);
    REQUIRE(corexy_rehome_xy(fr));
    store_sensitivity();
    REQUIRE(corexy_home_refine(fr, CoreXYCalibrationMode::force));
    REQUIRE(corexy_home_is_calibrated());

    config_store().corexy_home_tmc_sens.set_to_default();
    REQUIRE_FALSE(corexy_sens_is_calibrated());
    CHECK_THROWS(corexy_home_refine(fr, CoreXYCalibrationMode::disallow));
}

TEST_CASE("corexy_home_is_calibrated is false without the sensitivity calibration", "[homing_corexy][H3][!shouldfail][known_bug]") {
    fake::reset(150, 150);
    config_store().corexy_grid_origin.set({ .origin = { 0.f, 0.f }, .distance = { 0.f, 0.f } });
    // G28.cpp (corexy_refine_during_G28_once) calibrates only when this is false. G28.cpp is not
    // compiled in these tests.
    CHECK_FALSE(corexy_home_is_calibrated());
}

// The purpose of the refinement: after a calibration, each home gives the same machine
// coordinates for the same physical position, also when the stall of the classic homing is off by
// up to 0.5 mm. In the fake world the stepper counter is the rotor position plus an offset, so the
// offsets must be equal after each refinement.
TEST_CASE("refinement gives the same home after stall errors", "[homing_corexy]") {
    float steps_per_mm = 0;
    SECTION("2GT belt") {
        steps_per_mm = 100.f;
    }
    SECTION("1.5GT belt") {
        steps_per_mm = 101.587f;
    }
    fake::reset(200, 100, 300, 700, steps_per_mm);
    REQUIRE(corexy_rehome_xy(fr));
    REQUIRE(corexy_sens_calibrate(fr));
    REQUIRE(corexy_rehome_xy(fr));
    REQUIRE(corexy_home_refine(fr, CoreXYCalibrationMode::force));
    const int32_t offset_a = world.offset_a;
    const int32_t offset_b = world.offset_b;

    for (const int32_t noise : { -50, -30, -10, 0, 10, 30, 50 }) {
        CAPTURE(noise);
        world.home_noise = { noise, -noise };
        REQUIRE(corexy_rehome_xy(fr));
        REQUIRE(corexy_home_refine(fr, CoreXYCalibrationMode::disallow));
        CHECK(world.offset_a == offset_a);
        CHECK(world.offset_b == offset_b);
        CHECK_FALSE(corexy_home_is_unstable());
    }
    CHECK(world.wall_crashes == 0);
}

// Property tests of the calibration math

TEST_CASE("cdist_translate rounds to the grid around the origin", "[homing_corexy][property]") {
    std::mt19937 rng(1);
    std::uniform_real_distribution<float> origin_dist(-20.f, 20.f);
    std::uniform_int_distribution<int32_t> cell_dist(-10, 10);
    std::uniform_real_distribution<float> err_dist(-0.49f, 0.49f);
    for (int i = 0; i < 10000; ++i) {
        const hooks::Cycles o { origin_dist(rng), origin_dist(rng) };
        const int32_t ka = cell_dist(rng), kb = cell_dist(rng);
        const hooks::Cycles c { o.a + ka + err_dist(rng), o.b + kb + err_dist(rng) };
        CAPTURE(o.a, o.b, c.a, c.b);

        int32_t a, b;
        hooks::cdist_translate(c, o, a, b);
        CHECK(a == ka + int32_t(std::round(o.a)));
        CHECK(b == kb + int32_t(std::round(o.b)));

        // A shift of the point and the origin by full cycles shifts the result by the same cycles
        const int32_t n = cell_dist(rng);
        int32_t a2, b2;
        hooks::cdist_translate({ c.a + n, c.b + n }, { o.a + n, o.b + n }, a2, b2);
        CHECK(a2 == a + n);
        CHECK(b2 == b + n);
    }
}

TEST_CASE("point_is_unstable marks the points near a rounding boundary", "[homing_corexy][property]") {
    const float half_width = hooks::unstable_phase_threshold() / 2;
    std::mt19937 rng(2);
    std::uniform_real_distribution<float> pos_dist(-20.f, 20.f);
    std::uniform_int_distribution<int32_t> cell_dist(-10, 10);
    for (int i = 0; i < 10000; ++i) {
        const hooks::Cycles o { pos_dist(rng), pos_dist(rng) };
        const hooks::Cycles c { pos_dist(rng), pos_dist(rng) };
        CAPTURE(o.a, o.b, c.a, c.b);
        const bool unstable = hooks::point_is_unstable(c, o);

        // Symmetric, and the same for a shift of the point by full cycles
        CHECK(hooks::point_is_unstable(o, c) == unstable);
        const int32_t n = cell_dist(rng);
        CHECK(hooks::point_is_unstable({ c.a + n, c.b - n }, o) == unstable);

        // A stable point keeps its grid value for each error below the half width, with a margin
        // for the float rounding
        if (!unstable) {
            int32_t a, b;
            hooks::cdist_translate(c, o, a, b);
            for (const float e : { -half_width + 1e-3f, half_width - 1e-3f }) {
                int32_t a2, b2;
                hooks::cdist_translate({ c.a + e, c.b + e }, o, a2, b2);
                CHECK(a2 == a);
                CHECK(b2 == b);
            }
        }
    }

    // The boundaries: exact half cycle is unstable, exact cell is stable
    CHECK(hooks::point_is_unstable({ 3.5f, 1.f }, { 0.f, 0.f }));
    CHECK(hooks::point_is_unstable({ 1.f, -2.5f }, { 0.f, 0.f }));
    CHECK_FALSE(hooks::point_is_unstable({ 3.f, 1.f }, { 0.f, 0.f }));
    CHECK_FALSE(hooks::point_is_unstable({ 3.5f + half_width + 0.01f, 1.f }, { 0.f, 0.f }));
}

TEST_CASE("estimate_origin finds the origin of the measured grid", "[homing_corexy][property]") {
    const auto seq = hooks::measure_point_sequence();
    std::mt19937 rng(3);
    std::uniform_real_distribution<float> origin_dist(-10.f, 10.f);
    std::uniform_real_distribution<float> noise_dist(-0.1f, 0.1f);
    std::uniform_int_distribution<int> passes_dist(1, 10);
    std::uniform_int_distribution<int32_t> cell_dist(-10, 10);
    constexpr float resolution = 1.f / 128; // origin_sweep_res

    for (int i = 0; i < 200; ++i) {
        const hooks::Cycles origin { origin_dist(rng), origin_dist(rng) };
        const int passes = passes_dist(rng);
        const bool noisy = i % 2;
        std::vector<hooks::Cycles> points;
        for (int p = 0; p < passes; ++p) {
            for (const auto &[sa, sb] : seq) {
                points.push_back({ origin.a + sa + (noisy ? noise_dist(rng) : 0.f), origin.b + sb + (noisy ? noise_dist(rng) : 0.f) });
            }
        }
        CAPTURE(origin.a, origin.b, passes, noisy);

        const hooks::Cycles est = hooks::estimate_origin(points, seq[0].first, seq[0].second);
        REQUIRE(std::isfinite(est.a));
        REQUIRE(std::isfinite(est.b));
        const float tolerance = noisy ? 0.1f + resolution : resolution;
        CHECK(std::abs(est.a - origin.a) <= tolerance);
        CHECK(std::abs(est.b - origin.b) <= tolerance);

        // A shift of all points by full cycles shifts the origin by the same cycles
        const int32_t n = cell_dist(rng);
        std::vector<hooks::Cycles> shifted = points;
        for (auto &pt : shifted) {
            pt.a += n;
            pt.b += n;
        }
        const hooks::Cycles est2 = hooks::estimate_origin(shifted, seq[0].first, seq[0].second);
        CHECK(std::abs(est2.a - (est.a + n)) <= 1e-3f);
        CHECK(std::abs(est2.b - (est.b + n)) <= 1e-3f);
    }

    // The origin at the wrap of the cycle
    for (const float frac : { 0.f, 0.001f, 0.499f, 0.5f, 0.501f, 0.999f }) {
        CAPTURE(frac);
        std::vector<hooks::Cycles> points;
        for (const auto &[sa, sb] : seq) {
            points.push_back({ 2.f + frac + sa, -3.f + frac + sb });
        }
        const hooks::Cycles est = hooks::estimate_origin(points, seq[0].first, seq[0].second);
        CHECK(std::abs(est.a - (2.f + frac)) <= resolution);
        CHECK(std::abs(est.b - (-3.f + frac)) <= resolution);
    }
}
