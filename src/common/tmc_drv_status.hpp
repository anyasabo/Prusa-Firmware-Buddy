/// @file
/// Decode the DRV_STATUS register of a TMC2130 (and TMC5160: same layout) for metrics.
/// No Marlin dependency, so the host unit tests can check the bit positions.
#pragma once

#include <cstdint>

namespace buddy {

struct TmcDrvStatus {
    uint16_t sg_result; ///< bits 0..9: StallGuard load (lower = more load; valid only at speed in spreadCycle)
    uint8_t cs_actual; ///< bits 16..20: actual current scale
    bool overtemp_shutdown; ///< bit 25: ot. The driver is off
    bool overtemp_warning; ///< bit 26: otpw
    bool short_to_ground; ///< bits 27, 28: s2ga or s2gb
    bool open_load_a; ///< bit 29: ola. Information only: it can be set falsely in fast motion and at standstill
    bool open_load_b; ///< bit 30: olb. As ola, for coil B
    bool standstill; ///< bit 31: stst
};

/// False for a read that failed: SPI gives all ones, and a driver without power gives all zeros
/// (Marlin skips the same two values, tmc_util.cpp monitor_tmc_driver()). Do not decode those:
/// all ones would look like open load on both coils and a short.
constexpr bool drv_status_valid(uint32_t ds) {
    return ds != 0xFFFFFFFF && ds != 0;
}

constexpr TmcDrvStatus decode_drv_status(uint32_t ds) {
    return {
        .sg_result = static_cast<uint16_t>(ds & 0x3FF),
        .cs_actual = static_cast<uint8_t>((ds >> 16) & 0x1F),
        .overtemp_shutdown = ((ds >> 25) & 1) != 0,
        .overtemp_warning = ((ds >> 26) & 1) != 0,
        .short_to_ground = ((ds >> 27) & 0b11) != 0,
        .open_load_a = ((ds >> 29) & 1) != 0,
        .open_load_b = ((ds >> 30) & 1) != 0,
        .standstill = ((ds >> 31) & 1) != 0,
    };
}

} // namespace buddy
