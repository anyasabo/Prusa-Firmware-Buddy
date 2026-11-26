#pragma once
#include <module/motion.h>

struct FakePlanner {
    bool leveling_active = false;
    float max_printed_z = 0;
    void synchronize();
    bool draining() const;
};
extern FakePlanner planner;
void disable_xy_steppers();

struct FakeLoadcell {
    int clears = 0;
    void Clear() { ++clears; }
};
extern FakeLoadcell loadcell;
