#pragma once
#include <module/motion.h>
struct FakePlanner {
    void synchronize() {}
    float get_axis_position_mm(AxisEnum) const { return 0; }
};
extern FakePlanner planner;
