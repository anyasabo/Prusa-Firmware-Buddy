#include <catch2/catch_test_macros.hpp>

#include <feature/input_shaper/input_shaper_config.hpp>

#include <cmath>
#include <utility>

using namespace input_shaper;

// input_shaper::get() divides by sqrt(1 - damping_ratio^2) and calls bsod() for a damping
// ratio of 1 or more (input_shaper.cpp). Klipper limits the damping ratio of its classic
// shapers to 0.99 (768b19e8b).
TEST_CASE("M593 rejects a damping ratio of 1 or more") {
    CHECK(is_valid_damping_ratio(0.f));
    CHECK(is_valid_damping_ratio(0.1f));
    CHECK(is_valid_damping_ratio(0.99f));
    CHECK_FALSE(is_valid_damping_ratio(1.f));
    CHECK_FALSE(is_valid_damping_ratio(1.5f));
    CHECK_FALSE(is_valid_damping_ratio(-0.1f));
    CHECK_FALSE(is_valid_damping_ratio(NAN));
}
// The EI shapers use the vibration tolerance v_tol = 1 / R. For R below 1, v_tol is above 1:
// 2HUMP EI then takes sqrt(1 - v_tol^2) of a negative number, and all its amplitudes are NaN.
// EI and 3HUMP EI get negative amplitudes.
TEST_CASE("M593 rejects a vibration reduction below 1") {
    CHECK(is_valid_vibration_reduction(20.f));
    CHECK(is_valid_vibration_reduction(1.f));
    CHECK_FALSE(is_valid_vibration_reduction(0.5f));
    CHECK_FALSE(is_valid_vibration_reduction(0.f));
    CHECK_FALSE(is_valid_vibration_reduction(NAN));
}
TEST_CASE("Each accepted vibration reduction gives finite 2HUMP EI amplitudes") {
    // The amplitude formula of input_shaper::get(), Type::ei_2hump.
    for (const float r : { 0.5f, 0.9f, 1.f, 2.f, 20.f, 100.f, 1e6f, 1e23f, 1e30f }) {
        if (!is_valid_vibration_reduction(r)) {
            continue;
        }
        CAPTURE(r);
        const float v_tol = 1.f / r;
        const float V2 = v_tol * v_tol;
        const float X = std::pow(V2 * (std::sqrt(1.f - V2) + 1.f), 1.f / 3.f);
        const float a1 = (3.f * X * X + 2.f * X + 3.f * V2) / (16.f * X);
        CHECK(std::isfinite(a1));
        CHECK(a1 >= 0.f);
        CHECK(0.5f - a1 >= 0.f);
    }
}
// M593 starts from axis_defaults[axis] when the axis has no shaper yet, and M958 and M1959 fit
// with it. A default outside the limits gives the same NaN amplitudes, for example
// "M593 Z T3 F40" (EI on Z) with the vibration reduction of the Z default.
TEST_CASE("The default of each axis is within the limits") {
    for (const AxisConfig &config : axis_defaults) {
        CAPTURE(std::to_underlying(config.type), config.frequency);
        CHECK(is_valid_damping_ratio(config.damping_ratio));
        CHECK(is_valid_vibration_reduction(config.vibration_reduction));
    }
}
// "M958 X" (the naive ZV tune) stored R = 0. Then "M593 X T3" merged EI with R = 0 and gave
// infinite pulse amplitudes. M593 must check the merged configuration.
TEST_CASE("M593 checks the merged configuration") {
    const AxisConfig zv_from_m958 { .type = Type::zv, .frequency = 47.f, .damping_ratio = 0.1f, .vibration_reduction = 0.f };
    CHECK(is_valid_axis_config(zv_from_m958));
    for (const Type type : { Type::ei, Type::ei_2hump, Type::ei_3hump }) {
        CAPTURE(std::to_underlying(type));
        AxisConfig merged = zv_from_m958;
        merged.type = type;
        CHECK_FALSE(is_valid_axis_config(merged));
        merged.vibration_reduction = 20.f;
        CHECK(is_valid_axis_config(merged));
    }
    CHECK_FALSE(is_valid_axis_config({ .type = Type::mzv, .frequency = 47.f, .damping_ratio = 1.f, .vibration_reduction = 20.f }));
    CHECK_FALSE(is_valid_axis_config({ .type = static_cast<Type>(200), .frequency = 47.f, .damping_ratio = 0.1f, .vibration_reduction = 20.f }));
}
TEST_CASE("M593 rejects a vibration reduction above the maximum") {
    CHECK(is_valid_vibration_reduction(1e6f));
    CHECK_FALSE(is_valid_vibration_reduction(1e7f));
    CHECK_FALSE(is_valid_vibration_reduction(INFINITY));
}
