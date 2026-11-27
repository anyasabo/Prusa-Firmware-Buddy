/// @file
/// Conversion of measured values to Modbus registers.
#pragma once

#include <cstdint>
#include <limits>

namespace register_encoding {

/// Encode a value in tenths of its unit (decidegree Celsius, decivolt).
///
/// The result is truncated toward zero and saturated to the register range.
/// Negative values and NaN give 0. The main board reads 0 °C as MINTEMP, so
/// an open thermistor still stops the printer.
///
/// A plain float to uint16_t cast is undefined for these inputs. On the
/// Cortex-M33, GCC makes VCVT.U32.F32 for it, which gives these same
/// results for all values below 6553.6.
constexpr uint16_t tenths_register(float value) {
    const float tenths = value * 10.0f;
    if (!(tenths > 0.0f)) {
        return 0;
    }
    if (tenths >= static_cast<float>(std::numeric_limits<uint16_t>::max())) {
        return std::numeric_limits<uint16_t>::max();
    }
    return static_cast<uint16_t>(tenths);
}

} // namespace register_encoding
