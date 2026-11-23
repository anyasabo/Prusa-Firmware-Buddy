/// @file
#include "gcode_pressure_advance_calibration.hpp"

#include "calibrated_pressure_advance.hpp"
#include "pressure_advance_calibration.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <optional>

#include <Marlin/src/Marlin.h>
#include <Marlin/src/core/serial.h>
#include <Marlin/src/gcode/parser.h>
#include <Marlin/src/module/planner.h>
#include <config_store/store_instance.hpp>
#include <filament.hpp>
#include <fsm/pressure_advance_calibration_phases.hpp>
#include <logging/log.hpp>
#include <marlin_server.hpp>
#include <tool_index.hpp>

#include <option/has_mmu2.h>
#if HAS_MMU2()
    #include <feature/prusa/MMU2/mmu2_mk4.h>
#endif

LOG_COMPONENT_REF(PressureAdvanceCalibration);

namespace {

using pressure_advance_calibration::Error;
using pressure_advance_calibration::Result;
using pressure_advance_calibration::Stage;
using Phase = PhasePressureAdvanceCalibration;

/// Waits for the response to the phase; Abort when the planner drains (power panic, print stop)
FSMResponseVariant wait_for_response(Phase phase) {
    while (!planner.draining()) {
        if (const FSMResponseVariant response = marlin_server::get_response_variant_from_phase(phase)) {
            return response;
        }
        idle(true);
    }
    return FSMResponseVariant::make(Response::Abort);
}

bool responded(Phase phase, Response response) {
    return wait_for_response(phase).value_or<Response>(Response::_none) == response;
}

void report(const Result &result) {
    std::array<char, 128> buffer;
    if (result.error == Error::none) {
        snprintf(buffer.data(), buffer.size(), "Pressure advance calibration: %.3f at %d C (rise %.4f s, fall %.4f s, spread %.2f, load step %.0f g)",
            static_cast<double>(result.pressure_advance), result.nozzle_temperature, static_cast<double>(result.tau_rising),
            static_cast<double>(result.tau_falling), static_cast<double>(result.spread), static_cast<double>(result.load_step));
    } else {
        snprintf(buffer.data(), buffer.size(), "Pressure advance calibration failed: %s", pressure_advance_calibration::to_string(result.error));
    }
    SERIAL_ECHO_START();
    SERIAL_ECHOLN(buffer.data());
}

class Wizard {
public:
    /// @param nozzle_temperature proposed for the measurement [°C]; the default one when nullopt
    explicit Wizard(std::optional<int16_t> nozzle_temperature)
        : nozzle_temperature(nozzle_temperature) {}

    void run() {
        Phase phase = Phase::intro;
        while (phase != Phase::finish) {
            phase = handle(phase);
        }
    }

private:
    marlin_server::FSM_Holder holder { Phase::intro };
    FSMPressureAdvanceCalibrationData data;
    std::optional<int16_t> nozzle_temperature;
    /// Result of the last measurement, saved only when confirmed
    float pressure_advance = 0;
    int16_t measured_temperature = 0;

    void change(Phase phase) {
        marlin_server::fsm_change(phase, fsm::serialize_data(data));
    }

    Phase handle(Phase phase) {
        switch (phase) {
        case Phase::intro:
            return responded(Phase::intro, Response::Continue) ? select_temperature() : Phase::finish;

        case Phase::select_temperature: {
            const FSMResponseVariant response = wait_for_response(Phase::select_temperature);
            if (response.holds_alternative<int16_t>()) {
                nozzle_temperature = response.value_or<int16_t>(0);
            } else if (response.value_or<Response>(Response::_none) != Response::Continue) {
                return Phase::finish;
            }
            return measure();
        }

        case Phase::result:
            switch (wait_for_response(Phase::result).value_or<Response>(Response::_none)) {
            case Response::Yes:
                pressure_advance_calibration::save(pressure_advance, measured_temperature);
                break;
            case Response::Retry:
                return select_temperature();
            default:
                break;
            }
            return Phase::finish;

        case Phase::failed:
            return responded(Phase::failed, Response::Retry) ? select_temperature() : Phase::finish;

        case Phase::preparing:
        case Phase::measuring:
        case Phase::finish:
            break;
        }
        return Phase::finish;
    }

    Phase select_temperature() {
        if (!nozzle_temperature) {
            nozzle_temperature = pressure_advance_calibration::default_nozzle_temperature();
        }
        if (!nozzle_temperature) {
            // The measurement reports the missing filament
            return measure();
        }
        marlin_server::fsm_change(Phase::select_temperature, fsm::serialize_data(FSMPressureAdvanceCalibrationTemperature { .nozzle_temperature = *nozzle_temperature }));
        return Phase::select_temperature;
    }

    Phase measure() {
        data = {};
        Phase shown = Phase::preparing;
        change(shown);

        const pressure_advance_calibration::Callbacks callbacks {
            .on_progress = [&](Stage stage, uint8_t percent) {
                const Phase phase = (stage == Stage::preparing) ? Phase::preparing : Phase::measuring;
                if (phase != shown || percent != data.progress_percent) {
                    shown = phase;
                    data.progress_percent = percent;
                    change(phase);
                }
                return !planner.draining() && (phase != Phase::preparing || marlin_server::get_response_from_phase(Phase::preparing) != Response::Abort);
            },
        };
        const Result result = pressure_advance_calibration::run(nozzle_temperature, callbacks);
        report(result);

        switch (result.error) {
        case Error::none:
            pressure_advance = result.pressure_advance;
            measured_temperature = result.nozzle_temperature;
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

void pressure_advance_calibration::run_wizard(std::optional<int16_t> nozzle_temperature) {
    Wizard(nozzle_temperature).run();
}

bool pressure_advance_calibration::should_offer_after_load() {
    if (!config_store().pressure_advance_calibrate_after_load.get() || planner.draining()) {
        return false;
    }
#if HAS_MMU2()
    // The MMU loads a filament at every tool change
    if (MMU2::mmu2.Enabled()) {
        return false;
    }
#endif
    const auto tool = VirtualToolIndex::currently_selected_opt();
    if (!tool) {
        return false;
    }
    const FilamentType filament = FilamentType::for_tool(*tool);
    if (filament == FilamentType::none || filament.parameters().is_flexible) {
        return false;
    }
    log_info(PressureAdvanceCalibration, "offering the calibration after a filament load");
    return true;
}

/** \addtogroup G-Codes
 * @{
 */

/**
 *### M1572: Pressure advance calibration
 *
 * Measures the pressure advance of the loaded filament with the load cell.
 * Parks over the purge position, heats the nozzle and extrudes about 15 mm of filament, alternating
 * between a low and a high flow rate. The pressure advance depends on the temperature; measure at the
 * temperature the filament is printed at.
 * The result is printed to the serial line. A saved result stays in use for the loaded filament
 * until it is unloaded, also across restarts; M572 from G-code is ignored meanwhile, unless allowed
 * in the settings.
 *
 *#### Usage
 *
 *    M1572 [ S | A | R ]
 *
 *#### Parameters
 *
 * - `S` - Silent: no dialogs, only measure and print the result
 * - `A` - Save and use the measured value (silent mode only; the wizard asks)
 * - `R` - Nozzle temperature [°C]; by default the one of the saved calibration of the loaded filament,
 *         or its print temperature. The wizard offers it for a change.
 *
 *#### Examples
 *
 *    M1572           ; Run the calibration wizard
 *    M1572 S A       ; Measure the pressure advance and use it
 *    M1572 S A R235  ; Measure at 235 °C and use the result
 */
void PrusaGcodeSuite::M1572() {
    const std::optional<int16_t> nozzle_temperature = parser.seenval('R') ? std::optional<int16_t>(static_cast<int16_t>(parser.value_int())) : std::nullopt;

    if (!parser.seen('S')) {
        pressure_advance_calibration::run_wizard(nozzle_temperature);
        return;
    }

    const Result result = pressure_advance_calibration::run(nozzle_temperature);
    report(result);
    if (result.error == Error::none && parser.seen('A')) {
        pressure_advance_calibration::save(result.pressure_advance, result.nozzle_temperature);
    }
}

/** @}*/
