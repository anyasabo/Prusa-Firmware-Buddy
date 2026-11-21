#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include "probe_timing.hpp"

using namespace buddy::probe;
using Catch::Matchers::WithinAbs;

TEST_CASE("touchdown delay waits only for the rest of the dwell", "[probe_timing]") {
    CHECK(touchdown_remaining_delay_ms(0, 150) == 150);
    CHECK(touchdown_remaining_delay_ms(40, 150) == 110);
    CHECK(touchdown_remaining_delay_ms(150, 150) == 0);
    // Already later than the dwell: wait no more. An unsigned wrap must not give a full dwell.
    CHECK(touchdown_remaining_delay_ms(151, 150) == 0);
    CHECK(touchdown_remaining_delay_ms(400, 150) == 0);
}

static float radius(XYOffset o) {
    return std::hypot(o.x, o.y);
}

TEST_CASE("retry offsets go out on rings 2 mm apart", "[probe_timing]") {
    // Rings at 0, 2, 4 and 6 mm hold 1, 7, 13 and 19 tries: 40 tries in all (MULTIPLE_PROBING).
    CHECK_THAT(radius(offset_for_probe_try(0)), WithinAbs(0, 1e-6));
    for (int i = 1; i <= 7; i++) {
        CHECK_THAT(radius(offset_for_probe_try(i)), WithinAbs(2, 1e-4));
    }
    for (int i = 8; i <= 20; i++) {
        CHECK_THAT(radius(offset_for_probe_try(i)), WithinAbs(4, 1e-4));
    }
    for (int i = 21; i <= 39; i++) {
        CHECK_THAT(radius(offset_for_probe_try(i)), WithinAbs(6, 1e-4));
    }
    CHECK_THAT(radius(offset_for_probe_try(40)), WithinAbs(8, 1e-4));

    // The first try of each ring is on +X.
    CHECK_THAT(offset_for_probe_try(1).x, WithinAbs(2, 1e-4));
    CHECK_THAT(offset_for_probe_try(1).y, WithinAbs(0, 1e-4));

    // Neighbours on one ring are about 2 mm apart.
    const auto a = offset_for_probe_try(21), b = offset_for_probe_try(22);
    CHECK_THAT(std::hypot(a.x - b.x, a.y - b.y), WithinAbs(1.98, 0.02));
}
