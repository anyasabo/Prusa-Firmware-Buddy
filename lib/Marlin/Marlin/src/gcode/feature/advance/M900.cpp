/**
 * Marlin 3D Printer Firmware
 * Copyright (c) 2019 MarlinFirmware [https://github.com/MarlinFirmware/Marlin]
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "../../../inc/MarlinConfig.h"

#include "../../gcode.h"
#include "../../../module/planner.h"
#include "../../../feature/pressure_advance/pressure_advance_config.hpp"

/** \addtogroup G-Codes
 * @{
 */

/**
 *### M900: Get/Set Linear Advance K-factor <a href="https://reprap.org/wiki/G-code#M900:_Set_Linear_Advance_Scaling_Factors">M900: Set Linear Advance Scaling Factors</a>
 *
 *#### Usage
 *
 *    M900 [ T | K ]
 *
 *#### Parameters
 *
 * - `T` - Tool. Must be the active tool (default): pressure advance is one setting for the
 *         active tool, so a value for another tool is rejected and not applied.
 * - `K` - Set advance K factor
 *
 * With no parameters report Linear Advance K-factor of the active tool
 */
void GcodeSuite::M900() {

  // Slicer plugins (for example the Cura LinearAdvanceSettingPlugin) send `M900 K<k> T<n>` for
  // each extruder. If the firmware applied each of them, the value of the last tool would
  // replace the value of the active tool.
  if (parser.seenval('T')) {
    const int gcode_tool = parser.value_int();
    const auto target = get_target_virtual_from_command();
    const auto *const tool = std::get_if<VirtualToolIndex>(&target);
    if (!tool) {
      // The resolver reported the error
      return;
    }
    if (VirtualToolIndex::currently_selected_opt() != *tool) {
      SERIAL_ECHO_START();
      SERIAL_ECHOPAIR("M900 T", gcode_tool, " ignored: pressure advance applies only to the active tool");
      if (!VirtualToolIndex::currently_selected_opt()) {
        SERIAL_ECHOPGM(", and no tool is active");
      }
      SERIAL_EOL();
      return;
    }
  }

  if (parser.seenval('K')) {
    const float newK = parser.value_float();

    #if HAS_GCODE_COMPATIBILITY()
      if (gcode.compatibility.mk3_compatibility_mode && newK >= 3) {
        // Higher K values on MK3 mean LA version 1.0 => we don't support those
        // Lower values on MK3 are very similar to MK4's, so we can use them and expect OK results.
        return;
      }
    #endif

    if (WITHIN(newK, 0, 10)) {
      const pressure_advance::Config default_config = pressure_advance::Config();
      M572_internal(newK, default_config.smooth_time);
    }
    else
      SERIAL_ECHOLNPGM("?K value out of range (0-10).");
  }
  else {
    SERIAL_ECHO_START();
    SERIAL_ECHOLNPAIR("Advance K=", pressure_advance::get_axis_e_config().pressure_advance);
  }
}

/** @}*/
