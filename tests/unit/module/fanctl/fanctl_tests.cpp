#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>
#include <cstdint>

#include <fanctl/CFanCtl3Wire.hpp>

namespace {

// The fan controllers run tick() from a 1 kHz timer interrupt.
constexpr uint32_t ticks_per_second = 1000;

CFanCtlPWM::PWMParams pwm_params(uint8_t max_value, uint8_t phase_shift_threshold = 0) {
    return {
        .pwm_min = 0,
        .pwm_max = max_value,
        .phase_shift_threshold = phase_shift_threshold,
        .has_inverted_pwm = false,
    };
}

/// A 3-wire fan that turns at a constant speed. The tach output is valid only while the fan
/// has power (the PWM switches the fan supply). Without power, the pull-up keeps it high.
/// 4 tach edges for each revolution, as CFanCtlTach assumes.
struct SimulatedFan {
    double rpm;

    bool tach(uint32_t tick, bool powered) const {
        if (!powered) {
            return true;
        }
        const double edges_per_tick = rpm / 15.0 / ticks_per_second;
        return static_cast<uint64_t>(tick * edges_per_tick) % 2;
    }
};

} // namespace

TEST_CASE("CFanCtlPWM: on for exactly the PWM value, and pwm_on is as documented", "[fanctl]") {
    // The periods that CFanCtl3WireDynamic uses, and the default print fan period (50).
    const uint8_t max_value = GENERATE(as<uint8_t> {}, 40, 50, 125, 250);

    for (uint16_t val = 0; val <= max_value; ++val) {
        CAPTURE(max_value, val);
        bool pin = false;
        CFanCtlPWM pwm([&](bool v) { pin = v; }, pwm_params(max_value));
        pwm.set_PhaseShiftMode(CFanCtlPWM::none);
        pwm.set_PWM(static_cast<uint8_t>(val));

        // The new value takes effect at the start of the next period.
        for (uint16_t t = 0; t < max_value; ++t) {
            pwm.tick();
        }

        for (int period = 0; period < 3; ++period) {
            uint16_t on_ticks = 0;
            for (uint16_t t = 0; t < max_value; ++t) {
                const int16_t pwm_on = pwm.tick();
                // "positive number means pwm is on N ticks, negative number means pwm is off
                // and will be switched on in -N ticks"
                const int expected = (t < val) ? t : t - max_value;
                CAPTURE(t, pwm_on);
                REQUIRE(pwm_on == expected);
                REQUIRE(pin == (pwm_on >= 0));
                on_ticks += pin;
            }
            REQUIRE(on_ticks == val);
        }
    }
}

TEST_CASE("CFanCtlPWM: phase shifting keeps the on time of each period", "[fanctl]") {
    constexpr uint8_t max_value = 50;
    constexpr uint8_t phase_shift_threshold = 20;
    const auto mode = GENERATE(CFanCtlPWM::triangle, CFanCtlPWM::random);
    const uint8_t val = GENERATE(as<uint8_t> {}, 2, 5, 10, 20);
    CAPTURE(mode, val);

    bool pin = false;
    CFanCtlPWM pwm([&](bool v) { pin = v; }, pwm_params(max_value, phase_shift_threshold));
    pwm.set_PhaseShiftMode(mode);
    pwm.set_PWM(val);
    for (uint16_t t = 0; t < max_value; ++t) {
        pwm.tick();
    }

    for (int period = 0; period < 20; ++period) {
        uint16_t on_ticks = 0;
        for (uint16_t t = 0; t < max_value; ++t) {
            const int16_t pwm_on = pwm.tick();
            REQUIRE(pin == (pwm_on >= 0));
            on_ticks += pin;
        }
        REQUIRE(on_ticks == val);
    }
}

TEST_CASE("CFanCtlPWM: a change of the period in the middle of a period", "[fanctl]") {
    // CFanCtl3WireDynamic changes max_value at any time, also to a value below the counter.
    bool pin = false;
    CFanCtlPWM pwm([&](bool v) { pin = v; }, pwm_params(250));
    pwm.set_PhaseShiftMode(CFanCtlPWM::none);
    pwm.set_PWM(100);
    for (uint16_t t = 0; t < 250 + 200; ++t) {
        pwm.tick();
    }

    pwm.set_max_PWM(40);
    pwm.set_PWM(20);
    // The rest of the old period ends at once, then one period loads the new value.
    for (uint16_t t = 0; t < 1 + 40; ++t) {
        pwm.tick();
    }
    for (int period = 0; period < 3; ++period) {
        uint16_t on_ticks = 0;
        for (uint16_t t = 0; t < 40; ++t) {
            const int16_t pwm_on = pwm.tick();
            REQUIRE(pin == (pwm_on >= 0));
            on_ticks += pin;
        }
        REQUIRE(on_ticks == 20);
    }
}

TEST_CASE("CFanCtlTach: measured RPM with the PWM switching the fan supply", "[fanctl]") {
    struct Case {
        uint8_t max_value; ///< PWM period in ticks
        uint8_t val; ///< on ticks in each period
    };
    // The periods and values that CFanCtl3WireDynamic uses at print fan speeds from 10% to 100%,
    // and the fixed 50-tick period of CFanCtl3Wire at 20% and 25%.
    const Case c = GENERATE(
        Case { 250, 24 }, Case { 250, 39 }, Case { 250, 48 }, // below 50/255
        Case { 125, 24 }, Case { 125, 31 }, Case { 125, 38 }, // 50-79
        Case { 50, 15 }, Case { 50, 20 }, // 80-119
        Case { 40, 18 }, Case { 40, 40 }, // 120 and more
        Case { 50, 10 }, Case { 50, 12 }); // fixed period
    // Speeds that are not a multiple of the PWM frequency.
    const double rpm = GENERATE(1537.3, 3311.7, 5523.9);
    CAPTURE(c.max_value, c.val, rpm);

    // inplace_function has a small capture size, so the lambdas capture one pointer.
    struct State {
        bool powered = false;
        uint32_t tick = 0;
        SimulatedFan fan;
    } s { .fan = { rpm } };
    CFanCtlPWM pwm([p = &s](bool v) { p->powered = v; }, pwm_params(c.max_value));
    pwm.set_PhaseShiftMode(CFanCtlPWM::none);
    pwm.set_PWM(c.val);
    CFanCtlTach tach([p = &s]() { return p->fan.tach(p->tick, p->powered); });

    // The RPM filter takes 3/4 of each new 1 s value. After 10 s, the start value has no effect.
    for (; s.tick < 10 * ticks_per_second; ++s.tick) {
        tach.tick(pwm.tick());
    }

    // The tach ignores the first two ticks of each PWM pulse, but it counts them as on time.
    // Thus the measured value is low by about 2 / val: 5% at 100%, 20% at 10 of 50 ticks.
    // The self test RPM ranges were set with this measurement, so this test keeps it.
    const double expected = rpm * (c.val - 2) / c.val;
    // Resolution: one tach edge more or less in a 1 s window changes the value by 15 RPM, scaled by
    // 1 s / on time. At 24 of 250 ticks, that is about 156 RPM, as large as FANCTLPRINT_RPM_MIN.
    const double on_ticks_per_second = static_cast<double>(c.val) * ticks_per_second / c.max_value;
    const double step = 15.0 * ticks_per_second / on_ticks_per_second;
    CAPTURE(expected, step);
    const double measured = tach.getRPM();
    CHECK(measured > expected - 2 * step);
    CHECK(measured < rpm + 2 * step);
}

namespace {

constexpr uint16_t min_rpm = 150;

CFanCtl3Wire::FanParams fan_params() {
    return {
        .min_pwm = 5,
        .max_pwm = 50,
        .min_rpm = min_rpm,
        .max_rpm = 6850,
        .thr_pwm = 20,
        .autofan = is_autofan_t::no,
        .skip_tacho = skip_tacho_t::no,
        .min_pwm_to_measure_rpm = 0,
        .has_inverted_pwm = false,
    };
}

struct FanUnderTest {
    bool powered = false;
    uint32_t tick = 0;
    SimulatedFan fan { 3000.0 };
    CFanCtl3Wire ctl {
        [this](bool v) { powered = v; },
        [this]() { return fan.tach(tick, powered); },
        fan_params()
    };

    /// Runs \p ticks ticks. Returns the number of ticks with the fan powered.
    uint32_t run(uint32_t ticks) {
        uint32_t on = 0;
        for (uint32_t i = 0; i < ticks; ++i, ++tick) {
            ctl.tick();
            on += powered;
        }
        return on;
    }
};

} // namespace

TEST_CASE("CFanCtl3Wire: start, run and stall", "[fanctl]") {
    FanUnderTest t;
    REQUIRE(t.ctl.get_state() == CFanCtlCommon::idle);

    // 25%: 12 of 50 ticks.
    t.ctl.set_pwm(64);
    t.run(1);
    REQUIRE(t.ctl.get_state() == CFanCtlCommon::starting);

    SECTION("the start uses full power until the tach shows rotation") {
        // A new PWM value takes effect at the start of the next period. Thus the full power
        // starts after the current period (50 ticks here, 250 ticks at 4 Hz dynamic PWM).
        CHECK(t.run(49) == 0);
        // FANCTL_START_EDGES edges at 3000 RPM (200 edges/s) take about 20 ms.
        const uint32_t on = t.run(50);
        CHECK(t.ctl.get_state() == CFanCtlCommon::rpm_stabilization);
        CHECK(on >= 25);
    }

    SECTION("a running fan has an RPM in range") {
        t.run(FANCTL_RPM_DELAY + 3 * ticks_per_second);
        REQUIRE(t.ctl.get_state() == CFanCtlCommon::running);
        CHECK(t.ctl.get_rpm_is_ok());
        // 12 of 50 ticks: the measurement is about 2/12 low (see the CFanCtlTach test).
        CHECK(t.ctl.get_actual_rpm() > 0.95 * t.fan.rpm * 10 / 12);
        // The set PWM, not full power: 12 of 50 ticks.
        CHECK(t.run(50 * 20) == 12 * 20);
    }

    SECTION("a stopped fan gives error_running, and the PWM stays at the set value") {
        t.run(FANCTL_RPM_DELAY + 3 * ticks_per_second);
        REQUIRE(t.ctl.get_state() == CFanCtlCommon::running);

        t.fan.rpm = 0;
        t.run(5 * ticks_per_second);
        CHECK(t.ctl.get_state() == CFanCtlCommon::error_running);
        CHECK_FALSE(t.ctl.get_rpm_is_ok());

        // Current behaviour: the error state does not start the fan again at full power.
        // A fan that stopped at a low PWM thus stays stopped until the PWM goes to 0 and back.
        CHECK(t.run(50 * 20) == 12 * 20);
    }
}
