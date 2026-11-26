#pragma once
// Helpers shared by the test files of the differential test.
#include "compare.hpp"
#include "motion_model.hpp"

namespace motion_diff {

struct Run {
    Config config;
    BuddyResult buddy;
    KlipperResult klipper;
    std::array<AxisComparison, AXES> axis;
};

/// Runs Buddy, then Klipper on the move segments of Buddy, and compares each stepper.
inline Run run_both(const Config &config, const std::vector<Block> &blocks) {
    Run r;
    r.config = config;
    r.buddy = run_buddy(config, blocks);
    r.klipper = run_klipper(config, r.buddy.segments, start_position_mm(config));
    for (int i = 0; i < AXES; ++i) {
        r.axis[i] = compare_axis(r.buddy.steps.axis[i], r.klipper.raw.axis[i], r.buddy.steps.skipped_net[i], r.klipper.raw.skipped_net[i]);
    }
    return r;
}

/// A straight line from rest to rest, with a cruise phase.
inline Block line(double dx, double dy, double de, float v, float a) {
    return { .dx = dx, .dy = dy, .de = de, .v_start = 0, .v_cruise = v, .v_end = 0, .accel = a };
}

} // namespace motion_diff
