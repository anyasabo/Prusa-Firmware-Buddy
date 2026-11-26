/// @file
#include <puppies/indx_fault.hpp>

namespace buddy::puppies::indx_fault {

using indx_head::errors::FaultStatusMask;

// How the head reports a fault (src/puppy/indx_head at v8.0.2):
// - hal::panic() latches the bit in the backup register PWR->BKP0R, turns off the heater, sets
//   the fans to full and loops forever (src/puppy/indx_head/src/hal.cpp, panic()).
// - If the panic is in app_task (priority 1: the nozzle, board and TPIS temperatures), the modbus
//   task (priority 2) still runs, so the main board reads the bit at once.
// - If the panic is in an interrupt (hard fault, stack overflow hook, configASSERT with the
//   interrupts off), the head does not answer Modbus. The main board then gets a Modbus error,
//   goes to the puppy recovery and resets the head with its reset pin (puppy_task.cpp, run()).
//   The head design expects the backup register to keep the bit over this reset (README.md:
//   "latches a fault bit for later inspection"), so the main board reads it after the restart.
// - The watchdog of the head (1 s, started by the bootloader) resets the head after a panic, after
//   a hang, and also when the bus is silent: the modbus task waits for a request with no timeout
//   inside FOREVER_WITH_WATCHDOG(900) (modbus.cpp, run(), and hal_rs485.cpp). The head then sets
//   watchdog_reset at the next boot.
// A bit that is only logged is also acknowledged and cleared. After a restart of the head,
// Indx::initial_scan() sends the nozzle target again and IndxHotend::manage() resets the thermal
// safeties. Thus a fault that is only logged does not stop the print: the head heats again.
//
// The modular bed (modular_bed.cpp) and the XL Dwarf (Dwarf.cpp) also give a redscreen for each
// fault bit, with a fatal fallback for an unknown bit.

namespace {
    constexpr Redscreen text(const char *message) {
        return Redscreen { ErrCode::ERR_UNDEF, message };
    }

    constexpr Redscreen unknown_fault = text("The INDX head reported an unknown fault.");
} // namespace

std::optional<Redscreen> redscreen_for(FaultStatusMask bit) {
    // Each enumerator has a case and there is no default:
    // -Werror=switch then fails the build when a new bit has no decision.
    switch (bit) {
    case FaultStatusMask::no_fault:
        return std::nullopt;

    // The head sets these bits at each boot: a power-on reset, or the reset pin of the
    // bootstrap and of the puppy recovery of the main board. They are not faults.
    case FaultStatusMask::pin_reset:
    case FaultStatusMask::power_reset:
        return std::nullopt;

    // The head also gets a watchdog reset in normal operation: when the main board does not send
    // a request for 0.9 s (a reset or a redscreen of the main board, a power panic, a firmware
    // update). A redscreen for this bit alone thus gives false redscreens. A panic that ends in
    // a watchdog reset latches its own cause bit, and that bit gives the redscreen.
    case FaultStatusMask::watchdog_reset:
        return std::nullopt;

    // The head panics on the first load cell frame with a bad CRC (hal_spi.cpp,
    // loadcell::get_sample()), and then restarts. The CRC check stops the bad sample, so the
    // main board never uses it. One bad frame can come from electrical noise, and the main board
    // continues after a restart of the head (see above). Thus log only. A head that drops the
    // frame and does not panic is a better fix, in the head firmware.
    case FaultStatusMask::loadcell_crc_mismatch:
        return std::nullopt;

    // Thermal faults. The head panics and the heater is off. Without a redscreen, the head
    // restarts and the print continues with the same fault. The hotend codes have INDX text
    // ("Check the heat block ... for dirt or debris").
    case FaultStatusMask::nozzle_max_temp:
        return Redscreen { ErrCode::ERR_TEMPERATURE_HOTEND_MAXTEMP_ERROR, nullptr };
    case FaultStatusMask::nozzle_min_temp:
        return Redscreen { ErrCode::ERR_TEMPERATURE_HOTEND_MINTEMP_ERROR, nullptr };
    // The TPIS gave no valid nozzle temperature for 2 s. Marlin also uses MINTEMP when a sensor
    // gives no valid reading. The log line keeps the exact bit.
    case FaultStatusMask::tpis_invalid_timeout:
        return Redscreen { ErrCode::ERR_TEMPERATURE_HOTEND_MINTEMP_ERROR, nullptr };
    case FaultStatusMask::board_min_temp:
        return Redscreen { ErrCode::ERR_TEMPERATURE_INDX_HEAD_BOARD_MINTEMP_ERR, nullptr };
    case FaultStatusMask::board_max_temp:
        return Redscreen { ErrCode::ERR_TEMPERATURE_INDX_HEAD_BOARD_MAXTEMP_ERR, nullptr };
    case FaultStatusMask::tpis_ambient_min_temp:
        return Redscreen { ErrCode::ERR_TEMPERATURE_INDX_HEAD_TPIS_AMBIENT_MINTEMP_ERR, nullptr };
    case FaultStatusMask::tpis_ambient_max_temp:
        return Redscreen { ErrCode::ERR_TEMPERATURE_INDX_HEAD_TPIS_AMBIENT_MAXTEMP_ERR, nullptr };

    // Crash faults: a defect in the head firmware. The Dwarf of the XL also gives a redscreen
    // when its firmware stops (MARLIN_KILLED). There is no error code for them, so the
    // redscreen shows a text, like the Dwarf does. A new code needs translations.
    case FaultStatusMask::hard_fault:
        return text("The INDX head firmware crashed: hard fault.");
    case FaultStatusMask::stack_overflow:
        return text("The INDX head firmware crashed: stack overflow.");
    case FaultStatusMask::assert_failed:
        return text("The INDX head firmware crashed: assertion failed.");

    // No code of the head sets this bit.
    case FaultStatusMask::undefined_error:
        return unknown_fault;
    }

    // A bit with no enumerator.
    return unknown_fault;
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
