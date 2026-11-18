#include <feature/precise_stepping/resync.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace precise_stepping::resync;

TEST_CASE("precise_stepping resync: the drift keeps its sign beyond 32.768 ms", "[precise_stepping]") {
    // 1 tick per µs, as on the STM32F4
    CHECK(drift_ticks(1000, 900, 1) == 100);
    CHECK(drift_ticks(900, 1000, 1) == -100);

    // In int16_t, a drift of 40 ms was -25536: the correction made the drift larger
    CHECK(drift_ticks(140'000, 100'000, 1) == 40'000);
    CHECK(drift_ticks(100'000, 140'000, 1) == -40'000);
    CHECK(drift_ticks(100'000 + 65'536, 100'000, 1) == 65'536);

    // The time base wraps at 2^32 µs (71 min). The difference is still right.
    CHECK(drift_ticks(10, 0xFFFF'FFF0u, 1) == 26);
}

TEST_CASE("precise_stepping resync: the product with the ticks per µs does not wrap", "[precise_stepping]") {
    CHECK(drift_ticks(40'000, 0, 4) == 160'000);
    CHECK(drift_ticks(0x7FFF'FFFFu, 0, 4) == INT32_MAX);
    CHECK(drift_ticks(0, 0x7FFF'FFFFu, 4) == INT32_MIN);
}

TEST_CASE("precise_stepping resync: one step corrects at most one ISR period", "[precise_stepping]") {
    constexpr uint16_t period = 1000;
    CHECK(step_correction(40'000, 500, period) == period);
    CHECK(step_correction(100, 500, period) == 100);
    // No negative time to the next step
    CHECK(step_correction(-40'000, 500, period) == -500);
    CHECK(step_correction(-40'000, 5000, period) == -period);
    CHECK(step_correction(-100, 500, period) == -100);
}
