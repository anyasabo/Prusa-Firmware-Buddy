#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <tuple>
#include <vector>

#include <module/temperature/heater_watch.hpp>
#include <module/temperature/thermal_runaway.hpp>

namespace {

uint32_t now_ms = 1;

/// The values of one heater, from the printer configuration.
struct Config {
    /// How the firmware sets a new target.
    enum class Kind {
        /// BaseHotend::set_nozzle_target_temp(): only a changed target arms the watch and resets
        /// the runaway protection.
        hotend,
        /// Temperature::setTargetBed(): each call arms the watch. ThermalRunaway::step() resets
        /// itself when the target changes.
        bed,
    };

    const char *name;
    Kind kind;
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

using enum Config::Kind;
constexpr Config core_one_l_hotend { "CORE One L hotend", hotend, 2, 20, 3, 80, 15, 290, 3 };
constexpr Config mini_hotend { "MINI and XL Dwarf hotend", hotend, 2, 20, 3, 20, 6, 280, 3 };
constexpr Config mk4_hotend { "MK4 hotend", hotend, 2, 20, 3, 60, 12, 280, 3 };
constexpr Config mk4_bed { "MK4 bed", bed, 2, 60, 3, 60, 10, 110, 0.5f };
constexpr Config core_one_bed { "CORE One bed", bed, 2, 120, 3, 20, 10, 110, 0.5f };

constexpr uint32_t step_ms = 100;

/// The heater protection as BaseHotend (or the bed code) runs it: the heating watch and the
/// thermal runaway protection.
struct Protection {
    const Config &c;
    ThermalRunaway runaway;
    HeaterWatch watch;
    bool watch_fired = false;

    float set_target_value = 0;

    explicit Protection(const Config &config, uint32_t start_ms = 1)
        : c(config)
        , watch({ .temp_increase = c.watch_increase, .period_s = c.watch_period_s, .min_temp_diff = c.watch_stop() }) {
        now_ms = start_ms;
        set_target(c.target);
    }

    /// Sets a new target as the firmware does for this kind of heater.
    void set_target(float new_target) {
        if (c.kind == hotend && new_target == set_target_value) {
            return;
        }
        set_target_value = new_target;
        watch.arm(static_cast<int16_t>(new_target));
        if (c.kind == hotend) {
            runaway.reset(new_target);
        }
    }

    /// Runs \p duration_s seconds. Each \p interval_s seconds, the G-code sets the next target
    /// of \p targets (in a loop).
    std::optional<float> run_with_targets(float duration_s, float interval_s, const std::vector<float> &targets, const std::function<float(float)> &reading) {
        const uint32_t start = now_ms;
        size_t next = 0;
        uint32_t next_set_ms = 0;
        for (uint32_t t = 0; t < duration_s * 1000; t += step_ms, now_ms += step_ms) {
            if (t >= next_set_ms) {
                set_target(targets[next]);
                next = (next + 1) % targets.size();
                next_set_ms += static_cast<uint32_t>(interval_s * 1000);
            }
            if (step(reading(t / 1000.0f))) {
                return (now_ms - start) / 1000.0f;
            }
        }
        return std::nullopt;
    }

    /// One step of the checks, in the order of BaseHotend::manage() or Temperature::manage_heater().
    /// @returns true if one of the checks gave an error.
    bool step(float temp) {
        if (c.kind == hotend) {
            return runaway.step(temp, set_target_value, c.protection_period_s, c.protection_hysteresis) || watch.update(temp);
        } else {
            return watch.update(temp) || runaway.step(temp, set_target_value, c.protection_period_s, c.protection_hysteresis);
        }
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
    const Config c = GENERATE(core_one_l_hotend, mini_hotend, mk4_hotend, mk4_bed, core_one_bed);
    CAPTURE(c.name);

    Protection p(c);
    REQUIRE_FALSE(p.heat_to(c.target + 0.5f).has_value());
    CHECK_FALSE(p.watch_fired);
    p.require_error_after_thermistor_out();
}

TEST_CASE("thermal protection: the reading stops under the target, then falls", "[thermal]") {
    // HeaterWatch stops at watch_stop() under the target. Before the fix, ThermalRunaway waited
    // for the exact target, so a reading that stopped in this band had no protection at all.
    const Config c = GENERATE(core_one_l_hotend, mini_hotend, mk4_hotend, mk4_bed, core_one_bed);
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
    const Config c = GENERATE(core_one_l_hotend, mini_hotend, mk4_hotend, mk4_bed, core_one_bed);
    CAPTURE(c.name);

    Protection p(c);
    const float plateau = c.target - c.protection_hysteresis;
    // The watch can fire at this stall. Only the runaway part matters here.
    std::ignore = p.heat_to(plateau);
    REQUIRE_FALSE(p.run(600, [&](float) { return plateau; }).has_value());
    p.require_error_after_thermistor_out();
}

TEST_CASE("thermal protection: a slow, noisy heat-up through the band gives no error", "[thermal]") {
    const Config c = GENERATE(core_one_l_hotend, mini_hotend, mk4_hotend, mk4_bed, core_one_bed);
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

namespace {

/// A G-code that sets a new target more often than the watch period: for example the idle
/// temperature of a parked tool and the print temperature on each tool change.
float retarget_interval_s(const Config &c) {
    return c.watch_period_s / 2.0f;
}

} // namespace

TEST_CASE("thermal protection: a thermistor out of the heater, and targets that change often (TH-1)", "[thermal]") {
    const Config c = GENERATE(core_one_l_hotend, mini_hotend, mk4_hotend, mk4_bed, core_one_bed);
    // The reading stays at room temperature while the heater is on.
    const float delta = GENERATE(0.0f, 1.0f, 40.0f);
    CAPTURE(c.name, delta);

    Protection p(c);
    const auto fired = p.run_with_targets(1800, retarget_interval_s(c), { c.target, c.target - delta }, [](float) { return 30.0f; });
    REQUIRE(fired.has_value());
    // The watch period, plus one interval until the first period starts.
    CHECK(*fired <= c.watch_period_s + retarget_interval_s(c) + 1);
}

TEST_CASE("thermal protection: the thermistor falls out at the target, and targets that change often (TH-1)", "[thermal]") {
    const Config c = GENERATE(core_one_l_hotend, mini_hotend, mk4_hotend, mk4_bed, core_one_bed);
    const float delta = GENERATE(0.0f, 1.0f, 10.0f);
    CAPTURE(c.name, delta);

    Protection p(c);
    REQUIRE_FALSE(p.heat_to(c.target).has_value());
    const auto fired = p.run_with_targets(1800, retarget_interval_s(c), { c.target - delta, c.target }, [](float) { return 30.0f; });
    REQUIRE(fired.has_value());
    CHECK(*fired <= std::max<float>(c.protection_period_s, c.watch_period_s + retarget_interval_s(c)) + 1);
}

TEST_CASE("thermal protection: a heat-up with targets that change often gives no error", "[thermal]") {
    const Config c = GENERATE(core_one_l_hotend, mini_hotend, mk4_hotend, mk4_bed, core_one_bed);
    const float delta = GENERATE(1.0f, 10.0f, 40.0f);
    CAPTURE(c.name, delta);

    Protection p(c);
    // The heater holds the lower target.
    const float low = c.target - delta;
    float reading = 25;
    const auto fired = p.run_with_targets(1800, retarget_interval_s(c), { c.target, low }, [&](float) {
        if (reading < p.set_target_value) {
            reading = std::min(reading + c.heat_rate * step_ms / 1000.0f, p.set_target_value);
        } else {
            reading = std::max(reading - 0.5f * step_ms / 1000.0f, p.set_target_value);
        }
        return reading;
    });
    CHECK_FALSE(fired.has_value());
}

TEST_CASE("thermal protection: a lower target during a watch period gives no error", "[thermal]") {
    const Config c = GENERATE(core_one_l_hotend, mini_hotend, mk4_hotend, mk4_bed, core_one_bed);
    CAPTURE(c.name);

    Protection p(c);
    // Half of the watch period of heating, then a target under the reading. The heater turns off,
    // the reading falls to the new target and stays there.
    const float half = c.watch_period_s / 2.0f;
    const float peak = 25 + c.heat_rate * half;
    const float low_target = peak - 10;
    REQUIRE_FALSE(p.run(half, [&](float t) { return 25 + c.heat_rate * t; }).has_value());
    p.set_target(low_target);
    float reading = peak;
    for (uint32_t t = 0; t < 600'000; t += step_ms, now_ms += step_ms) {
        reading = std::max(reading - 0.5f * step_ms / 1000.0f, low_target);
        REQUIRE_FALSE(p.step(reading));
    }
}

TEST_CASE("thermal protection: the checks work across the wrap of the millisecond tick", "[thermal]") {
    const Config c = GENERATE(core_one_l_hotend, mini_hotend, mk4_hotend, mk4_bed, core_one_bed);
    // The wrap occurs during the heat-up, during the stable time, or after the thermistor falls out.
    const uint32_t before_wrap_s = GENERATE(10u, 400u, 1000u);
    CAPTURE(c.name, before_wrap_s);

    SECTION("the reading reaches the target, then falls") {
        Protection p(c, static_cast<uint32_t>(0 - before_wrap_s * 1000));
        REQUIRE_FALSE(p.heat_to(c.target).has_value());
        REQUIRE_FALSE(p.run(600, [&](float) { return c.target; }).has_value());
        p.require_error_after_thermistor_out();
    }

    SECTION("the reading stays at room temperature") {
        Protection p(c, static_cast<uint32_t>(0 - before_wrap_s * 1000));
        bool fired = false;
        for (uint32_t t = 0; t < (c.watch_period_s + 1) * 1000u && !fired; t += step_ms, now_ms += step_ms) {
            fired = p.step(30);
        }
        CHECK(fired);
    }
}

TEST_CASE("heater watch: a restart starts a new period (INDX park and head reset)", "[thermal]") {
    // IndxHotend::start_heating() and the head reset path restart the protection with the same
    // target. manage(), and thus update(), does not run while the tool is parked.
    constexpr HeaterWatch::Config indx { .temp_increase = 20, .period_s = 5, .min_temp_diff = 24 };
    now_ms = 1;
    HeaterWatch w(indx);
    w.arm(250);
    REQUIRE_FALSE(w.update(100)); // the period starts: 120 C after 5 s
    now_ms += 2000;
    REQUIRE_FALSE(w.update(110));

    SECTION("park") {
        // The tool is parked for 60 s and cools.
        now_ms += 60'000;
        w.restart(250);
        CHECK_FALSE(w.update(60));
        now_ms += 4000;
        CHECK_FALSE(w.update(75));
        now_ms += 2000;
        // 20 C more than at the restart: no error.
        CHECK_FALSE(w.update(81));
    }

    SECTION("head reset") {
        // The head resets, the heating stops for a short time.
        now_ms += 1000;
        w.restart(250);
        now_ms += 2500;
        CHECK_FALSE(w.update(110));
        now_ms += 4000;
        CHECK_FALSE(w.update(131));
    }

    SECTION("a restart still finds a heater that does not heat") {
        now_ms += 60'000;
        w.restart(250);
        CHECK_FALSE(w.update(60));
        now_ms += 5000;
        CHECK(w.update(61));
    }
}
