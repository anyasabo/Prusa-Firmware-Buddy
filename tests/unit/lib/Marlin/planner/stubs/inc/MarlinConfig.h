#pragma once
// The configuration (MarlinConfigPre.h), the real core types, and the step and move timers of the
// motion_klipper_diff stubs (motion_diff_hal.h, a copy of its inc/MarlinConfig.h).
#include "MarlinConfigPre.h"
#include "../core/types.h"
// The step event queue holds the logical directions. The pins (and their inversion) are not used.
// motion_diff_hal.h defines the inversions as false.
#undef INVERT_X_DIR
#undef INVERT_Y_DIR
#undef INVERT_Z_DIR
#undef INVERT_E0_DIR
#include "motion_diff_hal.h"

#include <cstring>

// From the HAL (Arduino.h)
#define sq(x)     ((x) * (x))
#define UNUSED(x) ((void)(x))

#define HAS_PLANNER() 1

// core/serial.h: the planner prints only warnings.
#define PGM_P                  const char *
#define PSTR(s)                (s)
#define SERIAL_ECHO_MSG(...)   ((void)0)
#define SERIAL_CHAR(...)       ((void)0)
#define SERIAL_ECHOPGM(...)    ((void)0)
#define SERIAL_ECHOLNPAIR(...) ((void)0)
#define serialprintPGM(...)    ((void)0)
#define STEPPER_ISR_ENABLED()  false
inline constexpr char axis_codes[] = { 'X', 'Y', 'Z', 'E' };
