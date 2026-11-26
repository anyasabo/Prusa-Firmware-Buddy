#pragma once
// Stand-in for Marlin.h: stepper enables and idle(). planner_fakes.cpp defines idle().
#include "inc/MarlinConfig.h"

void idle(bool waiting = false, bool no_stepper_sleep = false);

inline constexpr uint8_t active_extruder = 0;
#define DEBUGGING(F) false

inline void enable_XY() {}
inline void enable_Z() {}
inline void enable_E(uint8_t) {}
inline void disable_E(uint8_t) {}
inline void disable_all_steppers() {}
