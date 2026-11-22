/// @file
/// Decode the DRV_STATUS register of a TMC2130 (and TMC5160: same layout) for metrics.
/// No Marlin dependency, so the host unit tests can check the bit positions.
#pragma once

#include <cstdint>

namespace buddy {

struct TmcDrvStatus {
    uint16_t sg_result; ///< bits 0..9: StallGuard load (lower = more load; valid only at speed in spreadCycle)
    uint8_t cs_actual; ///< bits 16..20: actual current scale
    bool short_to_ground; ///< bits 27, 28: s2ga or s2gb
    bool overtemp_warning; ///< bit 26: otpw
    bool open_load_a; ///< bit 29: ola. Valid only while the motor turns slowly; can flag at high speed
    bool open_load_b; ///< bit 30: olb. As ola, for coil B
    bool standstill; ///< bit 31: stst
};

constexpr TmcDrvStatus decode_drv_status(uint32_t ds) {
    return {
        .sg_result = static_cast<uint16_t>(ds & 0x3FF),
        .cs_actual = static_cast<uint8_t>((ds >> 16) & 0x1F),
        .short_to_ground = ((ds >> 27) & 0b11) != 0,
        .overtemp_warning = ((ds >> 26) & 1) != 0,
        .open_load_a = ((ds >> 29) & 1) != 0,
        .open_load_b = ((ds >> 30) & 1) != 0,
        .standstill = ((ds >> 31) & 1) != 0,
    };
}

} // namespace buddy
