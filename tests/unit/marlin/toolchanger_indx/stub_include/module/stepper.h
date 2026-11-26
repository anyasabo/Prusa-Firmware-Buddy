#pragma once
#include <cstdint>
struct FakeStepperE {
    uint16_t current_ma = 500;
    uint16_t rms_current() const { return current_ma; }
    void rms_current(uint16_t ma) { current_ma = ma; }
};
extern FakeStepperE stepperE0;
