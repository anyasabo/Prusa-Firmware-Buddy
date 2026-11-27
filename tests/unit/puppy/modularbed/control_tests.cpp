#include "test_support.hpp"

#include "MeasurementCalculator.hpp"
#include "MovingAverageFilter.hpp"
#include "PuppyConfig.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>
#include <limits>

using namespace modularbed;
using namespace modular_bed_shared::registers;
using namespace modular_bed_shared::errors;
using Catch::Approx;
using fake::world;
using hal::ADCDriver::ADCChannel;

namespace {

constexpr uint32_t all_bedlets = HEATBEDLET_COUNT;

uint16_t flag(HeatbedletError e) {
    return std::to_underlying(e);
}

uint16_t flag(SystemError e) {
    return std::to_underlying(e);
}

uint16_t bedlet_faults(uint32_t index) {
    return test::hb_input_register(HBInputRegister::fault_status, index);
}

/// Heat all bedlets to the target with the thermal model and wait until all are stable.
void heat_to_stable(float target) {
    test::write_all_targets(target);
    for (int s = 0; s < 600; s++) {
        test::run_ms(1000, true);
        bool all_stable = true;
        for (uint32_t i = 0; i < all_bedlets; i++) {
            all_stable &= test::bedlet(i).m_State == HeatbedletState::Stable;
        }
        if (all_stable) {
            return;
        }
    }
    FAIL("The bedlets did not become stable");
}

} // namespace

TEST_CASE("modularbed: boot with a good bed") {
    test::boot();

    CHECK(test::system_fault_status() == 0);
    for (uint32_t i = 0; i < all_bedlets; i++) {
        CAPTURE(i);
        CHECK(bedlet_faults(i) == 0);
        CHECK(test::bedlet(i).m_State == HeatbedletState::Inactive);
        // 24 V / 13 ohm, measured at 22 °C and converted to 20 °C
        CHECK(ModbusRegisters::GetRegValue(HBHoldingRegister::measured_max_current, i) == Approx(1846).margin(3));
        CHECK(test::hb_input_register(HBInputRegister::measured_temperature, i) == Approx(220).margin(1));
    }

    // No target: no heat
    test::run_ms(2000);
    for (uint32_t i = 0; i < all_bedlets; i++) {
        CHECK(fake::duty(i) == 0);
    }
}

TEST_CASE("modularbed: thermistor conversion") {
    SECTION("the model and the firmware agree from 5 °C to 120 °C") {
        for (float t = 5; t <= 120; t += 0.5f) {
            CAPTURE(t);
            const uint16_t adc = fake::thermistor_adc(t);
            const float r = CalcThermistorResistance(ADCChannel::Heatbedlet_0, adc);
            CHECK(CalcThermistorTemperature(r) == Approx(t).margin(0.15));
        }
    }

    SECTION("the temperature decreases strictly with the ADC value") {
        float last = std::numeric_limits<float>::infinity();
        for (uint32_t adc = 1; adc <= 0xFFFF; adc++) {
            const float t = CalcThermistorTemperature(CalcThermistorResistance(ADCChannel::Heatbedlet_0, adc));
            REQUIRE(std::isfinite(t));
            REQUIRE(t < last);
            last = t;
        }
    }

    SECTION("a shorted and an open thermistor are out of range") {
        const auto celsius = [](uint16_t adc) {
            return CalcThermistorTemperature(CalcThermistorResistance(ADCChannel::Heatbedlet_0, adc));
        };
        // The thermistor is on the low side of the divider. A short reads above the maximum, but
        // exactly 0 reads -273 °C (the logarithm of 0).
        CHECK(celsius(0) < TEMPERATURE_MIN);
        CHECK(celsius(1) > TEMPERATURE_MAX);
        CHECK(celsius(100) > TEMPERATURE_MAX);
        CHECK(celsius(0xFFFF) < TEMPERATURE_MIN);
    }
}

TEST_CASE("modularbed: temperature register saturates") {
    test::boot();
    world.adc_override[3] = 0; // -273 °C
    world.adc_override[4] = 0xFFFF; // open
    test::run_ms(200);
    CHECK(test::hb_input_register(HBInputRegister::measured_temperature, 3) == 0);
    CHECK(test::hb_input_register(HBInputRegister::measured_temperature, 4) == 0);
    CHECK(bedlet_faults(3) & flag(HeatbedletError::TemperatureBelowMinimum));
    CHECK(bedlet_faults(4) & flag(HeatbedletError::TemperatureBelowMinimum));
}

TEST_CASE("modularbed: heater check at boot") {
    SECTION("a disconnected heater") {
        fake::reset_world();
        world.heater_resistance[7] = std::numeric_limits<float>::infinity();
        test::boot_with_world();
        CHECK(bedlet_faults(7) == flag(HeatbedletError::HeaterDisconnected));
        CHECK(test::system_fault_status() == flag(SystemError::HeatbedletError));
    }
    SECTION("a shorted heater") {
        fake::reset_world();
        world.heater_resistance[2] = 3.0f;
        test::boot_with_world();
        CHECK(bedlet_faults(2) == flag(HeatbedletError::HeaterShortCircuit));
    }
    SECTION("a heater with a high resistance") {
        fake::reset_world();
        world.heater_resistance[12] = 45.0f;
        test::boot_with_world();
        CHECK(bedlet_faults(12) == flag(HeatbedletError::HeaterDisconnected));
    }
    SECTION("no other bedlet has an error") {
        fake::reset_world();
        world.heater_resistance[7] = std::numeric_limits<float>::infinity();
        test::boot_with_world();
        for (uint32_t i = 0; i < all_bedlets; i++) {
            if (i != 7) {
                CHECK(bedlet_faults(i) == 0);
            }
        }
    }
}

TEST_CASE("modularbed: heat to a target and hold it") {
    test::boot();
    heat_to_stable(60);

    for (uint32_t i = 0; i < all_bedlets; i++) {
        CAPTURE(i);
        CHECK(world.temperature[i] == Approx(60).margin(1));
        CHECK(bedlet_faults(i) == 0);
    }
    CHECK(test::system_fault_status() == 0);

    // The branch currents stay below the limits of the power supply.
    test::run_ms(10'000, true);
    CHECK(fake::branch_current(0) <= PWM_MAX_CURRENT_AMPS[Branch::A]);
    CHECK(fake::branch_current(1) <= PWM_MAX_CURRENT_AMPS[Branch::B]);

    SECTION("target 0 turns all heaters off") {
        test::write_all_targets(0);
        test::run_ms(1000, true);
        for (uint32_t i = 0; i < all_bedlets; i++) {
            CHECK(test::bedlet(i).m_State == HeatbedletState::Inactive);
            CHECK(fake::duty(i) == 0);
        }
    }
}

TEST_CASE("modularbed: an error of one bedlet stops all heaters") {
    test::boot();
    heat_to_stable(60);

    SECTION("MAXTEMP") {
        world.frozen[5] = true;
        world.temperature[5] = 121;
        test::run_ms(1000, true);
        CHECK(bedlet_faults(5) & flag(HeatbedletError::TemperatureAboveMaximum));
        CHECK(test::bedlet(5).m_State == HeatbedletState::Error);
        CHECK(test::system_fault_status() == (flag(SystemError::HeatbedletError) | flag(SystemError::Maxtemp)));
    }
    SECTION("MINTEMP") {
        world.adc_override[11] = 0xFFFF;
        test::run_ms(1000, true);
        CHECK(bedlet_faults(11) & flag(HeatbedletError::TemperatureBelowMinimum));
        CHECK(test::system_fault_status() == (flag(SystemError::HeatbedletError) | flag(SystemError::Mintemp)));
    }

    for (uint32_t i = 0; i < all_bedlets; i++) {
        CAPTURE(i);
        CHECK(fake::duty(i) == 0);
    }
    // The heaters stay off.
    test::run_ms(10'000, true);
    for (uint32_t i = 0; i < all_bedlets; i++) {
        CHECK(fake::duty(i) == 0);
    }
}

TEST_CASE("modularbed: thermal runaway, temperature drop") {
    const uint32_t start_tick = GENERATE(0u, 0xFFFFFFFFu - 400'000u);
    CAPTURE(start_tick);
    fake::reset_world();
    world.tick_ms = start_tick;
    test::boot_with_world();
    heat_to_stable(60);
    REQUIRE(test::bedlet(4).m_State == HeatbedletState::Stable);

    // A cold steel sheet, or a heater that stops: 11 °C below the target
    world.frozen[4] = true;
    world.temperature[4] = 49;

    test::run_ms(355'000, true);
    CHECK(bedlet_faults(4) == 0);
    test::run_ms(10'000, true);
    CHECK(bedlet_faults(4) == flag(HeatbedletError::TemperatureDropDetected));
    CHECK(test::system_fault_status() == (flag(SystemError::HeatbedletError) | flag(SystemError::ThermalRunaway)));
}

TEST_CASE("modularbed: thermal runaway, the print fan allows a larger drop") {
    test::boot();
    heat_to_stable(60);
    test::write_coil(std::to_underlying(SystemCoil::print_fan_active), true);

    world.frozen[4] = true;
    world.temperature[4] = 30;
    test::run_ms(400'000, true);
    CHECK(bedlet_faults(4) == 0);

    world.temperature[4] = 24;
    test::run_ms(400'000, true);
    CHECK(bedlet_faults(4) == flag(HeatbedletError::TemperatureDropDetected));
}

TEST_CASE("modularbed: thermal runaway, temperature peak") {
    test::boot();
    heat_to_stable(60);

    world.frozen[9] = true;
    world.temperature[9] = 66;
    test::run_ms(55'000, true);
    CHECK(bedlet_faults(9) == 0);
    test::run_ms(10'000, true);
    CHECK(bedlet_faults(9) == flag(HeatbedletError::TemperaturePeakDetected));
}

TEST_CASE("modularbed: preheat error, a thermistor that does not see the heat") {
    test::boot();
    // Thermistor 6 has no contact with its heater.
    world.frozen[6] = true;
    world.temperature[6] = 25.7f;
    test::run_ms(2000);

    test::write_all_targets(60);
    test::run_ms(50'000, true);
    CHECK(bedlet_faults(6) == 0);
    test::run_ms(20'000, true);
    CHECK(bedlet_faults(6) == flag(HeatbedletError::PreheatError));
    CHECK(test::system_fault_status() == (flag(SystemError::HeatbedletError) | flag(SystemError::PreheatError)));
}

TEST_CASE("modularbed: overcurrent") {
    test::boot();
    const int branch = GENERATE(0, 1);
    CAPTURE(branch);
    world.extra_current[branch] = OVERCURRENT_THRESHOLD_AMPS[branch] + 0.5f;
    // The current filter averages 80 samples, approximately 1 s.
    test::run_ms(3000);
    CHECK(test::system_fault_status() & flag(SystemError::OverCurrent));
}

TEST_CASE("modularbed: unexpected current") {
    test::boot();
    const int branch = GENERATE(0, 1);
    CAPTURE(branch);

    SECTION("below the tolerance") {
        world.extra_current[branch] = UNEXPECTED_CURRENT_TOLERANCE[branch] - 0.2f;
        test::run_ms(3000);
        CHECK(test::system_fault_status() == 0);
    }
    SECTION("above the tolerance") {
        world.extra_current[branch] = UNEXPECTED_CURRENT_TOLERANCE[branch] + 0.2f;
        test::run_ms(3000);
        CHECK(test::system_fault_status() == flag(SystemError::UnexpectedCurrent));
    }
}

TEST_CASE("modularbed: panic and fault signals") {
    test::boot();
    world.panic = true;
    test::run_ms(1);
    CHECK(StateLogic::IsPowerPanicActive());
    world.fault = true;
    test::run_ms(1);
    CHECK(StateLogic::IsCurrentFaultActive());
    test::write_coil(std::to_underlying(SystemCoil::reset_overcurrent_fault), true);
    CHECK_FALSE(StateLogic::IsCurrentFaultActive());
}

TEST_CASE("modularbed: the controller runs at 5 Hz across the tick wrap") {
    fake::reset_world();
    world.tick_ms = 0xFFFFFFFFu - 1000;
    test::boot_with_world();
    test::write_all_targets(60);
    const uint32_t before = world.applied_patterns;
    // The ticks 0 to 2000 after the start: iterations at 200, 400, ..., 2000.
    test::run_ms(2001);
    // ApplyPWMValues() once per controller iteration
    CHECK(world.applied_patterns - before == 10);
}

TEST_CASE("modularbed: moving average filter") {
    MovingAverageFilter filter(4);
    CHECK(filter.AddValue(4) == 1);
    CHECK(filter.AddValue(4) == 2);
    CHECK(filter.AddValue(4) == 3);
    CHECK(filter.AddValue(4) == 4);
    CHECK(filter.AddValue(0) == 3);
    filter.Reset(0xFFFFFFFF);
    // The sum has 64 bits.
    CHECK(filter.AddValue(0xFFFFFFFF) == 0xFFFFFFFF);
}

TEST_CASE("modularbed: temperature filters of a bedlet") {
    test::boot();
    HeatbedletInfo &b = test::bedlet(0);
    for (int i = 0; i <= HeatbedletInfo::TEMP_HISTORY_SIZE; i++) {
        b.AddMeasuredTemperature(50);
    }
    CHECK(b.m_FilteredMeasuredTemperature == Approx(50));
    CHECK(b.m_FilteredMeasuredTemperatureDiff == Approx(0));

    // A rise of 1 °C per sample: about 9.8 samples per second
    for (int i = 1; i <= HeatbedletInfo::TEMP_HISTORY_SIZE; i++) {
        b.AddMeasuredTemperature(50.0f + i);
    }
    CHECK(b.m_FilteredMeasuredTemperature == Approx(50.0f + HeatbedletInfo::TEMP_HISTORY_SIZE - 0.5f));
    CHECK(b.m_FilteredMeasuredTemperatureDiff == Approx(1000.0f / ADC_CONVERSION_PERIOD / ADC_BATCH_SIZE).epsilon(0.001));
}

TEST_CASE("modularbed: heater resistance and temperature") {
    for (float t = 0; t <= 120; t += 10) {
        const float r = CalcHBResistanceAtTemperature(13, t);
        CHECK(CalcHBReferenceResistance(HEATBEDLET_VOLTAGE / r, t) == Approx(13));
    }
    CHECK(CalcHBReferenceResistance(0, 25) == INF_RESISTANCE);
}

TEST_CASE("modularbed: the preheat check uses the exact minimum rise (MB-1)") {
    test::boot();

    SECTION("no fan: a constant temperature is an error for each fraction of a degree") {
        // A rise of 0.5 °C per minute is necessary. Thermistor 6 has no contact with its heater
        // and stays at the same temperature.
        const float stuck = GENERATE(25.0f, 25.2f, 25.45f, 25.5f, 25.8f);
        CAPTURE(stuck);
        world.frozen[6] = true;
        world.temperature[6] = stuck;
        test::run_ms(2000);
        test::write_all_targets(60);
        test::run_ms(65'000, true);
        CHECK(bedlet_faults(6) == flag(HeatbedletError::PreheatError));
    }

    SECTION("print fan on: a drop below 20 °C is allowed") {
        // With the fan, the temperature can fall by 20 °C per minute. The firmware converted
        // 15 - 20 = -5 °C to uint32_t. On the Cortex-M0+ (soft float), __aeabi_f2uiz() gives
        // 0xFFFFFFFB for -5.0f, thus the next check needed 4.29e9 °C.
        test::write_coil(std::to_underlying(SystemCoil::print_fan_active), true);
        world.frozen[6] = true;
        world.temperature[6] = 15.0f;
        test::run_ms(2000);
        test::write_all_targets(60);
        test::run_ms(65'000, true);
        CHECK(bedlet_faults(6) == 0);
        CHECK(test::system_fault_status() == 0);
    }
}
