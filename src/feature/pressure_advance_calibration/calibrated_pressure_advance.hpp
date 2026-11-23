/// @file
#pragma once

#include <optional>

/// Keeps the calibrated pressure advance in effect for the filament it was measured with.
///
/// The value is stored together with the virtual tool and the filament loaded in it, so it survives a
/// restart. It is discarded once that filament is unloaded or replaced.
///
/// G-code (M572, M900) may set the pressure advance as well. Unless overriding is allowed in the
/// settings, a G-code request is remembered but not applied while a calibrated value is in effect;
/// it takes effect once the calibrated value is gone. When overriding is allowed, the last of the two
/// to be set wins.
namespace pressure_advance_calibration {

/// Save the measured pressure advance for the filament loaded in the active tool and start using it.
/// Marlin thread only.
void save(float pressure_advance);

/// Pressure advance saved for the filament loaded in the active tool
std::optional<float> saved_value();

/// Remember the nozzle temperature of a calibration of the filament loaded in the active tool; it is
/// proposed for the next one. Discarded together with the saved value. Marlin thread only.
void save_calibration_temperature(int16_t nozzle_temperature);

/// Nozzle temperature of the last calibration of the filament loaded in the active tool [°C]
std::optional<int16_t> saved_calibration_temperature();

/// Pressure advance settings requested by G-code; nullopt leaves the parameter as requested before.
/// Marlin thread only; waits for the queued moves when the effective settings change.
void gcode_request(std::optional<float> pressure_advance, std::optional<float> smooth_time);

/// Keep the effective pressure advance in sync with the loaded filament and the settings.
/// Call periodically from the Marlin thread.
void step();

} // namespace pressure_advance_calibration
