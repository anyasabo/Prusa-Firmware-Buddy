#pragma once
// Common types of the differential test: the motion that the test sends, and the steps that come out.

#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <feature/input_shaper/input_shaper_config.hpp>

namespace motion_diff {

inline constexpr int AXES = 4; // physical steppers: A, B (CoreXY), Z, E

/// One planner block: a straight line with a trapezoid speed profile (the input of PreciseStepping).
struct Block {
    double dx = 0, dy = 0, dz = 0, de = 0; // cartesian displacement (mm)
    float v_start = 0, v_cruise = 0, v_end = 0; // along the path (mm/s)
    float accel = 0; // along the path (mm/s^2)
    // The motion stops before this block and PreciseStepping comes back to a halt (reset_from_halt()),
    // as when the planner queue runs dry. v_start must be 0, and the block before must end at 0.
    bool halt_before = false;
};

/// The E position (steps) at which the next session starts. PreciseStepping starts a session at
/// the integer E step position of the halt. The session before moved E by `delta_msteps` (the
/// commanded delta, in planner mini-steps) from `offset_steps`. If the exact end is a whole step
/// plus a half (a tie), the last step can go either way: then take Buddy's E position, but only if
/// it is one of the two neighbours. Else a lost step would move the offset and never show.
inline int64_t next_session_e_offset(int64_t offset_steps, int64_t delta_msteps, int msteps_per_step, int64_t buddy_e_steps) {
    const int64_t end_msteps = offset_steps * msteps_per_step + delta_msteps;
    const int64_t rem = end_msteps % msteps_per_step;
    if (rem == msteps_per_step / 2 || rem == -msteps_per_step / 2) {
        const int64_t lo = (end_msteps - (rem > 0 ? rem : msteps_per_step + rem)) / msteps_per_step;
        if (buddy_e_steps == lo || buddy_e_steps == lo + 1) {
            return buddy_e_steps;
        }
    }
    return std::llround(double(end_msteps) / msteps_per_step);
}

/// The smooth time that Buddy uses: create_pressure_advance_params() rounds the sample period
/// (smooth_time / 40) to whole us. Klipper gets this value, so both smooth over the same window.
inline double effective_smooth_time(float smooth_time) {
    return double(std::llround(double(smooth_time) / 40. * 1e6)) * 40e-6;
}

struct Config {
    // CORE One L, 2GT belts, 16 microsteps (Configuration_COREONEL.h)
    std::array<float, AXES> steps_per_mm { 100.f, 100.f, 800.f, 380.f };
    std::array<std::optional<input_shaper::AxisConfig>, 3> shaper {}; // cartesian X, Y, Z
    float pressure_advance = 0.f;
    float smooth_time = 0.04f;
    // Start position in physical steps (A, B, Z). A + B must be even on CoreXY. E starts at 0.
    std::array<int32_t, 3> start_steps { 0, 0, 0 };
    // Steps before this time are generated but not recorded (long prefixes in the precision study).
    double record_from_us = 0;
};

/// One move segment (constant acceleration), as both Buddy move_t and Klipper struct move.
struct Segment {
    double print_time, move_time; // s
    double start_v, half_accel; // along the path
    std::array<double, AXES> start_pos; // cartesian X, Y, Z, E (mm)
    std::array<double, AXES> axes_r; // cartesian X, Y, Z, E
    uint32_t flags; // Buddy MoveFlag
};

struct Step {
    double time_us;
    bool positive;
};

struct Steps {
    std::array<std::vector<Step>, AXES> axis;
    std::array<int64_t, AXES> skipped_net {}; // net steps before Config::record_from_us
};

struct BuddyResult {
    Steps steps;
    std::vector<Segment> segments; // all move segments that PreciseStepping made, in order
    std::array<int32_t, AXES> final_count_position {}; // Stepper::count_position_from_startup
    std::array<int32_t, AXES> start_count_position {};
    bool finished = false; // the pipeline came back to a halt
    int sessions = 0; // motion from a halt to a halt (beginning empty moves)
};

struct KlipperSession; // owns the Klipper trapq and stepper kinematics after the run

struct KlipperResult {
    Steps raw; // each step of itersolve
    Steps filtered; // after the rollback filter of Klipper stepcompress
    std::shared_ptr<KlipperSession> session;
    std::array<double, AXES> step_dist {}; // mm

    /// The exact position of stepper `axis` at `time_us`, in steps (boundaries at k + 0.5, the same
    /// grid as the Buddy step counter).
    double position_steps(int axis, double time_us) const;
};

/// Input shaper pulses in the traditional definition, from the Buddy shaper definitions.
struct Pulses {
    int n = 0;
    std::array<double, 5> a {}, t {};
};
Pulses buddy_shaper_pulses(const input_shaper::AxisConfig &cfg);

BuddyResult run_buddy(const Config &config, const std::vector<Block> &blocks);
KlipperResult run_klipper(const Config &config, const std::vector<Segment> &segments, std::array<double, 3> start_pos_mm);

/// Cartesian start position (mm) of the Buddy run, from the start steps.
std::array<double, 3> start_position_mm(const Config &config);

} // namespace motion_diff
