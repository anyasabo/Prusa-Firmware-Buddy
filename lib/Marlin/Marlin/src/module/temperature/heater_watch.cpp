/// @file
#include "heater_watch.hpp"

void HeaterWatch::arm(int16_t target_temp) {
    if (target_temp <= 0) {
        state_ = State::disarmed;
        return;
    }
    target_temp_celsius_ = target_temp;
    state_ = (state_ == State::watching || state_ == State::retargeted) ? State::retargeted : State::pending;
}

void HeaterWatch::restart(int16_t target_temp) {
    state_ = State::disarmed;
    arm(target_temp);
}

bool HeaterWatch::engaged(float current_temp) const {
    return ((target_temp_celsius_ - current_temp) > config_.min_temp_diff) ^ config_.watch_cooling_instead;
}

bool HeaterWatch::update(float current_temp) {
    switch (state_) {
    case State::disarmed:
        return false;

    case State::retargeted:
        if (!engaged(current_temp)) {
            state_ = State::disarmed;
            return false;
        }
        state_ = State::watching;
        [[fallthrough]];

    case State::watching:
        if (!ELAPSED(millis(), next_check_ms_)) {
            return false;
        }
        if ((current_temp < baseline_threshold_celsius_) ^ config_.watch_cooling_instead) {
            // No re-arm - keeps firing every tick until the caller raises, or until a new target that no longer needs the watch
            return true;
        }
        // Period ended successfully — re-evaluate engage condition for the next one
        state_ = State::pending;
        [[fallthrough]];

    case State::pending:
        if (!engaged(current_temp)) {
            state_ = State::disarmed;
            return false;
        }
        baseline_threshold_celsius_ = static_cast<int16_t>(current_temp) + config_.temp_increase;
        next_check_ms_ = millis() + config_.period_s * 1000UL;
        state_ = State::watching;
        return false;
    }

    return false;
}
