#pragma once
#include "MarlinConfigPre.h"
#include <core/types.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numbers>
#include <optional>

#include <cmath_ext.h>

// From the HAL (Arduino.h) and Marlin.h
using std::max;
using std::min;
#define UNUSED(x)    ((void)(x))
#define DEBUGGING(F) false
// The serial output is not kept. Take the arguments, so that they count as used.
template <typename... Args>
inline void serial_discard(const Args &...) {}
#define SERIAL_ECHOLN(...)     serial_discard(__VA_ARGS__)
#define SERIAL_ECHOLNPAIR(...) serial_discard(__VA_ARGS__)
inline constexpr char physical_axis_codes[] = { 'A', 'B', 'C', 'E' };
void idle(bool waiting = false, bool no_stepper_sleep = false);
uint32_t ticks_us();
