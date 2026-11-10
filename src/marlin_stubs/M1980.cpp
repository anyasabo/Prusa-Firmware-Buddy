#include <marlin_stubs/PrusaGcodeSuite.hpp>
#include <feature/door_sensor_calibration/door_sensor_calibration.hpp>

namespace PrusaGcodeSuite {

/** \addtogroup G-Codes
 * @{
 */

/**
 *### M1980: Door Sensor Calibration
 *
 * Internal GCode
 *
 *#### Usage
 *
 *    M1980 [O]
 *
 *### Parameters
 *
 * - 'O' - Only ask the user whether they want to enable the emergency stop or not.
 *
 */
void M1980() {
    GCodeParser2 p;
    if (!p.parse_marlin_command()) {
        return;
    }

    door_sensor_calibration::RunArgs args;
    (void)p.store_option_if_present('O', args.ask_enable_only);

    door_sensor_calibration::run(args);
}

/** @}*/
} // namespace PrusaGcodeSuite
