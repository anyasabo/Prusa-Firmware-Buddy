#include "planner_harness.hpp"

#include <module/motion.h>
#include <module/stepper.h>

#include <cmath>

namespace planner_harness {

namespace {
    std::vector<Taken> taken_blocks;
    std::vector<Jump> position_jumps;
    void (*idle_hook)() = nullptr;
    struct FloatMove {
        MachinePosXYZE start, end;
    };
    FloatMove float_moves[BLOCK_BUFFER_SIZE]; // by block buffer index

    template <typename F>
    void jump(F &&set) {
        // A position set first queues the last move that the planner dropped as too short. The
        // jump comes after that block, and the checks need its float positions.
        const uint8_t head = planner.block_buffer_head;
        const MachinePosXYZE start = planner.position_float;
        planner.flush_dropped_move();
        if (planner.block_buffer_head != head) {
            float_moves[head] = { start, planner.position_float };
        }
        Jump j { taken_blocks.size() + planner.nonbusy_movesplanned(), planner.position, {}, {} };
        for (int i = 0; i < XYZE_N; ++i) {
            j.commanded_before[i] = int32_t(std::lround(current_position[i] * planner.settings.axis_msteps_per_mm[i]));
        }
        set();
        j.after = planner.position;
        position_jumps.push_back(j);
    }
} // namespace

void reset(const Settings &s) {
    planner.init();
    taken_blocks.clear();
    idle_hook = nullptr; // a test that threw can leave its hook
    position_jumps.clear();

    user_planner_settings_t u = planner.user_settings;
    for (int i = 0; i < XYZE_N; ++i) {
        u.max_acceleration_mm_per_s2[i] = s.max_acceleration[i];
        u.axis_steps_per_mm[i] = s.steps_per_mm[i];
        u.axis_msteps_per_mm[i] = s.steps_per_mm[i] * PLANNER_STEPS_MULTIPLIER;
        u.max_feedrate_mm_s[i] = s.max_feedrate[i];
    }
    u.min_segment_time_us = DEFAULT_MINSEGMENTTIME;
    u.acceleration = s.acceleration;
    u.retract_acceleration = s.retract_acceleration;
    u.travel_acceleration = s.travel_acceleration;
    u.max_filament_acceleration_mm_per_s2 = s.max_filament_acceleration;
    u.min_feedrate_mm_s = feedRate_t(DEFAULT_MINIMUMFEEDRATE);
    u.min_travel_feedrate_mm_s = feedRate_t(DEFAULT_MINTRAVELFEEDRATE);
    planner.apply_settings(u);
    planner.junction_deviation_mm = s.junction_deviation;

    // As MarlinSettings::postprocess() and M221.
    planner.flow_percentage[VirtualToolIndex {}] = s.flow_percentage;
    planner.calculate_volumetric_multipliers();

    current_position = {};
    stepper.count_position = {};
    planner.refresh_positioning();
}

bool move_to(const xyze_pos_t &target, float fr_mm_s, MoveHints hints, bool raw_block) {
    const uint8_t head = planner.block_buffer_head;
    const MachinePosXYZE start = planner.position_float;
    const bool queued = planner.buffer_line(to_machine_pos(target), fr_mm_s, PhysicalToolIndex {}, PlannerHints { .raw_block = raw_block, .move = hints });
    current_position = target;
    if (planner.block_buffer_head != head) {
        float_moves[head] = { start, planner.position_float };
    }
    return queued;
}

// The jump reads the commanded position before the G92, so the G92 changes it inside the jump.
void set_e_position(float e) {
    jump([&] {
        current_position.e = e;
        planner.set_e_position_mm(e);
    });
}

void set_position(const xyze_pos_t &pos) {
    jump([&] {
        current_position = pos;
        planner.set_position_mm(pos);
    });
}

size_t take(size_t n) {
    size_t count = 0;
    while (count < n) {
        block_t *block = planner.get_current_unprocessed_block();
        if (!block) {
            break;
        }
        const FloatMove &fm = float_moves[planner.block_buffer_nonbusy];
        BlockCopy c {
            .sync = block->is_sync(),
            .direction_bits = block->direction_bits,
            .nominal_speed = block->nominal_speed,
            .entry_speed_sqr = block->entry_speed_sqr,
            .max_entry_speed_sqr = block->max_entry_speed_sqr,
            .millimeters = block->millimeters,
            .acceleration = block->acceleration,
            .initial_speed = block->initial_speed,
            .final_speed = block->final_speed,
            .msteps = block->is_sync() ? xyze_msteps_t {} : block->msteps,
            .sync_step_position = block->is_sync() ? block->sync_step_position : xyze_steps_t {},
            .start_mm = fm.start,
            .end_mm = fm.end,
        };
        taken_blocks.push_back({ c, planner.position });
        planner.discard_current_unprocessed_block();
        planner.discard_current_block();
        ++count;
    }
    return count;
}

void take_all() {
    take(SIZE_MAX);
}

void on_idle() {
    if (idle_hook) {
        idle_hook();
    } else {
        take(1);
    }
}

void set_idle_hook(void (*hook)()) {
    idle_hook = hook;
}

const std::vector<Taken> &taken() {
    return taken_blocks;
}

const std::vector<Jump> &jumps() {
    return position_jumps;
}

} // namespace planner_harness
