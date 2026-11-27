#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>

namespace indx_head {

/// A temperature in 0.01 °C, saturated to int16_t (the type of the Modbus registers and the
/// controller inputs). The range ends at 327.67 °C, which is below the max temperature limit, so a
/// cast would wrap a reading of 328 °C to -327 °C: the heater controller would then give full power
/// to a nozzle that is already too hot. Saturated, the reading stays far above any target.
constexpr int16_t saturate_c100(int32_t c100) {
    return static_cast<int16_t>(std::clamp<int32_t>(c100, std::numeric_limits<int16_t>::min(), std::numeric_limits<int16_t>::max()));
}

} // namespace indx_head
