#pragma once
// The real CORE One L configuration (include/marlin/Configuration_COREONEL*.h) and the real
// Conditionals_LCD.h and Conditionals_adv.h, as in the planner tests. The full chain (HAL, pins,
// Conditionals_post.h) needs the STM32 HAL. The values of Conditionals_post.h that the homing code
// reads are at the end.
#include <stdint.h>
#include <core/macros.h>

// device/board.h needs the generated board options. The CORE One L has an xBuddy board.
#define BOARD_IS_BUDDY()   0
#define BOARD_IS_XBUDDY()  1
#define BOARD_IS_XLBUDDY() 0
#define BOARD_IS_DWARF()   0

#include <marlin/Configuration_COREONEL.h>
#include <inc/Conditionals_LCD.h>
#include <core/drivers.h>
#include <marlin/Configuration_COREONEL_adv.h>
#include <inc/Conditionals_adv.h>

// From Conditionals_post.h
#define IS_CORE     1
#define CORE_IS_XY  1
#define CORESIGN(n) (n)
#define X_HOME_POS  (X_HOME_DIR < 0 ? X_MIN_POS : X_MAX_POS)
#define Y_HOME_POS  (Y_HOME_DIR < 0 ? Y_MIN_POS : Y_MAX_POS)
#define Z_HOME_POS  0
