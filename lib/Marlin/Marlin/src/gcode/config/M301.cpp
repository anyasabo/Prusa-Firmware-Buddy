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

#include "../../inc/MarlinConfig.h"

#if ENABLED(PIDTEMP)

    #include "../gcode.h"
    #include "../../module/temperature.h"
    #include <gcode/gcode_parser.hpp>

/** \addtogroup G-Codes
 * @{
 */

/**
 *### M301: Get/Set PID parameters <a href="https://reprap.org/wiki/G-code#M301:_Set_PID_parameters">M301: Set PID parameters</a>
 *
 *#### Usage
 *
 *    M301 [ E | P | I | D | C ]
 *
 *#### Parameters
 *
 * -`E` - What tool to set the parameters to (default 0)
 * -`P` - Kp term
 * -`I` - Ki term
 * -`D` - Kd term
 * -`C` - Kc term
 *
 * Without parameters prints the current PID parameters
 */
void GcodeSuite::M301() {
    GCodeParser2 p;
    if (!p.parse_marlin_command()) {
        return;
    }

    // default behavior (omitting E parameter) is to update for extruder 0 only
    const auto e = p.option<uint8_t>('E').value_or(0); // extruder being updated
    if (e >= PhysicalToolIndex::count) {
        SERIAL_ERROR_MSG(MSG_INVALID_EXTRUDER);
        return;
    }

    const auto tool = PhysicalToolIndex::from_raw(e);
    Hotend &hotend = Hotend::for_tool(tool);

    HotendPIDConfig pid = hotend.nozzle_pid_config();
    (void)p.store_option_if_present('P', pid.Kp);
    if (auto val = p.option<float>('I')) {
        pid.Ki = scalePID_i(*val);
    }
    if (auto val = p.option<float>('D')) {
        pid.Kd = scalePID_d(*val);
    }
    #if ENABLED(PID_EXTRUSION_SCALING)
    (void)p.store_option_if_present('C', pid.Kc);
    #endif

    hotend.set_nozzle_pid_config(pid);

    SERIAL_ECHO_START();
    if constexpr (PhysicalToolIndex::count > 1) {
        SERIAL_ECHOPAIR(" e:", e); // specify extruder in serial output
    }
    SERIAL_ECHOPAIR(" p:", pid.Kp,
        " i:", unscalePID_i(pid.Ki),
        " d:", unscalePID_d(pid.Kd));
    #if ENABLED(PID_EXTRUSION_SCALING)
    // Kc does not have scaling applied above, or in resetting defaults
    SERIAL_ECHOPAIR(" c:", pid.Kc);
    #endif
    SERIAL_EOL();
}

/** @}*/

#endif // PIDTEMP
