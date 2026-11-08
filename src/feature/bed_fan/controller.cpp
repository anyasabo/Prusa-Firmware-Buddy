/// @file
#include <feature/bed_fan/controller.hpp>

#include <cmath>
#include <feature/bed_fan/bed_fan.hpp>
#include <feature/chamber/chamber.hpp>
#include <module/temperature.h>
#include <utils/overloaded_visitor.hpp>

static void apply_pwm(uint8_t pwm) {
    static uint8_t last_pwm = 0;
    if (pwm != last_pwm) {
        last_pwm = pwm;
        bed_fan::bed_fan().set_pwm(pwm);
    }
}

namespace bed_fan {

Controller &controller() {
    static Controller instance;
    return instance;
}

Controller::Mode Controller::get_mode() const {
    return mode;
}

void Controller::set_mode(const Mode &mode) {
    this->mode = mode;
}

void Controller::set_mode(const ManualMode &manual_mode) {
    mode = manual_mode;
    step();
}

void Controller::set_mode(const AutomaticMode &automatic_mode) {
    mode = automatic_mode;
    step();
}

void Controller::step() {
    const auto pwm = std::visit(Overloaded {
                                    [](const ManualMode &mode) { return mode.pwm; },
                                    [](const AutomaticMode &mode) { return mode.calculate_pwm(); },
                                },
        mode);
    apply_pwm(pwm);
}

uint8_t Controller::AutomaticMode::calculate_pwm() const {
    constexpr uint8_t min_pwm = 255 / 10; // minimum PWM is 10% of largest possible PWM
    const uint8_t max_pwm = this->max_pwm;
    if (max_pwm <= min_pwm) {
        return min_pwm;
    }
    const float scale = [this] {
        // Try chamber control first (has priority if both targets set)
        if (chamber_temp_threshold > 0.0f) {
            const auto chamber_current = buddy::chamber().current_temperature();
            const auto chamber_target = buddy::chamber().target_temperature();

            if (chamber_current.has_value() && chamber_target.has_value()) {
                const float temp_diff = chamber_target.value() - chamber_current.value();
                return temp_diff / chamber_temp_threshold;
            }
        }

        // Try bed control
        if (bed_temp_threshold > 0.0f) {
            const float bed_current = thermalManager.degBed();
            const float bed_target = thermalManager.degTargetBed();

            if (bed_current > 0.0f && bed_target > 0.0f) {
                const float temp_diff = bed_target - bed_current;
                return temp_diff / bed_temp_threshold;
            }
        }

        return 0.0f;
    }();
    // std::lerp extrapolates outside 0..1, and a float above 255 converted to uint8_t is undefined.
    // A difference above the threshold gives max_pwm. NaN gives min_pwm.
    const float clamped_scale = scale > 0.0f ? std::min(scale, 1.0f) : 0.0f;
    return static_cast<uint8_t>(std::lerp(static_cast<float>(min_pwm), static_cast<float>(max_pwm), clamped_scale));
}

} // namespace bed_fan
