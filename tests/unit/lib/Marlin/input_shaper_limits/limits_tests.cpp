#include <catch2/catch_test_macros.hpp>

#include <feature/input_shaper/input_shaper_config.hpp>

#include <bit>
#include <cmath>
#include <cstring>
#include <random>
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
// M593 starts from axis_defaults[axis] when the axis has no shaper yet, and M959 and M1959 fit
// with it. A default outside the limits gives the same NaN amplitudes, for example
// "M593 Z T3 F40" (EI on Z) with the vibration reduction of the Z default.
TEST_CASE("The default of each axis is within the limits") {
    for (const AxisConfig &config : axis_defaults) {
        CAPTURE(std::to_underlying(config.type), config.frequency);
        CHECK(is_valid_damping_ratio(config.damping_ratio));
        CHECK(is_valid_vibration_reduction(config.vibration_reduction));
    }
}
// "M959 X" (the naive ZV tune) stored R = 0. Then "M593 X T3" merged EI with R = 0 and gave
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

// input_shaper::init() loads the X and Y configuration from the config store at boot and
// after each print, and it does not go through the checks of M593. Older firmware accepted
// R between 0 and 1 and saved it with "M593 R0.5 W". A stored value outside the limits then
// gives NaN amplitudes (2HUMP EI) or the bsod() of input_shaper::get() at each boot.
TEST_CASE("A stored configuration outside the limits gets the defaults of the axis") {
    const AxisEnum x = static_cast<AxisEnum>(0);
    const AxisConfig &defaults = axis_defaults[0];

    SECTION("A valid configuration does not change") {
        const AxisConfig stored { .type = Type::ei, .frequency = 47.f, .damping_ratio = 0.12f, .vibration_reduction = 15.f };
        CHECK(sanitized_axis_config(x, stored) == stored);
    }

    SECTION("R below 1") {
        const AxisConfig stored { .type = Type::ei_2hump, .frequency = 47.f, .damping_ratio = 0.12f, .vibration_reduction = 0.5f };
        const AxisConfig result = sanitized_axis_config(x, stored);
        CHECK(result.vibration_reduction == defaults.vibration_reduction);
        CHECK(result.type == stored.type);
        CHECK(result.frequency == stored.frequency);
        CHECK(result.damping_ratio == stored.damping_ratio);
    }

    SECTION("D of 1 or more, and NaN") {
        for (const float d : { 1.f, 2.f, -0.5f, NAN }) {
            CAPTURE(d);
            const AxisConfig stored { .type = Type::mzv, .frequency = 47.f, .damping_ratio = d, .vibration_reduction = 20.f };
            CHECK(sanitized_axis_config(x, stored).damping_ratio == defaults.damping_ratio);
        }
    }

    SECTION("A frequency outside the safe range") {
        // input_shaper::get() calls bsod() for a frequency of 0 or less.
        const AxisConfig stored { .type = Type::mzv, .frequency = 0.f, .damping_ratio = 0.1f, .vibration_reduction = 20.f };
        CHECK(sanitized_axis_config(x, stored).frequency == frequency_safe_min);
    }

    SECTION("A type outside the enum") {
        const AxisConfig stored { .type = static_cast<Type>(200), .frequency = 47.f, .damping_ratio = 0.1f, .vibration_reduction = 20.f };
        CHECK(sanitized_axis_config(x, stored).type == defaults.type);
    }
}
// On CoreXY, set_logical_axis_config_internal(X_AXIS) also builds the Y shaper from the
// configuration. So all axes must be clean before init() applies the first one.
TEST_CASE("The stored configuration is clean on all axes") {
    Config stored;
    stored.axis[0] = AxisConfig { .type = Type::mzv, .frequency = 50.f, .damping_ratio = 0.1f, .vibration_reduction = 20.f };
    stored.axis[1] = AxisConfig { .type = Type::mzv, .frequency = 0.f, .damping_ratio = 1.f, .vibration_reduction = 20.f };
    const Config result = sanitized_config(stored);
    REQUIRE(result.axis[0].has_value());
    REQUIRE(result.axis[1].has_value());
    CHECK(*result.axis[0] == *stored.axis[0]);
    CHECK(result.axis[1]->frequency == frequency_safe_min);
    CHECK(result.axis[1]->damping_ratio == axis_defaults[1].damping_ratio);
    CHECK_FALSE(result.axis[2].has_value());
}
// R has an effect only on the EI shapers. A stored R below 1 on MZV is valid for MZV, and the
// clean step must not change it.
TEST_CASE("The clean step keeps R for the shapers that do not use it") {
    const AxisEnum x = static_cast<AxisEnum>(0);
    for (const Type type : { Type::zv, Type::zvd, Type::mzv, Type::null }) {
        CAPTURE(std::to_underlying(type));
        const AxisConfig stored { .type = type, .frequency = 47.f, .damping_ratio = 0.1f, .vibration_reduction = 0.f };
        CHECK(sanitized_axis_config(x, stored) == stored);
    }
}
namespace {
bool is_finite_value(float v) { return v - v == 0.f; }

float random_float(std::mt19937 &rng) {
    static constexpr float special[] = { NAN, -NAN, INFINITY, -INFINITY, 1e-40f, -1e-40f, 0.f, -0.f, 0.99f, 0.995f, 1.f, 0.5f, 1e6f, 1e7f, 1e30f, 47.f };
    if (rng() % 2) {
        return special[rng() % std::size(special)];
    }
    return std::bit_cast<float>(static_cast<uint32_t>(rng()));
}
} // namespace
// The clean step runs at each boot and at each print end. If its result is not a fixed point
// (for example a NaN that stays, because NaN != NaN), init() sees a change each time, logs it
// and writes the store again: flash wear.
TEST_CASE("The clean step is idempotent") {
    std::mt19937 rng(20261126);
    for (int n = 0; n < 200000; ++n) {
        const AxisConfig stored {
            .type = static_cast<Type>(rng() % 256 < 200 ? rng() % 7 : rng() % 256),
            .frequency = random_float(rng),
            .damping_ratio = random_float(rng),
            .vibration_reduction = random_float(rng),
        };
        for (uint8_t a = 0; a < 3; ++a) {
            const AxisEnum axis = static_cast<AxisEnum>(a);
            const AxisConfig once = sanitized_axis_config(axis, stored);
            const AxisConfig twice = sanitized_axis_config(axis, once);
            REQUIRE(std::memcmp(&once, &twice, sizeof(once)) == 0);
            REQUIRE(once == twice); // false if a NaN stays
            REQUIRE(is_finite_value(once.frequency));
            REQUIRE(is_finite_value(once.damping_ratio));
            REQUIRE(is_finite_value(once.vibration_reduction));
            REQUIRE(is_valid_axis_config(once));
        }
    }
}
