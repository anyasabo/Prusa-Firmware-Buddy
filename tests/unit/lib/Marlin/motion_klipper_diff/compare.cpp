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

    double sum_abs = 0;
    bool first = true;
    for (const auto &[key, b_idx] : b) {
        const auto it = k.find(key);
        static const std::vector<size_t> none;
        const std::vector<size_t> &k_idx = it == k.end() ? none : it->second;
        // Both lists are in time order. Pair the nearest crossings: if the two candidates are more
        // than PAIR_WINDOW_US apart, the earlier one has no partner (an extra step pair on one side).
        size_t i = 0, j = 0, n = 0;
        while (i < b_idx.size() && j < k_idx.size()) {
            const size_t ki = k_idx[j];
            const double d = buddy[b_idx[i]].time_us - klipper[ki].time_us;
            if (d < -PAIR_WINDOW_US) {
                ++i;
                continue;
            }
            if (d > PAIR_WINDOW_US) {
                ++j;
                continue;
            }
            ++i;
            ++j;
            ++n;
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
    c.unmatched_klipper = c.klipper_steps - c.matched;
    c.mean_abs_diff_us = c.matched ? sum_abs / double(c.matched) : 0.;
    return c;
}

std::string describe(const AxisComparison &c) {
    char buf[400];
    std::snprintf(buf, sizeof(buf),
        "buddy %zu klipper %zu matched %zu unmatched buddy %zu klipper %zu net %lld/%lld diff us [%.3f, %.3f] mean|d| %.3f worst at %.1f us over %zu err %.4f steps",
        c.buddy_steps, c.klipper_steps, c.matched, c.unmatched_buddy, c.unmatched_klipper, (long long)c.buddy_net, (long long)c.klipper_net,
        c.min_diff_us, c.max_diff_us, c.mean_abs_diff_us, c.worst_time_us, c.over_limit, c.max_err_steps);
    return buf;
}

double pa_jump_steps(const Config &config, const std::vector<Segment> &segments) {
    if (config.pressure_advance <= 0.f) {
        return 0.;
    }
    // As is_pressure_advance_active(): extrusion together with X or Y motion
    const auto pa_velocity = [&](const Segment &s, bool at_end) {
        const bool active = s.axes_r[3] > 0. && (s.axes_r[0] != 0. || s.axes_r[1] != 0.);
        const double v = s.start_v + (at_end ? 2. * s.half_accel * s.move_time : 0.);
        return active ? config.pressure_advance * v * s.axes_r[3] : 0.;
    };
    double max_jump = 0.;
    for (size_t i = 1; i < segments.size(); ++i) {
        max_jump = std::max(max_jump, std::abs(pa_velocity(segments[i], false) - pa_velocity(segments[i - 1], true)));
    }
    return max_jump * config.steps_per_mm[3];
}

std::string check_axis(const AxisComparison &c, int64_t stepper_net, double allowed_err_steps) {
    if (c.buddy_net != stepper_net) {
        return "the step events do not add up to the stepper position";
    }
    // The planner positions are quarter steps: an end position exactly on a step boundary is a tie, and
    // the last step at zero speed can go either way. The next move takes the step if it is needed.
    if (std::abs(c.buddy_net - c.klipper_net) > 1) {
        return "the end position differs from Klipper";
    }
    // Steps near zero speed: Klipper or Buddy can make one extra step pair where the other does not.
    const size_t allowed_unmatched = 4 + 2 * size_t(std::ceil(allowed_err_steps));
    if (c.unmatched_buddy > allowed_unmatched || c.unmatched_klipper > allowed_unmatched) {
        return "too many steps without a partner";
    }
    // One step per axis can land exactly on the end position of a move (a quarter-step tie, as above):
    // it happens at zero speed, so its time is not defined, up to the end of the shaped motion.
    if (c.max_err_steps > MAX_ERR_STEPS + allowed_err_steps && c.over_limit > 1) {
        return "step times differ from Klipper";
    }
    return {};
}

} // namespace motion_diff
