#include <catch2/catch_test_macros.hpp>

#include <gcode/feature/input_shaper/M593_params.hpp>

using namespace input_shaper;

namespace {

// What M593_set_axis_config() does with the parameters.
std::optional<AxisConfig> apply(const AxisConfig &prev, const M593Params &params) {
    bool clamped;
    return get_axis_config(prev, clamp_frequency(params, clamped));
}

constexpr AxisConfig prev {
    .type = Type::mzv,
    .frequency = 56.f,
    .damping_ratio = 0.1f,
    .vibration_reduction = 20.f,
};

} // namespace

TEST_CASE("M593 without F keeps the frequency") {
    // M593 X D0.15: the frequency is not given.
    M593Params params {};
    params.seen_x = true;
    params.axis.damping_ratio = 0.15f;

    // An empty std::optional<float> keeps old bytes in its storage. On the printer, M593Params
    // is on the stack, so the storage holds whatever the stack held. reset() leaves the old
    // value in the storage, which makes the test deterministic.
    params.axis.frequency = 5.f;
    params.axis.frequency.reset();

    const auto next = apply(prev, params);
    REQUIRE(next.has_value());
    CHECK(next->frequency == prev.frequency);
    CHECK(next->damping_ratio == 0.15f);
    CHECK(next->type == prev.type);
}

TEST_CASE("M593 without F does not report a clamp") {
    M593Params params {};
    params.axis.type = Type::ei;
    params.axis.frequency = 500.f;
    params.axis.frequency.reset();

    bool clamped = true;
    const M593Params result = clamp_frequency(params, clamped);
    CHECK_FALSE(clamped);
    CHECK_FALSE(result.axis.frequency.has_value());
}

TEST_CASE("M593 F clamps the frequency to the safe range") {
    M593Params params {};
    params.axis.frequency = 500.f;
    bool clamped = false;
    CHECK(clamp_frequency(params, clamped).axis.frequency == frequency_safe_max);
    CHECK(clamped);

    params.axis.frequency = 3.f;
    CHECK(apply(prev, params)->frequency == frequency_safe_min);

    params.axis.frequency = 47.f;
    CHECK(apply(prev, params)->frequency == 47.f);
}

TEST_CASE("M593 F0 disables the input shaper") {
    M593Params params {};
    params.axis.frequency = 0.f;
    CHECK_FALSE(apply(prev, params).has_value());
}
