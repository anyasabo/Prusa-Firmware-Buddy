/// @file
#include "heater_control.hpp"

#include <algorithm>
#include <limits>

namespace indx_head::heater {

namespace {

    struct IntervalLUTItem {
        float factor_pre;
        float factor_post;
    };

    struct IntervalLUTItemExt {
        float factor_pre;
        float factor_post;

        /// Squared duty cycle, calculated as factor_post + 0.25 * factor_pre
        fpm::fixed_16_16 duty_cycle_sq;

        constexpr IntervalLUTItemExt(const IntervalLUTItem &i)
            : factor_pre { i.factor_pre }
            , factor_post { i.factor_post }
            , duty_cycle_sq(i.factor_post + 0.25f * i.factor_pre) {
        }
    };

    // Values were found experimentally to switch MOSFET in zero voltage
    // crossing. Do not mess with them without measuring changes with
    // oscilloscope, or the MOSFET WILL BURN!
    // !!! INDEX intervalLUT[0] ~ (current_power = 1), current_power 0 is not in this table
    constexpr std::array<IntervalLUTItemExt, max_power> intervalLUT {
        // 0
        IntervalLUTItem {
            .factor_pre = 0.00f,
            .factor_post = 0.17f,
        },
        // 1
        IntervalLUTItem {
            .factor_pre = 0.12f,
            .factor_post = 0.20f,
        },
        // 2
        IntervalLUTItem {
            .factor_pre = 0.18f,
            .factor_post = 0.25f,
        },
        // 3
        IntervalLUTItem {
            .factor_pre = 0.20f,
            .factor_post = 0.30f,
        },
        // 4
        IntervalLUTItem {
            .factor_pre = 0.24f,
            .factor_post = 0.35f,
        },
        // 5
        IntervalLUTItem {
            .factor_pre = 0.27f,
            .factor_post = 0.40f,
        },
        // 6
        IntervalLUTItem {
            .factor_pre = 0.28f,
            .factor_post = 0.45f,
        },
        // 7
        IntervalLUTItem {
            .factor_pre = 0.29f,
            .factor_post = 0.50f,
        },
        // 8
        IntervalLUTItem {
            .factor_pre = 0.30f,
            .factor_post = 0.55f,
        },
        // 9
        IntervalLUTItem {
            .factor_pre = 0.31f,
            .factor_post = 0.60f,
        },
        // 10
        IntervalLUTItem {
            .factor_pre = 0.32f,
            .factor_post = 0.65f,
        },
        // 11
        IntervalLUTItem {
            .factor_pre = 0.33f,
            .factor_post = 0.70f,
        },
        // 12
        IntervalLUTItem {
            .factor_pre = 0.34f,
            .factor_post = 0.75f,
        },
        // 13
        IntervalLUTItem {
            .factor_pre = 0.34f,
            .factor_post = 0.80f,
        },
    };

    // Induction heater PID constants
    constexpr fpm::fixed_16_16 Tu = static_cast<fpm::fixed_16_16>(0.3345f); // in seconds
    constexpr fpm::fixed_16_16 Ku = static_cast<fpm::fixed_16_16>(0.1f);

    constexpr fpm::fixed_16_16 pid_p = static_cast<fpm::fixed_16_16>(0.6f) * Ku;
    constexpr fpm::fixed_16_16 pid_i = (static_cast<fpm::fixed_16_16>(1.2f) * Ku * Tu) / 300;
    constexpr fpm::fixed_16_16 pid_d = (static_cast<fpm::fixed_16_16>(0.075f) * Ku / Tu) * 300;

    // fixed_16_16 multiplies by an integer in int32_t, with no saturation. The D gain has a raw
    // value of approximately 441'000, so a change above 48.7 °C in one step overflowed. A real
    // nozzle changes much less in one step (3.3 ms), but the TPIS filter passes a jump below 50 °C
    // at once, and any jump after 3 readings. Clamp the inputs of the products: the output
    // saturates long before these limits, so the clamp does not change it.
    constexpr int32_t max_err_centideg = 65'535; // target 0 to 300 °C, current temperature in int16_t
    constexpr int32_t max_derr_centideg = 4'000;
    static_assert(int64_t { max_err_centideg } * pid_p.raw_value() + int64_t { max_derr_centideg } * pid_d.raw_value()
            + int64_t { limited_max_power } * fpm::fixed_16_16 { 1 }.raw_value() * 2
        < std::numeric_limits<int32_t>::max());
    static_assert(int64_t { max_err_centideg } * pid_i.raw_value() < std::numeric_limits<int32_t>::max() / 2);

} // namespace

uint16_t PowerControl::pid_calculate_pwr_i(int32_t target_centideg, int32_t current_centideg) {
    // During TURBO/LIMITED, PID output is computed but overridden with fixed power.
    int32_t err = std::clamp(target_centideg - current_centideg, -max_err_centideg, max_err_centideg);
    int32_t derr = std::clamp(last_centideg - current_centideg, -max_derr_centideg, max_derr_centideg);
    last_centideg = current_centideg;

    fpm::fixed_16_16 pwr = err * pid_p + derr * pid_d;
    if (!((err < 0 && pwr + pid_i_state < static_cast<fpm::fixed_16_16>(0))
            || (err > 0 && pwr + pid_i_state > static_cast<fpm::fixed_16_16>(limited_max_power)))) {
        pid_i_state += err * pid_i; // change integral only if it has any effect, so we avoid windup
    }
    pwr += pid_i_state;
    return std::clamp<int32_t>((int32_t)pwr, 0, limited_max_power);
}

uint16_t PowerControl::step(int32_t target_centideg, int32_t current_centideg) {
    using enum HeaterControlMode;

    constexpr int32_t proximity_threshold_centideg = 10 * 100;

    const bool was_off = heater_control_mode == HEATER_CONTROL_OFF;
    const bool way_below_target = current_centideg + proximity_threshold_centideg < target_centideg;
    const bool close_to_target = !way_below_target && current_centideg < target_centideg;

    // Limit power to the heater when we are closing target temperature
    if (target_centideg <= 0) {
        heater_control_mode = HEATER_CONTROL_OFF;
    } else if (way_below_target) {
        // Target increased significantly - go to turbo mode to heat up faster, then switch to limited for stable approach
        heater_control_mode = HEATER_CONTROL_TURBO;
    } else if (heater_control_mode != HEATER_CONTROL_PID && close_to_target) {
        // Supply limited power when we are <10deg below target, do not overwrite PID
        heater_control_mode = HEATER_CONTROL_LIMITED;
    } else {
        heater_control_mode = HEATER_CONTROL_PID;
    }

    // PID runs in all active modes for bumpless transfer
    uint16_t background_pwr_i = 0;
    if (heater_control_mode != HEATER_CONTROL_OFF) {
        if (was_off) {
            // The PID does not run while off, so the last temperature is from before the off (or 0
            // after boot). A D term from it can open the integral or give power above the target.
            last_centideg = current_centideg;
        }
        background_pwr_i = pid_calculate_pwr_i(target_centideg, current_centideg);
    }

    switch (heater_control_mode) {
    case HEATER_CONTROL_TURBO:
        // Full power to heat up fast
        return max_power;
    case HEATER_CONTROL_LIMITED:
        // Limited power - causes slight overshoot which is beneficial to transfer heat to nozzle core
        return limited_max_power;
    case HEATER_CONTROL_PID:
        // computed by PID above, clamped to limited_max_power
        return background_pwr_i;
    case HEATER_CONTROL_OFF:
    default:
        return 0;
    }
}

TimerConfigs compute_timer_config(uint16_t interval) {
    TimerConfigs result;
    for (int power = 0; power < max_power; ++power) {
        const uint16_t period = (uint16_t)interval;
        const auto &intervals = intervalLUT[power];
        const uint16_t duration_pre = static_cast<int16_t>(period * intervals.factor_pre);
        const uint16_t duration_post = static_cast<int16_t>(period * intervals.factor_post);

        TimerConfig &tc = result[power];
        tc.ARR = period + duration_post;
        tc.CCR1 = period - duration_pre + 1;
    }
    return result;
}

uint8_t power_to_pwm(uint16_t power) {
    if (power == 0) {
        return 0;
    }
    constexpr auto full_power = intervalLUT[max_power - 1].duty_cycle_sq;
    const auto current = intervalLUT[power - 1].duty_cycle_sq;
    constexpr auto scaling_factor = 255 / full_power;
    return static_cast<uint8_t>(current * scaling_factor);
}

} // namespace indx_head::heater
