/**
 * Marlin 3D Printer Firmware
 * Copyright (c) 2020 MarlinFirmware [https://github.com/MarlinFirmware/Marlin]
 *
 * Based on Sprinter and grbl.
 * Copyright (c) 2011 Camiel Gubbels / Erik van der Zalm
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

#include "../../inc/MarlinConfig.h"

#if ENABLED(SKEW_CORRECTION_GCODE)

#include "../gcode.h"
#include "../../module/planner.h"
#include "../../module/motion.h"
#include <config_store/store_instance.hpp>

/** \addtogroup G-Codes
 * @{
 */

/**
 *### M852: Bed Skew Compensation <a href="https://marlinfw.org/docs/gcode/M852.html">M852: Bed Skew Compensation</a>
 *
 * Get or set the machine skew factors; correct for misalignment
 *
 *#### Usage
 *
 *    M852 [ S | I | J | K | W ]
 *
 *#### Parameters
 *
 * - `S` - Alias for 'I'
 * - `I` - New XY skew factor
 * - `J` - New XZ skew factor
 * - `K` - New YZ skew factor
 * - `W` - Buddy: write the factors to the config store (Marlin uses M500)
 *
 * Without parameters, M852 reports the current values.
 * Buddy: the factors must be within SKEW_FACTOR_MIN and SKEW_FACTOR_MAX (-0.005 to 0.005).
 */
void GcodeSuite::M852() {
  if (!parser.seen("SIJKW")) return M852_report();

  uint8_t badval = 0, setval = 0;

  if (parser.seenval('I') || parser.seenval('S')) {
    const float value = parser.value_linear_units();
    if (WITHIN(value, SKEW_FACTOR_MIN, SKEW_FACTOR_MAX)) {
      if (planner.skew_factor.xy != value) {
        planner.skew_factor.xy = value;
        ++setval;
      }
    }
    else
      ++badval;
  }

  #if ENABLED(SKEW_CORRECTION_FOR_Z)

    if (parser.seenval('J')) {
      const float value = parser.value_linear_units();
      if (WITHIN(value, SKEW_FACTOR_MIN, SKEW_FACTOR_MAX)) {
        if (planner.skew_factor.xz != value) {
          planner.skew_factor.xz = value;
          ++setval;
        }
      }
      else
        ++badval;
    }

    if (parser.seenval('K')) {
      const float value = parser.value_linear_units();
      if (WITHIN(value, SKEW_FACTOR_MIN, SKEW_FACTOR_MAX)) {
        if (planner.skew_factor.yz != value) {
          planner.skew_factor.yz = value;
          ++setval;
        }
      }
      else
        ++badval;
    }

  #endif

  if (badval)
    SERIAL_ECHOLNPGM(MSG_SKEW_MIN " " STRINGIFY(SKEW_FACTOR_MIN) " " MSG_SKEW_MAX " " STRINGIFY(SKEW_FACTOR_MAX));

  // When skew is changed the current position changes
  if (setval) {
    // Buddy: wait for the queued moves, so the stepper position is the end of the last move
    planner.synchronize();
    set_current_from_steppers_for_axis(ALL_AXES_ENUM);
    sync_plan_position();
    report_current_position();
  }

  // Buddy: there is no M500, so W writes the factors to the config store (as M593 W does)
  if (parser.seen('W'))
    config_store().skew_factor.set(planner.skew_factor);
}

void GcodeSuite::M852_report(const bool forReplay/*=true*/) {
  if (!forReplay) {
    SERIAL_ECHO_START();
    SERIAL_ECHOLNPGM("Skew Factor:");
  }
  SERIAL_ECHO_START();
  SERIAL_ECHOPAIR_F("  M852 I", planner.skew_factor.xy, 6);
  #if ENABLED(SKEW_CORRECTION_FOR_Z)
    SERIAL_ECHOPAIR_F(" J", planner.skew_factor.xz, 6);
    SERIAL_ECHOPAIR_F(" K", planner.skew_factor.yz, 6);
    SERIAL_ECHOLNPGM(" ; XY, XZ, YZ");
  #else
    SERIAL_ECHOLNPGM(" ; XY");
  #endif
}

/** @}*/

#endif // SKEW_CORRECTION_GCODE
