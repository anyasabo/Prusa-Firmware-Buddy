#pragma once
// The fake stepper counters and TMC drivers (fake_world.cpp)
#include "../inc/MarlinConfig.h"

class Stepper {
public:
    int32_t position(AxisEnum axis) const;
};
extern Stepper stepper;

struct FakeTMC {
    int32_t current = 0;
    float hold = 1.f;
    int8_t sg = 0;
    int32_t rms_current() const { return current; }
    void rms_current(int32_t c, float h) {
        current = c;
        hold = h;
    }
    float hold_multiplier() const { return hold; }
    int8_t sgt() const { return sg; }
    void sgt(int8_t s) { sg = s; }
};
extern FakeTMC stepperX, stepperY, stepperZ;
FakeTMC &stepper_axis(AxisEnum axis);

void stepper_wait_for_standstill(uint8_t axis_mask);

namespace PreciseStepping {
inline void reset_from_halt(bool) {}
} // namespace PreciseStepping
