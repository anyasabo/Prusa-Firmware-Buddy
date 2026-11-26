#pragma once
#include <cstdint>
uint32_t ticks_ms();
inline int32_t ticks_diff(uint32_t ticks_a, uint32_t ticks_b) {
    return ((int32_t)(ticks_a - ticks_b));
}
