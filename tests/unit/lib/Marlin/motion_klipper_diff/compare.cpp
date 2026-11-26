#include "compare.hpp"

#include <cmath>
#include <cstdio>
#include <map>

namespace motion_diff {

namespace {
    // Key of the step boundary that a step crosses: the step from p to p+1 crosses p+0.5.
    using CrossingKey = std::pair<int64_t, bool>; // (2 * boundary, positive)

    // Index of each step in the list, grouped by the boundary that it crosses.
    std::map<CrossingKey, std::vector<size_t>> crossings(const std::vector<Step> &steps, int64_t start, int64_t &net) {
        std::map<CrossingKey, std::vector<size_t>> result;
        int64_t p = start;
        for (size_t i = 0; i < steps.size(); ++i) {
            const Step &s = steps[i];
            const int64_t boundary2 = s.positive ? 2 * p + 1 : 2 * p - 1;
            result[{ boundary2, s.positive }].push_back(i);
            p += s.positive ? 1 : -1;
        }
        net = p;
        return result;
    }

    // Klipper stepcompress removes a step that is followed or preceded by a step in the other direction
    // within SDS_FILTER_TIME (750 us). Such steps never reach the MCU.
    bool rolled_back(const std::vector<Step> &steps, size_t i) {
        constexpr double sds_filter_time_us = 750.;
        const auto opposite_near = [&](size_t j) {
            return steps[j].positive != steps[i].positive && std::abs(steps[j].time_us - steps[i].time_us) < sds_filter_time_us;
        };
        return (i > 0 && opposite_near(i - 1)) || (i + 1 < steps.size() && opposite_near(i + 1));
    }

    // Time between the neighbours of step i (us), or the interval to the one neighbour at the ends.
    double local_interval(const std::vector<Step> &steps, size_t i) {
        const size_t lo = i > 0 ? i - 1 : i;
        const size_t hi = i + 1 < steps.size() ? i + 1 : i;
        if (hi == lo) {
            return 1e9; // single step: no speed known
        }
        return (steps[hi].time_us - steps[lo].time_us) / double(hi - lo);
    }
} // namespace

AxisComparison compare_axis(const std::vector<Step> &buddy, const std::vector<Step> &klipper, int64_t buddy_start, int64_t klipper_start) {
    AxisComparison c;
    c.buddy_steps = buddy.size();
    c.klipper_steps = klipper.size();
    const auto b = crossings(buddy, buddy_start, c.buddy_net);
    auto k = crossings(klipper, klipper_start, c.klipper_net);

    std::vector<bool> klipper_matched(klipper.size(), false);
    std::vector<bool> buddy_matched(buddy.size(), false);
    double sum_abs = 0;
    bool first = true;
    for (const auto &[key, b_idx] : b) {
        const auto it = k.find(key);
        static const std::vector<size_t> none;
        const std::vector<size_t> &k_idx = it == k.end() ? none : it->second;
        // Both lists are in time order. Align them so that the sum of the time differences is
        // smallest; a step without a partner costs PAIR_WINDOW_US (as a time difference at the
        // window). Pairs that are more than PAIR_WINDOW_US apart are not allowed. Thus an extra step
        // pair on one side stays unmatched and does not shift the pairs after it.
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
        size_t i = 0, j = 0, n = 0;
        while (i < nb && j < nk) {
            const double d = diff(i, j);
            if (std::abs(d) <= PAIR_WINDOW_US && at(i, j) == at(i + 1, j + 1) + std::abs(d)) {
                // paired: handled below
            } else if (at(i, j) == at(i + 1, j) + PAIR_WINDOW_US) {
                ++i;
                continue;
            } else {
                ++j;
                continue;
            }
            const size_t ki = k_idx[j];
            ++i;
            ++j;
            ++n;
            klipper_matched[ki] = true;
            buddy_matched[b_idx[i - 1]] = true;
            if (rolled_back(klipper, ki)) {
                continue; // Klipper does not send this step: no time to compare
            }
            const double interval = local_interval(klipper, ki);
            if (interval > 0) {
                c.max_err_steps = std::max(c.max_err_steps, std::abs(d) / interval);
            }
            if (first || d < c.min_diff_us) {
                c.min_diff_us = d;
            }
            if (first || d > c.max_diff_us) {
                c.max_diff_us = d;
            }
            if (first || std::abs(d) >= c.max_abs_diff_us()) {
                c.worst_time_us = klipper[ki].time_us;
            }
            first = false;
            sum_abs += std::abs(d);
            if (std::abs(d) > KLIPPER_MAX_ERROR_US + 1. && (interval <= 0 || std::abs(d) / interval > MAX_ERR_STEPS)) {
                ++c.over_limit;
            }
        }
        c.matched += n;
        c.unmatched_buddy += b_idx.size() - n;
    }
    // Klipper steps without a partner count only if Klipper sends them to the MCU. itersolve can make a
    // step and a step back at the same time at a move boundary; stepcompress removes both.
    for (size_t ki = 0; ki < klipper.size(); ++ki) {
        if (!klipper_matched[ki] && !rolled_back(klipper, ki)) {
            ++c.unmatched_klipper;
        }
    }
    c.mean_abs_diff_us = c.matched ? sum_abs / double(c.matched) : 0.;

    // Walk the steps without a partner in time order: the position difference that they cause. The
    // matched steps have passed the time check above.
    {
        int64_t diff = buddy_start - klipper_start;
        c.max_pos_diff = std::abs(diff);
        size_t i = 0, j = 0;
        while (i < buddy.size() || j < klipper.size()) {
            const bool take_buddy = j == klipper.size() || (i < buddy.size() && buddy[i].time_us <= klipper[j].time_us);
            if (take_buddy) {
                if (!buddy_matched[i]) {
                    diff += buddy[i].positive ? 1 : -1;
                }
                ++i;
            } else {
                if (!klipper_matched[j] && !rolled_back(klipper, j)) {
                    diff -= klipper[j].positive ? 1 : -1;
                }
                ++j;
            }
            c.max_pos_diff = std::max(c.max_pos_diff, std::abs(diff));
        }
    }
    return c;
}

std::string describe(const AxisComparison &c) {
    char buf[400];
    std::snprintf(buf, sizeof(buf),
        "buddy %zu klipper %zu matched %zu unmatched buddy %zu klipper %zu net %lld/%lld diff us [%.3f, %.3f] mean|d| %.3f worst at %.1f us over %zu err %.4f steps pos diff %lld",
        c.buddy_steps, c.klipper_steps, c.matched, c.unmatched_buddy, c.unmatched_klipper, (long long)c.buddy_net, (long long)c.klipper_net,
        c.min_diff_us, c.max_diff_us, c.mean_abs_diff_us, c.worst_time_us, c.over_limit, c.max_err_steps, (long long)c.max_pos_diff);
    return buf;
}

double pa_error_bound_steps(const Config &config, const std::vector<Segment> &segments) {
    if (config.pressure_advance <= 0.f) {
        return 0.; // classic step generator on E: no sampling
    }
    const double pa = config.pressure_advance;
    const double st = config.smooth_time;
    const double h = st / 40.; // sample period (create_pressure_advance_params())
    // As is_pressure_advance_active(): extrusion together with X or Y motion
    const auto active = [](const Segment &s) { return s.axes_r[3] > 0. && (s.axes_r[0] != 0. || s.axes_r[1] != 0.); };
    // PA position p = e + pa * v_e (if active), its speed q = v_e + pa * a_e (if active)
    const auto v_e = [](const Segment &s, bool at_end) { return (s.start_v + (at_end ? 2. * s.half_accel * s.move_time : 0.)) * s.axes_r[3]; };
    const auto a_e = [](const Segment &s) { return 2. * s.half_accel * s.axes_r[3]; };
    const auto p_jump = [&](const Segment &s, bool at_end) { return active(s) ? pa * v_e(s, at_end) : 0.; };
    const auto q = [&](const Segment &s, bool at_end) { return v_e(s, at_end) + (active(s) ? pa * a_e(s) : 0.); };

    // Jumps at the segment boundaries, with their time
    struct Jump {
        double time, p, q;
    };
    std::vector<Jump> jumps;
    double max_a = 0.;
    for (size_t i = 0; i < segments.size(); ++i) {
        const Segment &s = segments[i];
        if (s.move_time > 1e6) {
            continue; // ending empty move
        }
        max_a = std::max(max_a, std::abs(a_e(s)));
        if (i > 0) {
            const Segment &prev = segments[i - 1];
            jumps.push_back({ s.print_time, std::abs(p_jump(s, false) - p_jump(prev, true)), std::abs(q(s, false) - q(prev, true)) });
        }
    }
    // The errors of all jumps within one smoothing window add up (for example a short retraction
    // between two extrusions: a jump down and a jump up). Take the largest sum in a window of st.
    double max_p_jump = 0., max_q_jump = 0.;
    for (size_t i = 0; i < jumps.size(); ++i) {
        double p_sum = 0., q_sum = 0.;
        for (size_t j = i; j < jumps.size() && jumps[j].time - jumps[i].time <= st; ++j) {
            p_sum += jumps[j].p;
            q_sum += jumps[j].q;
        }
        max_p_jump = std::max(max_p_jump, p_sum);
        max_q_jump = std::max(max_q_jump, q_sum);
    }
    // A position jump J moves to the next sample: error J * h / (st / 2) = J / 20.
    // A speed jump dq: the smoothed curve bends with dq / (st / 2), linear interpolation between
    // samples is off by h^2 / 8 of that: st * dq / 6400.
    // A constant acceleration a: linear interpolation is off by h^2 * a / 8.
    const double mm = max_p_jump / 20. + st * max_q_jump / 6400. + h * h * max_a / 8.;
    return mm * config.steps_per_mm[3];
}

std::string check_axis(const AxisComparison &c, int64_t stepper_net, double allowed_err_steps, int sessions) {
    if (c.buddy_net != stepper_net) {
        return "the step events do not add up to the stepper position";
    }
    // The planner positions are quarter steps: an end position exactly on a step boundary is a tie, and
    // the last step at zero speed can go either way. The next move takes the step if it is needed.
    if (std::abs(c.buddy_net - c.klipper_net) > 1) {
        return "the end position differs from Klipper";
    }
    // Near zero speed, one side can make step pairs (a step and a step back) that the other side does
    // not make, and the two positions are one step apart for a short time. Thus the limit is the
    // position difference, not the number of steps without a partner.
    const int64_t allowed_pos_diff = 1 + int64_t(std::ceil(allowed_err_steps));
    if (c.max_pos_diff > allowed_pos_diff) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "the positions differ by %lld steps (allowed %lld)", (long long)c.max_pos_diff, (long long)allowed_pos_diff);
        return buf;
    }
    // One step for each axis and motion can land exactly on the end position (a quarter-step tie, as
    // above): it happens at zero speed, so its time is not defined. Buddy can make it at the end of the
    // shaped motion or at the start of the next motion after the halt.
    if (c.max_err_steps > MAX_ERR_STEPS + allowed_err_steps && c.over_limit > size_t(std::max(sessions, 1))) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "step times differ from Klipper (allowed %.2f steps)", MAX_ERR_STEPS + allowed_err_steps);
        return buf;
    }
    return {};
}

} // namespace motion_diff
