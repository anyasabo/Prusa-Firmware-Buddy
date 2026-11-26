/// Fake of src/common/marlin_server_types/fsm/nozzle_mismatch_phases.hpp (the real one pulls in all printer options)
#pragma once
#include <cstdint>

enum class Response : uint8_t {
    _none,
    Abort,
    Back,
    Retry,
};

enum class PhaseNozzleMismatch : uint8_t {
    prompt,
    dock_selection,
    parking,
    dock_not_empty,
    tool_lost,
    homing,
    pickup_failed,
    park_failed,
    confirm_abort,
    _cnt,
    _last = _cnt - 1
};
