/// @file
#include "gcode_pressure_advance_calibration.hpp"

#include "pressure_advance_calibration.hpp"

#include <array>
#include <cstdio>

#include <Marlin/src/core/serial.h>
#include <Marlin/src/gcode/parser.h>

namespace {

void report(const pressure_advance_calibration::Result &result) {
    std::array<char, 128> buffer;
    if (result.error == pressure_advance_calibration::Error::none) {
        snprintf(buffer.data(), buffer.size(), "Pressure advance calibration: %.3f (rise %.4f s, fall %.4f s, spread %.2f, load step %.0f g)",
            static_cast<double>(result.pressure_advance), static_cast<double>(result.tau_rising), static_cast<double>(result.tau_falling),
            static_cast<double>(result.spread), static_cast<double>(result.load_step));
    } else {
        snprintf(buffer.data(), buffer.size(), "Pressure advance calibration failed: %s", pressure_advance_calibration::to_string(result.error));
    }
    SERIAL_ECHO_START();
    SERIAL_ECHOLN(buffer.data());
}

} // namespace

/** \addtogroup G-Codes
 * @{
 */

/**
 *### M1572: Pressure advance calibration
 *
 * Measures the pressure advance of the loaded filament with the load cell.
 * Parks over the purge position, heats the nozzle to the filament's print temperature and extrudes
 * about 15 mm of filament, alternating between a low and a high flow rate.
 * The result is printed to the serial line.
 *
 *#### Usage
 *
 *    M1572 [ A ]
 *
 *#### Parameters
 *
 * - `A` - Apply the measured value, like `M572 S<value>`
 *
 *#### Examples
 *
 *    M1572 A ; Measure the pressure advance and use it
 */
void PrusaGcodeSuite::M1572() {
    const auto result = pressure_advance_calibration::run();
    report(result);
    if (result.error == pressure_advance_calibration::Error::none && parser.seen('A')) {
        pressure_advance_calibration::apply(result.pressure_advance);
    }
}

/** @}*/
