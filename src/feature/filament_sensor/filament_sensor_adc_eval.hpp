/// @file
#pragma once
#include <limits>

#include <device/board.h>
#include "filament_sensor_states.hpp"

namespace FSensorADCEval {

using Value = int32_t;

static constexpr Value filtered_value_not_ready { std::numeric_limits<Value>::min() }; // invalid value of fs_filtered_value
static constexpr Value ref_value_not_calibrated { std::numeric_limits<Value>::min() }; // invalid value of fs_filtered_value
static constexpr Value lower_limit = // value for detecting disconnected sensor
#if (BOARD_IS_XLBUDDY())
    20;
#else
    2000;
#endif

static constexpr Value upper_limit =
#if (BOARD_IS_XLBUDDY())
    4096; // this is max value of 12 bit ADC, there is no value that would indicate broken sensor on XL
#else
    2'000'000;
#endif

inline bool within_limits(Value value) {
    return value >= lower_limit && value <= upper_limit;
}

/**
 * @brief Evaluate state of filament sensor
 * @param filtered_value current filtered value from ADC
 * @param fs_ref_nins_value Reference value with filament NOT inserted
 * @param fs_ref_ins_value Reference value with filament inserted
 * @param previous_state State from the previous evaluation, for the hysteresis
 * @param samples_stale True if the sensor gives no new samples, so filtered_value is frozen
 */
FilamentSensorState evaluate_state(Value filtered_value, Value fs_ref_nins_value, Value fs_ref_ins_value, FilamentSensorState previous_state, bool samples_stale = false);

} // namespace FSensorADCEval
