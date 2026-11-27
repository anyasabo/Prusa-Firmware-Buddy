/// @file
#include "heater.hpp"

#include <algorithm>
#include <stm32c0xx_hal.h>
#include "hal.hpp"
#include <app.hpp>
#include <raii/scope_guard.hpp>

using namespace indx_head::heater;

volatile uint16_t sample_buffer[sample_buffer_len];

InductionHeater::InductionHeater()
    : last_analysis {
        .status = RingdownAnalysisStatus::NOT_ENOUGH_PEAKS,
        .interval = 0,
        .decay = 0,
        .nozzle_presence = indx_head::NozzlePresence::unknown,
        .time = 0,
    }
    , dcOffset(0)
    , current_power(0)
    , last_valid_interval(0)
    , ramp_current_power(0)
    , ramp_target_power(0) {
}

void InductionHeater::heater_control(int32_t target_centideg, int32_t current_centideg) {
    update(power_control.step(target_centideg, current_centideg));
}

uint8_t InductionHeater::current_pwm() const {
    return power_to_pwm(current_power);
}

void InductionHeater::measure() {
    last_analysis.status = RingdownAnalysisStatus::NOT_ENOUGH_PEAKS;

    stop_timer();

    hal::config_adc_induction_heater();
    hal::peripherals::adc_semaphore.try_acquire_for(0); // Drain after config switch to catch any spurious DMA completion
    ScopeGuard g = [] { hal::config_adc_default(); };

    // Calibrate DC offset
    if (HAL_ADC_Start_DMA(&hal::peripherals::hadc1, (uint32_t *)sample_buffer, sample_buffer_len) != HAL_OK) {
        return;
    }
    if (!hal::peripherals::adc_semaphore.try_acquire_for(5)) {
        return;
    }
    dcOffset = dc_offset(sample_buffer);

    // Fire a single impulse and capture the LC circuit ringdown
    if (HAL_ADC_Stop_DMA(&hal::peripherals::hadc1) != HAL_OK) {
        return;
    }
    if (HAL_ADC_Start_DMA(&hal::peripherals::hadc1, (uint32_t *)sample_buffer, sample_buffer_len) != HAL_OK) {
        return;
    }

    uint16_t duration = 10;
    WRITE_REG(TIM16->ARR, duration); // Pulse width in MCU cycles
    WRITE_REG(TIM16->CCR1, 1);
    WRITE_REG(TIM16->CNT, 0);
    WRITE_REG(TIM16->EGR, TIM_EGR_UG); // Forcing to generate a update
    WRITE_REG(TIM16->SR, 0);
    SET_BIT(TIM16->CR1, TIM_CR1_CEN); // enable counter

    if (!hal::peripherals::adc_semaphore.try_acquire_for(5)) {
        return;
    }

    ringdown_analysis();
}

void InductionHeater::update(uint16_t target_power) {
    target_power = std::min(target_power, max_power);

    bool did_measure = false;
    if (should_measure(last_analysis, HAL_GetTick(), current_power == 0)) {
        measure();
        did_measure = true;
    }

    if (last_analysis.status != RingdownAnalysisStatus::VALID
        || last_analysis.nozzle_presence != indx_head::NozzlePresence::present) {
        // Do not heat without a valid analysis and a fully present nozzle — MOSFET would burn
        // without a nozzle (verified experimentally :) ). Partial-coupling readings classify as
        // `unknown`, so this single guard also covers them. The control loop will retry on the
        // next update() call in a few milliseconds.
        target_power = 0;
    }

    if (target_power == current_power && !did_measure) {
        return;
    }

    if (target_power == 0) {
        stop_timer();
        last_valid_interval = 0;
    } else {
        CLEAR_BIT(TIM16->DIER, TIM_DIER_UIE);
        ramp_target_power = target_power;
        if (current_power == 0) {
            apply_timer_config(1);
        } else if (current_power < target_power) {
            apply_timer_config(current_power);
        } else {
            apply_timer_config(target_power);
        }
        if (current_power == 0 || did_measure) {
            CLEAR_BIT(TIM16->CR1, TIM_CR1_OPM); // Disable one-pulse mode
            WRITE_REG(TIM16->CNT, 0);
            WRITE_REG(TIM16->EGR, TIM_EGR_UG); // Forcing to generate a update
            SET_BIT(TIM16->CR1, TIM_CR1_CEN); // Enable counter
        }
        WRITE_REG(TIM16->SR, 0);
        SET_BIT(TIM16->DIER, TIM_DIER_UIE);
    }
    current_power = target_power;
}

void InductionHeater::stop_timer() {
    // Disable update interrupt and set one-pulse mode, gracefully finishing
    // current pulse irrespective of its state and disabling counter after.
    CLEAR_BIT(TIM16->DIER, TIM_DIER_UIE);
    SET_BIT(TIM16->CR1, TIM_CR1_OPM);
}

void InductionHeater::apply_timer_config(uint16_t power) {
    const TimerConfig tc = timer_config[power - 1];
    WRITE_REG(TIM16->ARR, tc.ARR);
    WRITE_REG(TIM16->CCR1, tc.CCR1);
    ramp_current_power = power;
}

void InductionHeater::ramp_isr() {
    // clear interrupt flags
    WRITE_REG(TIM16->SR, 0);
    if (ramp_current_power < ramp_target_power) {
        // still ramping
        apply_timer_config(ramp_current_power + 1);
    } else {
        // done ramping
        CLEAR_BIT(TIM16->DIER, TIM_DIER_UIE);
    }
}

void InductionHeater::ringdown_analysis(void) {
    RingdownAnalysis analysis = analyze_ringdown(sample_buffer, dcOffset);
    analysis.time = HAL_GetTick();

    if (analysis.status != RingdownAnalysisStatus::VALID) {
        last_analysis = analysis;
        app::set_nozzle_present(indx_head::NozzlePresence::unknown);
        app::set_ringdown_decay(0);
        return; // invalid data, can't continue
    }

    if (ringdown_analysis_sanity_check(analysis, last_valid_interval)) {
        app::set_nozzle_present(analysis.nozzle_presence);
    } else {
        app::set_nozzle_present(indx_head::NozzlePresence::unknown);
    }

    timer_config = compute_timer_config(analysis.interval);

    last_analysis = analysis;
    app::set_ringdown_decay(static_cast<int16_t>(analysis.decay * 1000.0f));
}

InductionHeater inductionHeater;
