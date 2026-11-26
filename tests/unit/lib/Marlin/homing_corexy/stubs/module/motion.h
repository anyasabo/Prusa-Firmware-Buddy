#pragma once
// The functions of motion.h that the homing code calls. fake_world.cpp defines them.
#include "../inc/MarlinConfig.h"

inline float base_home_pos(AxisEnum axis) {
    return axis == X_AXIS ? X_HOME_POS : axis == Y_AXIS ? Y_HOME_POS
                                                        : Z_HOME_POS;
}
inline feedRate_t homing_feedrate(AxisEnum a) {
    return a == Z_AXIS ? HOMING_FEEDRATE_Z / 60.f : HOMING_FEEDRATE_XY / 60.f;
}

[[nodiscard]] bool homeaxis(const AxisEnum axis, const feedRate_t fr_mm_s = 0.0, bool invert_home_dir = false,
    void (*enable_wavetable)(AxisEnum) = NULL, bool can_calibrate = true, bool homing_z_with_probe = true, bool throw_homing_failed = true);
void do_blocking_move_to_y(const float &ry, const feedRate_t &fr_mm_s = 0.0f);
MachinePosXYZE current_machine_position();
void line_to_machine_pos(const MachinePosXYZE &target, feedRate_t fr_mm_s);
xyze_pos_t to_native_pos(const MachinePosXYZE &pos);
void set_current_position(const xyze_pos_t &pos);
