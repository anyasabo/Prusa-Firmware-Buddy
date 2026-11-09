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
 *    M9959 [ G ] [ P ] [ R ]
 *
 *#### Parameters
 *
 * - `G` - Minimum sleep in ms (1 to 10) when the screen has nothing to redraw. 1 is stock: the GUI
 *         loop then runs at about 1 kHz at idle. The value stays until the next M9959 G or a
 *         restart. The metrics gui_prof, gui_prof2, gui_prof3 and gui_min show the effect.
 * - `P` - Phase stepping rest fast path: 0 = stock (boot value), 1 = skip the refresh of an axis at
 *         rest, with a full refresh each 1 ms. The metric phstep_prof shows the effect.
 * - `R` - Phase stepping ISR body: 0 = from flash (stock, boot value), 1 = the same code from RAM.
 */
void PrusaGcodeSuite::M9959() {
    GCodeParser2 p;
    if (!p.parse_marlin_command()) {
        return;
    }
    if (const auto g = p.option<uint32_t>('G')) {
        buddy_gui_delay_min = std::clamp<uint32_t>(*g, 1, 10);
    }
    if (const auto ph = p.option<uint32_t>('P')) {
        buddy_phstep_fast = *ph ? 1 : 0;
    }
    if (const auto r = p.option<uint32_t>('R')) {
        buddy_phstep_ram = *r ? 1 : 0;
    }
    log_info(Marlin, "M9959: gui_delay_min=%u ms, phstep_fast=%u, phstep_ram=%u", static_cast<unsigned>(buddy_gui_delay_min), static_cast<unsigned>(buddy_phstep_fast),
        static_cast<unsigned>(buddy_phstep_ram));
}
