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
#pragma once

/**
 * Bed skew correction of the Planner, ported from upstream Marlin (module/planner.h).
 *
 * Buddy: the code is in a base class of Planner, so that the unit tests of to_machine_pos() use it too.
 */

#include <inc/MarlinConfigPre.h> // angle brackets: the unit tests use a stub
#include <core/types.h>

#if ENABLED(SKEW_CORRECTION)

static_assert(ENABLED(SKEW_CORRECTION_GCODE) && ENABLED(SKEW_CORRECTION_FOR_Z),
  "Buddy supports skew correction only with SKEW_CORRECTION_GCODE and SKEW_CORRECTION_FOR_Z");

#ifndef COORDINATE_OKAY
  #define COORDINATE_OKAY(N,L,H) WITHIN(N,L,H)
#endif

// Buddy: a named struct with operator==, for the config store
struct skew_factor_t {
  float xy, xz, yz;

  bool operator==(const skew_factor_t &) const = default;
};

class PlannerSkew {
  public:
    static inline skew_factor_t skew_factor { XY_SKEW_FACTOR, XZ_SKEW_FACTOR, YZ_SKEW_FACTOR };

    /// Buddy: number of active skew disablers (see module/prusa/skew.hpp).
    /// While it is not 0, skew() and unskew() do nothing, and skew_factor keeps the M852 values.
    static inline uint8_t skew_disabled = 0;

    /// Buddy: true if skew() and unskew() can change a position
    static bool skew_active() { return !skew_disabled && skew_factor != skew_factor_t { 0, 0, 0 }; }

    FORCE_INLINE static void skew(float &cx, float &cy, const float cz) {
      if (skew_disabled) return; // Buddy
      if (COORDINATE_OKAY(cx, X_MIN_POS + 1, X_MAX_POS) && COORDINATE_OKAY(cy, Y_MIN_POS + 1, Y_MAX_POS)) {
        const float sx = cx - cy * skew_factor.xy - cz * (skew_factor.xz - (skew_factor.xy * skew_factor.yz)),
                    sy = cy - cz * skew_factor.yz;
        if (COORDINATE_OKAY(sx, X_MIN_POS, X_MAX_POS) && COORDINATE_OKAY(sy, Y_MIN_POS, Y_MAX_POS)) {
          cx = sx; cy = sy;
        }
      }
    }
    FORCE_INLINE static void skew(xyz_pos_t &raw) { skew(raw.x, raw.y, raw.z); }

    FORCE_INLINE static void unskew(float &cx, float &cy, const float cz) {
      if (skew_disabled) return; // Buddy
      if (COORDINATE_OKAY(cx, X_MIN_POS, X_MAX_POS) && COORDINATE_OKAY(cy, Y_MIN_POS, Y_MAX_POS)) {
        const float sx = cx + cy * skew_factor.xy + cz * skew_factor.xz,
                    sy = cy + cz * skew_factor.yz;
        if (COORDINATE_OKAY(sx, X_MIN_POS, X_MAX_POS) && COORDINATE_OKAY(sy, Y_MIN_POS, Y_MAX_POS)) {
          cx = sx; cy = sy;
        }
      }
    }
    FORCE_INLINE static void unskew(xyz_pos_t &raw) { unskew(raw.x, raw.y, raw.z); }
};

#else

class PlannerSkew {};

#endif // SKEW_CORRECTION
