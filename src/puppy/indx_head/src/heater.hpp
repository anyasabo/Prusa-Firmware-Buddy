/// @file
#pragma once

#include "heater_control.hpp"
#include "ringdown.hpp"

#include <cstdint>

class InductionHeater {
public:
    InductionHeater(void);

    void ramp_isr();

    static constexpr uint16_t max_power = indx_head::heater::max_power;

    void heater_control(int32_t target_centideg, int32_t current_centideg);

    uint8_t current_pwm() const;

private:
    using RingdownAnalysis = indx_head::heater::RingdownAnalysis;
    using TimerConfig = indx_head::heater::TimerConfig;

    RingdownAnalysis last_analysis;
    uint16_t dcOffset;
    uint16_t current_power;
    uint16_t last_valid_interval;
    volatile uint16_t ramp_current_power;
    volatile uint16_t ramp_target_power;

    // !!! INDEX timer_config[0] ~ intervalLUT[0] ~ (current_power = 1), current_power 0 is not in this table
    indx_head::heater::TimerConfigs timer_config;

    indx_head::heater::PowerControl power_control;

    /// Apply precomputed timer configuration for given power.
    void apply_timer_config(uint16_t power);

    /// Stop timer.
    void stop_timer();

    void ringdown_analysis(void);

    /// Measure LC response and perform ringdown analysis.
    void measure();

    void update(uint16_t target_power);
};

extern InductionHeater inductionHeater;
