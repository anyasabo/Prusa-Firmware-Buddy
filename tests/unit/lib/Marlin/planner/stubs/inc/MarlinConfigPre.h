#pragma once
// The real CORE One L configuration (include/marlin/Configuration_COREONEL*.h) and the real
// Conditionals_LCD.h and Conditionals_adv.h. The full chain (HAL, pins, Conditionals_post.h) needs
// the STM32 HAL. The values of Conditionals_post.h that the planner reads are at the end.
#include <stdint.h>
#include "../core/macros.h"

// device/board.h needs the generated board options. The CORE One L has an xBuddy board.
#define BOARD_IS_BUDDY()   0
#define BOARD_IS_XBUDDY()  1
#define BOARD_IS_XLBUDDY() 0
#define BOARD_IS_DWARF()   0

#include <marlin/Configuration_COREONEL.h>
#include "Conditionals_LCD.h"
#include "../core/drivers.h"
#include <marlin/Configuration_COREONEL_adv.h>
#include "Conditionals_adv.h"

// From Conditionals_post.h
#define IS_CORE     1
#define CORE_IS_XY  1
#define CORESIGN(n) (n)
#if ENABLED(AUTO_BED_LEVELING_UBL)
    #define HAS_LEVELING 1
#endif
#if ENABLED(CLASSIC_JERK)
    #define HAS_CLASSIC_JERK 1
#endif
