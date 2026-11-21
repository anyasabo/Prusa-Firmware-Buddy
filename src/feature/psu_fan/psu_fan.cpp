/// @file
#include <feature/psu_fan/psu_fan.hpp>

#include <metric.h>
#include <option/has_ac_controller.h>
#include <timing.h>
#include <utils/timing/rate_limiter.hpp>

#if HAS_AC_CONTROLLER()
    #include <puppies/ac_controller.hpp>
#endif

namespace psu_fan {

PsuFan &psu_fan() {
    static PsuFan instance;
    return instance;
}

#if HAS_AC_CONTROLLER()

std::optional<uint8_t> PsuFan::get_pwm() const {
    return buddy::puppies::ac_controller.get_psu_fan_pwm();
}

std::optional<uint16_t> PsuFan::get_rpm() const {
    return buddy::puppies::ac_controller.get_psu_fan_rpm();
}

void PsuFan::record_metric() const {
    METRIC_DEF(metric_psu_fan, "psu_fan", METRIC_VALUE_CUSTOM, 0, METRIC_DISABLED);
    static RateLimiter<uint32_t> should_record(1000);
    if (!should_record.check(ticks_ms())) {
        return;
    }
    // pwm is the value that the main board requests. Outside the fan self test, it is 0.
    // Without valid data from the AC controller, record nothing.
    const auto pwm = get_pwm();
    const auto rpm = get_rpm();
    if (!pwm.has_value() || !rpm.has_value()) {
        return;
    }
    metric_record_custom(&metric_psu_fan, " pwm=%ui,rpm=%ui", *pwm, *rpm);
}

void PsuFan::set_pwm(uint8_t pwm) {
    if (pwm == 0) {
        start_timestamp_ms = 0;
    } else {
        start_timestamp_ms = start_timestamp_ms != 0 ? start_timestamp_ms : ticks_ms();
    }
    buddy::puppies::ac_controller.set_psu_fan_pwm(pwm);
}

bool PsuFan::is_rpm_ok() {
    static constexpr uint32_t rpm_start_delay_ms = 5000;
    const auto fan_rpm = get_rpm();
    if (!fan_rpm.has_value() || start_timestamp_ms == 0 || ticks_diff(start_timestamp_ms + rpm_start_delay_ms, ticks_ms()) >= 0) {
        return true;
    }

    return fan_rpm.value() > 0;
}

#else
// #error dead code found by automatic analyses (see BFW-5461)

    #error

#endif

} // namespace psu_fan
