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
