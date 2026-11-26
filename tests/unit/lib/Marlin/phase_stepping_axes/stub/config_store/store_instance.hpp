#pragma once

// Config store stand-in for the phase stepping axis tests: only the C API
// that axes.cpp reads. The values are in stub_store, which the tests set.

#include <cstdint>

struct StubStore {
    float steps_per_unit[4] = { 100, 100, 800, 550 };
    uint16_t microsteps[4] = { 16, 16, 16, 16 };
    bool has_400step_xy_motors = false;
};

inline StubStore stub_store;

inline float get_steps_per_unit_x() { return stub_store.steps_per_unit[0]; }
inline float get_steps_per_unit_y() { return stub_store.steps_per_unit[1]; }
inline float get_steps_per_unit_z() { return stub_store.steps_per_unit[2]; }
inline float get_steps_per_unit_e() { return stub_store.steps_per_unit[3]; }
inline uint16_t get_microsteps_x() { return stub_store.microsteps[0]; }
inline uint16_t get_microsteps_y() { return stub_store.microsteps[1]; }
inline uint16_t get_microsteps_z() { return stub_store.microsteps[2]; }
inline uint16_t get_microsteps_e() { return stub_store.microsteps[3]; }
inline bool get_has_400step_xy_motors() { return stub_store.has_400step_xy_motors; }
