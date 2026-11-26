#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_random.hpp>

#include <axes.hpp>
#include <config_store/store_instance.hpp>
#include <module/planner.h>
#include <feature/motordriver_util.h>

#include <cmath>
#include <cstdlib>

using namespace phase_stepping;

namespace {

// X/Y steps/mm of each belt type. The values are copies of
// AXIS_STEPS_PER_UNIT_2GT_XY and AXIS_STEPS_PER_UNIT_15GT_XY in
// include/marlin/Configuration_COREONEL.h and Configuration_XL.h. The unit
// tests build for the MINI and cannot include those headers.
constexpr float coreone_2gt = 100.0f;
constexpr float coreone_15gt = 101.587f;
constexpr float xl_2gt = 80.0f;
constexpr float xl_15gt = 80.f * 40.f / 40.5f;

/// Sets the X/Y steps/mm in the config store and in the planner, as at boot,
/// and initializes the phase stepping axis parameters.
void boot_with_xy_steps(float steps_per_mm) {
    for (int axis : { X_AXIS, Y_AXIS }) {
        stub_store.steps_per_unit[axis] = steps_per_mm;
        planner.settings.axis_steps_per_mm[axis] = steps_per_mm;
        // At boot, the drivers get the microsteps of the config store.
        stub_driver_microsteps[axis] = stub_store.microsteps[axis];
    }
    planner.refresh_positioning();
    initialize_axis_motor_params();
}

/// Step count of the planner for a position. Same as the conversion in
/// Planner::set_machine_position_mm(): LROUND(mm * steps_per_mm).
int32_t planner_steps(AxisEnum axis, float mm) {
    return std::lround(mm * planner.settings.axis_steps_per_mm[axis]);
}

} // namespace

TEST_CASE("phase_stepping axes: one motor revolution moves one pulley circumference", "[phase_stepping][belts]") {
    // 200 full steps x 16 microsteps per revolution.
    struct Case {
        float steps_per_mm;
        float pulley_mm;
    };
    const auto c = GENERATE(
        Case { coreone_2gt, 16 * 2.0f }, // 16 teeth x 2 mm
        Case { coreone_15gt, 21 * 1.5f }, // 21 teeth x 1.5 mm
        Case { xl_2gt, 20 * 2.0f }, // 20 teeth x 2 mm
        Case { xl_15gt, 27 * 1.5f } // 27 teeth x 1.5 mm
    );
    CAPTURE(c.steps_per_mm, c.pulley_mm);
    boot_with_xy_steps(c.steps_per_mm);

    for (AxisEnum axis : { X_AXIS, Y_AXIS }) {
        CHECK(get_motor_steps(axis) == 200);
        CHECK(phase_per_ustep(axis) == 256 / 16);
        // 101.587 is rounded. The error is less than 1 um for each revolution.
        CHECK(rev_to_mm(axis, 1.f) == Catch::Approx(c.pulley_mm).margin(0.001));
        CHECK(mm_to_rev(axis, c.pulley_mm) == Catch::Approx(1.f).margin(0.0001));
    }
}

TEST_CASE("phase_stepping axes: conversions agree with the planner at both belt types", "[phase_stepping][belts]") {
    const float steps_per_mm = GENERATE(coreone_2gt, coreone_15gt, xl_2gt, xl_15gt);
    CAPTURE(steps_per_mm);
    boot_with_xy_steps(steps_per_mm);

    // One electrical period of the motor (MOTOR_PERIOD = 1024 phase units) is
    // 4 full steps = 64 microsteps.
    const float period_mm = 64.f / steps_per_mm;

    auto gen = Catch::Generators::random(-400.f, 400.f);
    for (int i = 0; i < 2000; i++) {
        gen.next();
        const float mm = gen.get();
        CAPTURE(mm);
        for (AxisEnum axis : { X_AXIS, Y_AXIS }) {
            // Round trip mm -> revolutions -> mm.
            CHECK(rev_to_mm(axis, mm_to_rev(axis, mm)) == Catch::Approx(mm).margin(1e-4));

            // pos_to_steps() truncates and the planner rounds, so they can
            // differ by one step, not more.
            CHECK(std::abs(pos_to_steps(axis, mm) - planner_steps(axis, mm)) <= 1);

            // The phase repeats after one electrical period. Allow one phase
            // unit for float rounding.
            const int a = pos_to_phase(axis, mm);
            const int b = pos_to_phase(axis, mm + period_mm);
            const int d = std::abs(a - b);
            CHECK(std::min(d, opts::MOTOR_PERIOD - d) <= 1);
        }
    }
}

TEST_CASE("phase_stepping axes: a change of belt type changes the distance per phase", "[phase_stepping][belts]") {
    // The same 100 mm move needs 1.587 % more phase at 1.5GT. If the
    // parameters were stale after a belt change, the move would be 1.6 mm short.
    boot_with_xy_steps(coreone_2gt);
    const int32_t steps_2gt = pos_to_steps(X_AXIS, 100.f);
    boot_with_xy_steps(coreone_15gt);
    const int32_t steps_15gt = pos_to_steps(X_AXIS, 100.f);
    CHECK(std::abs(steps_2gt - 10000) <= 1);
    CHECK(std::abs(steps_15gt - 10158) <= 1);
}

TEST_CASE("phase_stepping axes: M92 changes the steps/mm that phase stepping uses", "[phase_stepping][belts][m92]") {
    boot_with_xy_steps(coreone_2gt);

    // M92 X120 Y120: the planner changes and calls refresh_positioning(). M92 does not write
    // the config store. Then M92 calls initialize_axis_motor_params().
    for (int axis : { X_AXIS, Y_AXIS }) {
        planner.settings.axis_steps_per_mm[axis] = 120.f;
    }
    planner.refresh_positioning();
    initialize_axis_motor_params();

    for (AxisEnum axis : { X_AXIS, Y_AXIS }) {
        // The classic stepper makes 1200 steps for 10 mm. Phase stepping must do the same.
        CHECK(std::abs(pos_to_steps(axis, 10.f) - planner_steps(axis, 10.f)) <= 1);
        CHECK(rev_to_mm(axis, 1.f) == Catch::Approx(200 * 16 / 120.f).margin(0.001));
    }
}

TEST_CASE("phase_stepping axes: use the steps/mm of the classic step generators", "[phase_stepping][m92]") {
    // The classic step generators use planner.mm_per_step, which only refresh_positioning()
    // writes. In v8.0.2, Motion_Parameters::reset() (G28, ramming) and the power panic resume
    // write planner.settings.axis_steps_per_mm without a refresh. Phase stepping must agree
    // with the classic step generators also then.
    boot_with_xy_steps(coreone_2gt);
    planner.settings.axis_steps_per_mm[X_AXIS] = 120.f;
    planner.refresh_positioning();
    planner.settings.axis_steps_per_mm[X_AXIS] = coreone_2gt;
    initialize_axis_motor_params();

    CHECK(std::abs(pos_to_steps(X_AXIS, 10.f) - 1200) <= 1);
}

TEST_CASE("phase_stepping axes: M350 with M92 keeps the distance of one motor revolution", "[phase_stepping][m350]") {
    boot_with_xy_steps(coreone_2gt);

    // M350 X32 Y32 changes the drivers, not the config store. M92 X200 Y200 then keeps the
    // distance per motor revolution: 200 full steps x 32 microsteps / 200 steps/mm = 32 mm.
    for (int axis : { X_AXIS, Y_AXIS }) {
        stub_driver_microsteps[axis] = 32;
        planner.settings.axis_steps_per_mm[axis] = 200.f;
    }
    planner.refresh_positioning();
    initialize_axis_motor_params();

    for (AxisEnum axis : { X_AXIS, Y_AXIS }) {
        CHECK(phase_per_ustep(axis) == 256 / 32);
        CHECK(rev_to_mm(axis, 1.f) == Catch::Approx(16 * 2.0f).margin(0.001));
        CHECK(std::abs(pos_to_steps(axis, 10.f) - planner_steps(axis, 10.f)) <= 1);
        // One electrical period (1024 phase units, 4 full steps) is 4 x 32 / 200 mm.
        CHECK(phase_to_pos(axis, opts::MOTOR_PERIOD) == Catch::Approx(4.f * 32.f / 200.f).margin(1e-5));
    }
}

TEST_CASE("phase_stepping axes: M350 without M92 scales the distance as the classic stepper does", "[phase_stepping][m350]") {
    boot_with_xy_steps(coreone_2gt);

    // M350 X32 without M92: the classic stepper makes 100 steps for each mm, at 32 microsteps
    // for each full step. The motor turns half as much. Phase stepping must do the same.
    stub_driver_microsteps[X_AXIS] = 32;
    initialize_axis_motor_params();

    CHECK(rev_to_mm(X_AXIS, 1.f) == Catch::Approx(200 * 32 / coreone_2gt).margin(0.001));
    CHECK(rev_to_mm(Y_AXIS, 1.f) == Catch::Approx(200 * 16 / coreone_2gt).margin(0.001));
}
