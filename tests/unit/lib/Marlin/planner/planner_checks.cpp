#include "planner_checks.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace planner_checks {

namespace {

    constexpr double min_speed = MINIMUM_PLANNER_SPEED;

    struct Geometry {
        double d[XYZE_N]; // signed displacement (mm)
        double length; // as _populate_block(): XYZ length, or |E| for E-only
        bool xyz; // an XYZ axis moves
    };

    Geometry geometry(const planner_harness::BlockCopy &b) {
        Geometry g {};
        for (int i = 0; i < XYZE_N; ++i) {
            const double sign = (b.direction_bits & (1 << i)) ? -1. : 1.;
            g.d[i] = sign * double(b.msteps[i]) / double(Planner::settings.axis_msteps_per_mm[i]);
        }
        g.xyz = b.msteps.x || b.msteps.y || b.msteps.z;
        g.length = g.xyz ? std::sqrt(g.d[0] * g.d[0] + g.d[1] * g.d[1] + g.d[2] * g.d[2]) : std::abs(g.d[3]);
        return g;
    }

    bool is_print_e_move(const planner_harness::BlockCopy &b) {
        return b.msteps.e && !(b.direction_bits & (1 << E_AXIS)) && (b.msteps.x || b.msteps.y);
    }

    /// The junction deviation limit (Configuration_COREONEL.h, CLASSIC_JERK off) of the junction
    /// prev -> cur, in double: the unit vectors over X, Y, Z and E, the acceleration of cur limited
    /// by M201 along the junction vector (and by M204 F on E if a print move is on either side). The
    /// planner can only be lower (JD_SMALL_SEGMENT_HANDLING, the nominal speeds).
    double junction_limit_sqr(const planner_harness::Settings &s, const planner_harness::BlockCopy &prev, const planner_harness::BlockCopy &cur) {
        // The directions from the float positions, as the planner (the rounded mini-steps change the
        // direction of short blocks, mostly in E).
        double dp[XYZE_N], dc[XYZE_N], up[XYZE_N], uc[XYZE_N], np = 0, nc = 0;
        for (int i = 0; i < XYZE_N; ++i) {
            dp[i] = double(prev.end_mm[i]) - double(prev.start_mm[i]);
            dc[i] = double(cur.end_mm[i]) - double(cur.start_mm[i]);
            np += dp[i] * dp[i];
            nc += dc[i] * dc[i];
        }
        np = std::sqrt(np);
        nc = std::sqrt(nc);
        double cos_theta = 0;
        for (int i = 0; i < XYZE_N; ++i) {
            up[i] = dp[i] / np;
            uc[i] = dc[i] / nc;
            cos_theta -= up[i] * uc[i];
        }
        if (cos_theta > 0.999999) {
            return min_speed * min_speed;
        }
        cos_theta = std::max(cos_theta, -0.999999);
        double ju[XYZE_N], nj = 0;
        for (int i = 0; i < XYZE_N; ++i) {
            ju[i] = uc[i] - up[i];
            nj += ju[i] * ju[i];
        }
        nj = std::sqrt(nj);
        double accel = cur.acceleration;
        for (int i = 0; i < XYZE_N; ++i) {
            if (ju[i] != 0) {
                double axis_max = Planner::settings.max_acceleration_mm_per_s2[i];
                if (i == E_AXIS && s.max_filament_acceleration > 0 && (is_print_e_move(prev) || is_print_e_move(cur))) {
                    axis_max = std::min(axis_max, double(s.max_filament_acceleration));
                }
                accel = std::min(accel, axis_max / std::abs(ju[i] / nj));
            }
        }
        const double sin_theta_d2 = std::sqrt(0.5 * (1 - cos_theta));
        const double v2 = accel * s.junction_deviation * sin_theta_d2 / (1 - sin_theta_d2);
        return std::min({ v2, double(prev.nominal_speed) * prev.nominal_speed, double(cur.nominal_speed) * cur.nominal_speed });
    }

    struct Failures {
        std::vector<std::string> &out;
        void add(size_t index, const char *what, double got, double limit) {
            if (out.size() < 20) {
                char buf[200];
                std::snprintf(buf, sizeof buf, "block %zu: %s: %.9g > %.9g", index, what, got, limit);
                out.emplace_back(buf);
            }
        }
        void le(size_t index, const char *what, double got, double limit, double rel = 1e-5, double abs = 1e-6) {
            if (!(got <= limit * (1 + rel) + abs)) {
                add(index, what, got, limit);
            }
        }
    };

} // namespace

Report check(const planner_harness::Settings &s, const std::vector<planner_harness::Taken> &blocks, const std::vector<planner_harness::Jump> &jumps, const Expected &expected) {
    Report r;
    Failures f { r.failures };
    const auto &set = Planner::settings; // the working settings (user settings with the HW limits)

    xyze_msteps_t pos {};
    // M221: the E of each section between two G92 is the commanded E times e_factor, within one
    // mini-step. The unscaled planner E at the start of the section, and the sum of the scaled E.
    const double e_factor = Planner::e_factor[VirtualToolIndex {}];
    int64_t section_start_e = 0, section_e = 0;
    const auto check_section = [&](size_t index, int32_t end_e) {
        const double commanded = double(end_e - section_start_e) * e_factor;
        if (std::abs(double(section_e) - commanded) > 1.) {
            f.add(index, "E mini-steps of the section before G92 differ from the commanded E x e_factor", section_e, commanded);
        }
    };
    size_t next_jump = 0;
    const planner_harness::BlockCopy *prev = nullptr;
    const auto apply_jumps = [&](size_t index) {
        while (next_jump < jumps.size() && jumps[next_jump].after_taken == index) {
            const auto &j = jumps[next_jump++];
            for (int i = 0; i < XYZE_N; ++i) {
                if (pos[i] != j.before[i] && (i != E_AXIS || s.flow_percentage == 100)) {
                    f.add(index, "sum of the block mini-steps before G92 differs from the planner position", pos[i], j.before[i]);
                }
                r.lost_at_g92[i] += j.commanded_before[i] - j.before[i];
            }
            check_section(index, j.before.e);
            section_start_e = j.after.e;
            section_e = 0;
            pos = j.after;
        }
    };

    for (size_t n = 0; n < blocks.size(); ++n) {
        apply_jumps(n);
        const planner_harness::BlockCopy &b = blocks[n].block;
        if (b.sync) {
            continue;
        }
        const Geometry g = geometry(b);
        for (int i = 0; i < XYZE_N; ++i) {
            pos[i] += (b.direction_bits & (1 << i)) ? -b.msteps[i] : b.msteps[i];
        }
        section_e += (b.direction_bits & (1 << E_AXIS)) ? -b.msteps.e : b.msteps.e;
        const double v0 = b.initial_speed, v1 = b.final_speed, vn = b.nominal_speed, a = b.acceleration, len = g.length;

        // An E-only block has the length of the scaled E before the rounding (M221): up to one
        // mini-step more or less.
        const double len_tolerance = g.xyz ? 0. : 1. / double(Planner::settings.axis_msteps_per_mm[E_AXIS]);
        if (std::abs(len - b.millimeters) > 1e-4 * len + 1e-6 + len_tolerance) {
            f.add(n, "block length differs from the mini-steps", b.millimeters, len);
        }
        if (!(v0 >= 0 && v1 >= 0 && vn > 0 && a > 0)) {
            f.add(n, "speed or acceleration not positive", std::min({ v0, v1, vn, a }), 0);
        }
        f.le(n, "entry speed above nominal", v0, vn);
        f.le(n, "exit speed above nominal", v1, vn);
        f.le(n, "nominal speed above the feed rate", vn, expected.max_fr);
        // The planner plans the newest block to end at MINIMUM_PLANNER_SPEED. If the block before it is
        // busy, its entry speed is fixed. A new block can have a junction limit below
        // MINIMUM_PLANNER_SPEED (an E-heavy junction with a tiny XYZ part, see the README). Then the
        // block cannot slow down to it, and the speed jumps by less than MINIMUM_PLANNER_SPEED.
        const double dv2 = std::abs(v1 * v1 - v0 * v0);
        if (v1 < min_speed && dv2 > 2 * a * len * (1 + 1e-4) + 1e-4 && dv2 <= 2 * a * len + min_speed * min_speed) {
            ++r.exit_below_minimum_speed;
        } else {
            f.le(n, "speed change not feasible over the block |v1^2 - v0^2| <= 2aL", dv2, 2 * a * len, 1e-4, 1e-4);
        }

        // M203: CoreXY motors run at |vx| + |vy| (the X limit), Z and E their own.
        f.le(n, "M203 XY (|vx|+|vy|)", vn * (std::abs(g.d[0]) + std::abs(g.d[1])) / len, set.max_feedrate_mm_s[X_AXIS], 1e-4);
        f.le(n, "M203 Z", vn * std::abs(g.d[2]) / len, set.max_feedrate_mm_s[Z_AXIS], 1e-4);
        f.le(n, "M203 E", vn * std::abs(g.d[3]) / len, set.max_feedrate_mm_s[E_AXIS], 1e-4);

        // M204 and M201
        if (!g.xyz) {
            if (std::abs(a - set.retract_acceleration) > 1e-3 * set.retract_acceleration) {
                f.add(n, "E-only move does not use M204 R", a, set.retract_acceleration);
            }
        } else {
            f.le(n, b.msteps.e ? "M204 P" : "M204 T", a, b.msteps.e ? set.acceleration : set.travel_acceleration, 1e-4, 1e-3);
            static const char *const names[] = { "M201 X", "M201 Y", "M201 Z", "M201 E" };
            for (int i = 0; i < XYZE_N; ++i) {
                f.le(n, names[i], a * std::abs(g.d[i]) / len, set.max_acceleration_mm_per_s2[i], 1e-4, 1e-3);
            }
            if (is_print_e_move(b) && set.max_filament_acceleration_mm_per_s2 > 0) {
                f.le(n, "M204 F", a * g.d[3] / len, set.max_filament_acceleration_mm_per_s2, 1e-4, 1e-3);
            }
        }

        if (prev) {
            if (std::abs(double(prev->final_speed) - v0) > 1e-6 * (1 + v0)) {
                f.add(n, "entry speed differs from the exit speed of the block before", v0, prev->final_speed);
            }
            const double limit = std::max(junction_limit_sqr(s, *prev, b), min_speed * min_speed);
            f.le(n, "entry speed^2 above the junction limit", v0 * v0, limit, 1e-3, 1e-3);
        }
        prev = &b;
    }
    apply_jumps(blocks.size());
    check_section(blocks.size(), planner.position.e);

    for (int i = 0; i < XYZE_N; ++i) {
        if (pos[i] != planner.position[i] && (i != E_AXIS || s.flow_percentage == 100)) {
            f.add(blocks.size(), "sum of the block mini-steps differs from the planner position", pos[i], planner.position[i]);
        }
    }
    return r;
}

} // namespace planner_checks
