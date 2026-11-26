#pragma once

// Stepper driver stand-in for the phase stepping axis tests. The tests set the
// microsteps of each driver. M350 changes the drivers, not the config store.

#include <core/types.h>

#include <cstdint>

struct StubDriver {
    uint16_t microsteps_ = 16;
    uint16_t microsteps() const { return microsteps_; }
};

inline StubDriver stub_drivers[4];

inline StubDriver &stepper_axis(const AxisEnum axis) { return stub_drivers[axis]; }
