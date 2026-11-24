/// @file
#pragma once

#include <cstdint>
#include <optional>

#include <option/has_pressure_advance_calibration.h>

static_assert(HAS_PRESSURE_ADVANCE_CALIBRATION());

namespace pressure_advance_calibration {

/// Run the calibration wizard on the active tool. Marlin thread only.
/// Gives up when the planner drains (power panic, print stop), so it may run within a print.
/// @param nozzle_temperature proposed for the measurement [°C]; the default one when nullopt
void run_wizard(std::optional<int16_t> nozzle_temperature = std::nullopt);

/// The calibration is to be offered after a filament was loaded into the active tool: it is enabled in
/// the settings and possible for that filament.
bool should_offer_after_load();

/// A print starts, from a file or over the serial line: consider a calibration before its first extrusion.
/// Marlin thread only.
void on_print_start();

/// Call before every extrusion move of G-code. Marlin thread only.
///
/// The first extrusion of a print above the preheat temperature is the purge line of the start G-code:
/// the nozzle is at the print temperature and about to be cleaned. If enabled in the settings and the
/// saved calibration is missing or was measured more than a few degrees off the target temperature, the
/// calibration runs there. Afterwards the nozzle is back at the start of the move; the G-code positions,
/// the feedrate, the move destination and the filament retraction are as they were.
void before_extrusion();

} // namespace pressure_advance_calibration

namespace PrusaGcodeSuite {

void M1572();

}
