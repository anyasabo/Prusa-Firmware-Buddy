#include <common/gcode/gcode_parser.hpp>
#include <gcode/calibrate/M958.hpp>
#include <logging/log.hpp>
#include <Marlin.h>

#include "PrusaGcodeSuite.hpp"

LOG_COMPONENT_REF(Marlin);

/**
 *### M9958: EXPERIMENT: select and time the input shaper fitting
 *
 *#### Usage
 *
 *    M9958 [ D ] [ B ]
 *
 *#### Parameters
 *
 * - `D` - 1: the fitting of upstream master (double). 0: the float fitting (the boot default).
 *         The value stays until the next M9958 D or a restart. It applies to M1959 and M958.
 * - `B` - Run the fitting on two fixed test spectra with both algorithms. No motion.
 *         The printer must be idle: the double fitting takes about 30 s or more for each spectrum.
 *
 * Each fit writes the log line "shaper fit: algo=... ms=..." and the metric "shaper_fit".
 */
void PrusaGcodeSuite::M9958() {
    GCodeParser2 p;
    if (!p.parse_marlin_command()) {
        return;
    }
    if (const auto d = p.option<bool>('D')) {
        vibrate_measure::shaper_fit_double = *d;
    }
    log_info(Marlin, "M9958: algo=%s", vibrate_measure::shaper_fit_double ? "double" : "float");
    if (p.has_option('B')) {
        if (printingIsActive()) {
            log_info(Marlin, "M9958: no benchmark during a print"); // it blocks the Marlin task for minutes
            return;
        }
        vibrate_measure::benchmark_fit();
    }
}
