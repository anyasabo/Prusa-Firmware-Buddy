#include <feature/xbuddy_extension/chamber_alarm.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace buddy;

namespace {

using Inputs = ChamberAlarm::Inputs;

constexpr Inputs normal { .critical_flag = false, .overheating_flag = false, .printing = true, .overheating_warning_active = false };
constexpr Inputs overheating { .critical_flag = false, .overheating_flag = true, .printing = true, .overheating_warning_active = false };
constexpr Inputs critical { .critical_flag = true, .overheating_flag = true, .printing = true, .overheating_warning_active = false };

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
