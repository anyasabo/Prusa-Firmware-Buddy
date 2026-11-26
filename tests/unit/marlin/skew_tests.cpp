/// Tests of the skew guards and the config store item (module/prusa/skew.cpp)
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <limits>

#include <module/prusa/skew.hpp>
#include <module/motion.h>
#include <module/planner.h>
#include <config_store/store_instance.hpp>

// The motion.cpp functions that skew.cpp uses. sync_plan_position() writes the planner position from
// current_position, as in motion.cpp.
xyze_pos_t current_position {};

void set_current_position(const xyze_pos_t &native) {
    current_position = native;
}

MachinePosXYZE current_machine_position() {
    return to_machine_pos(current_position);
}

void sync_plan_position() {
    planner.position_float = to_machine_pos(current_position);
}

namespace {

void reset_state() {
    Planner::set_z_fade_height(0);
    UBL::z_correction = 0;
    UBL::slope = {};
    Planner::skew_factor = { 0, 0, 0 };
    Planner::skew_disabled = 0;
    config_store().skew_factor.set({ 0, 0, 0 });
    current_position = {};
    planner.position_float = {};
    Planner::draining_state = false;
}

/// Puts the printer at the machine position \p m, as line_to_machine_pos() does
void move_to_machine_pos(const MachinePosXYZ &m) {
    planner.position_float.set(m);
    current_position.set(to_native_pos(m));
}

void check_planner_at(const MachinePosXYZ &m) {
    const MachinePosXYZE p = planner.get_machine_position_mm();
    CAPTURE(p.x, p.y, p.z, m.x, m.y, m.z);
    CHECK_THAT(p.x, Catch::Matchers::WithinAbs(m.x, 1e-5f));
    CHECK_THAT(p.y, Catch::Matchers::WithinAbs(m.y, 1e-5f));
    CHECK_THAT(p.z, Catch::Matchers::WithinAbs(m.z, 1e-5f));
}

} // namespace

TEST_CASE("skew guards nest") {
    reset_state();
    Planner::skew_factor = { 0.001f, 0.001f, -0.001f };

    disable_skew();
    disable_skew();
    CHECK(Planner::skew_disabled == 2);
    CHECK(!Planner::skew_active());

    restore_skew();
    CHECK(Planner::skew_disabled == 1);
    CHECK(!Planner::skew_active());

    restore_skew();
    CHECK(Planner::skew_disabled == 0);
    CHECK(Planner::skew_active());

    // A restore without a disable does not underflow
    restore_skew();
    CHECK(Planner::skew_disabled == 0);

    {
        TemporarySkewDisabler disabler;
        CHECK(Planner::skew_disabled == 1);
    }
    CHECK(Planner::skew_disabled == 0);

    // The guards do not change the factors
    CHECK(Planner::skew_factor == skew_factor_t { 0.001f, 0.001f, -0.001f });
}

TEST_CASE("skew guards keep the machine position") {
    reset_state();
    Planner::skew_factor = { 0.001f, 0.001f, -0.001f };

    SECTION("Away from the edge band") {
        const MachinePosXYZ m { 150, 150, 30 };
        move_to_machine_pos(m);
        disable_skew();
        check_planner_at(m);
        CHECK(current_machine_position().xyz() == m); // skew off: native == machine
        restore_skew();
        check_planner_at(m);
    }

    SECTION("In the edge band (CORE One L nozzle cleaner touchpoint)") {
        // Machine Y -7.5 is less than Y_MIN_POS + 1, so skew() does nothing there, but unskew() does:
        // to_machine_pos(to_native_pos(m)) is not m. The planner position must not change all the same.
        const MachinePosXYZ m { 299.5f, -7.5f, 30 };
        move_to_machine_pos(m);
        REQUIRE(to_machine_pos(current_position.xyz()).y != m.y);

        disable_skew();
        check_planner_at(m);
        restore_skew();
        check_planner_at(m);
        CHECK(current_position.xyz() == to_native_pos(m));
    }

    SECTION("New factors (M852), also in the edge band, and E does not change") {
        for (const MachinePosXYZ m : { MachinePosXYZ { 150, 150, 30 }, MachinePosXYZ { 299.5f, -7.5f, 30 } }) {
            CAPTURE(m.x, m.y, m.z);
            Planner::skew_factor = { 0.001f, 0.001f, -0.001f };
            move_to_machine_pos(m);
            current_position.e = 12.5f;

            set_skew_factor({ -0.002f, 0.001f, 0.002f });
            CHECK(Planner::skew_factor == skew_factor_t { -0.002f, 0.001f, 0.002f });
            check_planner_at(m);
            CHECK(current_position.xyz() == to_native_pos(m));
            CHECK(current_position.e == 12.5f);
        }
    }
}

TEST_CASE("skew new factors wait for the queued moves") {
    // If a crash or an abort empties the queue during the wait, the factors must not change: crash recovery
    // replays the moves before the M852 with the old factors, and then the M852 again.
    reset_state();
    Planner::skew_factor = { 0.001f, 0.001f, -0.001f };
    const MachinePosXYZ m { 150, 150, 30 };
    move_to_machine_pos(m);
    const xyze_pos_t native = current_position;

    Planner::draining_state = true;
    set_skew_factor({ -0.002f, 0.001f, 0.002f });
    CHECK(Planner::skew_factor == skew_factor_t { 0.001f, 0.001f, -0.001f });
    CHECK(current_position == native);
    check_planner_at(m);
}

TEST_CASE("skew stored factors") {
    reset_state();

    config_store().skew_factor.set({ 0.001f, -0.002f, 0.003f });
    CHECK(stored_skew_factor() == skew_factor_t { 0.001f, -0.002f, 0.003f });

    // A stored factor out of the M852 limits gives the defaults
    config_store().skew_factor.set({ 0.001f, 0.5f, 0.003f });
    CHECK(stored_skew_factor() == skew_factor_t { XY_SKEW_FACTOR, XZ_SKEW_FACTOR, YZ_SKEW_FACTOR });

    config_store().skew_factor.set({ std::numeric_limits<float>::quiet_NaN(), 0, 0 });
    CHECK(stored_skew_factor() == skew_factor_t { XY_SKEW_FACTOR, XZ_SKEW_FACTOR, YZ_SKEW_FACTOR });
}
