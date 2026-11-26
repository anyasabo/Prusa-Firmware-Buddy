#pragma once

// Stepper driver stand-in for the phase stepping axis tests. The tests set the
// microsteps of each driver. M350 changes the drivers, not the config store.

#include <core/types.h>

#include <cstdint>

inline uint16_t stub_driver_microsteps[4] = { 16, 16, 16, 16 };

inline uint16_t stepper_microsteps(AxisEnum axis) { return stub_driver_microsteps[axis]; }
