#pragma once
// Compares the steps of Buddy with Klipper's exact stepper position.
#include "motion_model.hpp"

#include <algorithm>
#include <string>

namespace motion_diff {

/// A step is in time if Klipper's exact position, at the Buddy step time or 1 us later (Buddy rounds
/// the step times down to whole us), is less than this many steps from the boundary that the step
/// crosses, plus the tolerances of the PA and IS bounds. All cases, the precision cases and the fuzz
/// inputs stay within 0.02 step (motion-audit.md, D1 measured up to 0.2 step at extreme float
/// conditions). A bias of 0.4 step must fail (harness_mutation_tests.cpp).
inline constexpr double STEP_POSITION_LIMIT = 0.25;

/// At each crossing of Klipper's position (a boundary n + 0.5), the Buddy position is n or n + 1,
/// with one step late or early within STEP_POSITION_LIMIT.
inline constexpr double HOLD_POSITION_LIMIT = 0.5 + STEP_POSITION_LIMIT;

/// The Buddy end position is a whole step, so it is up to 0.5 step from Klipper's exact end. A tie
/// (an end exactly on a boundary) agrees with both neighbours.
inline constexpr double END_POSITION_LIMIT = 0.5;

/// Klipper's default stepper `max_error` (25 us). Not a limit of this test (the Klipper side is the raw
/// itersolve output); the regression tests of the fix branches compare time differences with it.
inline constexpr double KLIPPER_MAX_ERROR_US = 25.;

/// Two crossings of the same step boundary in the same direction are paired only if they are this
/// close in time (for the statistics only).
inline constexpr double PAIR_WINDOW_US = 20000.;

struct AxisComparison {
    // Statistics from the pairing of the steps by boundary (information only, not a limit)
    size_t buddy_steps = 0;
    size_t klipper_steps = 0;
    size_t matched = 0;
    size_t unmatched_buddy = 0; // Buddy steps without a Klipper partner
    size_t unmatched_klipper = 0; // Klipper steps (sent to the MCU) without a Buddy partner
    double min_diff_us = 0, max_diff_us = 0, mean_abs_diff_us = 0; // time_buddy - time_klipper
    double max_abs_diff_us() const { return std::max(-min_diff_us, max_diff_us); }
    int64_t buddy_net = 0; // net steps of Buddy (positive - negative), from the start
    int64_t klipper_net = 0; // net steps of itersolve
    int64_t stepper_net = 0; // Stepper::count_position_from_startup at the end - at the start

    // Checks against Klipper's exact position (steps). Each "excess" is the amount beyond the limit:
    // 0 or less agrees.
    double step_excess = -1e9; // Buddy step too far from its boundary
    double step_excess_time_us = 0;
    double hold_excess = -1e9; // Buddy position too far from a Klipper crossing
    double hold_excess_time_us = 0;
    double end_excess = -1e9; // end positions
    int64_t buddy_end = 0; // Buddy end position (steps)
    double klipper_end = 0; // Klipper exact end position (steps)
};

/// Compares one stepper. Uses the pressure advance error bound (PaErrorBound) for E.
AxisComparison compare_axis(const Config &config, const BuddyResult &buddy, const KlipperResult &klipper, int axis);

std::string describe(const AxisComparison &c);

/// Returns an empty string if the axis agrees, else the reason.
std::string check_axis(const AxisComparison &c);

/// As check_axis(c), and also checks that the step events add up to `stepper_net`.
std::string check_axis(const AxisComparison &c, int64_t stepper_net);

/// Bound of the position error (E steps) of the pressure advance step generator at `time_us`. Buddy
/// samples the PA position every h = smooth_time / 40, filters the samples and interpolates linearly
/// between the filter outputs. Klipper integrates exactly. See the terms in compare.cpp.
class PaErrorBound {
public:
    PaErrorBound(const Config &config, const std::vector<Segment> &segments);
    double at(double time_us) const;

private:
    struct Jump {
        double time_us, mm;
    };
    std::vector<Jump> jumps_; // the error of each jump, valid within window_us of it
    double window_us_ = 0;
    double base_mm_ = 0; // constant acceleration term, everywhere
    double steps_per_mm_ = 0;
};

/// Bound of the position error (steps) of an input shaper step generator at `time_us`. Buddy sets
/// the acceleration of a micro segment to zero if |half_accel| <= 0.1 mm/s^2 and keeps its average
/// speed (up to |half_accel| * T^2 / 4), and sets the speed to 0 if it then changes sign (up to
/// |half_accel| * T^2). It sets speeds below 1e-4 mm/s to zero (up to 1e-4 * T). T is the segment
/// length for a segment with a stepper acceleration below the epsilon, else the pulse span (a micro
/// segment across a segment boundary). See compare.cpp.
class IsEpsilonBound {
public:
    IsEpsilonBound(const Config &config, const std::vector<Segment> &segments, int axis);
    double at(double time_us) const;

private:
    struct Range {
        double from_us, to_us, mm;
    };
    std::vector<Range> ranges_;
    double steps_per_mm_ = 0;
};

} // namespace motion_diff
