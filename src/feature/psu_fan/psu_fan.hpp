/// @file
#pragma once

#include <cstdint>
#include <optional>
#include <option/has_psu_fan.h>
#include <utils/uncopyable.hpp>

static_assert(HAS_PSU_FAN());

namespace psu_fan {

/// Hardware abstraction layer for PSU fan operations.
class PsuFan : Uncopyable {
public:
    /// Get current PSU fan PWM value.
    /// Returns std::nullopt if data is unavailable or stale.
    std::optional<uint8_t> get_pwm() const;

    /// Get current PSU fan RPM value.
    /// Returns std::nullopt if data is unavailable or stale.
    std::optional<uint16_t> get_rpm() const;

    /// Returns true if fan rpm follows expected behaviour.
    /// Returns true also if data is unavailable or stale (we cannot assume error occurred).
    bool is_rpm_ok();

    /// Set PSU fan PWM value.
    void set_pwm(uint8_t pwm);

    /// Record the PWM and the RPM in the metric psu_fan, at most once per second.
    void record_metric() const;

private:
    uint32_t start_timestamp_ms = 0; /// timestamp for calculating delay of error checking
};

/// Global PSU fan hardware abstraction instance.
PsuFan &psu_fan();

} // namespace psu_fan
