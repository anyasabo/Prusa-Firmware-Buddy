/// @file
/// Stub of the Marlin temperature module for the bed fan controller tests.
#pragma once

#include <cstdint>

class Temperature {
public:
    static float degBed() { return bed_current; }
    static int16_t degTargetBed() { return bed_target; }

    static inline float bed_current = 0.0f;
    static inline int16_t bed_target = 0;
};

inline Temperature thermalManager;
