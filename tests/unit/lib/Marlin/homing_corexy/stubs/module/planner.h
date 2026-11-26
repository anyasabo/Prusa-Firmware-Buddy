#pragma once
// A fake planner. Each move runs at once in the fake world (fake_world.cpp). The positions are in
// mini-steps, as in the real planner. homing_corexy.cpp reads module/motion.h through planner.h.
#include "../inc/MarlinConfig.h"
#include <core/mtypes.hpp>

constexpr const int PLANNER_STEPS_MULTIPLIER = 4;

struct PhysicalToolIndex {
    static PhysicalToolIndex currently_selected() { return {}; }
};

struct PlannerHints {
    bool raw_block = false;
};

struct PlannerSettings {
    float travel_acceleration = XY_HOMING_ACCELERATION;
};

class Planner {
public:
    float mm_per_step[XYZE_N] = { 0.01f, 0.01f, 1.f / 800, 1.f / 550 };
    PlannerSettings settings;
    bool draining_flag = false;
    xyze_msteps_t position_msteps {};

    void _buffer_msteps(const xyze_msteps_t &target, const MachinePosXYZE &target_mm, feedRate_t fr_mm_s,
        PhysicalToolIndex tool, const PlannerHints &hints);
    void synchronize() {}
    bool draining() const { return draining_flag; }
    xyze_msteps_t get_position_msteps() const { return position_msteps; }
    float get_axis_position_mm(AxisEnum axis) const;
    MachinePosXYZE get_machine_position_mm() const;
    void set_machine_position_mm(const MachinePosXYZE &pos);
    /// Take the position from the stepper counters
    void reset_position();
};

extern Planner planner;

#include "motion.h"
