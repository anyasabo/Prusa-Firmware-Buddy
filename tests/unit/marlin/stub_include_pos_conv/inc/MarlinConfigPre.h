#pragma once

#include <cstdint>

#include <core/macros.h>

#define NUM_AXES     3
#define LOGICAL_AXES 4
#define E_STEPPERS   1

#define HAS_EXTRUDERS               1
#define HAS_MESH                    1
#define ENABLE_LEVELING_FADE_HEIGHT 1

// Skew correction with the axis limits of the CORE One L
#define SKEW_CORRECTION
#define SKEW_CORRECTION_FOR_Z
#define SKEW_CORRECTION_GCODE
#define XY_SKEW_FACTOR  0.0f
#define XZ_SKEW_FACTOR  0.0f
#define YZ_SKEW_FACTOR  0.0f
#define SKEW_FACTOR_MIN -0.005
#define SKEW_FACTOR_MAX 0.005
#define X_MIN_POS       -2
#define X_MAX_POS       302
#define Y_MIN_POS       -8
#define Y_MAX_POS       300
