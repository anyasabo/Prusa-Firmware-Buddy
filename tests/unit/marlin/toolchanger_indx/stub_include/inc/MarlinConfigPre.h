/// Minimal Marlin configuration for the INDX toolchanger host tests (CORE One L INDX values).
#pragma once

#include <cstdint>

#include <core/macros.h>
#include <printers.h>

#define NUM_AXES          3
#define LOGICAL_AXES      4
#define E_STEPPERS        1
#define EXTRUDERS         9 // 8 + NoTool
#define HOTENDS           9
#define HAS_HOTEND_OFFSET 1
#define HAS_EXTRUDERS     1
#define HAS_MESH          1
#define HAS_LEVELING      1
#define ARC_SUPPORT       1
#define COREXY            1
#define GRID_MAX_POINTS_X 3
#define GRID_MAX_POINTS_Y 3
