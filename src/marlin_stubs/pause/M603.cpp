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

#include "config_features.h"

#include <option/has_pause.h>
static_assert(HAS_PAUSE());

#include "../../../lib/Marlin/Marlin/src/gcode/gcode.h"
#include "../../../lib/Marlin/Marlin/src/feature/pause.h"
#include <utils/variant_utils.hpp>
#include "../../../lib/Marlin/Marlin/src/module/motion.h"
#include "../../../lib/Marlin/Marlin/src/module/printcounter.h"

#if EXTRUDERS > 1
    #include "../../../lib/Marlin/Marlin/src/module/tool_change.h"
#endif

/** \addtogroup G-Codes
 * @{
 */

/**
 *### M603: Configure filament change <a href="https://reprap.org/wiki/G-code#M603:_Configure_Filament_Change">M603: Configure Filament Change</a>
 *
 *#### Usage
 *
 *    M603 [ T | U | L ]
 *
 *#### Parameters
 *
 * - `T` - Target extruder
 * - `U` - Amount of retraction for unload (negative)
 * - `L` - Load length, longer for bowden (positive)
 */
void GcodeSuite::M603() {

    const std::optional<VirtualToolIndex> virtual_tool = stdext::get_optional<VirtualToolIndex>(get_target_virtual_from_command());
    if (!virtual_tool.has_value()) {
        return;
    }

    // Unload length
    if (parser.seenval('U')) {
        fc_settings[*virtual_tool].unload_length = ABS(parser.value_axis_units(E_AXIS));
#if ENABLED(PREVENT_LENGTHY_EXTRUDE)
        NOMORE(fc_settings[*virtual_tool].unload_length, EXTRUDE_MAXLENGTH);
#endif
    }

    // Load length
    if (parser.seenval('L')) {
        fc_settings[*virtual_tool].load_length = ABS(parser.value_axis_units(E_AXIS));
#if ENABLED(PREVENT_LENGTHY_EXTRUDE)
        NOMORE(fc_settings[*virtual_tool].load_length, EXTRUDE_MAXLENGTH);
#endif
    }
}

/** @}*/
