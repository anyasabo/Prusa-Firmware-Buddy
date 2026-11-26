// Sends the move segments of the Buddy run to Klipper's step generation.
#include "motion_model.hpp"

#include "klipper_oracle.h"

#include <feature/precise_stepping/common.hpp>

#include <stdexcept>

namespace motion_diff {

struct KlipperSession {
    kl_session *s = nullptr;
    KlipperSession() = default;
    KlipperSession(const KlipperSession &) = delete;
    KlipperSession &operator=(const KlipperSession &) = delete;
    ~KlipperSession() {
        if (s) {
            kl_session_free(s);
        }
    }
};

double KlipperResult::position_steps(int axis, double time_us) const {
    return kl_session_position(session->s, axis, time_us * 1e-6) / step_dist[axis];
}

KlipperResult run_klipper(const Config &config, const std::vector<Segment> &segments, std::array<double, 3> start_pos_mm) {
    kl_params p {};
    p.corexy = 1;
    for (int i = 0; i < AXES; ++i) {
        // The same float mm per step as Planner::mm_per_step, so both sides step at the same positions.
        p.step_dist[i] = double(1.f / config.steps_per_mm[i]);
    }
    for (int i = 0; i < 3; ++i) {
        p.start_pos[i] = start_pos_mm[i];
        const auto &cfg = config.shaper[i];
        if (cfg && cfg->type != input_shaper::Type::null) {
            const Pulses pulses = buddy_shaper_pulses(*cfg);
            p.shaper[i].n = pulses.n;
            for (int k = 0; k < pulses.n; ++k) {
                p.shaper[i].a[k] = pulses.a[k];
                p.shaper[i].t[k] = pulses.t[k];
            }
        }
    }
    p.pressure_advance = config.pressure_advance;
    p.smooth_time = effective_smooth_time(config.smooth_time);
    p.record_from = config.record_from_us * 1e-6;

    std::vector<kl_move> moves;
    moves.reserve(segments.size());
    for (size_t i = 0; i < segments.size(); ++i) {
        const Segment &s = segments[i];
        double move_t = s.move_time;
        if (s.flags & MOVE_FLAG_ENDING_EMPTY_MOVE) {
            // Keep the ending empty move: PreciseStepping takes each segment's start position from the
            // exact planner position, and the segment before can end a little away from it (its
            // speeds are float). Up to the next motion, or 1 s, then the tail sentinel holds it.
            move_t = i + 1 < segments.size() ? segments[i + 1].print_time - s.print_time : 1.;
        }
        if (move_t < 0.) {
            throw std::runtime_error("move segments overlap in time"); // the trapq needs them in order
        }
        if (move_t == 0.) {
            continue;
        }
        kl_move m {};
        m.print_time = s.print_time;
        m.move_t = move_t;
        m.start_v = s.start_v;
        m.half_accel = s.half_accel;
        for (int i = 0; i < AXES; ++i) {
            m.start_pos[i] = s.start_pos[i];
            m.axes_r[i] = s.axes_r[i];
        }
        moves.push_back(m);
    }

    stepcompress *sc[AXES];
    for (auto &s : sc) {
        s = stepcompress_recorder_alloc();
    }
    KlipperResult result;
    result.session = std::make_shared<KlipperSession>();
    result.session->s = kl_session_create(&p, moves.data(), int(moves.size()));
    for (auto &s : sc) {
        s->record_from = p.record_from;
    }
    kl_session_generate(result.session->s, sc);
    for (int axis = 0; axis < AXES; ++axis) {
        result.step_dist[axis] = p.step_dist[axis];
        result.raw.skipped_net[axis] = sc[axis]->raw.skipped_net;
        result.filtered.skipped_net[axis] = sc[axis]->filtered.skipped_net;
        for (int k = 0; k < sc[axis]->raw.count; ++k) {
            result.raw.axis[axis].push_back({ sc[axis]->raw.time[k] * 1e6, sc[axis]->raw.dir[k] != 0 });
        }
        for (int k = 0; k < sc[axis]->filtered.count; ++k) {
            result.filtered.axis[axis].push_back({ sc[axis]->filtered.time[k] * 1e6, sc[axis]->filtered.dir[k] != 0 });
        }
        stepcompress_recorder_free(sc[axis]);
    }
    return result;
}

} // namespace motion_diff
