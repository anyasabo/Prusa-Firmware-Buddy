/// @file
#pragma once

#include <cstdint>
#include <optional>
#include <inplace_function.hpp>

/// Pressure advance calibration from the load cell.
///
/// The filament pushes on the hotend with a force proportional to the nozzle pressure, which the load
/// cell measures. The extruder alternates between a low and a high flow rate while parked over the
/// purge position; the time it takes the pressure to settle after each change is the pressure advance
/// that compensates it. A run takes about 7 s and roughly 15 mm of filament.
namespace pressure_advance_calibration {

/// Load cell sample hook; called from the load cell sample processing context.
/// @param time_us sample timestamp
/// @param load load cell reading [g]
/// @param e_pos extruder position at the sample time [mm], NAN when unknown
void on_loadcell_sample(uint32_t time_us, float load, float e_pos);

enum class Stage : uint8_t {
    /// Moving to the purge position and heating the nozzle
    preparing,
    /// Extruding the test pattern
    measuring,
};

enum class Error : uint8_t {
    none,
    /// No filament loaded in the active tool
    no_filament,
    /// The loaded filament is flexible; not supported
    flexible_filament,
    /// The user aborted the calibration
    aborted,
    /// The nozzle did not reach the temperature
    heating_failed,
    /// The test moves did not run or the load cell did not deliver samples
    measurement_failed,
    /// The load did not follow the flow rate (no filament flow or load cell problem)
    no_signal,
    /// The individual steps disagree too much
    inconsistent,
    /// The pressure settles too slowly for the test, even with longer steps
    too_slow,
    /// The result is outside of the plausible range
    out_of_range,
};

/// Short English description of the error, for logs and the serial line
const char *to_string(Error error);

struct Result {
    Error error = Error::none;
    /// Measured pressure advance [s]; valid when error is Error::none
    float pressure_advance = 0;
    /// Median time constant of the steps to the high flow rate [s]
    float tau_rising = 0;
    /// Median time constant of the steps to the low flow rate [s]
    float tau_falling = 0;
    /// Largest relative spread (median absolute deviation / median) of the time constants
    float spread = 0;
    /// Load difference between the flow rates [g]
    float load_step = 0;
    /// Nozzle temperature of the measurement [°C]
    int16_t nozzle_temperature = 0;
};

struct Callbacks {
    /// Reports progress; return false to abort. Aborting is honoured while preparing.
    stdext::inplace_function<bool(Stage stage, uint8_t percent)> on_progress;
};

/// Print temperature of the filament loaded in the active tool [°C]; nullopt when none is loaded
std::optional<int16_t> filament_nozzle_temperature();

/// Run the calibration on the active tool. Blocks until done.
/// Restores the nozzle target temperature, the pressure advance settings and the extruder position.
/// The result is not used until saved (@see calibrated_pressure_advance.hpp).
/// @param nozzle_temperature [°C]; the filament_nozzle_temperature() when nullopt. The melt viscosity and
///        with it the pressure advance depend on it, so it should match the print. Limited to the range
///        allowed for extrusion.
Result run(std::optional<int16_t> nozzle_temperature = std::nullopt, const Callbacks &callbacks = {});

} // namespace pressure_advance_calibration
