/// @file
/// Stub of the bed fan hardware for the controller tests.
#pragma once

#include <cstdint>

namespace bed_fan {

class BedFan {
public:
    void set_pwm(uint8_t pwm) { this->pwm = pwm; }
    uint8_t pwm = 0;
};

BedFan &bed_fan();

} // namespace bed_fan
