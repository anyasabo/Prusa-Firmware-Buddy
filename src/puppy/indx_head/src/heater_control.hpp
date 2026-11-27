/// @file
/// Power decisions of the induction heater, without hardware access. heater.cpp applies them.
#pragma once

#include <array>
#include <cstdint>
#include <fpm/fixed.hpp>

namespace indx_head::heater {

inline constexpr uint16_t max_power = 14;
inline constexpr uint16_t limited_max_power = 10; // ~46W

enum class HeaterControlMode {
    HEATER_CONTROL_OFF,
    HEATER_CONTROL_TURBO, // max_power until target - 10°C
    HEATER_CONTROL_LIMITED, // limited_max_power until overshoot past target
    HEATER_CONTROL_PID, // PID clamped to limited_max_power
};

/// Selects the heater power level from the target and the measured nozzle temperature.
class PowerControl {
public:
    /// One control step. Returns the power level, 0 to max_power.
    uint16_t step(int32_t target_centideg, int32_t current_centideg);

    HeaterControlMode mode() const { return heater_control_mode; }
    fpm::fixed_16_16 integral() const { return pid_i_state; }

private:
    HeaterControlMode heater_control_mode = HeaterControlMode::HEATER_CONTROL_OFF;
    fpm::fixed_16_16 pid_i_state = static_cast<fpm::fixed_16_16>(0);
    int32_t last_centideg = 0;

    uint16_t pid_calculate_pwr_i(int32_t target_centideg, int32_t current_centideg);
};

/// Precomputed timer configuration for one power level. It depends on the measured oscillation period.
struct TimerConfig {
    uint16_t ARR; // auto-reload register
    uint16_t CCR1; // capture/compare register 1
};

/// !!! INDEX [0] ~ (power = 1), power 0 is not in this table
using TimerConfigs = std::array<TimerConfig, max_power>;

/// Timer configuration of each power level for an LC oscillation period (in CPU cycles).
TimerConfigs compute_timer_config(uint16_t interval);

/// Power level as a 0-255 PWM value, scaled by the duty cycle of the level.
uint8_t power_to_pwm(uint16_t power);

} // namespace indx_head::heater
