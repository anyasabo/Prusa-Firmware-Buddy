/// @file
#pragma once

#include <marlin_server_types/client_response.hpp>

enum class PhasePressureAdvanceCalibration : PhaseUnderlyingType {
    intro,
    preparing,
    measuring,
    result,
    failed,
    finish,
    _last = finish,
};

constexpr inline ClientFSM client_fsm_from_phase(PhasePressureAdvanceCalibration) { return ClientFSM::PressureAdvanceCalibration; }

namespace ClientResponses {
inline constexpr EnumArray<PhasePressureAdvanceCalibration, PhaseResponses, CountPhases<PhasePressureAdvanceCalibration>()> pressure_advance_calibration_responses {
    { PhasePressureAdvanceCalibration::intro, { Response::Continue, Response::Abort } },
    { PhasePressureAdvanceCalibration::preparing, { Response::Abort } },
    { PhasePressureAdvanceCalibration::measuring, {} },
    { PhasePressureAdvanceCalibration::result, { Response::Yes, Response::No } },
    { PhasePressureAdvanceCalibration::failed, { Response::Retry, Response::Abort } },
    { PhasePressureAdvanceCalibration::finish, {} },
};
}

/// Data struct passed between the FSM and GUI
struct FSMPressureAdvanceCalibrationData {
    /// Measured pressure advance [0.1 ms]
    uint16_t pressure_advance_100us = 0;
    /// Progress of the measurement [%]
    uint8_t progress_percent = 0;
    /// pressure_advance_calibration::Error
    uint8_t error = 0;
};
