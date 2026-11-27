#include "test_support.hpp"

#include "ControlTask.hpp"
#include "MovingAverageFilter.hpp"
#include "modbus/ModbusBuffer.hpp"
#include "modbus/ModbusProtocol.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

// State of the control code that has no reset function. These globals are not static in the
// firmware sources.
namespace modularbed::StateLogic {
extern std::array<MovingAverageFilter, 2> s_ExpectedCurrentFilter;
}
namespace modularbed::MeasurementLogic {
extern uint32_t s_ADCIndex;
extern bool s_IsADCConversionRunning;
extern uint16_t s_CurrentADCZero_A;
extern uint16_t s_CurrentADCZero_B;
extern MovingAverageFilter s_CurrentFilter_A;
extern MovingAverageFilter s_CurrentFilter_B;
} // namespace modularbed::MeasurementLogic
namespace modularbed::ControlTask {
void ProcessADCMeasurements();
void ProcessSignals();
void ProcessModbusMessages();
} // namespace modularbed::ControlTask

using namespace modularbed;
using namespace modular_bed_shared::registers;

namespace test {

namespace {

    // The constructor of HeatbedletInfo is private, the copy is not. Keep a copy of the
    // state from before the first test.
    const std::vector<HeatbedletInfo> &pristine_bedlets() {
        static const std::vector<HeatbedletInfo> pristine = [] {
            std::vector<HeatbedletInfo> v;
            for (uint32_t i = 0; i < HEATBEDLET_COUNT; i++) {
                v.push_back(*HeatbedletInfo::Get(i));
            }
            return v;
        }();
        return pristine;
    }

    uint16_t crc16(const std::vector<uint8_t> &frame) {
        uint16_t crc = 0xFFFF;
        for (uint8_t byte : frame) {
            crc ^= byte;
            for (int bit = 0; bit < 8; bit++) {
                crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
            }
        }
        return crc;
    }

    void send_frame(std::vector<uint8_t> frame) {
        const uint16_t crc = crc16(frame);
        frame.push_back(crc & 0xFF);
        frame.push_back(crc >> 8);

        modbus::ModbusBuffer rx;
        modbus::ModbusBuffer tx;
        rx.CopyData(frame.data(), frame.size());
        REQUIRE(modbus::ModbusProtocol::ProcessFrame(&rx, &tx));
        // A reply with the error bit set in the function code is an exception.
        REQUIRE(tx.GetActualSize() >= 2);
        REQUIRE((tx[1] & 0x80) == 0);

        ControlTask::ProcessModbusMessages();
    }

} // namespace

void boot() {
    fake::reset_world();
    boot_with_world();
}

void boot_with_world() {
    const auto &pristine = pristine_bedlets();

    for (uint32_t i = 0; i < HEATBEDLET_COUNT; i++) {
        *HeatbedletInfo::Get(i) = pristine[i];
    }
    HeatbedletInfo::m_ChamberTemperature = DEFAULT_CHAMBER_TEMP;

    // Each call adds the register blocks again. The new blocks come first in the lookup, so
    // the old ones do not matter.
    ModbusRegisters::Init();
    modbus::ModbusProtocol::Init(modbus_address);

    MeasurementLogic::Init();
    MeasurementLogic::s_ADCIndex = 0;
    MeasurementLogic::s_IsADCConversionRunning = false;
    MeasurementLogic::s_CurrentADCZero_A = fake::current_adc_zero;
    MeasurementLogic::s_CurrentADCZero_B = fake::current_adc_zero;
    MeasurementLogic::s_CurrentFilter_A.Reset(fake::current_adc_zero);
    MeasurementLogic::s_CurrentFilter_B.Reset(fake::current_adc_zero);
    StateLogic::s_ExpectedCurrentFilter[0].Reset(0);
    StateLogic::s_ExpectedCurrentFilter[1].Reset(0);

    AutoconfLogic::StopTestHBHeating();
    PWMLogic::Init();
    ControlLogic::Init();
    ControlLogic::EnableControllers();
    REQUIRE(ControlTask::Init());

    // ControlTaskFunction() before its loop
    ControlTask::ProcessSignals();
    StateLogic::ClearSystemErrorBits(0xFFFFFFFF);
    MeasurementLogic::CalibrateCurrentChannels();
    AutoconfLogic::CheckHeatbedlets();
    AutoconfLogic::MeasureAndCheckAllHBCurrents();
    MeasurementLogic::StartADCMeasurements();
}

void run_ms(uint32_t ms, bool thermal) {
    for (uint32_t i = 0; i < ms; i++) {
        // The loop of ControlTaskFunction()
        ControlTask::ProcessADCMeasurements();
        ControlTask::ProcessSignals();
        ControlTask::ProcessModbusMessages();
        AutoconfLogic::IterateTesting();
        ControlLogic::IterateContollers();

        if (thermal) {
            // A heater at full power heats its bedlet by 1.5 °C/s. The loss to the ambient is
            // 1 % of the difference per second.
            for (uint32_t b = 0; b < HEATBEDLET_COUNT; b++) {
                if (fake::world.frozen[b]) {
                    continue;
                }
                float &t = fake::world.temperature[b];
                t += (fake::duty(b) * 1.5f - (t - fake::world.ambient) * 0.01f) / 1000.0f;
            }
        }
        fake::world.tick_ms++;
    }
}

void write_registers(uint16_t address, const std::vector<uint16_t> &values) {
    std::vector<uint8_t> frame {
        modbus_address,
        16,
        static_cast<uint8_t>(address >> 8),
        static_cast<uint8_t>(address),
        static_cast<uint8_t>(values.size() >> 8),
        static_cast<uint8_t>(values.size()),
        static_cast<uint8_t>(values.size() * 2),
    };
    for (uint16_t value : values) {
        frame.push_back(value >> 8);
        frame.push_back(value & 0xFF);
    }
    send_frame(frame);
}

void write_coil(uint16_t address, bool value) {
    send_frame({
        modbus_address,
        5,
        static_cast<uint8_t>(address >> 8),
        static_cast<uint8_t>(address),
        static_cast<uint8_t>(value ? 0xFF : 0x00),
        0x00,
    });
}

void write_targets(const std::array<float, fake::bedlet_count> &celsius) {
    std::vector<uint16_t> values;
    for (float c : celsius) {
        // ModularBed::set_target() on the main board
        values.push_back(static_cast<uint16_t>(c * MODBUS_TEMPERATURE_REGISTERS_SCALE));
    }
    write_registers(std::to_underlying(HBHoldingRegister::target_temperature), values);
}

void write_all_targets(float celsius) {
    std::array<float, fake::bedlet_count> targets;
    targets.fill(celsius);
    write_targets(targets);
}

void write_target(uint32_t index, float celsius) {
    std::array<float, fake::bedlet_count> targets;
    for (uint32_t i = 0; i < fake::bedlet_count; i++) {
        targets[i] = bedlet(i).m_TargetTemperature;
    }
    targets[index] = celsius;
    write_targets(targets);
}

HeatbedletInfo &bedlet(uint32_t index) {
    return *HeatbedletInfo::Get(index);
}

uint16_t hb_input_register(HBInputRegister reg, uint32_t index) {
    return ModbusRegisters::GetRegValue(reg, index);
}

uint16_t system_fault_status() {
    return ModbusRegisters::GetRegValue(SystemInputRegister::fault_status);
}

} // namespace test
