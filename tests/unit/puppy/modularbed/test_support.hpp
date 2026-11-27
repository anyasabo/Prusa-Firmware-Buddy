// Helpers that start the modular bed control code on the fake hardware and drive it.
#pragma once

#include "fake_hal.hpp"

#include "AutoconfLogic.hpp"
#include "ControlLogic.hpp"
#include "HeatbedletInfo.hpp"
#include "MeasurementLogic.hpp"
#include "ModbusRegisters.hpp"
#include "PWMLogic.hpp"
#include "StateLogic.hpp"

#include <cstdint>
#include <vector>

namespace test {

inline constexpr uint8_t modbus_address = 0x1A;

/// Reset all state of the control code and the fake world, then run the start sequence of
/// main() and ControlTaskFunction(): the init functions, the current calibration, the first
/// temperature check and the heater resistance check.
void boot();

/// Same as boot(), but with the world that the test prepared.
void boot_with_world();

/// Run the loop of ControlTaskFunction() for this many milliseconds (one iteration per ms).
/// If thermal is true, a simple thermal model moves the bedlet temperatures with the PWM.
void run_ms(uint32_t ms, bool thermal = false);

/// Send a Modbus frame "write multiple registers" (function 16) through the real protocol code.
void write_registers(uint16_t address, const std::vector<uint16_t> &values);

/// Send a Modbus frame "write single coil" (function 5) through the real protocol code.
void write_coil(uint16_t address, bool value);

/// Write the target temperature of all bedlets in one frame, as the main board does.
void write_targets(const std::array<float, fake::bedlet_count> &celsius);

/// Write the same target temperature to all bedlets.
void write_all_targets(float celsius);

/// Write the target temperature of one bedlet. The main board always writes all 16 targets.
void write_target(uint32_t bedlet, float celsius);

modularbed::HeatbedletInfo &bedlet(uint32_t index);

uint16_t hb_input_register(modular_bed_shared::registers::HBInputRegister reg, uint32_t bedlet);
uint16_t system_fault_status();

} // namespace test
