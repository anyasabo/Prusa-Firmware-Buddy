#pragma once
// Compares the step times of two step generators.
#include "motion_model.hpp"

#include <algorithm>
#include <string>

namespace motion_diff {

/// Klipper's default stepper `max_error` (klippy/stepper.py, 0.000025 s): Klipper itself sends step
/// times that differ from its ideal times by up to this much. A difference below this limit cannot
/// be told apart from Klipper's own output.
inline constexpr double KLIPPER_MAX_ERROR_US = 25.;

/// Near zero speed (ends of moves, direction changes of a shaped axis), a tiny position error is a
/// large time error. There a step also agrees if its time error is below half of the local step
/// interval: the step is still nearer to its own position than to the next step position.
inline constexpr double MAX_ERR_STEPS = 0.5;

/// Two crossings of the same step boundary in the same direction are paired only if they are this
/// close in time. Larger time differences are reported as unmatched steps.
inline constexpr double PAIR_WINDOW_US = 20000.;

struct AxisComparison {
    size_t buddy_steps = 0;
    size_t klipper_steps = 0;
    size_t matched = 0;
    size_t unmatched_buddy = 0; // steps that Klipper does not make
    size_t unmatched_klipper = 0; // steps that Buddy does not make (without the steps that Klipper rolls back)
    int64_t buddy_net = 0; // net steps (positive - negative)
    int64_t klipper_net = 0;
    // time_buddy - time_klipper of the matched steps (us). Buddy floors to whole us, so the
    // expected value is in (-1, 0].
    double min_diff_us = 0, max_diff_us = 0, mean_abs_diff_us = 0;
    double worst_time_us = 0; // Klipper time of the step with the largest |diff|
    // matched steps with |diff| > KLIPPER_MAX_ERROR_US + 1 and more than MAX_ERR_STEPS of a step interval
    size_t over_limit = 0;
    // The time difference as a part of a step: |diff| / (local step interval of Klipper). It is the
    // position error that the time difference causes. 1.0 = one whole step.
    double max_err_steps = 0;
    // The largest position difference (steps) at any time that the steps without a partner cause.
    // Klipper steps that the rollback filter removes are not counted.
    int64_t max_pos_diff = 0;
    double max_abs_diff_us() const { return std::max(-min_diff_us, max_diff_us); }
};

/// Pairs the steps by the position at which they occur: the k-th crossing of a step boundary in one
/// direction on one side is paired with the k-th crossing of the same boundary in the same direction
/// on the other side. Thus one extra step pair (step and step back) on one side does not shift the
/// rest of the comparison.
/// buddy_start and klipper_start: net steps before the first step in the list.
AxisComparison compare_axis(const std::vector<Step> &buddy, const std::vector<Step> &klipper, int64_t buddy_start = 0, int64_t klipper_start = 0);

std::string describe(const AxisComparison &c);

/// Bound of the position error (E steps) of the pressure advance step generator against Klipper. Buddy
/// samples the PA position every smooth_time / 40, filters the samples and interpolates linearly
/// between the filter outputs. Klipper integrates exactly. See the terms in compare.cpp.
double pa_error_bound_steps(const Config &config, const std::vector<Segment> &segments);

/// Checks one axis of a run against Klipper. Returns an empty string if the axis agrees, else the
/// reason. `allowed_err_steps` is the extra position error that is allowed (pa_error_bound_steps() for E).
/// `sessions`: the number of motions from a halt to a halt. Each can end with one tie step (see
/// check_axis()).
std::string check_axis(const AxisComparison &c, int64_t stepper_net, double allowed_err_steps = 0., int sessions = 1);

} // namespace motion_diff
