#pragma once
#include <core/types.h>
struct sensorless_t {};
inline sensorless_t start_sensorless_homing_per_axis(AxisEnum) { return {}; }
inline void end_sensorless_homing_per_axis(AxisEnum, sensorless_t) {}
