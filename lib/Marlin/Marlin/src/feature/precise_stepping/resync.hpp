/// @file
/// The arithmetic of the resync between precise stepping and phase stepping
/// (see "special_event_time_us" in precise_stepping.cpp). A header of its own for the unit test.
#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>

namespace precise_stepping::resync {

/// The drift of the step timeline, in stepper ticks.
/// Positive: the steps are early, delay the next one. Negative: they are late, speed up.
/// @param expected_us when the special step event shall happen, in µs since the start of the motion
/// @param now_us when it happens, in the same time base
/// The difference is computed in 32 bits, the product in 64 bits. Before, it was an int16_t, which
/// wrapped at ±32.768 ms (at 1 tick/µs): the correction then went the wrong way.
constexpr int32_t drift_ticks(uint32_t expected_us, uint32_t now_us, uint32_t ticks_per_us) {
    const int64_t ticks = int64_t(static_cast<int32_t>(expected_us - now_us)) * ticks_per_us;
    return static_cast<int32_t>(std::clamp<int64_t>(ticks, std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max()));
}

/// The part of the drift that the next step corrects. It does not speed up the next step by more
/// than its own ticks (no negative time to the next step), and it does not delay it by more than
/// one ISR period. The rest is left for the next resync.
constexpr int32_t step_correction(int32_t drift, uint16_t ticks_to_next_isr, uint16_t isr_period_ticks) {
    const int32_t low = -int32_t(std::min(ticks_to_next_isr, isr_period_ticks));
    return std::clamp<int32_t>(drift, low, isr_period_ticks);
}

} // namespace precise_stepping::resync
