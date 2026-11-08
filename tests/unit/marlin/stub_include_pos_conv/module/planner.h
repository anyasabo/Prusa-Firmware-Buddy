#pragma once

#include <core/macros.h>
#include <core/types.h>
#include <module/planner_skew.h>

inline struct Planner : PlannerSkew {
    static constexpr bool leveling_active = true;

    static inline float z_fade_height = 0;
    static inline float inverse_z_fade_height = 0;

    static void set_z_fade_height(const float zfh) {
        z_fade_height = zfh > 0 ? zfh : 0;
        inverse_z_fade_height = RECIPROCAL(z_fade_height);
    }
} planner;

/// Stub mesh: a plane, z_correction + slope.x * x + slope.y * y
inline struct UBL {

    static inline float z_correction = 0;
    static inline xy_pos_t slope {};

    static inline float get_z_correction(const xy_pos_t &pos) {
        return z_correction + slope.x * pos.x + slope.y * pos.y;
    }

} ubl;
