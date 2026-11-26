#pragma once
// Stand-in for the temperature module: the hotend is always hot.
#include "../inc/MarlinConfig.h"
#include <tool_index.hpp>

class Temperature {
public:
    static inline uint8_t print_fan_speed = 0;
    static void apply_print_fan_speed(uint8_t = 0) {}
    static bool tooColdToExtrude(PhysicalToolIndex) { return false; }
};
inline Temperature thermalManager;
