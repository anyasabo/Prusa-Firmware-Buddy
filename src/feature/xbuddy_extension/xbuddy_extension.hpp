#pragma once

#include <option/xbuddy_extension_variant.h>
#if XBUDDY_EXTENSION_VARIANT_IS_STANDARD()
    #include "chamber_alarm.hpp"
    #include "cooling.hpp"
#endif

#include <optional>

#include <utils/enum_array.hpp>
#include <utils/led_color.hpp>
#include <utils/timing/latching_debouncer.hpp>
#include <freertos/mutex.hpp>
#include <temperature.hpp>
#include <pwm_utils.hpp>
#include <utils/timing/latching_debouncer.hpp>

#include <xbuddy_extension/shared_enums.hpp>
#include <option/xbuddy_extension_variant.h>

namespace buddy {

/// Thread-safe API, can be read/written to from any thread
class XBuddyExtension {
public: // General things, status
    XBuddyExtension();

    enum class Status {
        disabled,
        not_connected,
        ready,
    };

    using FilamentSensorState = xbuddy_extension::FilamentSensorState;

#if XBUDDY_EXTENSION_VARIANT_IS_STANDARD()
    using FanRPM = uint16_t;
    using FanPWM = PWM255;
    using Fan = xbuddy_extension::Fan;
    using FanPWMOrAuto = PWM255OrAuto;
#endif

    Status status() const;

#if XBUDDY_EXTENSION_VARIANT_IS_STANDARD()
    void step();

public: // Fans
    /// \returns measured RPM of the specified fan
    std::optional<FanRPM> fan_rpm(Fan fan) const;

    /// \returns False if the fan has unexpectedly read 0 RPM at positive PWM for longer than a short debounce window.
    bool is_fan_ok(const Fan fan) const;

    /// \returns shared target PWM of the specified fan
    FanPWMOrAuto fan_target_pwm(Fan fan) const;

    /// \returns actual PWM the fans are controlled to
    FanPWM fan_actual_pwm(Fan fan) const;

    /// Sets target PWM for the given fan. The PWM can be overriden by some emergency events
    /// * Please note than PWM control for the cooling fans is shared (so calling this with Fan::cooling_fan_1 does the same as with Fan::cooling_fan_2)
    void set_fan_target_pwm(Fan fan, FanPWMOrAuto target);

    // called on print start to use legacy chamber regulator for compatibility with old gcodes
    void set_chamber_regulator_legacy(bool legacy) {
        chamber_cooling.regulator_legacy = legacy;
    };

    void set_chamber_regulator_ramp_breakpoint_pwm(uint8_t pwm) {
        chamber_cooling.ramp_breakpoint_pwm = pwm;
        chamber_cooling.regulator_legacy = false;
    };

    void set_chamber_regulator_ramp_slope(float slope) {
        chamber_cooling.ramp_slope = slope;
        chamber_cooling.regulator_legacy = false;
    };

    /// A convenience function returning a structure of data to be used in the Connect interface
    /// The key idea here is to avoid locking the internal mutex for every member while providing a consistent state of values.
    struct FanState {
        uint16_t fan1rpm, fan2rpm;
        FanPWMOrAuto fan1_fan2_target_pwm;
    };

    FanState get_fan12_state() const;

    /// \returns whether the fan 3 is connected/used and thus whether we should consider it in sensor info, selftest results and such
    bool using_filtration_fan_instead_of_cooling_fans() const;

    /// \returns maximum PWM that is used for cooling in non-emergency situations
    PWM255 max_cooling_pwm() const;

    void set_max_cooling_pwm(PWM255 set);

    /// \returns whether the current configuration allows automatic chamber cooling (cooling fans are not set to a hard value)
    bool can_auto_cool() const;

public: // LEDs
    /// \returns color set for the bed LED strip
    leds::ColorRGBW bed_leds_color() const;

    /// Sets PWM for the led strip that is under the bed
    void set_bed_leds_color(leds::ColorRGBW set);

    /// Sets the white led strobe mode.
    ///
    /// * If set to nullopt, strobe mode is disabled. Led goes to shining
    ///   according to the requested amount of light inside the chamber and PWM
    ///   frequencies return to default.
    /// * If set to a value, it'll blink at that frequency (configures PWM with
    ///   the given frequency and some small-ish duty cycle to provide a
    ///   stroboscopic effect). In Hz.
    ///
    /// Notes:
    /// * 0 as a frequency doesn't make sense and is asserted against.
    /// * Values 1, 2 and 3 were observed to act "weird". The prescaler in the
    ///   HW is only 16 bits and we overflow at that case. Starting at 4Hz, it
    ///   seems to act OK.
    /// * We use the same hardware timer for controlling some fans. As
    ///   controlling goes, it seems to work fine even with lower frequencies
    ///   (tested even with the 4Hz) - they just do some tiny audible clicks.
    ///   So while we don't expect them to be running at the time (we are using
    ///   the strobe at specific wizard, with open door and no heating at the
    ///   time), they _could_ be and everything would be likely fine. And the
    ///   actual frequency will be in around the 100Hz range.
    void set_strobe(std::optional<uint16_t> frequency);

    /// @returns percentage 0-100% converted from PWM value (0-max_pwm)
    /// @note in the future, non-linear mapping between intensity pct and PWM shall be implemented here
    static constexpr uint8_t led_pwm2pct(uint8_t pwm) {
        return static_cast<uint8_t>(((uint16_t)pwm) * 100U / 255U);
    }

    /// @returns PWM value (0-max_pwm) from percentage 0-100%
    /// @note in the future, non-linear mapping between intensity pct and PWM shall be implemented here
    static constexpr uint8_t led_pct2pwm(uint8_t pct) {
        return static_cast<uint8_t>(((uint16_t)pct) * 255U / 100U);
    }

public: // USB
    void set_usb_power(bool enabled);
    bool usb_power() const;

public: // Other
    /// \returns chamber temperature measured through the thermistor connected to the board, in degrees Celsius
    std::optional<Temperature> chamber_temperature();

#elif XBUDDY_EXTENSION_VARIANT_IS_iX()
    void set_heatbreak_fan_pwm(uint32_t value);
    uint32_t get_heatbreak_fan_pwm();
    uint32_t get_heatbreak_fan_rpm();
    bool is_heatbreak_fan_ok();

    void set_white_led(uint32_t intensity);
    void set_strobe(std::optional<uint16_t> freq);
    void set_rgbw_led(leds::ColorRGBW rgbw);
#endif

    /// Single GPIO sensor (PA5 on standard, PA9 on iX)
    std::optional<FilamentSensorState> gpio_filament_sensor() const;

    /// TMP1826 multi-tool sensor (PC14/EXT connector)
    std::optional<FilamentSensorState> ext_filament_sensor(uint8_t index) const;

private:
    mutable freertos::Mutex mutex_;

#if XBUDDY_EXTENSION_VARIANT_IS_STANDARD()
    leds::ColorRGBW bed_leds_color_;
    std::optional<uint16_t> strobe_freq_ = std::nullopt;

    FanCooling chamber_cooling;

    FanPWMOrAuto cooling_fans_target_pwm_ = pwm_auto;
    FanPWMOrAuto filtration_fan_target_pwm_ = pwm_auto;

    FanPWM cooling_fans_actual_pwm_;
    FanPWM filtration_fan_actual_pwm_;

    // keeps the last timestamp of Fan PWM update
    uint32_t last_fan_update_ms;

    // keeps fan power up timestamp to measure headstart delay
    EnumArray<Fan, uint32_t, xbuddy_extension::fan_count> fan_start_timestamp = {};

    // Latches failure state once fan reports 0 RPM at positive PWM beyond grace period.
    EnumArray<Fan, utils::LatchingDebouncer, xbuddy_extension::fan_count> fan_failure_latch = {};

    bool can_auto_cool_ = false;
    ChamberAlarm chamber_alarm_;

#elif XBUDDY_EXTENSION_VARIANT_IS_iX()
    uint32_t hbr_fan_start_timestamp;

    std::optional<uint32_t> white_intensity_override;
#endif
};

XBuddyExtension &xbuddy_extension();

} // namespace buddy
