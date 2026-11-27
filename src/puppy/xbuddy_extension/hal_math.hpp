/// @file
/// Calculations of the HAL that do not touch the hardware, so that host tests can use them.
#pragma once

#include <algorithm>
#include <cstdint>
#include <utils/timing/timer_event_period_tracker.hpp>

namespace hal::math {

/// Frequency of TIM1, which measures the fan tachometer periods.
/// 30 MHz peripheral clock, *2 to timer, /60 prescaler.
inline constexpr uint32_t tach_timer_hz = 1'000'000;

/// Rising edges of the tachometer signal for each revolution of the fan.
inline constexpr uint32_t tach_edges_per_revolution = 2;

/// Convert the period between two tachometer edges to RPM.
/// \param period TIM1 ticks between the last two edges, from TimerEventPeriodTracker::period_unsafe()
/// \returns RPM, or 0 if there is no valid period (no edges, or edges too far apart)
constexpr uint32_t tach_period_to_rpm(uint32_t period) {
    if (period == TimerEventPeriodTracker::invalid_period || period == 0) {
        return 0;
    }
    return (60 * tach_timer_hz / tach_edges_per_revolution) / period;
}

/// Input clock of TIM3 (6 MHz with the default prescaler of 10).
inline constexpr uint32_t pwm_timer_base_hz = 60'000'000;

/// Default prescaler of the PWM timers.
inline constexpr uint32_t default_pwm_prescaler = 10;

/// Value for TIM3->PSC that gives a PWM cycle of approximately \p freq Hz.
/// \param freq frequency in Hz, 0 selects the default prescaler
/// \returns at most 0xFFFF (the PSC register has 16 bits), thus below
///          approximately 4 Hz the frequency is 3.6 Hz
constexpr uint32_t w_led_prescaler_register(uint16_t freq) {
    uint32_t prescaler = default_pwm_prescaler;
    if (freq != 0) {
        const uint32_t timer_full_cycle = pwm_timer_base_hz / 255; // Counts down from this every full cycle
        prescaler = std::min<uint32_t>(timer_full_cycle / freq, 0x1'0000);
    }
    return prescaler - 1;
}

} // namespace hal::math
