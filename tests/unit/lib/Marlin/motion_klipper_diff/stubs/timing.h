#pragma once
// Stand-in for timing.h: a clock that the test controls.
#include <cstdint>

inline uint32_t stub_now_us = 0;
inline uint32_t ticks_us() { return stub_now_us; }
inline uint32_t ticks_ms() { return stub_now_us / 1000; }
inline int64_t get_timestamp_us() { return stub_now_us; }
inline int32_t ticks_diff(uint32_t a, uint32_t b) { return (int32_t)(a - b); }
