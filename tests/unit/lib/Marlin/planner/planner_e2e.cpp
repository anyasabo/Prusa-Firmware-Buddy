// End-to-end: commands -> the real planner -> the real PreciseStepping -> step events, and the same
// move segments -> Klipper (the comparator of motion_klipper_diff). The motion_klipper_diff driver
// (buddy_driver.cpp) makes the blocks itself. This driver gets them from the real planner, with
// its junction speeds and its sync blocks.
#include "planner_e2e.hpp"

#include <config_store/store_instance.hpp>
#include <feature/input_shaper/input_shaper.hpp>
#include <feature/precise_stepping/precise_stepping.hpp>
#include <feature/pressure_advance/pressure_advance.hpp>
#include <feature/pressure_advance/pressure_advance_config.hpp>
#include <module/stepper.h>
#include <timing.h>

#include <cmath>
#include <stdexcept>

namespace motion_diff {

// buddy_driver.cpp defines these for the motion_klipper_diff tests. klipper_runner.cpp needs them.
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

std::array<double, 3> start_position_mm(const Config &) {
    return { 0, 0, 0 }; // planner_harness::reset() starts at 0
}

} // namespace motion_diff

namespace planner_e2e {

using namespace motion_diff;

namespace {

    struct State {
        Result *result = nullptr;
        uint8_t seen_head = 0;
        int64_t now_us = 0;
        double e_offset_mm = 0; // Klipper E = PreciseStepping E + offset (G92 E0 resets the PreciseStepping E)
        bool have_last = false;
        Segment last {}; // the last segment, in PreciseStepping coordinates
    };
    State st;

    double segment_end(const Segment &s, int axis) {
        const double dist = s.start_v * s.move_time + s.half_accel * s.move_time * s.move_time;
        return s.start_pos[axis] + s.axes_r[axis] * dist;
    }

    /// One move ISR (1 ms of time) and then the step ISR until the step event queue is empty.
    void pump() {
        stub_now_us += 1000;
        PreciseStepping::move_isr();
        BuddyResult &out = st.result->buddy;

        while (st.seen_head != PreciseStepping::move_segment_queue.head) {
            const move_t &move = PreciseStepping::move_segment_queue.data[st.seen_head];
            if (move.flags & MOVE_FLAG_BEGINNING_EMPTY_MOVE) {
                ++out.sessions;
            }
            Segment s {
                .print_time = move.print_time.to_seconds(),
                .move_time = move.move_time.to_seconds(),
                .start_v = move.start_v,
                .half_accel = move.half_accel,
                .start_pos = { move.start_pos[0], move.start_pos[1], move.start_pos[2], move.start_pos[3] },
                .axes_r = { move.axes_r[0], move.axes_r[1], move.axes_r[2], move.axes_r[3] },
                .flags = move.flags,
            };
            if ((move.flags & MOVE_FLAG_RESET_POSITION_E) && st.have_last) {
                // The sync block of G92 E0 moved the PreciseStepping E back by whole steps
                // (append_move_segments_to_queue()). Klipper continues: add the jump to the offset,
                // rounded to mini-steps.
                const double mm_per_mstep = double(Planner::mm_per_mstep[E_AXIS]);
                st.e_offset_mm += std::round((segment_end(st.last, 3) - s.start_pos[3]) / mm_per_mstep) * mm_per_mstep;
            }
            st.last = s;
            st.have_last = true;
            s.start_pos[3] += st.e_offset_mm;
            out.segments.push_back(s);
            st.seen_head = PreciseStepping::move_segment_queue_next_index(st.seen_head);
        }

        while (const step_event_u16_t *ev = PreciseStepping::get_current_step_event()) {
            st.now_us += ev->time_ticks;
            for (int axis = 0; axis < AXES; ++axis) {
                if (ev->flags & (STEP_EVENT_FLAG_STEP_X << axis)) {
                    const bool negative = ev->flags & (STEP_EVENT_FLAG_X_DIR << axis);
                    out.steps.axis[axis].push_back({ double(st.now_us), !negative });
                }
            }
            PreciseStepping::process_one_step_event_from_queue();
        }
    }

} // namespace

Result run(const Config &config, const std::function<void()> &program) {
    Result result;
    result.config = config;
    st = State { .result = &result };

    planner_harness::Settings settings;
    for (int i = 0; i < AXES; ++i) {
        settings.steps_per_mm[i] = config.steps_per_mm[i];
    }
    planner_harness::reset(settings);

    PreciseStepping::move_segment_queue_clear();
    PreciseStepping::step_event_queue_clear();
    Stepper::count_position = {};
    Stepper::count_position_last_block = {};
    Stepper::count_position_from_startup = {};
    Stepper::last_direction_bits = 0;
    Stepper::axis_did_move = 0;
    auto &store = config_store().input_shaper_config;
    store = {};
    for (int i = 0; i < 3; ++i) {
        store.axis[i] = config.shaper[i];
    }
    input_shaper::init();
    pressure_advance::set_axis_e_config({ .pressure_advance = config.pressure_advance, .smooth_time = config.smooth_time });
    // See buddy_driver.cpp: a debug_assert() in input_shaper.cpp reads these before the init sets them.
    for (auto &is_state : InputShaper::is_state) {
        is_state.m_logical_axis_pulses = { &InputShaper::logical_axis_pulses[X_AXIS], &InputShaper::logical_axis_pulses[Y_AXIS] };
    }
    PreciseStepping::init_stepper();
    PreciseStepping::init();
    st.seen_head = PreciseStepping::move_segment_queue.head;

    planner_harness::set_idle_hook(pump);
    program();
    // The end of the program (M400): deliver at once, then run until the motion halts.
    planner.start_moving();
    for (int i = 0; i < 10'000'000 && planner.processing(); ++i) {
        pump();
    }
    planner_harness::set_idle_hook(nullptr);
    if (planner.processing()) {
        throw std::runtime_error("the motion did not halt");
    }

    for (int i = 0; i < AXES; ++i) {
        result.buddy.final_count_position[i] = Stepper::count_position_from_startup[i];
    }
    result.buddy.finished = true;
    result.planner_end = planner.position;

    result.klipper = run_klipper(config, result.buddy.segments, start_position_mm(config));
    for (int i = 0; i < AXES; ++i) {
        result.axis[i] = compare_axis(config, result.buddy, result.klipper, i);
    }
    return result;
}

} // namespace planner_e2e
