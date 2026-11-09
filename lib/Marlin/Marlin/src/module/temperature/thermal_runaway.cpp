/// @file
#include "thermal_runaway.hpp"

#include <wiring_time.h>

void ThermalRunaway::reset(float target) {
    timer = 0;
    tr_target_temperature = target;
    state = (target > 0) ? TRFirstHeating : TRInactive;
}

bool ThermalRunaway::step(float current, float target, uint16_t period_seconds, uint16_t hysteresis_degc) {
    // If the target temperature changes, restart
    if (tr_target_temperature != target) {
        reset(target);
    }

    switch (state) {
    // Inactive state waits for a target temperature to be set
    case TRInactive:
        break;

    // When first heating, wait until the temperature is within the hysteresis of the target,
    // then go to Stable state. Do not wait for the exact target: the heating watch (HeaterWatch)
    // stops at WATCH_TEMP_INCREASE + TEMP_HYSTERESIS + 1 under the target. The two checks must
    // overlap (static_assert in base_hotend.cpp and temperature.cpp), or a heater that stops
    // between them has no protection.
    case TRFirstHeating:
        if (current < tr_target_temperature - hysteresis_degc) {
            break;
        }
        state = TRStable;

    // While the temperature is stable watch for a bad temperature
    case TRStable:
        if (current >= tr_target_temperature - hysteresis_degc) {
            timer = millis() + period_seconds * 1000UL;
            break;
        } else if (PENDING(millis(), timer)) {
            break;
        }
        state = TRRunaway;

    case TRRunaway:
        return true;
    }

    return false;
}
