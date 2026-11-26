#pragma once

#include <core/types.h>

MachinePosXYZ to_machine_pos(const xyz_pos_t &pos);
xyz_pos_t to_native_pos(const MachinePosXYZ &pos);

inline MachinePosXYZE to_machine_pos(const xyze_pos_t &pos) {
    MachinePosXYZE result = pos;
    result.set(to_machine_pos(pos.xyz()));
    return result;
}

inline xyze_pos_t to_native_pos(const MachinePosXYZE &pos) {
    xyze_pos_t result = pos;
    result.set(to_native_pos(pos.xyz()));
    return result;
}

// Defined by the tests that use them (skew_tests.cpp)
extern xyze_pos_t current_position;
void set_current_position(const xyze_pos_t &native);
MachinePosXYZE current_machine_position();
void sync_plan_position();
