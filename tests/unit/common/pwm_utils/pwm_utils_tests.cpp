#include <catch2/catch_test_macros.hpp>
#include <pwm_utils.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

// The conversions are constant expressions: the config store uses from_percent() in template
// arguments. Clang does not evaluate std::round() in a constant expression.
static_assert(PWMValue<uint8_t, 255>::from_percent(40).value == 102);
static_assert(PWMValue<uint8_t, 255>::from_percent(70).value == 179);
static_assert(PWMValue<uint8_t, 255> { 128 }.to_percent() == 50);

namespace {

// The float formulas of the earlier implementation, as the reference
template <typename P>
long reference_from_percent(int8_t percent) {
    return std::lround((std::clamp<int8_t>(percent, 0, 100) * P::max) / 100.f);
}

template <typename P>
long reference_to_percent(typename P::Value value) {
    return std::lround((value * 100.f) / P::max);
}

template <typename P>
void check_all() {
    for (int p = INT8_MIN; p <= INT8_MAX; ++p) {
        CAPTURE(p);
        CHECK(long(P::from_percent(static_cast<int8_t>(p)).value) == reference_from_percent<P>(static_cast<int8_t>(p)));
    }
    for (long v = 0; v <= long(P::max); ++v) {
        CAPTURE(v);
        CHECK(long(P { static_cast<typename P::Value>(v) }.to_percent()) == reference_to_percent<P>(static_cast<typename P::Value>(v)));
    }
}

} // namespace

TEST_CASE("pwm_utils: the integer conversions equal the float conversions", "[pwm_utils]") {
    check_all<PWMValue<uint8_t, 255>>();
    check_all<PWMValue<uint8_t, 100>>();
    check_all<PWMValue<uint16_t, 1000>>();
}
