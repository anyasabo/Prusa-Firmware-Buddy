#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cmath>
#include "probe_point_tries.hpp"

using namespace buddy::probe;
using Next = ProbePointTries::Next;
using Catch::Matchers::Equals;
using Catch::Matchers::WithinAbs;

// The CORE One L mesh: 40 tries, 1 good probe, trigger threshold 125 g.
static constexpr ProbePointTries::Config mesh { .max_tries = 40, .required_successes = 1, .single_only = false, .max_tare_offset = 125 };

TEST_CASE("a good first try finishes the point", "[probe_point_tries]") {
    ProbePointTries p(mesh);
    REQUIRE(p.start_try());
    CHECK(p.try_offset().x == 0);
    CHECK(p.try_offset().y == 0);
    CHECK_FALSE(p.tare_needs_new_reference(5));
    CHECK(p.on_good(-1.25f) == Next::done);
    CHECK_THAT(p.result(), WithinAbs(-1.25, 1e-6));
    CHECK(p.tries() == 1);
}

TEST_CASE("NOK tries move out on the spiral until a good one", "[probe_point_tries]") {
    ProbePointTries p(mesh);
    REQUIRE(p.start_try());
    CHECK(p.on_nok("angle_after") == Next::retry);
    REQUIRE(p.start_try());
    CHECK_THAT(std::hypot(p.try_offset().x, p.try_offset().y), WithinAbs(2, 1e-4));
    CHECK(p.on_nok("sanity-check") == Next::retry);
    REQUIRE(p.start_try());
    CHECK(p.on_good(-0.5f) == Next::done);
    CHECK(p.tries() == 3);
    CHECK_THAT(p.last_nok(), Equals("sanity-check"));
}

TEST_CASE("a tare far from the reference asks for a new reference, and the try goes on", "[probe_point_tries]") {
    ProbePointTries p(mesh);
    REQUIRE(p.start_try());
    // 2026-11-19 on a CORE One L: offsets of -353 g and 381 g between neighbouring tries.
    CHECK(p.tare_needs_new_reference(-353.28f));
    CHECK(p.tare_needs_new_reference(381.47f));
    CHECK_FALSE(p.tare_needs_new_reference(125));
    CHECK_FALSE(p.tare_needs_new_reference(-124.9f));
    CHECK(p.on_good(0.1f) == Next::done);
}

TEST_CASE("a descent with no contact fails the point at once", "[probe_point_tries]") {
    ProbePointTries p(mesh);
    REQUIRE(p.start_try());
    CHECK(p.on_nok("sanity-check") == Next::retry);
    REQUIRE(p.start_try());
    CHECK(p.on_no_contact() == Next::fail);
    CHECK_THAT(p.failure(), Equals("bed-not-reached"));
    CHECK_THAT(p.last_nok(), Equals("sanity-check"));
    CHECK(std::isnan(p.result()));
}

TEST_CASE("safety trips use up tries; one that does not recover fails the point", "[probe_point_tries]") {
    ProbePointTries p(mesh);
    REQUIRE(p.start_try());
    CHECK(p.on_safety_trip(true) == Next::retry);
    REQUIRE(p.start_try());
    CHECK(p.tries() == 2);
    CHECK(p.trips() == 1);
    CHECK(p.on_safety_trip(false) == Next::fail);
    CHECK_THAT(p.failure(), Equals("safety-trip-no-recovery"));
}

TEST_CASE("after 40 NOK tries the point fails", "[probe_point_tries]") {
    ProbePointTries p(mesh);
    int tries = 0;
    while (p.start_try()) {
        ++tries;
        REQUIRE(p.on_nok("load_mean_before_compression") == Next::retry);
    }
    CHECK(tries == 40);
    CHECK_THAT(p.failure(), Equals("tries-exhausted"));
    CHECK_THAT(p.last_nok(), Equals("load_mean_before_compression"));
    CHECK(std::isnan(p.result()));
}

TEST_CASE("after 40 recovered trips the point fails with last NOK none", "[probe_point_tries]") {
    ProbePointTries p(mesh);
    while (p.start_try()) {
        REQUIRE(p.on_safety_trip(true) == Next::retry);
    }
    CHECK(p.trips() == 40);
    CHECK_THAT(p.failure(), Equals("tries-exhausted"));
    CHECK_THAT(p.last_nok(), Equals("none"));
}

TEST_CASE("single_only ends after the first analysis, but retries after a trip", "[probe_point_tries]") {
    const ProbePointTries::Config single { .max_tries = 40, .required_successes = 1, .single_only = true, .max_tare_offset = 125 };

    SECTION("NOK") {
        ProbePointTries p(single);
        REQUIRE(p.start_try());
        CHECK(p.on_nok("low-precision") == Next::fail);
        CHECK_THAT(p.failure(), Equals("single-try-nok"));
    }
    SECTION("good") {
        ProbePointTries p(single);
        REQUIRE(p.start_try());
        CHECK(p.on_good(0.2f) == Next::done);
    }
    SECTION("trip, then good") {
        ProbePointTries p(single);
        REQUIRE(p.start_try());
        CHECK(p.on_safety_trip(true) == Next::retry);
        REQUIRE(p.start_try());
        CHECK(p.on_good(0.2f) == Next::done);
    }
}

TEST_CASE("required_successes averages the good tries", "[probe_point_tries]") {
    ProbePointTries p({ .max_tries = 40, .required_successes = 3, .single_only = false, .max_tare_offset = 125 });
    REQUIRE(p.start_try());
    CHECK(p.on_good(0.1f) == Next::retry);
    REQUIRE(p.start_try());
    CHECK(p.on_nok("z-lines") == Next::retry);
    REQUIRE(p.start_try());
    CHECK(p.on_good(0.2f) == Next::retry);
    REQUIRE(p.start_try());
    CHECK(p.on_good(0.3f) == Next::done);
    CHECK_THAT(p.result(), WithinAbs(0.2, 1e-6));
}

TEST_CASE("a failure before the first try", "[probe_point_tries]") {
    ProbePointTries p(mesh);
    CHECK(p.on_setup_failure("reference-tare-tripped") == Next::fail);
    CHECK_THAT(p.failure(), Equals("reference-tare-tripped"));
    CHECK(p.tries() == 0);
    CHECK(std::isnan(p.result()));
}
