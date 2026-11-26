#pragma once
// The properties that each sequence of planner blocks must have. The limits are computed again here
// in double, from the block mini-steps and the settings, not read from the block.
#include "planner_harness.hpp"

#include <string>
#include <vector>

namespace planner_checks {

struct Expected {
    float max_fr = 0; // the highest feed rate of the program
};

struct Report {
    std::vector<std::string> failures;
    /// Mini-steps that a G92 discarded: the planner position before the G92 minus the commanded
    /// position. The planner drops a move shorter than MIN_STEPS_PER_SEGMENT and adds it to the
    /// next move. A G92 directly after the short move discards it.
    xyze_msteps_t lost_at_g92 {};
    /// Blocks with a speed change that is not feasible, because the entry or the exit speed is below
    /// MINIMUM_PLANNER_SPEED (see check()).
    int exit_below_minimum_speed = 0;
};

Report check(const planner_harness::Settings &s, const std::vector<planner_harness::Taken> &blocks, const std::vector<planner_harness::Jump> &jumps, const Expected &expected);

} // namespace planner_checks
