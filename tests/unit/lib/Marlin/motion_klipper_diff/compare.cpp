#include "compare.hpp"

#include <feature/input_shaper/input_shaper.hpp>
#include <feature/precise_stepping/common.hpp>

#include <cmath>
#include <cstdio>
#include <map>
#include <set>

namespace motion_diff {

namespace {
    // Key of the step boundary that a step crosses: the step from p to p+1 crosses p+0.5.
    using CrossingKey = std::pair<int64_t, bool>; // (2 * boundary, positive)

    // Index of each step in the list, grouped by the boundary that it crosses.
    std::map<CrossingKey, std::vector<size_t>> crossings(const std::vector<Step> &steps, int64_t start) {
        std::map<CrossingKey, std::vector<size_t>> result;
        int64_t p = start;
        for (size_t i = 0; i < steps.size(); ++i) {
            const Step &s = steps[i];
            const int64_t boundary2 = s.positive ? 2 * p + 1 : 2 * p - 1;
            result[{ boundary2, s.positive }].push_back(i);
            p += s.positive ? 1 : -1;
        }
        return result;
    }

    // Statistics: pair the steps of the same boundary and direction so that the sum of the time
    // differences is smallest (dynamic programming); a step without a partner costs PAIR_WINDOW_US.
    void pair_statistics(AxisComparison &c, const std::vector<Step> &buddy, const std::vector<Step> &klipper, const std::vector<bool> &klipper_sent, int64_t buddy_start,
        int64_t klipper_start) {
        const auto b = crossings(buddy, buddy_start);
        const auto k = crossings(klipper, klipper_start);
        std::vector<bool> klipper_matched(klipper.size(), false);
        double sum_abs = 0;
        bool first = true;
        for (const auto &[key, b_idx] : b) {
            const auto it = k.find(key);
            static const std::vector<size_t> none;
            const std::vector<size_t> &k_idx = it == k.end() ? none : it->second;
            const size_t nb = b_idx.size(), nk = k_idx.size();
            std::vector<double> cost((nb + 1) * (nk + 1));
            const auto at = [&](size_t i, size_t j) -> double & { return cost[i * (nk + 1) + j]; };
            const auto diff = [&](size_t i, size_t j) { return buddy[b_idx[i]].time_us - klipper[k_idx[j]].time_us; };
            for (size_t i = nb + 1; i-- > 0;) {
                for (size_t j = nk + 1; j-- > 0;) {
                    if (i == nb || j == nk) {
                        at(i, j) = double((nb - i) + (nk - j)) * PAIR_WINDOW_US;
                        continue;
                    }
                    double best = std::min(at(i + 1, j), at(i, j + 1)) + PAIR_WINDOW_US;
                    if (std::abs(diff(i, j)) <= PAIR_WINDOW_US) {
                        best = std::min(best, at(i + 1, j + 1) + std::abs(diff(i, j)));
                    }
                    at(i, j) = best;
                }
            }
            size_t i = 0, j = 0;
            while (i < nb && j < nk) {
                const double d = diff(i, j);
                if (std::abs(d) <= PAIR_WINDOW_US && at(i, j) == at(i + 1, j + 1) + std::abs(d)) {
                    klipper_matched[k_idx[j]] = true;
                    ++i;
                    ++j;
                    ++c.matched;
                    if (first || d < c.min_diff_us) {
                        c.min_diff_us = d;
                    }
                    if (first || d > c.max_diff_us) {
                        c.max_diff_us = d;
                    }
                    first = false;
                    sum_abs += std::abs(d);
                } else if (at(i, j) == at(i + 1, j) + PAIR_WINDOW_US) {
                    ++i;
                } else {
                    ++j;
                }
            }
        }
        c.unmatched_buddy = c.buddy_steps - c.matched;
        for (size_t ki = 0; ki < klipper.size(); ++ki) {
            if (!klipper_matched[ki] && klipper_sent[ki]) {
                ++c.unmatched_klipper;
            }
        }
        c.mean_abs_diff_us = c.matched ? sum_abs / double(c.matched) : 0.;
    }

    // Which raw Klipper steps the stepcompress rollback filter keeps (from the recorder's filtered list).
    std::vector<bool> sent_steps(const std::vector<Step> &raw, const std::vector<Step> &filtered) {
        std::multiset<std::pair<double, bool>> kept;
        for (const Step &s : filtered) {
            kept.insert({ s.time_us, s.positive });
        }
        std::vector<bool> sent(raw.size(), false);
        for (size_t i = 0; i < raw.size(); ++i) {
            if (const auto it = kept.find({ raw[i].time_us, raw[i].positive }); it != kept.end()) {
                sent[i] = true;
                kept.erase(it);
            }
        }
        return sent;
    }

    // Distance from x to the interval [lo, hi].
    double distance_to(double x, double lo, double hi) {
        return x < lo ? lo - x : (x > hi ? x - hi : 0.);
    }

    constexpr double EPS_STEPS = 1e-6;
} // namespace

PaErrorBound::PaErrorBound(const Config &config, const std::vector<Segment> &segments) {
    if (config.pressure_advance <= 0.f) {
        return; // classic step generator on E: no sampling
    }
    const double pa = config.pressure_advance;
    const double st = config.smooth_time;
    const double h = st / 40.; // sample period (create_pressure_advance_params())
    steps_per_mm_ = config.steps_per_mm[3];
    window_us_ = (st / 2. + h) * 1e6;
    // As is_pressure_advance_active(): extrusion together with X or Y motion
    const auto active = [](const Segment &s) { return s.axes_r[3] > 0. && (s.axes_r[0] != 0. || s.axes_r[1] != 0.); };
    // PA position p = e + pa * v_e (if active), its speed q = v_e + pa * a_e (if active)
    const auto v_e = [](const Segment &s, bool at_end) { return (s.start_v + (at_end ? 2. * s.half_accel * s.move_time : 0.)) * s.axes_r[3]; };
    const auto a_e = [](const Segment &s) { return 2. * s.half_accel * s.axes_r[3]; };
    const auto p_jump = [&](const Segment &s, bool at_end) { return active(s) ? pa * v_e(s, at_end) : 0.; };
    const auto q = [&](const Segment &s, bool at_end) { return v_e(s, at_end) + (active(s) ? pa * a_e(s) : 0.); };

    double max_a = 0.;
    for (size_t i = 0; i < segments.size(); ++i) {
        // The ending empty move (a halt) stays in the loop: E stops there, so the PA speed can jump
        // (fuzz stop r2, the end of a deceleration with pa * a_e = -165 mm/s). Its speed and
        // acceleration are 0, so it adds nothing else.
        const Segment &s = segments[i];
        max_a = std::max(max_a, std::abs(a_e(s)));
        if (i > 0) {
            const Segment &prev = segments[i - 1];
            const double dp = std::abs(p_jump(s, false) - p_jump(prev, true));
            const double dq = std::abs(q(s, false) - q(prev, true));
            // A position jump J moves to the next sample (up to h late). The smoothed jump rises with
            // a slope of at most J / (st / 2) (triangle window). A speed jump dq bends the smoothed
            // curve with dq / (st / 2), and linear interpolation between samples cuts the bend.
            // scripts/pa_bounds.py compares the sampled and the exact smoothing over all jump phases:
            // the largest errors are exactly J / 40 and st * dq / 4800.
            if (dp > 0. || dq > 0.) {
                jumps_.push_back({ s.print_time * 1e6, dp / 40. + st * dq / 4800. });
            }
        }
    }
    // A constant acceleration a: linear interpolation between samples is off by h^2 * a / 8.
    base_mm_ = h * h * max_a / 8.;
}

double PaErrorBound::at(double time_us) const {
    if (steps_per_mm_ == 0.) {
        return 0.;
    }
    double mm = base_mm_;
    // jumps_ is in time order
    auto it = std::lower_bound(jumps_.begin(), jumps_.end(), time_us - window_us_, [](const Jump &j, double t) { return j.time_us < t; });
    for (; it != jumps_.end() && it->time_us <= time_us + window_us_; ++it) {
        mm += it->mm;
    }
    return mm * steps_per_mm_;
}

IsEpsilonBound::IsEpsilonBound(const Config &config, const std::vector<Segment> &segments, int axis) {
    std::vector<const input_shaper::AxisConfig *> shapers;
    if (axis == 0 || axis == 1) { // CoreXY: A and B follow the X and Y shapers
        for (int i = 0; i < 2; ++i) {
            if (config.shaper[i]) {
                shapers.push_back(&*config.shaper[i]);
            }
        }
    } else if (axis == 2 && config.shaper[2]) {
        shapers.push_back(&*config.shaper[2]);
    }
    if (shapers.empty()) {
        return;
    }
    // The pulse span S: a micro segment with pulses on both sides of a segment boundary is at most
    // S long.
    double span_s = 0.;
    int pulse_count = axis < 2 ? 2 : 0; // a null shaper of X or Y has one pulse
    for (const auto *c : shapers) {
        const input_shaper::Shaper p = input_shaper::get(c->damping_ratio, c->frequency, c->vibration_reduction, c->type);
        span_s = std::max(span_s, double(p.t[p.num_pulses - 1] - p.t[0]));
        pulse_count += p.num_pulses - (axis < 2 ? 1 : 0);
    }
    // The stepper share of the path: CoreXY A = X + Y, B = X - Y (get_move_axis_r()).
    const auto r_axis = [axis](const Segment &s) {
        return axis == 0 ? s.axes_r[0] + s.axes_r[1] : (axis == 1 ? s.axes_r[0] - s.axes_r[1] : s.axes_r[2]);
    };
    // The firmware compares a float with the float epsilons. On CoreXY it sums the X and the Y terms
    // in float over the pulses and takes the difference (calc_micro_move_segment()), so its result
    // can be off by a few float ulps of the logical terms for each pulse. `float_margin` covers this
    // (fourth review: the double h 0.1000012 is 0.0999756 in float, and the firmware zeroes it).
    const auto logical = [axis](const Segment &s) {
        return axis < 2 ? std::abs(s.axes_r[0]) + std::abs(s.axes_r[1]) : std::abs(s.axes_r[2]);
    };
    const auto float_margin = [&](double value_of_path) { return 4. * pulse_count * std::ldexp(value_of_path, -24); };
    steps_per_mm_ = config.steps_per_mm[axis];
    for (const Segment &s : segments) {
        if (s.flags & MOVE_FLAG_ENDING_EMPTY_MOVE) {
            continue;
        }
        const double T = s.move_time;
        const double accel_eps = double(INPUT_SHAPER_ACCELERATION_EPSILON) + float_margin(std::abs(s.half_accel) * logical(s));
        const double v_max = std::max(std::abs(s.start_v), std::abs(s.start_v + 2. * s.half_accel * T));
        const double v_eps = double(INPUT_SHAPER_VELOCITY_EPSILON) + float_margin(v_max * logical(s));
        const double h = std::abs(s.half_accel * r_axis(s)); // stepper half acceleration
        const double v0 = s.start_v * r_axis(s), v1 = v0 + 2. * s.half_accel * r_axis(s) * T;
        // input_shaper_state_update() sets |half_accel| <= 0.1 to zero and keeps the average speed:
        // the error is up to |half_accel| * T^2 / 4. If the speed then has the other sign, it sets
        // the speed to 0: up to |half_accel| * T^2. A micro segment with all pulses in this segment
        // has the stepper acceleration h of the segment. With pulses across a boundary, it is at
        // most S long and its acceleration is below the epsilon.
        // The speed of one segment keeps its sign (the path speed is not negative). Thus the speed
        // of a micro segment inside it changes sign only if it is near zero: `slow`.
        const bool slow = std::min(std::abs(v0), std::abs(v1)) <= v_eps || (v0 < 0.) != (v1 < 0.);
        double mm = accel_eps * span_s * span_s;
        if (h > 0. && h <= accel_eps) {
            mm = std::max(mm, slow ? h * T * T : h * T * T / 4.);
        }
        // It sets |start_v| <= 1e-4 to zero: up to 1e-4 * T, with T the segment if its speed comes
        // near zero, else S.
        mm += v_eps * (slow ? std::max(T, span_s) : span_s);
        ranges_.push_back({ (s.print_time - span_s) * 1e6, (s.print_time + T + span_s) * 1e6, mm });
    }
}

double IsEpsilonBound::at(double time_us) const {
    double mm = 0.;
    for (const Range &r : ranges_) {
        if (r.from_us <= time_us && time_us <= r.to_us) {
            mm = std::max(mm, r.mm);
        }
    }
    return mm * steps_per_mm_;
}

namespace {
    struct Pulse {
        double t, a;
        int axis; // logical X = 0, Y = 1
    };

    // The pulses of one logical axis as init_input_shaper_pulses() makes them: reversed, negated
    // and centered on their weighted mean time. No shaper: one pulse at 0.
    void add_pulses(std::vector<Pulse> &out, const std::optional<input_shaper::AxisConfig> &c, int axis) {
        if (!c) {
            out.push_back({ 0., 1., axis });
            return;
        }
        const input_shaper::Shaper s = input_shaper::get(c->damping_ratio, c->frequency, c->vibration_reduction, c->type);
        double sum_a = 0., mean_t = 0.;
        for (int i = 0; i < s.num_pulses; ++i) {
            sum_a += s.a[i];
        }
        for (int i = 0; i < s.num_pulses; ++i) {
            mean_t += s.a[i] / sum_a * -s.t[i];
        }
        for (int i = s.num_pulses - 1; i >= 0; --i) {
            out.push_back({ -s.t[i] - mean_t, s.a[i] / sum_a, axis });
        }
    }
} // namespace

PulseMerge merge_pulses(const Config &config) {
    PulseMerge r;
    if (!config.shaper[0] && !config.shaper[1]) {
        return r;
    }
    std::vector<Pulse> pulses;
    add_pulses(pulses, config.shaper[0], 0);
    add_pulses(pulses, config.shaper[1], 1);
    std::stable_sort(pulses.begin(), pulses.end(), [](const Pulse &x, const Pulse &y) { return x.t < y.t; });
    // The same walk as adjust_input_shaper_pulses_to_match_time_pulses(): a pulse within 10 us of
    // the last kept pulse moves to the time of the kept pulse.
    for (const Pulse &p : pulses) {
        if (!r.grid.empty() && p.t - r.grid.back().t <= 10e-6) {
            r.grid.back().a[p.axis] += p.a;
            if (p.t > r.grid.back().t) {
                r.moved.push_back({ p.axis, p.a, p.t, r.grid.back().t });
            }
        } else {
            PulseMerge::GridPulse g { p.t, { 0., 0. } };
            g.a[p.axis] = p.a;
            r.grid.push_back(g);
        }
    }
    return r;
}

IsPulseMergeBound::IsPulseMergeBound(const Config &config, const std::vector<Segment> &segments, int axis) {
    if (axis > 1) {
        return; // the merge is only for the CoreXY steppers A and B
    }
    const std::vector<PulseMerge::Moved> moved = merge_pulses(config).moved;
    steps_per_mm_ = config.steps_per_mm[axis];
    terms_ = moved.size();
    for (size_t m = 0; m < moved.size(); ++m) {
        const PulseMerge::Moved &p = moved[m];
        for (const Segment &s : segments) {
            if (s.flags & MOVE_FLAG_ENDING_EMPTY_MOVE) {
                continue;
            }
            // The moved pulse reads the axis at T + t_kept in place of T + t_from: an error of up to
            // a * (t_from - t_kept) * |v| while T + t_from or T + t_kept is in the segment.
            const double v = std::max(std::abs(s.start_v), std::abs(s.start_v + 2. * s.half_accel * s.move_time)) * std::abs(s.axes_r[p.axis]);
            const double mm = p.a * (p.t_from - p.t_to) * v;
            if (mm > 0.) {
                ranges_.push_back({ (s.print_time - p.t_from) * 1e6, (s.print_time + s.move_time - p.t_to) * 1e6, mm, m });
            }
        }
    }
}

double IsPulseMergeBound::at(double time_us) const {
    // Each moved pulse adds its largest error at this time (A = X + Y and B = X - Y: the errors of
    // the X and Y pulses add up).
    std::vector<double> term(terms_, 0.);
    for (const Range &r : ranges_) {
        if (r.from_us <= time_us && time_us <= r.to_us) {
            term[r.term] = std::max(term[r.term], r.mm);
        }
    }
    double mm = 0.;
    for (const double t : term) {
        mm += t;
    }
    return mm * steps_per_mm_;
}

AxisComparison compare_axis(const Config &config, const BuddyResult &buddy_result, const KlipperResult &klipper_result, int axis) {
    AxisComparison c;
    const auto &buddy = buddy_result.steps.axis[axis];
    const auto &klipper = klipper_result.raw.axis[axis];
    c.buddy_steps = buddy.size();
    c.klipper_steps = klipper.size();
    const int64_t buddy_start = buddy_result.start_count_position[axis] + buddy_result.steps.skipped_net[axis];
    const int64_t klipper_start = int64_t(std::llround(klipper_result.position_steps(axis, 0.))) + klipper_result.raw.skipped_net[axis];
    const std::vector<bool> sent = sent_steps(klipper, klipper_result.filtered.axis[axis]);
    pair_statistics(c, buddy, klipper, sent, buddy_start, klipper_start);

    const PaErrorBound pa = axis == 3 ? PaErrorBound(config, buddy_result.segments) : PaErrorBound(Config { .pressure_advance = 0.f }, {});
    const IsEpsilonBound is_eps(config, buddy_result.segments, axis);
    const IsPulseMergeBound is_merge(config, buddy_result.segments, axis);
    const auto tolerance = [&](double t_us) { return pa.at(t_us) + is_eps.at(t_us) + is_merge.at(t_us); };
    const auto q = [&](double t_us) { return klipper_result.position_steps(axis, t_us); };

    // Each Buddy step: Klipper's exact position at the step time (or up to 1 us later) is near the
    // boundary that the step crosses.
    int64_t n = buddy_start;
    for (const Step &s : buddy) {
        const int64_t next = n + (s.positive ? 1 : -1);
        const double boundary = 0.5 * double(n + next);
        const double q0 = q(s.time_us), q1 = q(s.time_us + 1.);
        const double excess = distance_to(boundary, std::min(q0, q1), std::max(q0, q1)) - (STEP_POSITION_LIMIT + tolerance(s.time_us));
        if (excess > c.step_excess) {
            c.step_excess = excess;
            c.step_excess_time_us = s.time_us;
        }
        n = next;
    }
    c.buddy_end = n;
    c.buddy_net = n - buddy_result.start_count_position[axis];
    c.stepper_net = buddy_result.final_count_position[axis] - buddy_result.start_count_position[axis];
    {
        int64_t net = klipper_result.raw.skipped_net[axis];
        for (const Step &k : klipper) {
            net += k.positive ? 1 : -1;
        }
        c.klipper_net = net;
    }

    // Each crossing of Klipper's position: the Buddy position at that time is near it.
    {
        size_t i = 0;
        int64_t nb = buddy_start;
        for (const Step &k : klipper) {
            while (i < buddy.size() && buddy[i].time_us <= k.time_us) {
                nb += buddy[i++].positive ? 1 : -1;
            }
            const double excess = std::abs(double(nb) - q(k.time_us)) - (HOLD_POSITION_LIMIT + tolerance(k.time_us));
            if (excess > c.hold_excess) {
                c.hold_excess = excess;
                c.hold_excess_time_us = k.time_us;
            }
        }
    }

    // The end positions (1 s after the last step, where both are at rest). An end position exactly on
    // a boundary (a quarter-step tie) agrees with both neighbours.
    double end_time = 0.;
    if (!buddy.empty()) {
        end_time = std::max(end_time, buddy.back().time_us);
    }
    if (!klipper.empty()) {
        end_time = std::max(end_time, klipper.back().time_us);
    }
    if (!buddy_result.segments.empty()) {
        const Segment &last = buddy_result.segments.back();
        end_time = std::max(end_time, (last.flags & MOVE_FLAG_ENDING_EMPTY_MOVE ? last.print_time : last.print_time + last.move_time) * 1e6);
    }
    end_time += 1e6;
    c.klipper_end = q(end_time);
    c.end_excess = std::abs(double(c.buddy_end) - c.klipper_end) - (END_POSITION_LIMIT + tolerance(end_time));
    return c;
}

std::string describe(const AxisComparison &c) {
    char buf[400];
    std::snprintf(buf, sizeof(buf),
        "buddy %zu klipper %zu matched %zu unmatched buddy %zu klipper %zu diff us [%.3f, %.3f] mean|d| %.3f | step excess %.3f at %.1f us, hold excess %.3f at %.1f us, end %lld/%.3f",
        c.buddy_steps, c.klipper_steps, c.matched, c.unmatched_buddy, c.unmatched_klipper, c.min_diff_us, c.max_diff_us, c.mean_abs_diff_us, c.step_excess,
        c.step_excess_time_us, c.hold_excess, c.hold_excess_time_us, (long long)c.buddy_end, c.klipper_end);
    return buf;
}

std::string check_axis(const AxisComparison &c) {
    if (c.buddy_net != c.stepper_net) {
        return "the step events do not add up to the stepper position";
    }
    if (c.end_excess > EPS_STEPS) {
        return "the end position differs from Klipper";
    }
    if (c.step_excess > EPS_STEPS) {
        return "a step is not at its position on Klipper's motion";
    }
    if (c.hold_excess > EPS_STEPS) {
        return "the position is not near Klipper's motion";
    }
    return {};
}

std::string check_axis(const AxisComparison &c, int64_t stepper_net) {
    if (c.buddy_net != stepper_net) {
        return "the step events do not add up to the stepper position";
    }
    return check_axis(c);
}

} // namespace motion_diff
