#pragma once
// Access to the static functions of homing_corexy.cpp. homing_corexy_tu.cpp includes the real
// source file and defines these functions.
#include <cstddef>
#include <cstdint>
#include <vector>

namespace hooks {

struct Cycles {
    float a, b;
};

/// Rehome XY (when rehome is true), move to the refinement origin and align the phases
bool rehome_and_phase(bool rehome);
/// Measure the phase cycles at the current origin (rehome_and_phase() first). measure_phase_cycles().
bool measure_phase_cycles(Cycles &c_dist);
/// Number of measurement pairs that measure_phase_cycles() averages (measure_probe_n)
int measure_probe_n();

bool point_is_unstable(Cycles c_dist, Cycles origin);
void cdist_translate(Cycles c_dist, Cycles origin, int32_t &a, int32_t &b);
/// estimate_origin() with the stride of measure_origin_multipoint()
Cycles estimate_origin(const std::vector<Cycles> &points, int32_t anchor_a, int32_t anchor_b);
/// The measurement grid of the calibration (measure_point_sequence)
std::vector<std::pair<int32_t, int32_t>> measure_point_sequence();
float unstable_phase_threshold();

} // namespace hooks
