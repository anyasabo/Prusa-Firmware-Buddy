#include <heater_control.hpp>

#include <algorithm>
#include <catch2/catch_get_random_seed.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <cstdint>
#include <limits>
#include <random>

using namespace indx_head::heater;
using enum HeaterControlMode;

namespace {

constexpr int32_t target = 250 * 100;

/// Heat up from the ambient temperature at 1 °C per control step. Only for the mode tests:
/// this fast approach winds the integral up (see the windup test).
void heat_up(PowerControl &control, int32_t to) {
    for (int32_t t = 2500; t < to; t += 100) {
        control.step(target, t);
    }
}

/// A controller in PID mode at the target, with an integral of approximately 0. The target moves
/// with the temperature, so the error stays 0 and the integral does not change. Each step is
/// below the 48 °C that overflows the D term.
PowerControl settled_at_target() {
    PowerControl control;
    control.step(2600, 2500);
    for (int32_t t = 2600; t <= target; t += 100) {
        control.step(t, t);
    }
    control.step(target, target);
    REQUIRE(control.mode() == HEATER_CONTROL_PID);
    REQUIRE(static_cast<float>(control.integral()) < 0.02f);
    return control;
}

} // namespace

TEST_CASE("indx_head heater: no power without a target", "[indx_head][heater]") {
    PowerControl control;
    const int32_t current = GENERATE(-32768, 0, 2500, 25000, 32767);
    const int32_t off_target = GENERATE(0, -1, -25000);
    CHECK(control.step(off_target, current) == 0);
    CHECK(control.mode() == HEATER_CONTROL_OFF);
}

TEST_CASE("indx_head heater: the target turns the heater off from each mode", "[indx_head][heater]") {
    PowerControl control;
    control.step(target, 2500);
    CHECK(control.mode() == HEATER_CONTROL_TURBO);
    CHECK(control.step(0, 2500) == 0);
    CHECK(control.mode() == HEATER_CONTROL_OFF);

    PowerControl settled = settled_at_target();
    CHECK(settled.step(0, target) == 0);
    CHECK(settled.mode() == HEATER_CONTROL_OFF);
}

TEST_CASE("indx_head heater: full power more than 10 °C below the target", "[indx_head][heater]") {
    PowerControl control;
    CHECK(control.step(target, 2500) == max_power);
    CHECK(control.mode() == HEATER_CONTROL_TURBO);
    heat_up(control, target - 1001);
    CHECK(control.step(target, target - 1001) == max_power);
    CHECK(control.mode() == HEATER_CONTROL_TURBO);
}

TEST_CASE("indx_head heater: limited power in the last 10 °C", "[indx_head][heater]") {
    PowerControl control;
    heat_up(control, target - 1000);
    // Exactly 10 °C below is not "way below"
    CHECK(control.step(target, target - 1000) == limited_max_power);
    CHECK(control.mode() == HEATER_CONTROL_LIMITED);
    CHECK(control.step(target, target - 1) == limited_max_power);
    CHECK(control.mode() == HEATER_CONTROL_LIMITED);
}

TEST_CASE("indx_head heater: the PID starts at the target and stays", "[indx_head][heater]") {
    PowerControl control;
    heat_up(control, target - 500);
    control.step(target, target - 500);
    REQUIRE(control.mode() == HEATER_CONTROL_LIMITED);
    control.step(target, target);
    CHECK(control.mode() == HEATER_CONTROL_PID);

    // A small drop below the target does not go back to the limited power
    const uint16_t pwr = control.step(target, target - 500);
    CHECK(control.mode() == HEATER_CONTROL_PID);
    CHECK(pwr <= limited_max_power);

    // A drop of more than 10 °C does
    CHECK(control.step(target, target - 1001) == max_power);
    CHECK(control.mode() == HEATER_CONTROL_TURBO);
}

TEST_CASE("indx_head heater: no power far above the target", "[indx_head][heater]") {
    PowerControl control = settled_at_target();
    const int32_t current = GENERATE(target + 100, target + 2000, 32767);
    // Approach the value slowly, as a real nozzle does
    for (int32_t t = target; t < current; t += 10) {
        control.step(target, t);
    }
    CHECK(control.step(target, current) == 0);
    CHECK(control.step(target, current) == 0);
    CHECK(control.mode() == HEATER_CONTROL_PID);
}

TEST_CASE("indx_head heater: the integral does not wind up below the target", "[indx_head][heater]") {
    PowerControl control = settled_at_target();
    // 5 °C below the target: the P term alone saturates the output, so the integral must not grow
    const auto integral_before = control.integral();
    for (int i = 0; i < 300 * 60; ++i) {
        CHECK(control.step(target, target - 500) == limited_max_power);
    }
    CHECK(control.integral() == integral_before);
}

TEST_CASE("indx_head heater: the integral stops at the output limit", "[indx_head][heater]") {
    PowerControl control = settled_at_target();
    // 0.1 °C below the target: the P term is 0.6, the integral gives the rest. One minute at 300 Hz.
    for (int i = 0; i < 300 * 60; ++i) {
        control.step(target, target - 10);
    }
    CHECK(control.step(target, target - 10) >= limited_max_power - 1);
    // The integral stops when P + I reaches the output limit
    CHECK(static_cast<float>(control.integral()) <= limited_max_power - 0.6f + 0.01f);
    CHECK(static_cast<float>(control.integral()) >= limited_max_power - 0.6f - 0.01f);
}

TEST_CASE("indx_head heater: the integral stops at 0 above the target", "[indx_head][heater]") {
    PowerControl control = settled_at_target();
    const auto integral_before = control.integral();
    for (int i = 0; i < 300 * 60; ++i) {
        CHECK(control.step(target, target + 10) == 0);
    }
    // The output is already 0, so the integral does not go down
    CHECK(control.integral() <= integral_before);
    CHECK(static_cast<float>(control.integral()) >= -0.01f);
}

TEST_CASE("indx_head heater: property: the power stays in range for random walks", "[indx_head][heater]") {
    std::mt19937 rng(Catch::getSeed());
    // At most 1 °C per control step (300 °C/s): faster than the real heater
    std::uniform_int_distribution<int32_t> step_dist(-100, 100);
    std::uniform_int_distribution<int32_t> target_dist(0, 300);
    for (int run = 0; run < 200; ++run) {
        PowerControl control;
        int32_t t = 2500;
        int32_t tgt = target_dist(rng) * 100;
        for (int i = 0; i < 3000; ++i) {
            if (i % 1000 == 999) {
                tgt = target_dist(rng) * 100;
            }
            t = std::clamp<int32_t>(t + step_dist(rng), 500, 32767);
            const uint16_t pwr = control.step(tgt, t);
            REQUIRE(pwr <= max_power);
            if (tgt == 0) {
                REQUIRE(pwr == 0);
            }
            if (control.mode() == HEATER_CONTROL_PID) {
                REQUIRE(pwr <= limited_max_power);
            }
            const float integral = static_cast<float>(control.integral());
            // Bound for this rate of change: the D term (6.7 per 0.01 °C/step) can hold the
            // integral away from the output range
            REQUIRE(integral > -1000.0f);
            REQUIRE(integral < 1000.0f);
        }
    }
}

TEST_CASE("indx_head heater: timer configuration", "[indx_head][heater]") {
    const auto configs = compute_timer_config(238);
    // Power 1: no pre-time, 0.17 post-time
    CHECK(configs[0].CCR1 == 239);
    CHECK(configs[0].ARR == 238 + 40);
    // Power 14: 0.34 pre-time, 0.80 post-time
    CHECK(configs[max_power - 1].CCR1 == 238 - 80 + 1);
    CHECK(configs[max_power - 1].ARR == 238 + 190);
}

TEST_CASE("indx_head heater: property: timer configuration in the valid interval range", "[indx_head][heater]") {
    for (uint16_t interval = 170; interval <= 300; ++interval) {
        const auto configs = compute_timer_config(interval);
        for (size_t p = 0; p < configs.size(); ++p) {
            INFO("interval " << interval << " power " << p + 1);
            CHECK(configs[p].CCR1 < configs[p].ARR);
            CHECK(configs[p].CCR1 <= interval + 1);
            CHECK(configs[p].ARR > interval);
            if (p > 0) {
                // More power: a longer on-time, never a shorter one
                CHECK(configs[p].ARR >= configs[p - 1].ARR);
                CHECK(configs[p].CCR1 <= configs[p - 1].CCR1);
            }
        }
    }
}

TEST_CASE("indx_head heater: PWM value of the power levels", "[indx_head][heater]") {
    CHECK(power_to_pwm(0) == 0);
    // 0.17 / 0.885 of full scale
    CHECK(power_to_pwm(1) == 48);
    uint8_t last = 0;
    for (uint16_t p = 1; p <= max_power; ++p) {
        INFO("power " << p);
        CHECK(power_to_pwm(p) > last);
        last = power_to_pwm(p);
    }
    CHECK(power_to_pwm(max_power) >= 254);
}

TEST_CASE("indx_head heater: the first step after off has no D term", "[indx_head][heater]") {
    SECTION("boot") {
        // The P term alone saturates the output, so the integral must not change. A D term from the
        // initial 0 °C (a rise of 25 °C in one step) would open the integral for one step: +2.7.
        PowerControl control;
        CHECK(control.step(target, 2500) == max_power);
        CHECK(static_cast<float>(control.integral()) == 0.0f);
    }
    SECTION("a lower target after off") {
        // M104 S0 at 250 °C, then a target of 200 °C while the nozzle is still at 245 °C. The
        // last temperature from before the off (250 °C) would give a D term of +5 °C per step.
        PowerControl control = settled_at_target();
        control.step(0, target);
        control.step(0, 24800);
        CHECK(control.step(20000, 24500) == 0);
        CHECK(control.mode() == HEATER_CONTROL_PID);
    }
}

TEST_CASE("indx_head heater: a large temperature jump does not overflow the PID", "[indx_head][heater]") {
    // The fixed_16_16 products are int32_t. The raw value of the D gain is approximately 441'000,
    // so a change of more than 48.7 °C in one step can overflow. The TPIS filter passes a jump below
    // 50 °C at once, and any jump after 3 readings.
    SECTION("up, in PID mode") {
        PowerControl control = settled_at_target();
        const int32_t jump = GENERATE(4900, 4999, 7767);
        CHECK(control.step(target, target + jump) == 0);
        CHECK(control.mode() == HEATER_CONTROL_PID);
    }
    SECTION("down, from far above the target") {
        PowerControl control = settled_at_target();
        for (int32_t t = target; t <= 32700; t += 100) {
            control.step(target, t);
        }
        // A fall from 327 °C to 278 °C: the D term of a fall is positive and saturates the output.
        // An overflow would give it the wrong sign, and the output 0.
        CHECK(control.step(target, 27800) == limited_max_power);
    }
    SECTION("the int16_t limits after boot") {
        PowerControl control;
        CHECK(control.step(30000, std::numeric_limits<int16_t>::max()) == 0);
        CHECK(control.step(30000, std::numeric_limits<int16_t>::min()) == max_power);
        CHECK(control.step(30000, std::numeric_limits<int16_t>::max()) == 0);
    }
}
