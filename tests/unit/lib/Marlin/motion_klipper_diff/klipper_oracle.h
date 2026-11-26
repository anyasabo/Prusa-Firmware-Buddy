#pragma once
// Runs Klipper's step generation (itersolve + kin_corexy/kin_cartesian + kin_shaper + kin_extruder)
// on a list of move segments and records the step times of each stepper.

#include "klipper/stepcompress_recorder.h"

#ifdef __cplusplus
extern "C" {
#endif

// One move segment with constant acceleration, as Klipper `struct move` (axes 0..3 = X, Y, Z, E).
struct kl_move {
    double print_time, move_t;
    double start_v, half_accel;
    double start_pos[4];
    double axes_r[4];
};

// Input shaper pulses in the traditional definition (Klipper shaper_defs.py): t[0] = 0, increasing.
struct kl_shaper {
    int n; // 0 = no shaper on this axis
    double a[5], t[5];
};

struct kl_params {
    int corexy; // 1: steppers A = X + Y, B = X - Y. 0: cartesian X, Y.
    double step_dist[4]; // mm per step of the steppers (A/X, B/Y, Z, E)
    double start_pos[3]; // cartesian start position X, Y, Z (mm); E starts at 0
    struct kl_shaper shaper[3]; // per cartesian axis X, Y, Z
    double pressure_advance; // 0 = off
    double smooth_time; // s, used only if pressure_advance > 0
    double record_from; // s: steps before this time are generated but not recorded
};

// out[i] receives the steps of stepper i (A/X, B/Y, Z, E). Allocate them with
// stepcompress_recorder_alloc().
void kl_generate_steps(const struct kl_params *params, const struct kl_move *moves, int move_count, struct stepcompress *out[4]);

#ifdef __cplusplus
}
#endif
