#pragma once
#include <option/has_phase_stepping.h>
#include <core/types.h>
namespace phase_stepping {
// The MSCNT of the motor (fake_world.cpp: the rotor position of the fake world)
int logical_ustep(AxisEnum axis);
} // namespace phase_stepping
