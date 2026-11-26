#include "compare.hpp"

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
        const Segment &s = segments[i];
        if (s.move_time > 1e6) {
            continue; // ending empty move
        }
        max_a = std::max(max_a, std::abs(a_e(s)));
        if (i > 0) {
            const Segment &prev = segments[i - 1];
            const double dp = std::abs(p_jump(s, false) - p_jump(prev, true));
            const double dq = std::abs(q(s, false) - q(prev, true));
            // A position jump J moves to the next sample (up to h late). The smoothed jump rises with
            // a slope of at most J / (st / 2) (triangle window), so the error is up to J * h / st * 2
            // in the worst sample phase and J / 40 on average; the review measured J / 40 as the bound
            // of the step position (scripts/pa_jump.py). A speed jump dq bends the smoothed curve with
            // dq / (st / 2); linear interpolation between samples is off by h^2 / 8 of that, plus the
            // sampled kernel error: st * dq / 4800 (scripts/pa_kink.py).
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
    double f_min = 1e9;
    for (const auto *s : shapers) {
        f_min = std::min(f_min, double(s->frequency));
    }
    // The pulses span at most 2 damped periods (3-hump EI): a micro segment near a move can reach
    // this far around it.
    const double span_us = 2.5 / f_min * 1e6;
    steps_per_mm_ = config.steps_per_mm[axis];
    for (const Segment &s : segments) {
        if (s.move_time > 1e6) {
            continue; // ending empty move
        }
        const double t = s.move_time;
        ranges_.push_back({ s.print_time * 1e6 - span_us, (s.print_time + t) * 1e6 + span_us, 0.1 * t * t / 4. + 1e-4 * t });
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
    const auto tolerance = [&](double t_us) { return pa.at(t_us) + is_eps.at(t_us); };
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
        end_time = std::max(end_time, (last.move_time > 1e6 ? last.print_time : last.print_time + last.move_time) * 1e6);
    }
    end_time += 1e6;
    c.klipper_end = q(end_time);
    c.end_excess = std::abs(double(c.buddy_end) - c.klipper_end) - (STEP_POSITION_LIMIT + tolerance(end_time));
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

std::string check_axis(const AxisComparison &c, int64_t stepper_net, double, int) {
    if (c.buddy_net != stepper_net) {
        return "the step events do not add up to the stepper position";
    }
    return check_axis(c);
}

} // namespace motion_diff
