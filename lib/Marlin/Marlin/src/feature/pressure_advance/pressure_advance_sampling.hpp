/// @file
/// Sampling interval of the pressure advance filter. It is in a header without Marlin
/// dependencies so that the unit tests can use it.
#pragma once

#include <cmath>
#include <cstdint>

namespace pressure_advance {

/// Returns the sampling interval of the extruder position in whole microseconds.
/// The filter of @p filter_length samples spans @p smooth_time seconds.
/// Whole microseconds make the integer division by the interval exact.
inline int64_t sampling_interval_us(float smooth_time, uint16_t filter_length) {
    return (filter_length > 1)
        ? llround(double(smooth_time) / (filter_length - 1) * 1e6)
        : 1000; // 1 ms fallback when filter_length == 1
}

} // namespace pressure_advance
