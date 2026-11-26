// The real homing_corexy.cpp (a copy in the mirror of the build directory), and access to its
// static functions for the tests.
#include <module/prusa/homing_corexy.cpp>

#include "test_hooks.hpp"

namespace hooks {

namespace {
    constexpr AxisEnum measured_axis = (X_HOME_DIR == Y_HOME_DIR ? B_AXIS : A_AXIS);
    constexpr float fr = XY_HOMING_MEASURE_FR;
    constexpr size_t grid_n = std::size(::measure_point_sequence);
} // namespace

bool rehome_and_phase(bool rehome) {
    MachinePosXYZE origin_pos;
    ab_steps_t origin_steps;
    return corexy_rehome_and_phase(origin_pos, origin_steps, fr, rehome);
}

bool measure_phase_cycles(Cycles &c_dist) {
    xy_pos_t c;
    const bool ok = ::measure_phase_cycles(measured_axis, { 0, 0 }, c, fr);
    c_dist = { c[0], c[1] };
    return ok;
}

int measure_probe_n() {
    return ::measure_probe_n;
}

bool point_is_unstable(Cycles c_dist, Cycles origin) {
    return ::point_is_unstable({ c_dist.a, c_dist.b }, { origin.a, origin.b });
}

void cdist_translate(Cycles c_dist, Cycles origin, int32_t &a, int32_t &b) {
    const ab_grid_t r = ::cdist_translate({ c_dist.a, c_dist.b }, { origin.a, origin.b });
    a = r[0];
    b = r[1];
}

Cycles estimate_origin(const std::vector<Cycles> &points, int32_t anchor_a, int32_t anchor_b) {
    measure_data data[grid_n * origin_max_passes];
    for (size_t i = 0; i != points.size() && i != std::size(data); ++i) {
        data[i].c_dist = { points[i].a, points[i].b };
    }
    const xy_pos_t o = ::estimate_origin(data, std::min(points.size(), std::size(data)), { anchor_a, anchor_b });
    return { o[0], o[1] };
}

std::vector<std::pair<int32_t, int32_t>> measure_point_sequence() {
    std::vector<std::pair<int32_t, int32_t>> r;
    for (const auto &p : ::measure_point_sequence) {
        r.emplace_back(p[0], p[1]);
    }
    return r;
}

float unstable_phase_threshold() {
    return ::unstable_phase_threshold;
}

} // namespace hooks
