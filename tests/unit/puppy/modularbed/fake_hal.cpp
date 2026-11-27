#include "fake_hal.hpp"

#include "cmsis_os.h"
#include "hal/HAL_GPIO.hpp"
#include "hal/HAL_OTP.hpp"
#include "hal/HAL_PWM.hpp"
#include "hal/HAL_System.hpp"
#include "modbus/ModbusTask.hpp"
#include "otp.hpp"
#include "PuppyConfig.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdlib>
#include <deque>
#include <limits>

namespace fake {

World world;

void reset_world() {
    world = World {};
    world.temperature.fill(22.0f);
    world.heater_resistance.fill(13.0f);
}

uint16_t thermistor_adc(float celsius) {
    // Inverse of CalcThermistorTemperature() and CalcThermistorResistance().
    const float kelvin = celsius + 273.15f;
    const float r = HEATBEDLET_THERMISTOR_RESISTANCE_AT_T_ZERO
        * std::exp(HEATBEDLET_THERMISTOR_B_CONSTANT * (1.0f / kelvin - 1.0f / (HEATBEDLET_THERMISTOR_T_ZERO + 273.15f)));
    const float voltage = r / (r + HEATBEDLET_THERMISTOR_VOLTAGE_DIVIDER_RESISTANCE);
    return static_cast<uint16_t>(std::lround(std::clamp(voltage * 0x10000, 0.0f, 65535.0f)));
}

uint16_t current_adc(float amps) {
    // Inverse of CalcElectricCurrent() with the default zero offset.
    const float volts = amps * CURRENT_SENSOR_SENSITIVITY_MILLIVOLTS_PER_AMPERE / 1000.0f;
    const float adc = current_adc_zero + volts / ADC_REFERENCE_VOLTAGE * 0x10000;
    return static_cast<uint16_t>(std::lround(std::clamp(adc, 0.0f, 65535.0f)));
}

int branch_of(uint32_t bedlet) {
    return bedlet <= 5 ? 1 : 0;
}

float duty(uint32_t bedlet) {
    uint32_t length = 0;
    for (const Pulse &pulse : world.active_pulses) {
        if (pulse.bedlet == bedlet) {
            length += pulse.length;
        }
    }
    return static_cast<float>(length) / PWM_PERIOD_LENGTH;
}

float branch_current(int branch) {
    float current = world.extra_current[branch];
    for (uint32_t bedlet = 0; bedlet < bedlet_count; bedlet++) {
        if (branch_of(bedlet) != branch || std::isinf(world.heater_resistance[bedlet])) {
            continue;
        }
        const float resistance = world.heater_resistance[bedlet]
            * (1 + HEATBEDLET_RESISTANCE_TEMPERATURE_COEFFICIENT * (world.temperature[bedlet] - HEATBEDLET_REFERENCE_TEMPERATURE));
        current += duty(bedlet) * HEATBEDLET_VOLTAGE / resistance;
    }
    return current;
}

static uint16_t model_adc(hal::ADCDriver::ADCChannel channel) {
    using hal::ADCDriver::ADCChannel;
    const auto index = static_cast<size_t>(channel);
    if (world.adc_override[index]) {
        return *world.adc_override[index];
    }
    switch (channel) {
    case ADCChannel::Current_A:
        return current_adc(branch_current(0));
    case ADCChannel::Current_B:
        return current_adc(branch_current(1));
    case ADCChannel::VREF:
    case ADCChannel::MCUTemperature:
        return 24150;
    default:
        return thermistor_adc(world.temperature[index]);
    }
}

} // namespace fake

using fake::world;

uint32_t osKernelSysTick() {
    return world.tick_ms;
}

// The mail queue of ControlTask. The firmware queue holds 40 messages.
static std::deque<void *> mail_queue;
static size_t mail_queue_capacity = 0;
static size_t mail_item_size = 0;

osThreadId osThreadCreate(const osThreadDef_t *, void *) {
    static int thread;
    return &thread;
}

osMailQId osMailCreate(const osMailQDef_t *queue_def, osThreadId) {
    mail_queue_capacity = queue_def->queue_sz;
    mail_item_size = queue_def->item_sz;
    return &mail_queue;
}

void *osMailAlloc(osMailQId, uint32_t) {
    // The firmware waits for a free slot. The tests must not fill the queue.
    REQUIRE(mail_queue.size() < mail_queue_capacity);
    return std::calloc(1, mail_item_size);
}

osStatus osMailPut(osMailQId, void *mail) {
    mail_queue.push_back(mail);
    return osOK;
}

osEvent osMailGet(osMailQId, uint32_t) {
    osEvent event {};
    if (mail_queue.empty()) {
        event.status = osOK;
        return event;
    }
    event.status = osEventMail;
    event.value.p = mail_queue.front();
    mail_queue.pop_front();
    return event;
}

osStatus osMailFree(osMailQId, void *mail) {
    std::free(mail);
    return osOK;
}

osStatus osDelay(uint32_t millisec) {
    world.tick_ms += millisec;
    return osOK;
}

namespace hal::ADCDriver {

void PrepareConversion(ADCChannel) {}

void StartConversion(ADCChannel channel) {
    world.converted_channel = channel;
    world.started_conversions++;
}

bool CancelConversion() {
    return true;
}

bool IsConversionFinished() {
    return world.conversion_finished;
}

ADCChannel GetConvertedChannel() {
    return world.converted_channel;
}

uint16_t GetConversionResult() {
    return fake::model_adc(world.converted_channel);
}

float CalculateMCUTemperature(float TEMP_ADCValue, uint32_t) {
    return TEMP_ADCValue / 1000.0f;
}

float CalculateVREF(uint32_t VREF_ADCValue) {
    return VREF_ADCValue / 10000.0f;
}

} // namespace hal::ADCDriver

namespace hal::PWMDriver {

void AddPWMPulse(uint32_t heatbedletIndex, uint32_t pulseStartEdge, uint32_t pulseLength) {
    // The real driver calls bsod() for a pulse that ends after the next period.
    REQUIRE(heatbedletIndex < fake::bedlet_count);
    REQUIRE(pulseStartEdge < PWM_PERIOD_LENGTH);
    REQUIRE(pulseLength <= PWM_PERIOD_LENGTH);
    if (pulseLength > 0) {
        world.pending_pulses.push_back({ heatbedletIndex, pulseStartEdge, pulseLength });
    }
}

void ApplyPWMPattern() {
    world.active_pulses = world.pending_pulses;
    world.pending_pulses.clear();
    world.applied_patterns++;
}

void TurnOffAll() {
    world.active_pulses.clear();
    world.pending_pulses.clear();
}

} // namespace hal::PWMDriver

namespace hal::System {

uint32_t GetMicroeconds() {
    return world.micros;
}

void WaitMicroseconds(uint32_t microseconds) {
    world.micros += microseconds;
}

} // namespace hal::System

namespace hal::GPIODriver {

bool ReadPANICSignal() {
    return world.panic;
}

bool ReadFAULTSignal() {
    return world.fault;
}

void ResetOverCurrentFault() {
    world.fault = false;
}

} // namespace hal::GPIODriver

namespace hal::OTPDriver {

bool ReadOTPMemory(uint32_t addressOffset, void *pBuffer, size_t byteCount) {
    // Same bounds as the real driver. The fake OTP contains the low byte of each address.
    if (addressOffset > 0x400 || byteCount > 0x400 || (addressOffset + byteCount) > 0x400) {
        return false;
    }
    auto *bytes = static_cast<uint8_t *>(pBuffer);
    for (size_t i = 0; i < byteCount; i++) {
        bytes[i] = static_cast<uint8_t>(addressOffset + i);
    }
    return true;
}

} // namespace hal::OTPDriver

namespace modbus::ModbusTask {

void EnableModbus() {}

} // namespace modbus::ModbusTask

std::optional<uint8_t> otp_get_bom_id() {
    return std::nullopt;
}

uint32_t otp_get_timestamp() {
    return 0;
}

uint8_t otp_get_serial_nr(serial_nr_t &) {
    return 0;
}
