#pragma once
#include "../core/types.h"
inline uint16_t stepper_microsteps(AxisEnum) { return 16; }
inline uint16_t stepper_mscnt(AxisEnum) { return 0; }
inline void motor_prepare_move_xy() {}
inline void motor_prepare_move_z() {}
inline void motor_prepare_move_e() {}
