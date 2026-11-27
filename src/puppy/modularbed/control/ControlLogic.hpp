#pragma once

#include <cstdint>

#include "ModbusRegisters.hpp"

namespace modularbed::ControlLogic {

void Init();

void EnableControllers();
void DisableControllers();

void IterateContollers();

/// Value of a PID action register: the action limited to -1 to 1, in units of 1/10000.
/// A negative value is a 16-bit two's complement value.
uint16_t PIDActionRegister(float action);

} // namespace modularbed::ControlLogic
