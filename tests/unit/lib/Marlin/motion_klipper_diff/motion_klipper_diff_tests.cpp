// Differential tests: the Buddy motion core against Klipper's step generation. See README.md.
#include "compare.hpp"
#include "motion_model.hpp"
#include "scenario.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>

using namespace motion_diff;
using input_shaper::Type;

namespace {

input_shaper::AxisConfig shaper(Type type, float frequency, float damping = 0.1f) {
    return { .type = type, .frequency = frequency, .damping_ratio = damping, .vibration_reduction = 20.f };
}

constexpr char axis_names[AXES] = { 'A', 'B', 'Z', 'E' };

void print(const char *name, const Run &r) {
    for (int i = 0; i < AXES; ++i) {
        if (r.axis[i].buddy_steps || r.axis[i].klipper_steps) {
            std::printf("%s %c: %s (klipper after rollback filter %zu)\n", name, axis_names[i], describe(r.axis[i]).c_str(), r.klipper.filtered.axis[i].size());
        }
    }
}

/// Both sides reach the same position, make the same steps and agree on the step times (see
/// check_axis() for the limits).
void check_agreement(const Run &r) {
    REQUIRE(r.buddy.finished);
    for (int i = 0; i < AXES; ++i) {
        INFO("axis " << axis_names[i] << ": " << describe(r.axis[i]));
        CHECK(check_axis(r.axis[i]) == "");
    }
}

/// A print-like path: a square perimeter of short extruding segments with corner speed limits,
/// then a retraction, a travel and a deretraction.
std::vector<Block> perimeter(double size, int segments_per_side, float v, float a, float e_ratio) {
    std::vector<Block> blocks;
    const double seg = size / segments_per_side;
    const float corner_v = 8.f;
    const double dirs[4][2] = { { 1, 0 }, { 0, 1 }, { -1, 0 }, { 0, -1 } };
    float v_prev = 0.f;
    for (int side = 0; side < 4; ++side) {
        for (int k = 0; k < segments_per_side; ++k) {
            const bool last_of_side = k + 1 == segments_per_side;
            const float v_end_limit = last_of_side ? corner_v : v;
            const float reach = std::sqrt(2.f * a * float(seg));
            const float v_start = std::min(v_prev, v);
            const float v_end = std::min(v_end_limit, std::sqrt(v_start * v_start + reach * reach));
            blocks.push_back({ .dx = dirs[side][0] * seg, .dy = dirs[side][1] * seg, .de = seg * e_ratio, .v_start = v_start, .v_cruise = v, .v_end = v_end, .accel = a });
            v_prev = v_end;
        }
    }
    // Slow down to rest within the last segment, as the planner does before a retraction.
    blocks.back().v_end = 0.f;
    blocks.back().v_start = std::min(blocks.back().v_start, std::sqrt(2.f * a * float(seg)));
    blocks.push_back({ .de = -0.8, .v_start = 0, .v_cruise = 35, .v_end = 0, .accel = 1250 }); // retraction
    blocks.push_back(line(20, 15, 0, 300, 5000)); // travel
    blocks.push_back({ .de = 0.8, .v_start = 0, .v_cruise = 35, .v_end = 0, .accel = 1250 }); // deretraction
    return blocks;
}

std::vector<Block> zigzag(int count, double length, float v, float a) {
    std::vector<Block> blocks;
    for (int i = 0; i < count; ++i) {
        blocks.push_back(line(i % 2 ? -length : length, 0.3, 0, v, a));
    }
    return blocks;
}

} // namespace

TEST_CASE("motion diff: classic step generator", "[motion_diff]") {
    Config config;
    const Run r = run_both(config, { line(30, 10, 0, 200, 4000), line(-5, 25, 0, 150, 2500), { .dz = 0.6, .v_cruise = 12, .accel = 400 } });
    print("classic", r);
    check_agreement(r);
}

TEST_CASE("motion diff: input shaper, each type", "[motion_diff]") {
    const Type type = GENERATE(Type::zv, Type::zvd, Type::mzv, Type::ei, Type::ei_2hump, Type::ei_3hump);
    Config config;
    config.shaper[0] = shaper(type, 50.f);
    config.shaper[1] = shaper(type, 40.f);
    const Run r = run_both(config, zigzag(6, 12, 200, 7000));
    print(input_shaper::to_string(type), r);
    check_agreement(r);
}

TEST_CASE("motion diff: input shaper, X only and Z", "[motion_diff]") {
    Config config;
    config.shaper[0] = shaper(Type::mzv, 47.f);
    config.shaper[2] = shaper(Type::zv, 30.f);
    const Run r = run_both(config, { line(20, 20, 0, 250, 6000), { .dz = 1.2, .v_cruise = 12, .accel = 400 }, line(-20, 5, 0, 250, 6000) });
    print("X+Z", r);
    check_agreement(r);
}

TEST_CASE("motion diff: halts in the middle", "[motion_diff]") {
    Config config;
    config.shaper[0] = shaper(Type::mzv, 52.4f);
    config.shaper[1] = shaper(Type::ei, 43.f);
    std::vector<Block> blocks = { line(20, 5, 0.9, 200, 6000), line(-10, 10, 0.6, 150, 6000) };
    blocks.back().halt_before = true; // the planner queue runs dry: reset_from_halt() between the lines
    blocks.push_back(line(5, -20, 0.8, 250, 6000));
    blocks.back().halt_before = true;
    const Run r = run_both(config, blocks);
    print("halts", r);
    check_agreement(r);
}

// On v8.0.2, these cases fail because of these defects (README.md, "Defects found"):
// - the pressure advance steps of the last smooth_time / 2 before a halt are lost,
// - the last pressure advance step before a direction change has the new direction,
// - a short extruder move just before a halt makes no steps,
// - CoreXY: the Z input shaper is not in the lookback time when X or Y is shaped.
// The fixes are in this tree, and each fix has a regression_<topic>.cpp test.
TEST_CASE("motion diff: pressure advance, halt in the middle of a run", "[motion_diff][!mayfail]") {
    // Defect M1: the PA steps after the last segment of a motion are lost at the halt. The next motion
    // continues at the commanded E, so the loss shows in the second motion.
    Config config;
    config.pressure_advance = 0.05f;
    config.smooth_time = 0.04f;
    std::vector<Block> blocks = { { .de = 0.8, .v_start = 0, .v_cruise = 35, .v_end = 0, .accel = 1250 }, line(10, 5, 0.4, 120, 4000) };
    blocks.back().halt_before = true;
    const Run r = run_both(config, blocks);
    print("PA halt", r);
    check_agreement(r);
}

TEST_CASE("motion diff: pressure advance, direction changes inside a run", "[motion_diff][!mayfail]") {
    // Defect M2: the last PA step before a direction change goes in the new direction.
    Config config;
    config.pressure_advance = 0.05f;
    config.smooth_time = 0.04f;
    const Run r = run_both(config, { line(10, 0, 0.45, 120, 4000), line(10, 10, 0, 200, 4000), line(-10, 0, 0.45, 120, 4000), line(5, -15, 0, 200, 4000) });
    print("PA reversal", r);
    check_agreement(r);
}

TEST_CASE("motion diff: pressure advance", "[motion_diff]") {
    Config config;
    config.pressure_advance = GENERATE(0.02f, 0.05f);
    config.smooth_time = 0.04f;
    const Run r = run_both(config, perimeter(20, 4, 120, 4000, 0.045f));
    print("PA", r);
    check_agreement(r);
}

TEST_CASE("motion diff: everything on, print-like path", "[motion_diff]") {
    Config config;
    config.shaper[0] = shaper(Type::mzv, 52.4f);
    config.shaper[1] = shaper(Type::ei, 43.f, 0.08f);
    config.pressure_advance = 0.035f;
    config.smooth_time = 0.03f;
    const Run r = run_both(config, perimeter(30, 6, 200, 7000, 0.04f));
    print("all", r);
    check_agreement(r);
}

// Precision study (single precision on the MCU against double in Klipper). Hidden: run with
// `motion_klipper_diff_tests "[precision]"`. It prints one line per axis; the results are in the
// notes of the differential test.
TEST_CASE("motion precision: large X/Y positions", "[.][precision]") {
    const double start_mm = GENERATE(0., 150., 300.);
    Config config;
    config.shaper[0] = shaper(Type::mzv, 52.4f);
    config.shaper[1] = shaper(Type::mzv, 43.f);
    // A = X + Y, B = X - Y: X = Y = start_mm puts A at 2 * start_mm and B at 0.
    const int32_t a = int32_t(2. * start_mm * 100.);
    config.start_steps = { a, 0, 0 };
    const Run r = run_both(config, perimeter(20, 4, 200, 7000, 0.f));
    char name[64];
    std::snprintf(name, sizeof(name), "XY at A=%.0f mm", 2. * start_mm);
    print(name, r);
    REQUIRE(r.buddy.finished);
}

TEST_CASE("motion precision: large E position", "[.][precision]") {
    const double prefix_e = GENERATE(0., 2000., 8000., 33000.);
    Config config;
    config.pressure_advance = 0.04f;
    config.smooth_time = 0.04f;
    std::vector<Block> blocks;
    double prefix_end_s = 0;
    if (prefix_e > 0) {
        // Extrude without a stop: PreciseStepping resets E only at a halt or at G92 E0.
        const float v = 2000.f, a = 20000.f;
        blocks.push_back({ .de = prefix_e, .v_start = 0, .v_cruise = v, .v_end = 0, .accel = a });
        prefix_end_s = prefix_e / v + v / a;
    }
    const auto path = perimeter(20, 4, 120, 4000, 0.045f);
    blocks.insert(blocks.end(), path.begin(), path.end());
    config.record_from_us = (prefix_end_s + 0.05) * 1e6;
    const Run r = run_both(config, blocks);
    char name[64];
    std::snprintf(name, sizeof(name), "E after %.0f mm", prefix_e);
    print(name, r);
    REQUIRE(r.buddy.finished);
}

TEST_CASE("motion precision: long single move", "[.][precision]") {
    const double duration_s = GENERATE(1., 30., 300., 3000.);
    Config config;
    const float v = float(20. / duration_s); // 20 mm in X
    const Run r = run_both(config, { { .dx = 20, .v_start = 0, .v_cruise = v, .v_end = 0, .accel = 1000 } });
    char name[64];
    std::snprintf(name, sizeof(name), "move of %.0f s", duration_s);
    print(name, r);
    REQUIRE(r.buddy.finished);
}

// Writes the steps and segments of a run as CSV, for the investigation of differences.
void dump_run(const Run &r) {
    for (int axis = 0; axis < AXES; ++axis) {
        char fname[32];
        std::snprintf(fname, sizeof(fname), "steps_%c.csv", axis_names[axis]);
        FILE *f = std::fopen(fname, "w");
        std::fprintf(f, "side,time_us,positive\n");
        for (const auto &s : r.buddy.steps.axis[axis]) {
            std::fprintf(f, "buddy,%.3f,%d\n", s.time_us, s.positive);
        }
        for (const auto &s : r.klipper.raw.axis[axis]) {
            std::fprintf(f, "klipper,%.3f,%d\n", s.time_us, s.positive);
        }
        std::fclose(f);
    }
    FILE *f = std::fopen("segments.csv", "w");
    std::fprintf(f, "print_time,move_time,start_v,half_accel,x,y,z,e,rx,ry,rz,re,flags\n");
    for (const auto &s : r.buddy.segments) {
        std::fprintf(f, "%.9f,%.9f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%u\n", s.print_time, s.move_time, s.start_v, s.half_accel, s.start_pos[0], s.start_pos[1], s.start_pos[2], s.start_pos[3], s.axes_r[0], s.axes_r[1], s.axes_r[2], s.axes_r[3], s.flags);
    }
    std::fclose(f);
}

Scenario random_scenario(int index) {
    std::mt19937 rng(20261126);
    std::vector<uint8_t> bytes(256);
    for (int run = 0; run <= index; ++run) {
        for (auto &b : bytes) {
            b = uint8_t(rng());
        }
    }
    ByteReader in(bytes.data(), bytes.size());
    return make_scenario(in);
}

void print_scenario(const Scenario &s) {
    std::printf("steps/mm %g %g %g %g pa %g st %g start %d %d %d\n", s.config.steps_per_mm[0], s.config.steps_per_mm[1], s.config.steps_per_mm[2], s.config.steps_per_mm[3], s.config.pressure_advance, s.config.smooth_time, s.config.start_steps[0], s.config.start_steps[1], s.config.start_steps[2]);
    for (int i = 0; i < 3; ++i) {
        if (s.config.shaper[i]) {
            std::printf("shaper %d: type %d f %g d %g\n", i, int(s.config.shaper[i]->type), s.config.shaper[i]->frequency, s.config.shaper[i]->damping_ratio);
        }
    }
    for (const auto &b : s.blocks) {
        std::printf("block %g %g %g %g v %g %g %g a %g\n", b.dx, b.dy, b.dz, b.de, b.v_start, b.v_cruise, b.v_end, b.accel);
    }
}

// Run: MOTION_DIFF_SCENARIO=<n> motion_klipper_diff_tests "[dump]" (writes into the current directory).
// n = -1: one extruding line with pressure advance. MOTION_DIFF_INPUT=<file>: a fuzz input.
TEST_CASE("motion dump", "[.][dump]") {
    const char *env = std::getenv("MOTION_DIFF_SCENARIO");
    const char *input = std::getenv("MOTION_DIFF_INPUT"); // a fuzz input file
    const int index = env ? std::atoi(env) : -1;
    Scenario s;
    if (input) {
        std::vector<uint8_t> bytes;
        if (FILE *f = std::fopen(input, "rb")) {
            for (int c; (c = std::fgetc(f)) != EOF;) {
                bytes.push_back(uint8_t(c));
            }
            std::fclose(f);
        }
        ByteReader in(bytes.data(), bytes.size());
        ScenarioLimits limits;
        limits.max_blocks = 8; // as tests/fuzz/motion_klipper.cpp
        s = make_scenario(in, limits);
    } else if (index < 0) {
        s.config.pressure_advance = 0.05f;
        s.config.smooth_time = 0.04f;
        s.blocks = { line(10, 0, 0.45, 120, 4000) };
    } else {
        s = random_scenario(index);
    }
    print_scenario(s);
    const Run r = run_both(s.config, s.blocks);
    print("dump", r);
    dump_run(r);
}

TEST_CASE("motion diff: random scenarios", "[motion_diff]") {
    for (int run = 0; run < 200; ++run) {
        const Scenario s = random_scenario(run);
        INFO("random scenario " << run);
        const Run r = run_both(s.config, s.blocks);
        check_agreement(r);
    }
}
