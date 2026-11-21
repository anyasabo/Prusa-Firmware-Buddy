#include <common/gcode/gcode_parser.hpp>
#include <logging/log.hpp>

#include "PrusaGcodeSuite.hpp"
#include "../../feature/bedlevel/bedlevel.h"

LOG_COMPONENT_REF(Marlin);

/**
 *### M9961: EXPERIMENT: continue the mesh after a failed probe point
 *
 *#### Usage
 *
 *    M9961 [ S ]
 *
 *#### Parameters
 *
 * - `S` - 1 = on, 0 = off (boot value). With S1, G29 P1 does not stop at a failed point. It leaves
 *         the point empty, lifts the head and probes the other points. At the end it fills the empty
 *         points as the start G-code does (G29 P3.2, then P3.13), logs the fill of each failed point
 *         ("MBL fill at X Y: Z"), and then fails the mesh as before. The retry probes all points
 *         again, so the log has the true Z of the same points to compare with the fill.
 *
 * The setting stays until the next restart.
 */
void PrusaGcodeSuite::M9961() {
    GCodeParser2 p;
    if (!p.parse_marlin_command()) {
        return;
    }
    if (const auto s = p.option<uint32_t>('S')) {
        ubl.g29_continue_after_fail = *s != 0;
    }
    log_info(Marlin, "M9961: continue after a failed probe point = %i", ubl.g29_continue_after_fail);
}
