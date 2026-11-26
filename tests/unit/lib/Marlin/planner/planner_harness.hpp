#pragma once
// Drives the real planner as the motion code and PreciseStepping do, and keeps a copy of each block
// at the time PreciseStepping takes it. After that time the planner cannot change the block.
#include <module/planner.h>

#include <array>
#include <cstddef>
#include <vector>

namespace planner_harness {

/// The planner settings of a CORE One L with 1.5GT belts (MarlinSettings::reset_motion() with the
/// defaults of Configuration_COREONEL.h).
struct Settings {
    std::array<float, XYZE_N> steps_per_mm { AXIS_STEPS_PER_UNIT_15GT_XY, AXIS_STEPS_PER_UNIT_15GT_XY, DEFAULT_AXIS_STEPS_PER_UNIT_Z, DEFAULT_AXIS_STEPS_PER_UNIT_E0 };
    std::array<uint32_t, XYZE_N> max_acceleration DEFAULT_MAX_ACCELERATION; // M201
    std::array<float, XYZE_N> max_feedrate DEFAULT_MAX_FEEDRATE; // M203
    float acceleration = DEFAULT_ACCELERATION; // M204 P
    float retract_acceleration = DEFAULT_RETRACT_ACCELERATION; // M204 R
    float travel_acceleration = DEFAULT_TRAVEL_ACCELERATION; // M204 T
    float max_filament_acceleration = DEFAULT_MAX_FILAMENT_ACCELERATION; // M204 F
    float junction_deviation = JUNCTION_DEVIATION_MM; // M205 J
    int16_t flow_percentage = 100; // M221
};

/// The fields of a block_t (which cannot be copied: it has volatile members).
struct BlockCopy {
    bool sync;
    uint8_t direction_bits;
    float nominal_speed, entry_speed_sqr, max_entry_speed_sqr, millimeters, acceleration;
    float initial_speed, final_speed;
    xyze_msteps_t msteps; // move blocks
    xyze_steps_t sync_step_position; // sync blocks
    // Planner::position_float before and after the block (move blocks). The planner computes the
    // junction from these, not from the rounded mini-steps.
    MachinePosXYZE start_mm, end_mm;
};

/// A block as PreciseStepping took it.
struct Taken {
    BlockCopy block;
    xyze_msteps_t planner_position_after; // Planner::position when the block was taken (for the log)
};

/// Starts again with an empty queue, the position 0 and the given settings.
void reset(const Settings &settings = {});

/// G0/G1 to `target` (machine mm) at `fr_mm_s`, as prepare_move_to_destination() without
/// segmentation: Planner::buffer_line() and current_position = target.
bool move_to(const xyze_pos_t &target, float fr_mm_s, MoveHints hints = {});

/// G92 E<e>, G92 with all axes.
void set_e_position(float e);
void set_position(const xyze_pos_t &pos);

/// PreciseStepping takes up to `n` blocks (sync blocks too). Returns the number of blocks taken.
size_t take(size_t n);
/// PreciseStepping takes all the blocks.
void take_all();

/// The planner waits in idle() when the queue is full. Then PreciseStepping takes one block, or
/// the hook runs (the end-to-end test runs the real PreciseStepping there).
void on_idle();
void set_idle_hook(void (*hook)());

const std::vector<Taken> &taken();

/// A G92 jump of the planner position. It applies after the first `after_taken` taken blocks: a sync
/// block, if the planner made one, is the block at index `after_taken`.
struct Jump {
    size_t after_taken;
    xyze_msteps_t before, after;
    xyze_msteps_t commanded_before; // LROUND(current_position * msteps/mm) before the G92
};
const std::vector<Jump> &jumps();

} // namespace planner_harness
