#pragma once
#include "MarlinConfigPre.h"
#include <core/types.h>

// CORE One L INDX limits, copied by hand from include/marlin/Configuration_COREONEL.h (v8.0.2).
// If those values change, change them here too.
#define X_MAX_OFFSET                1
#define Y_MAX_OFFSET                1
#define X_MIN_OFFSET                -1
#define Z_MAX_OFFSET                1.45f
#define X_BED_SIZE                  301
#define Y_BED_SIZE                  275
#define X_MIN_POS                   (0 - X_MAX_OFFSET)
#define Y_MIN_POS                   (-35 - Y_MAX_OFFSET)
#define Z_MIN_POS                   (0 - Z_MAX_OFFSET)
#define X_MAX_POS                   (X_BED_SIZE - X_MIN_OFFSET + 10)
#define Y_MAX_POS                   276
#define Z_MAX_POS                   330
#define Y_DOCK_SAFE_OFFSET          28.6f
#define Y_DOCK_PARKING_MIN_SAFE_POS (Y_MIN_POS + Y_DOCK_SAFE_OFFSET + 5.f)
#define HWLIMIT_STEALTH_MAX_FEEDRATE \
    { 140, 140, 12, 100 }
#define XY_PROBE_SPEED_INITIAL     8000
#define HOMING_FEEDRATE_INVERTED_Z 10.f
#define Z_NOZZLE_PARK_RISE         50.0f
#define Z_NOZZLE_PARK_POINT_MIN    100.0f
