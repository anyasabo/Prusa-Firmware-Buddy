/// @file
#include "gcode_pressure_advance_calibration.hpp"

#include "calibrated_pressure_advance.hpp"
#include "pressure_advance_calibration.hpp"

#include <array>
#include <cmath>
#include <cstdio>

#include <Marlin/src/core/serial.h>
#include <Marlin/src/gcode/parser.h>
#include <fsm/pressure_advance_calibration_phases.hpp>
#include <marlin_server.hpp>

namespace {

using pressure_advance_calibration::Error;
using pressure_advance_calibration::Result;
using pressure_advance_calibration::Stage;
using Phase = PhasePressureAdvanceCalibration;

void report(const Result &result) {
    std::array<char, 128> buffer;
    if (result.error == Error::none) {
        snprintf(buffer.data(), buffer.size(), "Pressure advance calibration: %.3f (rise %.4f s, fall %.4f s, spread %.2f, load step %.0f g)",
            static_cast<double>(result.pressure_advance), static_cast<double>(result.tau_rising), static_cast<double>(result.tau_falling),
            static_cast<double>(result.spread), static_cast<double>(result.load_step));
    } else {
        snprintf(buffer.data(), buffer.size(), "Pressure advance calibration failed: %s", pressure_advance_calibration::to_string(result.error));
    }
    SERIAL_ECHO_START();
    SERIAL_ECHOLN(buffer.data());
}

class Wizard {
public:
    void run() {
        Phase phase = Phase::intro;
        while (phase != Phase::finish) {
            phase = handle(phase);
        }
    }

private:
    marlin_server::FSM_Holder holder { Phase::intro };
    FSMPressureAdvanceCalibrationData data;
    float pressure_advance = 0;

    void change(Phase phase) {
        marlin_server::fsm_change(phase, fsm::serialize_data(data));
    }

    Phase handle(Phase phase) {
        switch (phase) {
        case Phase::intro:
            return marlin_server::wait_for_response(Phase::intro) == Response::Continue ? measure() : Phase::finish;

        case Phase::result:
            if (marlin_server::wait_for_response(Phase::result) == Response::Yes) {
                pressure_advance_calibration::save(pressure_advance);
            }
            return Phase::finish;

        case Phase::failed:
            return marlin_server::wait_for_response(Phase::failed) == Response::Retry ? measure() : Phase::finish;

        case Phase::preparing:
        case Phase::measuring:
        case Phase::finish:
            break;
        }
        return Phase::finish;
    }

    Phase measure() {
        data = {};
        Phase shown = Phase::preparing;
        change(shown);

        const Result result = pressure_advance_calibration::run({
            .on_progress = [&](Stage stage, uint8_t percent) {
                const Phase phase = (stage == Stage::preparing) ? Phase::preparing : Phase::measuring;
                if (phase != shown || percent != data.progress_percent) {
                    shown = phase;
                    data.progress_percent = percent;
                    change(phase);
                }
                return phase != Phase::preparing || marlin_server::get_response_from_phase(Phase::preparing) != Response::Abort;
            },
        });
        report(result);

        switch (result.error) {
        case Error::none:
            pressure_advance = result.pressure_advance;
            data.pressure_advance_100us = static_cast<uint16_t>(std::lround(result.pressure_advance * 10000.f));
            change(Phase::result);
            return Phase::result;
        case Error::aborted:
            return Phase::finish;
        default:
            data.error = static_cast<uint8_t>(result.error);
            change(Phase::failed);
            return Phase::failed;
        }
    }
};

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
 * The result is printed to the serial line. A saved result stays in use for the loaded filament
 * until it is unloaded, also across restarts; M572 from G-code is ignored meanwhile, unless allowed
 * in the settings.
 *
 *#### Usage
 *
 *    M1572 [ S | A ]
 *
 *#### Parameters
 *
 * - `S` - Silent: no dialogs, only measure and print the result
 * - `A` - Save and use the measured value (silent mode only; the wizard asks)
 *
 *#### Examples
 *
 *    M1572     ; Run the calibration wizard
 *    M1572 S A ; Measure the pressure advance and use it
 */
void PrusaGcodeSuite::M1572() {
    if (!parser.seen('S')) {
        Wizard().run();
        return;
    }

    const Result result = pressure_advance_calibration::run();
    report(result);
    if (result.error == Error::none && parser.seen('A')) {
        pressure_advance_calibration::save(result.pressure_advance);
    }
}

/** @}*/
