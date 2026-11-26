/// @file
#include <puppies/indx_fault.hpp>

#include <utility>

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
// - The watchdog of the head (1 s, started by the bootloader) also resets a hung head. The head
//   then also sets watchdog_reset.
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
    // The head panics on the first load cell frame with a bad CRC (hal_spi.cpp,
    // loadcell::get_sample()). The head is stopped, so the main board cannot continue with
    // only a log line.
    case FaultStatusMask::loadcell_crc_mismatch:
        return text("The INDX head received corrupt data from its load cell (CRC mismatch).");
    // The head hung and its watchdog reset it. A panic also ends in a watchdog reset, but then
    // its cause bit is also set and wins (see redscreen_for_status()).
    case FaultStatusMask::watchdog_reset:
        return text("The INDX head firmware stopped responding and its watchdog reset it.");

    // No code of the head sets this bit.
    case FaultStatusMask::undefined_error:
        return unknown_fault;
    }

    // A bit with no enumerator.
    return unknown_fault;
}

std::optional<Redscreen> redscreen_for_status(uint16_t fault_status) {
    // A panic latches its cause, and the watchdog reset comes after it. Thus the cause wins.
    constexpr uint16_t watchdog_reset = std::to_underlying(FaultStatusMask::watchdog_reset);

    // errors.hpp documents the priority: a higher bit has a higher priority.
    for (int bit = 15; bit >= 0; --bit) {
        const uint16_t mask = static_cast<uint16_t>(1u << bit);
        if ((fault_status & mask) && mask != watchdog_reset) {
            if (const auto redscreen = redscreen_for(static_cast<FaultStatusMask>(mask))) {
                return redscreen;
            }
        }
    }
    if (fault_status & watchdog_reset) {
        return redscreen_for(FaultStatusMask::watchdog_reset);
    }
    return std::nullopt;
}

} // namespace buddy::puppies::indx_fault
