#include "measure_range.hpp"

#include <catch2/catch_test_macros.hpp>

#include <limits>

using vibrate_measure::is_frequency_increment_valid;
using vibrate_measure::MeasureRange;

namespace {
MeasureRange range_with_increment(float increment) {
    return { .start_frequency = 5, .end_frequency = 150, .frequency_increment = increment };
}
} // namespace

TEST_CASE("M959 accepts a usable frequency step") {
    CHECK(is_frequency_increment_valid(range_with_increment(1)));
    CHECK(is_frequency_increment_valid(range_with_increment(0.5f)));
    CHECK(is_frequency_increment_valid(range_with_increment(vibrate_measure::min_frequency_increment)));
    CHECK(is_frequency_increment_valid(range_with_increment(std::numeric_limits<float>::infinity())));
}

TEST_CASE("M959 rejects a frequency step that does not advance the frequency") {
    // M959 X H0: the frequency stays at the start frequency
    CHECK_FALSE(is_frequency_increment_valid(range_with_increment(0)));
    // M959 X H0.0000001: frequency + H == frequency in float
    CHECK_FALSE(is_frequency_increment_valid(range_with_increment(1e-7f)));
    CHECK_FALSE(is_frequency_increment_valid(range_with_increment(0.001f)));
    CHECK_FALSE(is_frequency_increment_valid(range_with_increment(std::numeric_limits<float>::quiet_NaN())));
}
