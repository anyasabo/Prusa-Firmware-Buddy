#pragma once
// Stand-in for the configuration chain of the Marlin G-code parser. The values are the values of a
// CORE One L build:
// - FASTER_GCODE_PARSER and PRINT_CHECKING_Q_CMDS: include/marlin/Configuration_COREONEL_adv.h
// - USE_GCODE_SUBCODES: lib/Marlin/Marlin/src/inc/Conditionals_post.h
// - NUM_SERIAL 1: SERIAL_PORT_2 is not set in include/marlin/Configuration_COREONEL.h
// - HAS_MMU2() 1: option/has_mmu2.h of the build (see ../option/has_mmu2.h)
// DEBUG_GCODE_PARSER and EXPECTED_PRINTER_CHECK stay off, as in the firmware.
#include <algorithm>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../core/macros.h"

#define FASTER_GCODE_PARSER
#define PRINT_CHECKING_Q_CMDS
#define USE_GCODE_SUBCODES ANY(G38_PROBE_TARGET, PRINT_CHECKING_Q_CMDS)
#define NUM_SERIAL         1
#define CPU_32_BIT

// From core/types.h and core/millis_t.h
enum AxisEnum : uint8_t { X_AXIS,
    Y_AXIS,
    Z_AXIS,
    E_AXIS };
using feedRate_t = float;
using millis_t = uint32_t;
#define MMM_TO_MMS(MM_M) feedRate_t(static_cast<float>(MM_M) / 60.0f)

// From core/serial.h and core/language.h, for GCodeParser::unknown_command_error()
#define MSG_UNKNOWN_COMMAND    "Unknown command: \""
#define SERIAL_ECHO_START()    ((void)0)
#define SERIAL_ECHOLNPAIR(...) ((void)0)
