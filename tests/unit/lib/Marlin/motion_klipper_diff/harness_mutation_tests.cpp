// Negative tests of the comparator: a real run passes, and each change of the Buddy steps that
// makes them wrong must fail. Without these tests, a limit that is too wide passes in silence.
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace motion_diff;

namespace {

constexpr int A = 0; // CoreXY A = X + Y

/// The X line of the mutations, without the shaper and PA: their tolerances are 0 on A.
Run x_line() {
    return run_both(Config {}, { line(20, 0, 0, 100, 2000) });
}

std::string check_mutated(const Run &r, BuddyResult buddy) {
    return check_axis(compare_axis(r.config, buddy, r.klipper, A), buddy.final_count_position[A] - buddy.start_count_position[A]);
}

} // namespace

TEST_CASE("comparator: the X line agrees", "[motion_diff][comparator]") {
    const Run r = x_line();
    REQUIRE(r.buddy.steps.axis[A].size() == 2000);
    CHECK(check_mutated(r, r.buddy) == "");
}

TEST_CASE("comparator: each step 0.4 step late fails", "[motion_diff][comparator]") {
    // Each step at n + 0.9, not at the boundary n + 0.5. A bias of 0.5 step (a floor in place of a
    // round) is in the cruise exactly at a limit of 0.5, so this test uses 0.4 step.
    const Run r = x_line();
    BuddyResult buddy = r.buddy;
    auto &steps = buddy.steps.axis[A];
    for (size_t i = 0; i + 1 < steps.size(); ++i) {
        steps[i].time_us += 0.4 * (steps[i + 1].time_us - steps[i].time_us);
    }
    CHECK(check_mutated(r, buddy) != "");
}

TEST_CASE("comparator: an extra step pair fails", "[motion_diff][comparator]") {
    // One step forward and back in the middle of a step interval: the position is one step ahead
    // for a moment, and the end is correct.
    const Run r = x_line();
    BuddyResult buddy = r.buddy;
    auto &steps = buddy.steps.axis[A];
    const size_t i = steps.size() / 2;
    const double t = 0.5 * (steps[i].time_us + steps[i + 1].time_us);
    steps.insert(steps.begin() + i + 1, { { t, true }, { t, false } });
    CHECK(check_mutated(r, buddy) != "");
}

TEST_CASE("comparator: a lost step fails", "[motion_diff][comparator]") {
    const Run r = x_line();
    BuddyResult buddy = r.buddy;
    auto &steps = buddy.steps.axis[A];
    steps.erase(steps.begin() + steps.size() / 2);
    --buddy.final_count_position[A];
    CHECK(check_mutated(r, buddy) != "");
}

TEST_CASE("comparator: the E offset after a tie halt keeps a lost step", "[motion_diff][comparator]") {
    // The session moves E by 303.5 steps from 100 (4 mini-steps for each step): a tie, so Buddy
    // can end at 403 or 404. A Buddy end outside these two is a lost or extra step. Then the
    // offset must not follow Buddy, else Klipper continues from the wrong position and the loss is
    // never visible.
    constexpr int m = 4;
    CHECK(next_session_e_offset(100, 1214, m, 403) == 403);
    CHECK(next_session_e_offset(100, 1214, m, 404) == 404);
    CHECK(next_session_e_offset(100, 1214, m, 402) == 404);
    CHECK(next_session_e_offset(100, -1214, m, -204) == -204);
    CHECK(next_session_e_offset(100, -1214, m, -203) == -203);
    CHECK(next_session_e_offset(100, -1214, m, -202) == -204);
    // Not a tie: always the rounded commanded position
    CHECK(next_session_e_offset(100, 1213, m, 402) == 403);
}

TEST_CASE("comparator: E steps 0.4 step late with pressure advance fail", "[motion_diff][comparator][!mayfail]") {
    // The PA tolerance must stay small in a cruise: only the steps in the middle of the cruise move.
    Config config;
    config.pressure_advance = 0.05f;
    config.smooth_time = 0.04f;
    // [!mayfail]: on v8.0.2, M1 and M2 (motion_klipper_diff_tests.cpp) change the unmutated run.
    // Remove the tag where the fixes are merged (anya/main), as for the other PA cases.
    const Run r = run_both(config, { line(60, 0, 3, 100, 2000) }); // E cruise from 50 ms to 600 ms
    constexpr int E = 3;
    const PaErrorBound pa(config, r.buddy.segments);
    REQUIRE(pa.at(0.3e6) < 0.1);
    REQUIRE(check_axis(r.axis[E]) == "");
    BuddyResult buddy = r.buddy;
    auto &steps = buddy.steps.axis[E];
    for (size_t i = 0; i + 1 < steps.size(); ++i) {
        if (steps[i].time_us > 0.2e6 && steps[i].time_us < 0.4e6) {
            steps[i].time_us += 0.4 * (steps[i + 1].time_us - steps[i].time_us);
        }
    }
    CHECK(check_axis(compare_axis(r.config, buddy, r.klipper, E)) != "");
}
