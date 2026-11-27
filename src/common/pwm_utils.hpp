#pragma once

#include <cstdint>
#include <expected>
#include <algorithm>
#include <cmath>
#include <type_traits>

/// Strong class for representing a PWM value
template <typename Value_, Value_ max_>
struct PWMValue {
    using Value = Value_;
    static constexpr Value max = max_;
    // The integer conversions below need this (no overflow in uint32_t, no negative values)
    static_assert(std::is_unsigned_v<Value> && max <= UINT16_MAX);

    Value value = 0;

    constexpr PWMValue() {}
    constexpr PWMValue(const PWMValue &o) = default;

    explicit constexpr PWMValue(Value value)
        : value(value) {}

    // Integer rounding (half up, all values are not negative): the config store uses these in
    // template arguments, and clang does not evaluate std::round() in a constant expression.
    constexpr int8_t to_percent() const {
        return static_cast<uint8_t>((uint32_t(value) * 100 + max / 2) / max);
    }
    static constexpr int8_t to_percent_static(PWMValue v) {
        return v.to_percent();
    }
    static constexpr PWMValue from_percent(int8_t percent) {
        return PWMValue { static_cast<Value>((uint32_t(std::clamp<int8_t>(percent, 0, 100)) * max + 50) / 100) };
    }

    constexpr auto operator<=>(const PWMValue &) const = default;
};

struct PWMAuto {
    constexpr bool operator==(const PWMAuto &) const = default;
};

constexpr std::unexpected pwm_auto { PWMAuto {} };

using PWM255 = PWMValue<uint8_t, 255>;
using PWM255OrAuto = std::expected<PWM255, PWMAuto>;
