#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <optional>
#include <tuple>

#include <module/temperature/heater_watch.hpp>
#include <module/temperature/thermal_runaway.hpp>

namespace {

uint32_t now_ms = 1;

// Values of the CORE One L hotend (Configuration_COREONEL.h, Configuration_COREONEL_adv.h).
constexpr uint16_t protection_period_s = 80; // THERMAL_PROTECTION_PERIOD
constexpr uint16_t protection_hysteresis = 15; // THERMAL_PROTECTION_HYSTERESIS
constexpr int16_t temp_hysteresis = 3; // TEMP_HYSTERESIS

struct WatchValues {
    int16_t increase; // WATCH_TEMP_INCREASE
    uint16_t period_s; // WATCH_TEMP_PERIOD
};
constexpr WatchValues watch_core_one_l { 2, 20 };
constexpr WatchValues watch_indx { 20, 5 };

constexpr float target = 290;
constexpr uint32_t step_ms = 100;

/// The hotend protection as BaseHotend runs it: the heating watch and the thermal runaway.
struct Protection {
    ThermalRunaway runaway;
    HeaterWatch watch;

    explicit Protection(WatchValues w)
        : watch({ .temp_increase = w.increase, .period_s = w.period_s, .min_temp_diff = static_cast<int16_t>(w.increase + temp_hysteresis + 1) }) {
        now_ms = 1;
        watch.arm(static_cast<int16_t>(target));
    }

    /// Runs \p duration_s seconds with the reading from \p reading(t), t in seconds from now.
    /// @returns the time in seconds from now when the runaway protection fired, if it fired.
    std::optional<float> run(float duration_s, const std::function<float(float)> &reading, float current_target = target) {
        const uint32_t start = now_ms;
        for (uint32_t t = 0; t < duration_s * 1000; t += step_ms, now_ms += step_ms) {
            const float temp = reading(t / 1000.0f);
            std::ignore = watch.update(temp);
            if (runaway.step(temp, current_target, protection_period_s, protection_hysteresis)) {
                return (now_ms - start) / 1000.0f;
            }
        }
        return std::nullopt;
    }

    /// Heats at \p rate C/s from 25 C, and stops at \p plateau.
    std::optional<float> heat_to(float plateau, float rate) {
        return run((plateau - 25) / rate + 60, [&](float t) { return std::min(25 + rate * t, plateau); });
    }
};

constexpr auto thermistor_out = [](float) { return 30.0f; };

} // namespace

uint32_t millis() {
    return now_ms;
}

TEST_CASE("thermal runaway: the reading reaches the target, then falls", "[thermal]") {
    Protection p(watch_core_one_l);
    REQUIRE_FALSE(p.heat_to(target + 0.5f, 3).has_value());

    const auto fired = p.run(300, thermistor_out);
    REQUIRE(fired.has_value());
    // The period starts at the last reading in the band, one step before the first low reading.
    CHECK(*fired >= protection_period_s - step_ms / 1000.0f);
    CHECK(*fired <= protection_period_s + 1);
}

TEST_CASE("thermal runaway: the reading stops just under the target, then falls", "[thermal]") {
    // The heating watch disarms at (increase + TEMP_HYSTERESIS + 1) under the target. If the
    // runaway protection waited for the exact target, a reading that stops in this band would
    // leave the heater without protection.
    const auto [watch, rate, under] = GENERATE(
        std::tuple { watch_core_one_l, 3.0f, 0.5f },
        std::tuple { watch_core_one_l, 3.0f, 4.0f },
        std::tuple { watch_core_one_l, 3.0f, 5.5f },
        std::tuple { watch_indx, 6.0f, 4.0f });
    CAPTURE(watch.increase, under);

    Protection p(watch);
    REQUIRE_FALSE(p.heat_to(target - under, rate).has_value());
    // The reading stays there for a long time. The stable band accepts it.
    REQUIRE_FALSE(p.run(1800, [&](float) { return target - under; }).has_value());

    const auto fired = p.run(300, thermistor_out);
    REQUIRE(fired.has_value());
    // The period starts at the last reading in the band, one step before the first low reading.
    CHECK(*fired >= protection_period_s - step_ms / 1000.0f);
    CHECK(*fired <= protection_period_s + 1);
}

TEST_CASE("thermal runaway: a short drop under the band does not give an error", "[thermal]") {
    Protection p(watch_core_one_l);
    REQUIRE_FALSE(p.heat_to(target, 3).has_value());

    // For example a fan at full speed. The reading comes back within the period.
    const auto fired = p.run(600, [](float t) { return t < protection_period_s - 10 ? target - 25 : target; });
    CHECK_FALSE(fired.has_value());
}

TEST_CASE("thermal runaway: a lower target", "[thermal]") {
    Protection p(watch_core_one_l);
    REQUIRE_FALSE(p.heat_to(target, 3).has_value());

    // The new target is under the reading. The protection is armed at once.
    constexpr float lower_target = 250;
    REQUIRE_FALSE(p.run(
                       60, [=](float t) { return std::max(target - 2 * t, lower_target); }, lower_target)
                      .has_value());

    const auto fired = p.run(300, thermistor_out, lower_target);
    REQUIRE(fired.has_value());
    CHECK(*fired <= protection_period_s + 1);
}
