// Fake hardware for the host tests of the modular bed control code.
//
// The fake ADC converts the temperatures and the heater currents of a simple world model into
// raw ADC values. The fake PWM driver keeps the pulses of the last applied pattern. The current of
// a branch is the average current of the heaters that the active pattern turns on.
#pragma once

#include "hal/HAL_ADC.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace fake {

inline constexpr size_t bedlet_count = 16;
inline constexpr uint16_t current_adc_zero = 32900;

struct Pulse {
    uint32_t bedlet;
    uint32_t start;
    uint32_t length;
};

struct World {
    uint32_t tick_ms = 0;
    uint32_t micros = 0;

    /// Temperature at each thermistor (°C)
    std::array<float, bedlet_count> temperature {};
    /// Heater resistance at 20 °C (ohm). Infinity is a disconnected heater.
    std::array<float, bedlet_count> heater_resistance {};
    /// If set, the ADC gives this raw value for the channel instead of the model value.
    std::array<std::optional<uint16_t>, static_cast<size_t>(hal::ADCDriver::ADCChannel::CHANNEL_COUNT)> adc_override {};
    /// The thermal model of run_ms() does not change the temperature of a frozen bedlet, for
    /// example a thermistor that has no contact with its heater.
    std::array<bool, bedlet_count> frozen {};
    float ambient = 22.0f;
    /// Current that flows in a branch with no PWM (A), for example through a shorted MOSFET.
    std::array<float, 2> extra_current {};

    bool panic = false;
    bool fault = false;
    bool conversion_finished = true;

    std::vector<Pulse> pending_pulses;
    std::vector<Pulse> active_pulses;
    uint32_t applied_patterns = 0;
    uint32_t started_conversions = 0;
    hal::ADCDriver::ADCChannel converted_channel = hal::ADCDriver::ADCChannel::Heatbedlet_0;
};

extern World world;

/// Set the world to 22 °C, 13 ohm heaters, no override, no pulses.
void reset_world();

/// Raw ADC value (16 bits after oversampling) of a thermistor at this temperature.
uint16_t thermistor_adc(float celsius);

/// Raw ADC value of the current sensor of a branch for this current.
uint16_t current_adc(float amps);

/// Branch of a bedlet: 0 = A (bedlets 6 to 15), 1 = B (bedlets 0 to 5).
int branch_of(uint32_t bedlet);

/// Duty cycle (0 to 1) of a bedlet in the active pattern.
float duty(uint32_t bedlet);

/// Average current of a branch in the active pattern (A).
float branch_current(int branch);

} // namespace fake
