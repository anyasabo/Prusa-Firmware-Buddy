#pragma once
// Stand-in for the stepper: only the counters that the step event consumer changes.
#include "../inc/MarlinConfig.h"
#include "../core/mtypes.hpp"

class Stepper {
public:
    static inline abce_steps_t count_position {};
    static inline abce_steps_t count_position_from_startup {};
    static inline abce_steps_t count_position_last_block {};
    static inline xyze_int8_t count_direction { 1, 1, 1, 1 };
    static inline uint8_t axis_did_move = 0;
    static inline uint8_t last_direction_bits = 0;

    static void _set_position(const xyze_steps_t &pos) { count_position = { pos.x, pos.y, pos.z, pos.e }; }
    static bool axis_is_moving(AxisEnum axis) { return axis_did_move & (1 << axis); }
};

inline Stepper stepper;
