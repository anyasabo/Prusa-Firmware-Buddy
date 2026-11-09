#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <tuple>

#include <module/temperature/heater_watch.hpp>
#include <module/temperature/thermal_runaway.hpp>

namespace {

uint32_t now_ms = 1;

/// The values of one heater, from the printer configuration.
struct Config {
    const char *name;
    int16_t watch_increase; // WATCH_TEMP_INCREASE or WATCH_BED_TEMP_INCREASE
    uint16_t watch_period_s; // WATCH_TEMP_PERIOD or WATCH_BED_TEMP_PERIOD
    int16_t temp_hysteresis; // TEMP_HYSTERESIS or TEMP_BED_HYSTERESIS
    uint16_t protection_period_s; // THERMAL_PROTECTION_PERIOD or _BED_PERIOD
    uint16_t protection_hysteresis; // THERMAL_PROTECTION_HYSTERESIS or _BED_HYSTERESIS
    float target;
    float heat_rate; // C/s during the heat-up

    /// Where HeaterWatch stops (base_hotend.cpp, temperature.cpp).
    int16_t watch_stop() const { return watch_increase + temp_hysteresis + 1; }
};

constexpr Config core_one_l_hotend { "CORE One L hotend", 2, 20, 3, 80, 15, 290, 3 };
constexpr Config mini_hotend { "MINI and XL Dwarf hotend", 2, 20, 3, 20, 6, 280, 3 };
constexpr Config mk4_hotend { "MK4 hotend", 2, 20, 3, 60, 12, 280, 3 };
constexpr Config mk4_bed { "MK4 bed", 2, 60, 3, 60, 10, 110, 0.5f };

constexpr uint32_t step_ms = 100;

/// The heater protection as BaseHotend (or the bed code) runs it: the heating watch and the
/// thermal runaway protection.
struct Protection {
    const Config &c;
    ThermalRunaway runaway;
    HeaterWatch watch;
    bool watch_fired = false;

    explicit Protection(const Config &config)
        : c(config)
        , watch({ .temp_increase = c.watch_increase, .period_s = c.watch_period_s, .min_temp_diff = c.watch_stop() }) {
        now_ms = 1;
        watch.arm(static_cast<int16_t>(c.target));
    }

    /// Runs \p duration_s seconds with the reading from \p reading(t), t in seconds from now.
    /// @returns the time in seconds from now when the runaway protection fired, if it fired.
    std::optional<float> run(float duration_s, const std::function<float(float)> &reading, std::optional<float> target = std::nullopt) {
        const uint32_t start = now_ms;
        for (uint32_t t = 0; t < duration_s * 1000; t += step_ms, now_ms += step_ms) {
            const float temp = reading(t / 1000.0f);
            watch_fired |= watch.update(temp);
            if (runaway.step(temp, target.value_or(c.target), c.protection_period_s, c.protection_hysteresis)) {
                return (now_ms - start) / 1000.0f;
            }
        }
        return std::nullopt;
    }

    /// Heats from 25 C at the heat rate of the configuration, and stops at \p plateau.
    std::optional<float> heat_to(float plateau) {
        return run((plateau - 25) / c.heat_rate + 60, [&](float t) { return std::min(25 + c.heat_rate * t, plateau); });
    }

    /// The reading falls to room temperature: the thermistor came out of the heater block.
    /// The error must come after the protection period (from the last reading in the band,
    /// one step before the first low reading).
    void require_error_after_thermistor_out() {
        const auto fired = run(300, [](float) { return 30.0f; });
        REQUIRE(fired.has_value());
        CHECK(*fired >= c.protection_period_s - step_ms / 1000.0f);
        CHECK(*fired <= c.protection_period_s + 1);
    }
};

} // namespace

uint32_t millis() {
    return now_ms;
}

TEST_CASE("thermal protection: the reading reaches the target, then falls", "[thermal]") {
    const Config c = GENERATE(core_one_l_hotend, mini_hotend, mk4_hotend, mk4_bed);
    CAPTURE(c.name);

    Protection p(c);
    REQUIRE_FALSE(p.heat_to(c.target + 0.5f).has_value());
    CHECK_FALSE(p.watch_fired);
    p.require_error_after_thermistor_out();
}

TEST_CASE("thermal protection: the reading stops under the target, then falls", "[thermal]") {
    // HeaterWatch stops at watch_stop() under the target. Before the fix, ThermalRunaway waited
    // for the exact target, so a reading that stopped in this band had no protection at all.
    const Config c = GENERATE(core_one_l_hotend, mini_hotend, mk4_hotend, mk4_bed);
    // From just under the target to exactly where the watch stops.
    const float fraction = GENERATE(0.1f, 0.5f, 1.0f);
    const float under = fraction * c.watch_stop() - 0.01f;
    CAPTURE(c.name, under);
    REQUIRE(c.watch_stop() <= c.protection_hysteresis); // the overlap that base_hotend.cpp asserts

    Protection p(c);
    REQUIRE_FALSE(p.heat_to(c.target - under).has_value());
    // The reading stays there for a long time. The stable band accepts it, and the watch is off.
    REQUIRE_FALSE(p.run(1800, [&](float) { return c.target - under; }).has_value());
    CHECK_FALSE(p.watch_fired);
    CHECK_FALSE(p.watch.is_running());

    p.require_error_after_thermistor_out();
}

TEST_CASE("thermal protection: a stall exactly at the hysteresis arms the protection", "[thermal]") {
    const Config c = GENERATE(core_one_l_hotend, mini_hotend, mk4_hotend, mk4_bed);
    CAPTURE(c.name);

    Protection p(c);
    const float plateau = c.target - c.protection_hysteresis;
    // The watch can fire at this stall. Only the runaway part matters here.
    std::ignore = p.heat_to(plateau);
    REQUIRE_FALSE(p.run(600, [&](float) { return plateau; }).has_value());
    p.require_error_after_thermistor_out();
}

TEST_CASE("thermal protection: a slow, noisy heat-up through the band gives no error", "[thermal]") {
    const Config c = GENERATE(core_one_l_hotend, mini_hotend, mk4_hotend, mk4_bed);
    CAPTURE(c.name);

    Protection p(c);
    const float start = c.target - 2 * c.protection_hysteresis;
    REQUIRE_FALSE(p.heat_to(start).has_value());
    // 0.5 C/s through the band, with +-1 C of noise, to the target.
    const auto fired = p.run(600, [&](float t) { return std::min(start + 0.5f * t, c.target) + std::sin(t * 7.3f); });
    CHECK_FALSE(fired.has_value());
}

TEST_CASE("thermal protection: a short drop under the band does not give an error", "[thermal]") {
    const Config &c = core_one_l_hotend;
    Protection p(c);
    REQUIRE_FALSE(p.heat_to(c.target).has_value());

    // For example a fan at full speed. The reading comes back within the period.
    const auto fired = p.run(600, [&](float t) { return t < c.protection_period_s - 10 ? c.target - 25 : c.target; });
    CHECK_FALSE(fired.has_value());
}

TEST_CASE("thermal protection: a lower target", "[thermal]") {
    const Config &c = core_one_l_hotend;
    Protection p(c);
    REQUIRE_FALSE(p.heat_to(c.target).has_value());

    // The new target is under the reading. The protection is armed at once.
    constexpr float lower_target = 250;
    REQUIRE_FALSE(p.run(
                       60, [&](float t) { return std::max(c.target - 2 * t, lower_target); }, lower_target)
                      .has_value());

    const auto fired = p.run(
        300, [](float) { return 30.0f; }, lower_target);
    REQUIRE(fired.has_value());
    CHECK(*fired <= c.protection_period_s + 1);
}
