#include <feature/xbuddy_extension/chamber_alarm.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace buddy;

namespace {

using Inputs = ChamberAlarm::Inputs;

constexpr Inputs normal { .critical_flag = false, .overheating_flag = false, .at_critical_temp = false, .printing = true, .overheating_warning_active = false };
constexpr Inputs overheating { .critical_flag = false, .overheating_flag = true, .at_critical_temp = false, .printing = true, .overheating_warning_active = false };
constexpr Inputs critical { .critical_flag = true, .overheating_flag = true, .at_critical_temp = true, .printing = true, .overheating_warning_active = false };
/// After the critical temperature, the chamber cooled, but not under the recovery temperature.
constexpr Inputs after_critical { .critical_flag = true, .overheating_flag = true, .at_critical_temp = false, .printing = true, .overheating_warning_active = false };

Inputs not_printing(Inputs inputs) {
    inputs.printing = false;
    return inputs;
}

bool no_actions(const ChamberAlarm::Actions &a) {
    return !a.abort_print && !a.disable_heaters && !a.show_critical_warning && !a.show_overheating_warning;
}

} // namespace

TEST_CASE("chamber alarm: a normal chamber gives no actions", "[chamber]") {
    ChamberAlarm alarm;
    for (int i = 0; i < 10; ++i) {
        CHECK(no_actions(alarm.step(normal)));
        CHECK(no_actions(alarm.step(not_printing(normal))));
    }
}

TEST_CASE("chamber alarm: overheating shows one warning for each episode", "[chamber]") {
    ChamberAlarm alarm;
    auto a = alarm.step(overheating);
    CHECK(a.show_overheating_warning);
    CHECK_FALSE(a.abort_print);
    CHECK_FALSE(a.disable_heaters);

    // The user closes the warning. The chamber is still too hot.
    for (int i = 0; i < 10; ++i) {
        CHECK(no_actions(alarm.step(overheating)));
    }

    // The chamber recovers, then gets too hot again.
    CHECK(no_actions(alarm.step(normal)));
    CHECK(alarm.step(overheating).show_overheating_warning);
}

TEST_CASE("chamber alarm: no second overheating warning while one is active", "[chamber]") {
    ChamberAlarm alarm;
    Inputs active = overheating;
    active.overheating_warning_active = true;
    CHECK(no_actions(alarm.step(active)));
    CHECK(alarm.step(overheating).show_overheating_warning);
}

TEST_CASE("chamber alarm: the critical temperature during a print", "[chamber]") {
    ChamberAlarm alarm;
    // Each step while the print continues: abort, heaters off, warning.
    for (int i = 0; i < 3; ++i) {
        const auto a = alarm.step(critical);
        CHECK(a.abort_print);
        CHECK(a.disable_heaters);
        CHECK(a.show_critical_warning);
        CHECK_FALSE(a.show_overheating_warning);
    }
}

TEST_CASE("chamber alarm: the critical temperature outside a print", "[chamber]") {
    ChamberAlarm alarm;
    const auto a = alarm.step(not_printing(critical));
    CHECK(a.disable_heaters);
    CHECK(a.show_critical_warning);

    // The chamber cools under the recovery temperature: a new episode can start.
    CHECK(no_actions(alarm.step(not_printing(normal))));
    CHECK(alarm.step(not_printing(critical)).disable_heaters);
}

TEST_CASE("chamber alarm: the chamber cools, but not under the recovery temperature", "[chamber]") {
    ChamberAlarm alarm;
    REQUIRE(alarm.step(not_printing(critical)).disable_heaters);

    // The user can heat again outside a print.
    for (int i = 0; i < 10; ++i) {
        CHECK(no_actions(alarm.step(not_printing(after_critical))));
    }

    // A print that starts is aborted.
    const auto a = alarm.step(after_critical);
    CHECK(a.abort_print);
    CHECK(a.disable_heaters);
}

TEST_CASE("chamber alarm: the critical temperature again outside a print turns off the heaters (TH-2)", "[chamber]") {
    ChamberAlarm alarm;
    REQUIRE(alarm.step(not_printing(critical)).disable_heaters);

    // The user turns on the heaters again outside a print, for example a preheat. The chamber
    // gets to the critical temperature again before it cools under the recovery temperature.
    CHECK(no_actions(alarm.step(not_printing(after_critical))));
    const auto a = alarm.step(not_printing(critical));
    CHECK(a.disable_heaters);
    CHECK(a.show_critical_warning);
}

TEST_CASE("chamber alarm: one critical warning for each time at the critical temperature outside a print", "[chamber]") {
    // The user can close the warning. It must not come back at each step while the chamber cools.
    ChamberAlarm alarm;
    REQUIRE(alarm.step(not_printing(critical)).show_critical_warning);
    for (int i = 0; i < 10; ++i) {
        const auto a = alarm.step(not_printing(critical));
        CHECK(a.disable_heaters);
        CHECK_FALSE(a.show_critical_warning);
    }
    CHECK(no_actions(alarm.step(not_printing(after_critical))));
    CHECK(alarm.step(not_printing(critical)).show_critical_warning);
}
