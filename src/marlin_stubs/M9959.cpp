#include <algorithm>
#include <common/gcode/gcode_parser.hpp>
#include <buddy/cpu_prof.h>
#include <logging/log.hpp>

#include "PrusaGcodeSuite.hpp"

LOG_COMPONENT_REF(Marlin);

/**
 *### M9959: EXPERIMENT: minimum sleep of the GUI loop
 *
 *#### Usage
 *
 *    M9959 [ G ]
 *
 *#### Parameters
 *
 * - `G` - Minimum sleep in ms (1 to 10) when the screen has nothing to redraw. 1 is stock: the GUI
 *         loop then runs at about 1 kHz at idle. The value stays until the next M9959 G or a
 *         restart. The metrics gui_prof, gui_prof2, gui_prof3 and gui_min show the effect.
 */
void PrusaGcodeSuite::M9959() {
    GCodeParser2 p;
    if (!p.parse_marlin_command()) {
        return;
    }
    if (const auto g = p.option<uint32_t>('G')) {
        buddy_gui_delay_min = std::clamp<uint32_t>(*g, 1, 10);
    }
    log_info(Marlin, "M9959: gui_delay_min=%u ms", static_cast<unsigned>(buddy_gui_delay_min));
}
