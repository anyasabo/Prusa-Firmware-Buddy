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

} // namespace pressure_advance_calibration

namespace PrusaGcodeSuite {

void M1572();

}
