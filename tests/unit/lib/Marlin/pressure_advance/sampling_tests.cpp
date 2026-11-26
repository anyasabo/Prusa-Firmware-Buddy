#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <feature/pressure_advance/pressure_advance_sampling.hpp>

#include <cstdint>
#include <limits>

using pressure_advance::sampling_interval_us;

namespace {
// PRESSURE_ADVANCE_MAX_FILTER_LENGTH in pressure_advance.hpp, which needs Marlin.
constexpr uint16_t filter_length = 41;
} // namespace

TEST_CASE("The limits of the smooth time") {
    CHECK(sampling_interval_us(pressure_advance::smooth_time_min, filter_length) == 250);
    CHECK(sampling_interval_us(pressure_advance::smooth_time_max, filter_length) == 5000);
    CHECK(sampling_interval_us(0.f, filter_length) == 250);
    CHECK(sampling_interval_us(1.f, filter_length) == 5000);
}

TEST_CASE("The default smooth time samples each millisecond") {
    CHECK(sampling_interval_us(0.04f, filter_length) == 1000);
    CHECK(sampling_interval_us(0.02f, filter_length) == 500);
    CHECK(sampling_interval_us(0.2f, filter_length) == 5000);
}

// M572 accepted W from 0 to 0.2 before the minimum. Power panic also restores the smooth time
// without the M572 check. The step generator divides the print time by the interval
// (pressure_advance_precalculate_parameters()), and it counts the samples in uint32_t
// (pressure_advance_state_t::total_sample_idx). Thus the helper must clamp each value.
TEST_CASE("Each smooth time gives a usable sampling interval") {
    const float smooth_time = GENERATE(0.f, 1e-6f, 1e-5f, 1e-4f, 0.001f, 0.005f, 0.01f, 0.04f, 0.2f);
    CAPTURE(smooth_time);
    const int64_t interval_us = sampling_interval_us(smooth_time, filter_length);

    // Zero divides by zero. On the MCU, __aeabi_ldivmod() then returns INT64_MAX, and the step
    // generator samples the beginning empty move for 2^32 samples in the move ISR.
    REQUIRE(interval_us > 0);

    // The sample counter must not wrap during one motion without a stop (a long print).
    constexpr int64_t seven_days_us = int64_t(7) * 24 * 3600 * 1000000;
    CHECK(int64_t(std::numeric_limits<uint32_t>::max()) * interval_us >= seven_days_us);
}
