/// @file
#include <puppies/indx_fault.hpp>

namespace buddy::puppies::indx_fault {

using indx_head::errors::FaultStatusMask;

std::optional<Redscreen> redscreen_for(FaultStatusMask bit) {
    // Each enumerator has a case and there is no default:
    // -Werror=switch then fails the build when a new bit has no decision.
    switch (bit) {
    case FaultStatusMask::no_fault:
    case FaultStatusMask::undefined_error:
    case FaultStatusMask::nozzle_min_temp:
    case FaultStatusMask::tpis_invalid_timeout:
    case FaultStatusMask::nozzle_max_temp:
    case FaultStatusMask::hard_fault:
    case FaultStatusMask::stack_overflow:
    case FaultStatusMask::assert_failed:
    case FaultStatusMask::loadcell_crc_mismatch:
    case FaultStatusMask::watchdog_reset:
    case FaultStatusMask::pin_reset:
    case FaultStatusMask::power_reset:
        return std::nullopt;

    case FaultStatusMask::board_min_temp:
        return Redscreen { ErrCode::ERR_TEMPERATURE_INDX_HEAD_BOARD_MINTEMP_ERR, nullptr };
    case FaultStatusMask::board_max_temp:
        return Redscreen { ErrCode::ERR_TEMPERATURE_INDX_HEAD_BOARD_MAXTEMP_ERR, nullptr };
    case FaultStatusMask::tpis_ambient_min_temp:
        return Redscreen { ErrCode::ERR_TEMPERATURE_INDX_HEAD_TPIS_AMBIENT_MINTEMP_ERR, nullptr };
    case FaultStatusMask::tpis_ambient_max_temp:
        return Redscreen { ErrCode::ERR_TEMPERATURE_INDX_HEAD_TPIS_AMBIENT_MAXTEMP_ERR, nullptr };
    }
    return std::nullopt;
}

std::optional<Redscreen> redscreen_for_status(uint16_t fault_status) {
    // errors.hpp documents the priority: a higher bit has a higher priority.
    for (int bit = 15; bit >= 0; --bit) {
        const uint16_t mask = static_cast<uint16_t>(1u << bit);
        if (fault_status & mask) {
            if (const auto redscreen = redscreen_for(static_cast<FaultStatusMask>(mask))) {
                return redscreen;
            }
        }
    }
    return std::nullopt;
}

} // namespace buddy::puppies::indx_fault
