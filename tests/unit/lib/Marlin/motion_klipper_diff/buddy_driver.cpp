// Runs the real PreciseStepping pipeline (precise_stepping.cpp, input_shaper.cpp,
// pressure_advance.cpp) on planner blocks and reads the step event queue, as the step ISR does.
#include "motion_model.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

#include <config_store/store_instance.hpp>
#include <feature/input_shaper/input_shaper.hpp>
#include <feature/precise_stepping/precise_stepping.hpp>
#include <feature/pressure_advance/pressure_advance.hpp>
#include <feature/pressure_advance/pressure_advance_config.hpp>
#include <module/planner.h>
#include <module/stepper.h>

namespace motion_diff {

Pulses buddy_shaper_pulses(const input_shaper::AxisConfig &cfg) {
    const input_shaper::Shaper s = input_shaper::get(cfg.damping_ratio, cfg.frequency, cfg.vibration_reduction, cfg.type);
    Pulses p;
    p.n = s.num_pulses;
    for (int i = 0; i < s.num_pulses; ++i) {
        p.a[i] = s.a[i];
        p.t[i] = s.t[i];
    }
    return p;
}

std::array<double, 3> start_position_mm(const Config &config) {
    // As PreciseStepping::reset_from_halt(false) for CoreXY.
    const auto &s = config.start_steps;
    const int32_t msteps_x = (s[0] + s[1]) * PLANNER_STEPS_MULTIPLIER / 2;
    const int32_t msteps_y = (s[0] - s[1]) * PLANNER_STEPS_MULTIPLIER / 2;
    const int32_t msteps_z = s[2] * PLANNER_STEPS_MULTIPLIER;
    const float mm_per_mstep[3] = {
        1.f / config.steps_per_mm[0] / PLANNER_STEPS_MULTIPLIER,
        1.f / config.steps_per_mm[1] / PLANNER_STEPS_MULTIPLIER,
        1.f / config.steps_per_mm[2] / PLANNER_STEPS_MULTIPLIER,
    };
    return { msteps_x * double(mm_per_mstep[0]), msteps_y * double(mm_per_mstep[1]), msteps_z * double(mm_per_mstep[2]) };
}

namespace {

    void reset_pipeline(const Config &config) {
        PreciseStepping::move_segment_queue_clear();
        PreciseStepping::step_event_queue_clear();
        Planner::clear_block_buffer();

        Planner::set_steps_per_mm({ config.steps_per_mm[0], config.steps_per_mm[1], config.steps_per_mm[2], config.steps_per_mm[3] });

        Stepper::count_position = {};
        Stepper::count_position_last_block = {};
        Stepper::count_position_from_startup = { config.start_steps[0], config.start_steps[1], config.start_steps[2], 0 };
        Stepper::last_direction_bits = 0;
        Stepper::axis_did_move = 0;

        // Input shaper: the real configuration path (input_shaper::init() reads the config store).
        auto &store = config_store().input_shaper_config;
        store = {};
        for (int i = 0; i < 3; ++i) {
            store.axis[i] = config.shaper[i];
        }
        input_shaper::init();

        pressure_advance::set_axis_e_config({ .pressure_advance = config.pressure_advance, .smooth_time = config.smooth_time });

        // input_shaper_step_generator_init() has a debug_assert() that reads m_logical_axis_pulses[0]
        // and [1] before it sets them (input_shaper.cpp:276). At the first init they are null. Release
        // builds do not have the assert, and on the MCU address 0 is readable, so only a host build
        // with asserts crashes. Set them as the init does.
        for (auto &is_state : InputShaper::is_state) {
            is_state.m_logical_axis_pulses = { &InputShaper::logical_axis_pulses[X_AXIS], &InputShaper::logical_axis_pulses[Y_AXIS] };
        }

        PreciseStepping::init_stepper();
        PreciseStepping::init();
    }

    block_t make_block(const Block &b, std::array<int64_t, AXES> &position_msteps, std::array<double, AXES> &position_mm) {
        block_t block;
        const double d[AXES] = { b.dx, b.dy, b.dz, b.de };
        std::array<int32_t, AXES> delta {};
        for (int i = 0; i < AXES; ++i) {
            position_mm[i] += d[i];
            const double msteps_per_mm = double(Planner::settings.axis_steps_per_mm[i]) * PLANNER_STEPS_MULTIPLIER;
            const int64_t target = std::llround(position_mm[i] * msteps_per_mm);
            delta[i] = int32_t(target - position_msteps[i]);
            position_msteps[i] = target;
        }

        // As Planner::_populate_block()
        block.direction_bits = (delta[0] < 0 ? 1 : 0) | (delta[1] < 0 ? 2 : 0) | (delta[2] < 0 ? 4 : 0) | (delta[3] < 0 ? 8 : 0);
        block.msteps = xyze_msteps_t { .a = std::abs(delta[0]), .b = std::abs(delta[1]), .c = std::abs(delta[2]), ._e = std::abs(delta[3]) };
        float delta_mm[AXES];
        for (int i = 0; i < AXES; ++i) {
            delta_mm[i] = float(delta[i]) * Planner::mm_per_mstep[i];
        }
        if (block.msteps.a || block.msteps.b || block.msteps.c) {
            block.millimeters = std::sqrt(delta_mm[0] * delta_mm[0] + delta_mm[1] * delta_mm[1] + delta_mm[2] * delta_mm[2]);
        } else {
            block.millimeters = std::abs(delta_mm[3]);
        }
        block.acceleration = b.accel;
        block.initial_speed = b.v_start;
        block.final_speed = b.v_end;
        block.nominal_speed = b.v_cruise;
        return block;
    }

    Segment to_segment(const move_t &m) {
        Segment s;
        s.print_time = m.print_time.to_seconds();
        s.move_time = m.move_time.to_seconds();
        s.start_v = m.start_v;
        s.half_accel = m.half_accel;
        for (int i = 0; i < AXES; ++i) {
            s.start_pos[i] = m.start_pos[i];
            s.axes_r[i] = m.axes_r[i];
        }
        s.flags = m.flags;
        return s;
    }

} // namespace

BuddyResult run_buddy(const Config &config, const std::vector<Block> &blocks) {
    reset_pipeline(config);

    BuddyResult result;
    for (int i = 0; i < AXES; ++i) {
        result.start_count_position[i] = Stepper::count_position_from_startup[i];
    }

    std::array<int64_t, AXES> position_msteps {};
    std::array<double, AXES> position_mm {};
    {
        const auto start = start_position_mm(config);
        for (int i = 0; i < 3; ++i) {
            position_msteps[i] = std::llround(start[i] * double(Planner::settings.axis_steps_per_mm[i]) * PLANNER_STEPS_MULTIPLIER);
            position_mm[i] = start[i];
        }
    }
    // Blocks in groups: a group starts at a block with halt_before. The next group goes to the planner
    // only after the motion has come back to a halt.
    std::vector<std::vector<block_t>> groups(1);
    for (const Block &b : blocks) {
        if (b.halt_before && !groups.back().empty()) {
            groups.emplace_back();
        }
        block_t block = make_block(b, position_msteps, position_mm);
        if (block.msteps.a || block.msteps.b || block.msteps.c || block.msteps.e) {
            groups.back().push_back(block);
        }
    }
    size_t next_group = 0;
    const auto push_next_group = [&] {
        for (const block_t &block : groups[next_group]) {
            Planner::unprocessed.push_back(block);
        }
        ++next_group;
    };
    push_next_group();

    uint8_t seen_head = PreciseStepping::move_segment_queue.head;
    int64_t now_us = 0; // time of the last consumed step event
    bool started = false;
    // After a halt, PreciseStepping starts again at time 0 and E 0 (reset_from_halt()). The segments
    // for Klipper continue the time and the E position (at the E stepper position of the halt).
    double time_offset_s = 0., e_offset_mm = 0.;
    int64_t e_net_steps = 0;
    bool first_session = true;

    // Each iteration is one move ISR and then the step ISR until the step event queue is empty.
    const bool trace = std::getenv("MOTION_DIFF_TRACE") != nullptr;
    for (int iteration = 0; iteration < 10'000'000; ++iteration) {
        PreciseStepping::move_isr();
        if (trace) {
            const auto &pa = PressureAdvance::pressure_advance_state;
            std::printf("trace it %d pa total %u local %u last %u prev %.6f next %.6f start_pos %.6f same %u move flags %x pt %.6f steps %zu ending %d\n", iteration,
                pa.total_sample_idx, pa.local_sample_idx, pa.current_move_last_total_sample_idx, pa.prev_position, pa.next_position, pa.start_pos,
                pa.buffer.same_samples_cnt, pa.current_move ? pa.current_move->flags : 0, pa.current_move ? pa.current_move->print_time.to_seconds() : -1.,
                result.steps.axis[3].size(), int(PreciseStepping::total_print_time.to_seconds() > 1e6));
        }

        // Copy new move segments before the step ISR can discard and reuse them.
        while (seen_head != PreciseStepping::move_segment_queue.head) {
            const move_t &move = PreciseStepping::move_segment_queue.data[seen_head];
            if (move.flags & MOVE_FLAG_BEGINNING_EMPTY_MOVE) {
                if (!first_session) {
                    // All events of the session before are consumed (reset_from_halt() needs empty queues).
                    time_offset_s = double(now_us) * 1e-6;
                    e_offset_mm = double(e_net_steps) * double(Planner::mm_per_step[E_AXIS]);
                }
                first_session = false;
                ++result.sessions;
            }
            Segment segment = to_segment(move);
            segment.print_time += time_offset_s;
            segment.start_pos[3] += e_offset_mm;
            result.segments.push_back(segment);
            seen_head = PreciseStepping::move_segment_queue_next_index(seen_head);
            started = true;
        }

        while (const step_event_u16_t *ev = PreciseStepping::get_current_step_event()) {
            now_us += ev->time_ticks;
            const StepEventFlag_t flags = ev->flags;
            for (int axis = 0; axis < AXES; ++axis) {
                if (flags & (STEP_EVENT_FLAG_STEP_X << axis)) {
                    const bool negative = flags & (STEP_EVENT_FLAG_X_DIR << axis);
                    if (axis == E_AXIS) {
                        e_net_steps += negative ? -1 : 1;
                    }
                    if (double(now_us) >= config.record_from_us) {
                        result.steps.axis[axis].push_back({ double(now_us), !negative });
                    } else {
                        result.steps.skipped_net[axis] += negative ? -1 : 1;
                    }
                }
            }
            PreciseStepping::process_one_step_event_from_queue();
        }

        const bool halted = Planner::unprocessed.empty() && Planner::processed.empty() && !PreciseStepping::has_blocks_queued()
            && PreciseStepping::total_print_time == TimeTicks::zero();
        if (halted && next_group < groups.size()) {
            push_next_group();
            continue;
        }
        if (started && halted) {
            result.finished = true;
            break;
        }
        if (!started && Planner::unprocessed.empty()) {
            result.finished = true; // nothing to do
            break;
        }
    }

    for (int i = 0; i < AXES; ++i) {
        result.final_count_position[i] = Stepper::count_position_from_startup[i];
    }
    return result;
}

} // namespace motion_diff
