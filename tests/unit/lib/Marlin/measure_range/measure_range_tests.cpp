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

TEST_CASE("M959 rejects a frequency step that is too small for the end frequency") {
    // M959 X F300000 G300001 H0.01: above 262144 Hz, frequency + 0.01 == frequency in float
    CHECK_FALSE(is_frequency_increment_valid({ .start_frequency = 300000, .end_frequency = 300001, .frequency_increment = 0.01f }));
    // M959 X G100000000: the default step of 1 Hz does not change a frequency of 1e8 Hz
    CHECK_FALSE(is_frequency_increment_valid({ .start_frequency = 5, .end_frequency = 1e8f, .frequency_increment = 1 }));
    CHECK(is_frequency_increment_valid({ .start_frequency = 300000, .end_frequency = 300010, .frequency_increment = 1 }));
}

namespace {
// An example step length (m)
constexpr float step_len = 0.0000025f;
} // namespace

TEST_CASE("M958 accepts an excitation that moves the axis") {
    using vibrate_measure::excitation_amplitude;
    using vibrate_measure::is_excitation_amplitude_valid;
    // The defaults of M958: 35 Hz at 2.5 m/s^2
    CHECK(is_excitation_amplitude_valid(excitation_amplitude(35, 2.5f), step_len));
    // Less than one step rounds up to one step
    CHECK(is_excitation_amplitude_valid(excitation_amplitude(200, 0.001f), step_len));
}

TEST_CASE("M958 rejects an excitation with no steps or too many steps") {
    using vibrate_measure::excitation_amplitude;
    using vibrate_measure::is_excitation_amplitude_valid;
    // M958 X A0: 0 steps in a period, and the step loop takes the step number modulo 0
    CHECK_FALSE(is_excitation_amplitude_valid(excitation_amplitude(35, 0), step_len));
    // M958 X F0: an infinite amplitude
    CHECK_FALSE(is_excitation_amplitude_valid(excitation_amplitude(0, 2.5f), step_len));
    // M958 X F0 A0: 0 / 0
    CHECK_FALSE(is_excitation_amplitude_valid(excitation_amplitude(0, 0), step_len));
    // M958 X F0.0001: the steps of one period do not fit in an int
    CHECK_FALSE(is_excitation_amplitude_valid(excitation_amplitude(0.0001f, 2.5f), step_len));
    // 4 * 2^29 steps in a period overflow an int
    CHECK_FALSE(vibrate_measure::is_excitation_amplitude_valid(536870912.0f, 1));
    CHECK(vibrate_measure::is_excitation_amplitude_valid(536870880.0f, 1));
}
