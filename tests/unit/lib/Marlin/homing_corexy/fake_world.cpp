#include "fake_world.hpp"

#include <config_store/store_instance.hpp>
#include <feature/input_shaper/input_shaper_config.hpp>
#include <feature/phase_stepping/phase_stepping.hpp>

#include <cstdlib>

Planner planner;
Stepper stepper;
Endstops endstops;
FakeTMC stepperX, stepperY, stepperZ;

namespace fake {

World world;

namespace {
    FakeConfigStore store;
    std::optional<input_shaper::AxisConfig> shaper[2];

    int32_t div_round(int64_t num, int64_t den) {
        return static_cast<int32_t>(std::llround(static_cast<double>(num) / static_cast<double>(den)));
    }

    float steps_per_mm() {
        return 1.f / planner.mm_per_step[X_AXIS];
    }
} // namespace

void World::move_rotors(int32_t a, int32_t b, bool with_endstops) {
    const int32_t da = a - rotor_a;
    const int32_t db = b - rotor_b;
    const int32_t n = std::max(std::abs(da), std::abs(db));
    Move m { rotor_a, rotor_b, a, b, with_endstops, false, machine_y(rotor_a, rotor_b) };

    const bool single_motor = (da == 0) != (db == 0);
    const bool start_at_wall = touches_wall(rotor_a, rotor_b);
    int32_t stop = n;
    for (int32_t i = 1; i <= n; ++i) {
        const int32_t pa = rotor_a + div_round(int64_t(da) * i, n);
        const int32_t pb = rotor_b + div_round(int64_t(db) * i, n);
        if (!touches_wall(pa, pb)) {
            continue;
        }
        if (with_endstops) {
            m.hit = true;
            stop = i;
            if (single_motor && !bump_noise.empty()) {
                stop = std::clamp(i + bump_noise.front(), 1, n);
                bump_noise.pop_front();
            }
        } else if (!start_at_wall) {
            ++wall_crashes;
        }
        break;
    }

    rotor_a = m.a0 + div_round(int64_t(da) * stop, std::max(n, 1));
    rotor_b = m.b0 + div_round(int64_t(db) * stop, std::max(n, 1));
    m.a1 = rotor_a;
    m.b1 = rotor_b;
    if (with_endstops) {
        endstops.hit_state = m.hit ? 1 : 0;
    }
    moves.push_back(m);
}

size_t World::single_motor_bumps() const {
    size_t count = 0;
    for (const Move &m : moves) {
        if (m.endstops && ((m.a0 == m.a1) != (m.b0 == m.b1))) {
            ++count;
        }
    }
    return count;
}

float machine_x(int32_t a, int32_t b) {
    return static_cast<float>(a + world.offset_a + b + world.offset_b) / 2.f * planner.mm_per_step[X_AXIS];
}

float machine_y(int32_t a, int32_t b) {
    return static_cast<float>((a + world.offset_a) - (b + world.offset_b)) / 2.f * planner.mm_per_step[Y_AXIS];
}

void reset(float x_mm, float y_mm, int phase_a, int phase_b) {
    world = World {};
    const float spm = steps_per_mm();
    world.x2_wall = static_cast<int32_t>(std::lround(2 * X_MAX_POS * spm));
    world.y2_wall = static_cast<int32_t>(std::lround(2 * Y_MIN_POS * spm));
    world.rotor_a = static_cast<int32_t>(std::lround((x_mm + y_mm) * spm));
    world.rotor_b = static_cast<int32_t>(std::lround((x_mm - y_mm) * spm));
    world.phase_a = phase_a;
    world.phase_b = phase_b;

    planner.draining_flag = false;
    planner.position_msteps = {};
    planner.reset_position();
    endstops = Endstops {};
    stepperX = stepperY = stepperZ = FakeTMC {};
    store = FakeConfigStore {};
    shaper[0] = shaper[1] = input_shaper::AxisConfig { 50.f };
}

} // namespace fake

using fake::world;

// Planner

void Planner::_buffer_msteps(const xyze_msteps_t &target, const MachinePosXYZE &, feedRate_t, PhysicalToolIndex, const PlannerHints &) {
    // Mini-steps to stepper counters (CoreXY: a = x + y, b = x - y), then to rotor positions
    const int32_t ca = fake::div_round(int64_t(target.x) + target.y, PLANNER_STEPS_MULTIPLIER);
    const int32_t cb = fake::div_round(int64_t(target.x) - target.y, PLANNER_STEPS_MULTIPLIER);
    world.move_rotors(ca - world.offset_a, cb - world.offset_b, endstops.is_enabled());
    position_msteps.x = target.x;
    position_msteps.y = target.y;
}

float Planner::get_axis_position_mm(AxisEnum axis) const {
    return static_cast<float>(position_msteps[axis]) / PLANNER_STEPS_MULTIPLIER * mm_per_step[axis];
}

MachinePosXYZE Planner::get_machine_position_mm() const {
    MachinePosXYZE pos;
    LOOP_XYZE(i) {
        pos[i] = get_axis_position_mm(AxisEnum(i));
    }
    return pos;
}

void Planner::set_machine_position_mm(const MachinePosXYZE &pos) {
    LOOP_XYZE(i) {
        position_msteps[i] = static_cast<int32_t>(std::lround(pos[i] / mm_per_step[i] * PLANNER_STEPS_MULTIPLIER));
    }
    const int32_t ca = fake::div_round(int64_t(position_msteps.x) + position_msteps.y, PLANNER_STEPS_MULTIPLIER);
    const int32_t cb = fake::div_round(int64_t(position_msteps.x) - position_msteps.y, PLANNER_STEPS_MULTIPLIER);
    world.offset_a = ca - world.rotor_a;
    world.offset_b = cb - world.rotor_b;
}

void Planner::reset_position() {
    const int32_t ca = stepper.position(A_AXIS);
    const int32_t cb = stepper.position(B_AXIS);
    position_msteps.x = (ca + cb) * PLANNER_STEPS_MULTIPLIER / 2;
    position_msteps.y = (ca - cb) * PLANNER_STEPS_MULTIPLIER / 2;
}

// Steppers

int32_t Stepper::position(AxisEnum axis) const {
    switch (axis) {
    case A_AXIS:
        return world.rotor_a + world.offset_a;
    case B_AXIS:
        return world.rotor_b + world.offset_b;
    default:
        return 0;
    }
}

FakeTMC &stepper_axis(AxisEnum axis) {
    switch (axis) {
    case X_AXIS:
        return stepperX;
    case Y_AXIS:
        return stepperY;
    default:
        return stepperZ;
    }
}

void stepper_wait_for_standstill(uint8_t) {}

int phase_stepping::logical_ustep(AxisEnum axis) {
    // 16 microsteps: one full step is 1024 / 4 / 16 = 16 MSCNT units
    constexpr int per_step = 256 / X_MICROSTEPS;
    const int64_t raw = axis == A_AXIS ? world.phase_a + int64_t(world.rotor_a) * per_step
                                       : world.phase_b + int64_t(world.rotor_b) * per_step;
    return static_cast<int>(((raw % 1024) + 1024) % 1024);
}

// motion.h

MachinePosXYZE current_machine_position() {
    return planner.get_machine_position_mm();
}

void line_to_machine_pos(const MachinePosXYZE &target, feedRate_t fr_mm_s) {
    xyze_msteps_t msteps = planner.position_msteps;
    msteps.x = static_cast<int32_t>(std::lround(target.x / planner.mm_per_step[X_AXIS] * PLANNER_STEPS_MULTIPLIER));
    msteps.y = static_cast<int32_t>(std::lround(target.y / planner.mm_per_step[Y_AXIS] * PLANNER_STEPS_MULTIPLIER));
    planner._buffer_msteps(msteps, target, fr_mm_s, PhysicalToolIndex::currently_selected(), {});
}

bool homeaxis(const AxisEnum axis, const feedRate_t fr_mm_s, bool, void (*)(AxisEnum), bool, bool, bool) {
    MachinePosXYZE pos = planner.get_machine_position_mm();
    world.homes.push_back({ axis, pos.x, pos.y });

    // stall at the wall, plus the error of the stall detection
    const int32_t noise = world.home_noise.empty() ? 0 : world.home_noise.front();
    if (!world.home_noise.empty()) {
        world.home_noise.pop_front();
    }
    int32_t a = world.rotor_a, b = world.rotor_b;
    if (axis == X_AXIS) {
        const int32_t k = std::max(0, (world.x2_wall - world.x2() + 1) / 2) + noise;
        a += k;
        b += k;
    } else {
        const int32_t k = std::max(0, (world.y2() - world.y2_wall + 1) / 2) + noise;
        a -= k;
        b += k;
    }
    world.moves.push_back({ world.rotor_a, world.rotor_b, a, b, true, true, pos.y });
    world.rotor_a = a;
    world.rotor_b = b;

    // set the home position, then back off (HOMING_BACKOFF_POST_MM)
    constexpr xyz_float_t backoff = HOMING_BACKOFF_POST_MM;
    pos = planner.get_machine_position_mm();
    pos[axis] = base_home_pos(axis);
    planner.set_machine_position_mm(pos);
    const bool endstops_on = endstops.is_enabled();
    endstops.enable(false);
    pos[axis] -= backoff[axis] * (axis == X_AXIS ? X_HOME_DIR : Y_HOME_DIR);
    line_to_machine_pos(pos, fr_mm_s);
    endstops.enable(endstops_on);
    return true;
}

void do_blocking_move_to_y(const float &ry, const feedRate_t &fr_mm_s) {
    world.y_moves.push_back(ry);
    MachinePosXYZE pos = planner.get_machine_position_mm();
    pos.y = ry;
    line_to_machine_pos(pos, fr_mm_s);
}

xyze_pos_t to_native_pos(const MachinePosXYZE &pos) {
    return { pos.x, pos.y, pos.z, pos.e };
}

void set_current_position(const xyze_pos_t &) {}

// Others

FakeConfigStore &config_store() {
    return fake::store;
}

std::optional<input_shaper::AxisConfig> input_shaper::get_axis_config(AxisEnum axis) {
    return fake::shaper[axis];
}

void input_shaper::set_axis_config(AxisEnum axis, std::optional<AxisConfig> config) {
    fake::shaper[axis] = config;
}

bool has_inverted_x() { return DEFAULT_INVERT_X_DIR; }
bool has_inverted_y() { return DEFAULT_INVERT_Y_DIR; }
bool has_inverted_z() { return DEFAULT_INVERT_Z_DIR; }
bool has_inverted_e() { return DEFAULT_INVERT_E0_DIR; }
float get_z_max_pos_mm() { return 0; }

void idle(bool, bool) {}
uint32_t ticks_us() { return 0; }
