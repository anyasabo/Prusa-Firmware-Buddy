#include "axes.hpp"
#include "lut.hpp"
#include <array>
#include <module/planner.h>
#include <config_store/store_instance.hpp>
#include <bsod/bsod.h>

struct AxisMotorParams {
    int steps_per_rev = 0;
    float pos_to_phase = 0;
    float pos_to_steps = 0;
    float pos_to_msteps = 0;
    float mm_to_rev = 0;
    float rev_to_mm = 0;
    int phase_per_ustep = 0;

    static AxisMotorParams make_for_motor(int steps_per_rev, float steps_per_unit, int microsteps) {
        return {
            .steps_per_rev = steps_per_rev,
            .pos_to_phase = 256 * steps_per_unit / microsteps,
            .pos_to_steps = steps_per_unit,
            .pos_to_msteps = steps_per_unit / PLANNER_STEPS_MULTIPLIER,
            .mm_to_rev = 1.f / (steps_per_rev * float(microsteps) / steps_per_unit),
            .rev_to_mm = steps_per_rev * float(microsteps) / steps_per_unit,
            .phase_per_ustep = 256 / microsteps
        };
    };
};

static std::array<AxisMotorParams, phase_stepping::opts::SUPPORTED_AXIS_COUNT> axis_motor_params;

/// Steps/mm that the step generators use for the axis.
///
/// Take steps/mm from the planner, not from the config store. M92 changes only the planner,
/// so the config store value made phase stepping ignore M92: the phase stepped axes moved at
/// the config store steps/mm and wrote step counts that the planner read at the M92 value.
/// Use planner.mm_per_step, not planner.settings.axis_steps_per_mm: the classic step
/// generators (precise_stepping, input_shaper, pressure_advance) use mm_per_step, and only
/// Planner::refresh_positioning() writes it. Thus phase stepping and the classic step
/// generators agree also if code writes the settings without a refresh (in v8.0.2,
/// Motion_Parameters::reset() and the power panic resume do this).
static float planner_steps_per_unit(AxisEnum axis) {
    // Zero before the first refresh_positioning(), in settings.first_load().
    debug_assert(planner.mm_per_step[axis] > 0.f);
    return 1.f / planner.mm_per_step[axis];
}

void phase_stepping::initialize_axis_motor_params() {
    int xy_steps_per_rev = get_has_400step_xy_motors() ? 400 : 200;

    axis_motor_params[0] = AxisMotorParams::make_for_motor(xy_steps_per_rev, planner_steps_per_unit(X_AXIS), get_microsteps_x());
    axis_motor_params[1] = AxisMotorParams::make_for_motor(xy_steps_per_rev, planner_steps_per_unit(Y_AXIS), get_microsteps_y());
    if (opts::SUPPORTED_AXIS_COUNT > 2) {
        axis_motor_params[2] = AxisMotorParams::make_for_motor(200, planner_steps_per_unit(Z_AXIS), get_microsteps_z());
    }
    if (opts::SUPPORTED_AXIS_COUNT > 3) {
        axis_motor_params[3] = AxisMotorParams::make_for_motor(200, planner_steps_per_unit(E_AXIS), get_microsteps_e());
    }
}

int phase_stepping::get_motor_steps(AxisEnum axis) {
    return axis_motor_params[static_cast<int>(axis)].steps_per_rev;
}

int32_t phase_stepping::pos_to_phase(AxisEnum axis, float position) {
    return normalize_motor_phase(static_cast<int>(position * axis_motor_params[axis].pos_to_phase));
}

float phase_stepping::phase_to_pos(AxisEnum axis, int32_t phase) {
    return phase / axis_motor_params[axis].pos_to_phase;
}

int32_t phase_stepping::pos_to_steps(AxisEnum axis, float position) {
    return static_cast<int32_t>(position * axis_motor_params[static_cast<int>(axis)].pos_to_steps);
}

int32_t phase_stepping::pos_to_msteps(AxisEnum axis, float position) {
    return static_cast<int32_t>(position * axis_motor_params[static_cast<int>(axis)].pos_to_msteps);
}

float phase_stepping::mm_to_rev(AxisEnum axis, float mm) {
    return mm * axis_motor_params[static_cast<int>(axis)].mm_to_rev;
}

float phase_stepping::rev_to_mm(AxisEnum axis, float revs) {
    return revs * axis_motor_params[static_cast<int>(axis)].rev_to_mm;
}

int phase_stepping::phase_per_ustep(AxisEnum axis) {
    return axis_motor_params[static_cast<int>(axis)].phase_per_ustep;
}
