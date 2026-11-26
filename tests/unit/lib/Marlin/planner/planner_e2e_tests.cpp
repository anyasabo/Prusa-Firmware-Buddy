// End-to-end tests: the real planner and the real PreciseStepping, compared with Klipper.
#include "planner_e2e.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>

using namespace planner_e2e;
using namespace planner_harness;

namespace {

motion_diff::Config core_one_l(bool shaper_and_pa) {
    motion_diff::Config c;
    c.steps_per_mm = { AXIS_STEPS_PER_UNIT_15GT_XY, AXIS_STEPS_PER_UNIT_15GT_XY, DEFAULT_AXIS_STEPS_PER_UNIT_Z, DEFAULT_AXIS_STEPS_PER_UNIT_E0 };
    if (shaper_and_pa) {
        c.shaper[0] = input_shaper::AxisConfig { .type = input_shaper::Type::mzv, .frequency = 50.f, .damping_ratio = 0.1f, .vibration_reduction = 20.f };
        c.shaper[1] = input_shaper::AxisConfig { .type = input_shaper::Type::mzv, .frequency = 40.f, .damping_ratio = 0.1f, .vibration_reduction = 20.f };
        c.pressure_advance = 0.04f;
        c.smooth_time = 0.04f;
    }
    return c;
}

/// Each stepper agrees with Klipper, and its end position is the planner position (CoreXY: A = X + Y,
/// B = X - Y, in steps). `e_total_msteps` is the commanded E over the whole run.
void require_agreement(const Result &r, int64_t e_total_msteps) {
    CHECK(r.buddy.sessions == 1);
    for (int i = 0; i < motion_diff::AXES; ++i) {
        CAPTURE(i, motion_diff::describe(r.axis[i]));
        CHECK(motion_diff::check_axis(r.axis[i]).empty());
    }
    const double expected[] = {
        (double(r.planner_end.x) + double(r.planner_end.y)) / PLANNER_STEPS_MULTIPLIER,
        (double(r.planner_end.x) - double(r.planner_end.y)) / PLANNER_STEPS_MULTIPLIER,
        double(r.planner_end.z) / PLANNER_STEPS_MULTIPLIER,
        double(e_total_msteps) / PLANNER_STEPS_MULTIPLIER,
    };
    for (int i = 0; i < motion_diff::AXES; ++i) {
        CAPTURE(i, expected[i], r.buddy.final_count_position[i]);
        CHECK(std::abs(r.buddy.final_count_position[i] - expected[i]) <= 0.5);
    }
}

/// The commanded E over the run: the planner E at the end plus what each G92 took away.
int64_t e_total_msteps(const Result &r) {
    int64_t e = r.planner_end.e;
    for (const auto &j : jumps()) {
        e += j.before.e - j.after.e;
    }
    return e;
}

void perimeters(bool shaper_and_pa) {
    const bool g92_e0 = GENERATE(false, true);
    CAPTURE(shaper_and_pa, g92_e0);
    const Result r = run(core_one_l(shaper_and_pa), [&] {
        float e = 0;
        for (int layer = 0; layer < 3; ++layer) {
            const float z = 0.2f * float(layer + 1);
            move_to({ 20, 20, z, e }, 15);
            for (int side = 0; side < 4; ++side) {
                const float x = (side == 0 || side == 3) ? 60.f : 20.f;
                const float y = (side >= 2) ? 20.f : 60.f;
                move_to({ x, y, z, e += 1.33f }, 120, { .is_printing_move = true });
            }
            move_to({ 20, 20, z, e -= 0.7f }, 35); // retract
            move_to({ 20, 20, z + 0.4f, e }, 12); // Z hop
            move_to({ 80, 30, z + 0.4f, e }, 300); // travel
            move_to({ 80, 30, z, e }, 12);
            move_to({ 80, 30, z, e += 0.7f }, 35); // deretract
            move_to({ 20, 20, z, e }, 300); // travel back
            if (g92_e0) {
                set_e_position(e = 0);
            }
        }
        move_to({ 100, 100, 1, e }, 200); // the motion ends without E
    });
    require_agreement(r, e_total_msteps(r));
}

void arc(bool shaper_and_pa) {
    CAPTURE(shaper_and_pa);
    const Result r = run(core_one_l(shaper_and_pa), [] {
        float e = 0;
        const int n = 400;
        for (int i = 0; i <= n; ++i) {
            const float a = 2 * float(M_PI) * float(i) / float(n);
            e += 0.01f;
            move_to({ 100 + 20 * std::cos(a), 100 + 20 * std::sin(a), 0.2f, e }, 150, { .is_printing_move = true });
        }
        move_to({ 150, 150, 0.2f, e }, 200);
    });
    require_agreement(r, e_total_msteps(r));
}

} // namespace

TEST_CASE("planner end-to-end: perimeters, retractions and travels") {
    perimeters(false);
}

// With pressure advance, E ends 3.5 steps short on v8.0.2: the defects M1 and M2 of the
// motion_klipper_diff README (fixes on anya/pa-end-of-motion and anya/step-direction-at-emit).
// The fixes are in this tree.
TEST_CASE("planner end-to-end: perimeters with input shaper and pressure advance") {
    perimeters(true);
}

TEST_CASE("planner end-to-end: an arc of short segments") {
    arc(GENERATE(false, true));
}
