#pragma once
// Stand-in for motion.h: the move hints, current_position and the MBL transform. MBL is off:
// machine and native positions are the same.
#include "../inc/MarlinConfig.h"
#include <tool_index.hpp>

struct MoveHints {
    bool is_printing_move : 1 = false;
    bool extrusion_safety_checks : 1 = true;
    bool ignore_e_factor : 1 = false;
    bool is_service_extruder_move : 1 = false;
};

inline xyze_pos_t current_position {};

inline xyz_pos_t to_native_pos(const MachinePosXYZ &pos) { return pos; }
inline xyze_pos_t to_native_pos(const MachinePosXYZE &pos) { return pos; }
inline MachinePosXYZ to_machine_pos(const xyz_pos_t &pos) { return pos; }
inline MachinePosXYZE to_machine_pos(const xyze_pos_t &pos) { return pos; }
inline bool is_xy_in_print_region(const xy_pos_t &) { return true; }
