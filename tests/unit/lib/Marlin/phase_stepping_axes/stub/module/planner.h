#pragma once

// Planner stand-in for the phase stepping axis tests. The tests set the
// steps/mm that the planner uses, and the values that the config store holds,
// separately. After M92, the two are different.

#include <core/types.h>

constexpr const int PLANNER_STEPS_MULTIPLIER = 4;

struct PlannerSettings {
    float axis_steps_per_mm[4] = { 100, 100, 800, 550 };
};

inline struct Planner {
    static inline PlannerSettings settings;
    static inline float mm_per_step[4] = { 1.f / 100, 1.f / 100, 1.f / 800, 1.f / 550 };

    /// Same as the real Planner::refresh_positioning() for the cached reciprocals.
    static void refresh_positioning() {
        for (int i = 0; i < 4; i++) {
            mm_per_step[i] = 1.f / settings.axis_steps_per_mm[i];
        }
    }
} planner;
