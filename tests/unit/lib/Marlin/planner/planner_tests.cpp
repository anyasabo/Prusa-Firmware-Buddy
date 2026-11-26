// Property tests of the real planner (planner.cpp) as a CORE One L. See README.md.
#include "planner_checks.hpp"
#include "planner_harness.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <algorithm>
#include <cmath>
#include <random>
#include <string>

using namespace planner_harness;
using namespace planner_checks;

namespace {

/// Runs `program` and checks all properties. `take_rate` is the chance that PreciseStepping takes
/// blocks after a command (0: only when the queue is full and at the end).
struct Runner {
    Settings settings;
    std::mt19937 rng { 1 };
    double take_rate = 0.3;
    Expected expected;

    Runner(const Settings &s = {})
        : settings(s) {
        reset(settings);
    }

    void after_command() {
        if (std::uniform_real_distribution<>(0, 1)(rng) < take_rate) {
            take(std::uniform_int_distribution<>(1, 3)(rng));
        }
    }
    void move(const xyze_pos_t &target, float fr, MoveHints hints = {}) {
        move_to(target, fr, hints);
        expected.max_fr = std::max(expected.max_fr, fr);
        after_command();
    }
    void g92_e(float e) {
        set_e_position(e);
        after_command();
    }
    void g92(const xyze_pos_t &p) {
        set_position(p);
        after_command();
    }
    Report finish() {
        take_all();
        return check(settings, taken(), jumps(), expected);
    }
};

void require_clean(const Report &r) {
    std::string failures;
    for (const auto &f : r.failures) {
        failures += f + "\n";
    }
    CHECK(failures == "");
}

} // namespace

TEST_CASE("planner: one line makes one block") {
    Runner r;
    r.move({ 10, 0, 0, 0 }, 100);
    const Report rep = r.finish();
    require_clean(rep);
    REQUIRE(taken().size() == 1);
    CHECK(taken()[0].block.msteps.x == std::lround(10 * AXIS_STEPS_PER_UNIT_15GT_XY * 4));
}

TEST_CASE("planner: a square at print and travel speed") {
    Runner r;
    r.take_rate = GENERATE(0.0, 0.5, 1.0);
    float e = 0;
    for (int loop = 0; loop < 3; ++loop) {
        r.move({ 50, 0, 0.2f, e += 1.6f }, 200);
        r.move({ 50, 50, 0.2f, e += 1.6f }, 200);
        r.move({ 0, 50, 0.2f, e += 1.6f }, 200);
        r.move({ 0, 0, 0.2f, e += 1.6f }, 200);
    }
    r.move({ 100, 100, 0.2f, e }, 500); // travel
    require_clean(r.finish());
}

TEST_CASE("planner: retract, travel, deretract and G92 E0") {
    Runner r;
    r.take_rate = GENERATE(0.0, 0.5);
    float e = 0;
    for (int i = 0; i < 20; ++i) {
        r.move({ 10.f + i, 10, 0.2f, e += 0.5f }, 150);
        r.move({ 10.f + i, 10, 0.2f, e -= 0.8f }, 35); // retract (E only)
        r.move({ 10.f + i, 10, 0.6f, e }, 15); // Z hop
        r.move({ 20.f + i, 30, 0.6f, e }, 400); // travel
        r.move({ 20.f + i, 30, 0.2f, e }, 15);
        r.move({ 20.f + i, 30, 0.2f, e += 0.8f }, 35); // deretract
        if (i % 3 == 0) {
            r.g92_e(e = 0);
        }
    }
    require_clean(r.finish());
}

TEST_CASE("planner: direction reversals and zero-length moves") {
    Runner r;
    r.take_rate = GENERATE(0.0, 0.5);
    for (int i = 0; i < 40; ++i) {
        const float x = (i % 2) ? -20.f : 20.f;
        r.move({ x, -x, 0, 0 }, 300);
        r.move({ x, -x, 0, 0 }, 300); // zero length
    }
    require_clean(r.finish());
}

TEST_CASE("planner: arcs of short segments") {
    Runner r;
    r.take_rate = GENERATE(0.0, 0.2, 1.0);
    const float seg = GENERATE(0.02f, 0.1f, 0.5f, 2.f);
    const float radius = 20;
    const int n = int(2 * M_PI * radius / seg);
    float e = 0;
    for (int i = 0; i <= n; ++i) {
        const float a = 2 * float(M_PI) * float(i) / float(n);
        e += seg * 0.033f;
        r.move({ 100 + radius * std::cos(a), 100 + radius * std::sin(a), 0.2f, e }, 200);
    }
    require_clean(r.finish());
}

TEST_CASE("planner: random move sequences") {
    const uint32_t seed = GENERATE(range(1u, 41u));
    CAPTURE(seed);
    Runner r;
    r.rng.seed(seed);
    std::mt19937 &g = r.rng;
    std::uniform_real_distribution<float> u(0, 1);
    r.take_rate = u(g);

    xyze_pos_t p {};
    for (int i = 0; i < 300; ++i) {
        const float kind = u(g);
        // Lengths over 6 decades: zero, below one step, short segments, long lines.
        const float len = (u(g) < 0.1f) ? 0.f : std::pow(10.f, -3.f + 5.f * u(g));
        const float ang = 2 * float(M_PI) * u(g);
        if (kind < 0.45f) { // print move
            p.x = std::clamp(p.x + len * std::cos(ang), -50.f, 350.f);
            p.y = std::clamp(p.y + len * std::sin(ang), -50.f, 350.f);
            p.e += len * 0.04f * u(g);
            r.move(p, 20 + 280 * u(g), { .is_printing_move = true });
        } else if (kind < 0.65f) { // travel
            p.x = std::clamp(p.x + len * std::cos(ang), -50.f, 350.f);
            p.y = std::clamp(p.y + len * std::sin(ang), -50.f, 350.f);
            r.move(p, 100 + 400 * u(g));
        } else if (kind < 0.8f) { // retract or deretract
            p.e += (u(g) < 0.5f ? -1.f : 1.f) * 2.f * u(g) * u(g);
            r.move(p, 5 + 60 * u(g));
        } else if (kind < 0.88f) { // Z
            p.z = std::clamp(p.z + (u(g) - 0.5f) * 2.f, -1.f, 300.f);
            r.move(p, 1 + 20 * u(g));
        } else if (kind < 0.94f) { // XYZE together
            p.x = std::clamp(p.x + len * std::cos(ang), -50.f, 350.f);
            p.z = std::clamp(p.z + 0.2f * u(g), -1.f, 300.f);
            p.e -= 0.5f * u(g);
            r.move(p, 50 + 200 * u(g));
        } else if (kind < 0.98f) { // G92 E0
            r.g92_e(p.e = 0);
        } else { // G92 X Y Z E
            p = { (u(g) - 0.5f) * 100, (u(g) - 0.5f) * 100, u(g) * 5, (u(g) - 0.5f) * 10 };
            r.g92(p);
        }
    }
    require_clean(r.finish());
}

namespace {
int64_t sum_e_msteps() {
    int64_t sum = 0;
    for (const auto &t : taken()) {
        if (!t.block.sync) {
            sum += (t.block.direction_bits & (1 << E_AXIS)) ? -int64_t(t.block.msteps.e) : int64_t(t.block.msteps.e);
        }
    }
    return sum;
}
} // namespace

// M221 (flow) scales the E of each block and rounds it to a mini-step (_populate_block()). The
// rounding error does not carry to the next block. With many short print moves of the same E, the
// errors add up in one direction.
TEST_CASE("planner: M221 flow scales the total E of many short moves", "[!shouldfail][known_bug]") {
    Settings s;
    s.flow_percentage = 95;
    Runner r(s);
    r.take_rate = 1;
    const int n = 1000;
    for (int i = 1; i <= n; ++i) {
        // 0.3 mm lines at 0.2 mm layer height, 0.45 mm width: 0.01 mm of filament each.
        r.move({ 0.3f * float(i % 100), 0.45f * float(i / 100), 0.2f, 0.01f * float(i) }, 100, { .is_printing_move = true });
    }
    require_clean(r.finish());
    const double commanded = double(planner.position.e) * double(planner.e_factor[VirtualToolIndex {}]);
    CAPTURE(commanded, sum_e_msteps());
    CHECK(std::abs(double(sum_e_msteps()) - commanded) <= 1);
}

// The planner drops a move with fewer than MIN_STEPS_PER_SEGMENT steps on each axis and adds it to
// the next move. A G92 directly after it discards it.
TEST_CASE("planner: a short move before G92 E0 is not lost", "[!shouldfail][known_bug]") {
    Runner r;
    r.take_rate = 1;
    r.move({ 10, 0, 0.2f, 0.5f }, 100);
    r.move({ 10, 0, 0.2f, 0.51f }, 35); // 0.01 mm: 15 mini-steps, below 6 steps
    r.g92_e(0);
    r.move({ 20, 0, 0.2f, 0.5f }, 100);
    const Report rep = r.finish();
    require_clean(rep);
    CHECK(rep.lost_at_g92.e == 0);
}

// JD_SMALL_SEGMENT_HANDLING limits the junction of a block shorter than 1 mm to v^2 = L * a / theta,
// the centripetal limit of the arc through the segments (radius L / theta). theta comes from a
// polynomial approximation of acos() with an error of up to 0.033 rad, which does not go below
// 0.033 rad. On a smooth curve of short segments, theta is much smaller.
TEST_CASE("planner: speed on an arc of short segments is the centripetal limit", "[!shouldfail][known_bug]") {
    const float seg = GENERATE(0.1f, 0.3f, 0.6f);
    CAPTURE(seg);
    Runner r;
    r.take_rate = 0;
    const float radius = 20;
    const int n = int(2 * M_PI * radius / seg);
    float e = 0;
    for (int i = 0; i <= n; ++i) {
        const float a = 2 * float(M_PI) * float(i) / float(n);
        e += seg * 0.033f;
        r.move({ 100 + radius * std::cos(a), 100 + radius * std::sin(a), 0.2f, e }, 300, { .is_printing_move = true });
    }
    require_clean(r.finish());
    // The blocks in the middle of the arc: far from the start and the end.
    std::vector<float> v;
    for (size_t i = taken().size() / 4; i < 3 * taken().size() / 4; ++i) {
        v.push_back(taken()[i].block.initial_speed);
    }
    std::sort(v.begin(), v.end());
    const double centripetal = std::sqrt(double(DEFAULT_ACCELERATION) * radius);
    // The queue also limits the speed: the newest block ends at MINIMUM_PLANNER_SPEED, so the
    // other blocks of a full queue (BLOCK_BUFFER_SIZE - 1) must be able to stop.
    const double lookahead = std::sqrt(2. * DEFAULT_ACCELERATION * (BLOCK_BUFFER_SIZE - 2) * seg);
    CAPTURE(v.front(), v.back(), centripetal, lookahead);
    CHECK(v.back() <= 1.05 * centripetal);
    CHECK(v.front() >= 0.9 * std::min(centripetal, lookahead));
}
