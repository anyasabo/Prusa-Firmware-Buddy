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
#include "../../../module/temperature.h"
#include <option/has_modular_bed.h>

#if HAS_MODULAR_BED()

#include "../../../module/modular_heatbed.h"

/** \addtogroup G-Codes
 * @{
 */

/**
 *### M556: Override modular bedled <a href="https://reprap.org/wiki/G-code#M556:_Override_modular_bedled">M556: Override modular bedled</a>
 *
 * Only iX and XL
 *
 *#### Usage
 *
 *    M556 [ X | Y | I | A | D]
 *
 *#### Parameters
 *
 * - `X` - Coordinate on X axis
 * - `Y` - Coordinate on Y axis
 * - `I` - Betlet number
 * - `A` - activate selected betlet
 * - `D` - deactivate selected betlet
 */
void GcodeSuite::M556() {

    // by default set all bedlets
    uint16_t affected_beds = std::numeric_limits<uint16_t>::max();

    // set bedlet based on XY coordinates
    if (parser.seen('X') && parser.seen('Y')) {
        const long x = parser.seenval('X') ? parser.value_long() : -1;
        const long y = parser.seenval('Y') ? parser.value_long() : -1;
        if (!WITHIN(x, 0, X_HBL_COUNT - 1) || !WITHIN(y, 0, Y_HBL_COUNT - 1)) {
            SERIAL_ECHOLNPGM("?X or Y out of range");
            return;
        }

        affected_beds = 1 << advanced_modular_bed->idx(x, y);

    } else if (parser.seen("I")) {
        // set bedlet based on its index
        const long i = parser.seenval('I') ? parser.value_long() : 0;
        if (!WITHIN(i, 1, std::numeric_limits<uint16_t>::digits)) {
            SERIAL_ECHOLNPGM("?I out of range");
            return;
        }
        affected_beds = 1 << (i - 1);
    }

    // now activate/deactivate selected bedlet
    uint16_t active = Temperature::getEnabledBedletMask();
    if (parser.seen("A")) {
        active |= affected_beds;
    } else if (parser.seen("D")) {
        active &= ~affected_beds;
    } else {
        SERIAL_ECHOLNPGM("No operation specified");
        return;
    }

    Temperature::setEnabledBedletMask(active);
}

/** @}*/

#endif
