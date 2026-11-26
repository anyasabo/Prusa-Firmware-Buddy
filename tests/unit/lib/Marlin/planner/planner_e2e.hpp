#pragma once
// End-to-end run: the real planner and the real PreciseStepping, compared with Klipper.
#include "planner_harness.hpp"

#include <compare.hpp>
#include <motion_model.hpp>

#include <functional>

namespace planner_e2e {

struct Result {
    motion_diff::Config config;
    motion_diff::BuddyResult buddy;
    motion_diff::KlipperResult klipper;
    std::array<motion_diff::AxisComparison, motion_diff::AXES> axis;
    xyze_msteps_t planner_end; // Planner::position at the end
};

/// Resets the planner and PreciseStepping with `config` (steps/mm, input shaper, pressure advance),
/// runs `program` (planner_harness::move_to() and the G92 functions), and runs the motion to its
/// end. PreciseStepping runs only when the planner queue is full and at the end, so the motion is
/// one session without a halt.
Result run(const motion_diff::Config &config, const std::function<void()> &program);

} // namespace planner_e2e
