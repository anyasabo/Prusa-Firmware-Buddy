/// @file
/// Sampling interval of the pressure advance filter. It is in a header without Marlin
/// dependencies so that the unit tests can use it.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace pressure_advance {

/// Limits of the smooth time (M572 W) in seconds.
/// The step generator samples the extruder position at smooth_time / (filter length - 1).
/// 0 gives an interval of 0 and a division by zero. A small value gives a high sample rate in
/// the move ISR, and the uint32_t sample counter wraps sooner (at 0.01 s: 4000 samples/s,
/// 4 times the default, and a wrap after 12.4 days of motion without a stop).
/// Klipper accepts 0 as "no smoothing", because it integrates on the host.
inline constexpr float smooth_time_min = 0.01f;
inline constexpr float smooth_time_max = 0.2f;

/// Returns the sampling interval of the extruder position in whole microseconds.
/// The filter of @p filter_length samples spans @p smooth_time seconds.
/// Whole microseconds make the integer division by the interval exact.
/// @p smooth_time is clamped to [smooth_time_min, smooth_time_max].
inline int64_t sampling_interval_us(float smooth_time, uint16_t filter_length) {
    smooth_time = std::clamp(smooth_time, smooth_time_min, smooth_time_max);
    return (filter_length > 1)
        ? llround(double(smooth_time) / (filter_length - 1) * 1e6)
        : 1000; // 1 ms fallback when filter_length == 1
}

} // namespace pressure_advance
